/* libcgf_rt: Sprint 44's opt-in heap safety runtime.
 *
 * The public allocation layout is fixed:
 *
 *     [ 32-byte CgfSafeHdr ][ user bytes ][ 8-byte canary ]
 *
 * A private prefix precedes that layout.  It is an intrusive ownership
 * registry: looking up a pointer in the registry BEFORE reading its header is
 * what makes the foreign-pointer contract honest.  Blindly reading p - 32
 * would fault for a perfectly valid foreign pointer near a page boundary.
 *
 * The registry also lets checks accept interior pointers.  Temporal detection
 * is deterministic while a block remains in the bounded quarantine and
 * probabilistic after eviction, when its registry entry and header are gone.
 */

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cgf_safe_internal.h"

#define CGF_SAFE_MAGIC UINT64_C(0xC6F5AFE0C6F5AFE0)
#define CGF_SAFE_CANARY UINT64_C(0xC6FCA9A2C6FCA9A2)
#define CGF_SAFE_LIVE UINT32_C(1)
#define CGF_SAFE_FREED UINT32_C(2)
#define CGF_SAFE_Q_BLOCKS 1024U
#define CGF_SAFE_Q_BYTES (UINT64_C(8) * 1024U * 1024U)
#define CGF_SAFE_HASH_BUCKETS 4096U

typedef struct CgfSafeHdr {
    uint64_t magic;
    uint64_t size;
    uint32_t state;
    uint32_t site_id;
    uint64_t pad; /* raw allocation base, represented as uintptr_t */
} CgfSafeHdr;

typedef struct CgfSafePrefix {
    struct CgfSafePrefix *all_next;
    struct CgfSafePrefix *all_previous;
    struct CgfSafePrefix *hash_next;
    struct CgfSafePrefix *quarantine_next;
    CgfSafeHdr *hdr;
    unsigned char *user;
    size_t raw_size;
} CgfSafePrefix;

_Static_assert(sizeof(CgfSafeHdr) == 32, "cgf-safe header must be 32 bytes");
_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t),
               "cgf-safe header cannot store a raw pointer");
_Static_assert(sizeof(CgfSafeHdr) % _Alignof(max_align_t) == 0,
               "cgf-safe header must preserve malloc alignment");

/* These names are produced by GNU ld's --wrap option. */
extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t, size_t);
extern void *__real_realloc(void *, size_t);
extern void __real_free(void *);
extern void *__real_aligned_alloc(size_t, size_t);
extern int __real_posix_memalign(void **, size_t, size_t);

static atomic_flag cgf_safe_lock = ATOMIC_FLAG_INIT;
static atomic_uint_fast64_t cgf_safe_generation;
static CgfSafePrefix *cgf_safe_all;
static CgfSafePrefix *cgf_safe_hash[CGF_SAFE_HASH_BUCKETS];
static CgfSafePrefix *cgf_safe_q_head;
static CgfSafePrefix *cgf_safe_q_tail;
static size_t cgf_safe_q_count;
static uint64_t cgf_safe_q_bytes;
static _Thread_local unsigned cgf_safe_in_wrapper;
static _Thread_local uint32_t cgf_safe_pending_site;
static _Thread_local struct {
    uintptr_t base;
    uint64_t size;
    uint64_t generation;
    int valid;
} cgf_safe_last_live;

/* The compiler emits this immediately before a wrapped allocation call.
 * TLS makes the handoff nesting-safe across threads; every wrapper consumes
 * exactly one value, while allocations from foreign objects naturally use
 * site zero. */
void cgf_safe_set_next_site(uint32_t site_id)
{
    cgf_safe_pending_site = site_id;
}

static uint32_t take_pending_site(void)
{
    uint32_t site_id = cgf_safe_pending_site;

    cgf_safe_pending_site = 0;
    return site_id;
}

static void lock_registry(void)
{
    while (
        atomic_flag_test_and_set_explicit(&cgf_safe_lock, memory_order_acquire))
        ;
}

static void unlock_registry(void)
{
    atomic_flag_clear_explicit(&cgf_safe_lock, memory_order_release);
}

static int add_overflow_size(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b)
        return 1;
    *out = a + b;
    return 0;
}

static int mul_overflow_size(size_t a, size_t b, size_t *out)
{
    if (a != 0 && b > SIZE_MAX / a)
        return 1;
    *out = a * b;
    return 0;
}

static int is_power_of_two(size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

static uintptr_t align_up_uintptr(uintptr_t p, size_t alignment)
{
    return (p + (uintptr_t)alignment - 1U) & ~((uintptr_t)alignment - 1U);
}

static unsigned char *user_of(const CgfSafeHdr *hdr)
{
    return (unsigned char *)(uintptr_t)(hdr + 1);
}

static CgfSafePrefix *prefix_of(const CgfSafeHdr *hdr)
{
    return (CgfSafePrefix *)(uintptr_t)hdr->pad;
}

static int header_valid(const CgfSafePrefix *prefix)
{
    uintptr_t raw = (uintptr_t)prefix;
    uintptr_t hdr_addr = (uintptr_t)prefix->hdr;
    const CgfSafeHdr *hdr;
    uintptr_t user;
    size_t offset;

    if (prefix->raw_size <
            sizeof(CgfSafePrefix) + sizeof(CgfSafeHdr) + sizeof(uint64_t) ||
        hdr_addr < raw + sizeof(CgfSafePrefix) || hdr_addr < raw ||
        hdr_addr - raw >
            prefix->raw_size - sizeof(CgfSafeHdr) - sizeof(uint64_t))
        return 0;
    hdr = (const CgfSafeHdr *)hdr_addr;
    user = (uintptr_t)user_of(hdr);
    if (hdr->magic != CGF_SAFE_MAGIC || prefix_of(hdr) != prefix ||
        user_of(hdr) != prefix->user ||
        (hdr->state != CGF_SAFE_LIVE && hdr->state != CGF_SAFE_FREED) ||
        user < raw)
        return 0;
    offset = (size_t)(user - raw);
    if (offset > prefix->raw_size ||
        prefix->raw_size - offset < sizeof(uint64_t))
        return 0;
    return hdr->size <= prefix->raw_size - offset - sizeof(uint64_t);
}

static void canary_store(CgfSafeHdr *hdr)
{
    uint64_t canary = CGF_SAFE_CANARY;

    memcpy(user_of(hdr) + (size_t)hdr->size, &canary, sizeof(canary));
}

static int canary_ok(const CgfSafeHdr *hdr)
{
    uint64_t canary;

    memcpy(&canary, user_of(hdr) + (size_t)hdr->size, sizeof(canary));
    return canary == CGF_SAFE_CANARY;
}

static CgfSafePrefix *find_prefix_exact_locked(const void *ptr)
{
    CgfSafePrefix *it;
    size_t bucket = ((uintptr_t)ptr >> 4U) & (CGF_SAFE_HASH_BUCKETS - 1U);

    for (it = cgf_safe_hash[bucket]; it; it = it->hash_next) {
        if (it->user == (const unsigned char *)ptr)
            return it;
    }
    return NULL;
}

/* Find pointers into the entire owned allocation, including the header,
 * alignment padding, and trailing canary.  This catches the usual p[-1] and
 * p[n] boundary failures without guessing at foreign memory. */
static CgfSafePrefix *find_containing_locked(const void *ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    CgfSafePrefix *it;

    for (it = cgf_safe_all; it; it = it->all_next) {
        uintptr_t raw = (uintptr_t)it;

        if (addr >= raw && addr - raw < it->raw_size)
            return it;
    }
    return NULL;
}

static void remove_registry_locked(CgfSafePrefix *prefix)
{
    CgfSafePrefix **link;
    size_t bucket =
        ((uintptr_t)prefix->user >> 4U) & (CGF_SAFE_HASH_BUCKETS - 1U);

    if (prefix->all_previous)
        prefix->all_previous->all_next = prefix->all_next;
    else
        cgf_safe_all = prefix->all_next;
    if (prefix->all_next)
        prefix->all_next->all_previous = prefix->all_previous;

    link = &cgf_safe_hash[bucket];
    while (*link && *link != prefix)
        link = &(*link)->hash_next;
    if (*link)
        *link = prefix->hash_next;
}

static CgfSafePrefix *quarantine_evict_locked(void)
{
    CgfSafePrefix *evicted = NULL;
    CgfSafePrefix **tail = &evicted;

    while (cgf_safe_q_head && (cgf_safe_q_count > CGF_SAFE_Q_BLOCKS ||
                               cgf_safe_q_bytes > CGF_SAFE_Q_BYTES)) {
        CgfSafePrefix *old = cgf_safe_q_head;
        CgfSafeHdr *hdr = old->hdr;

        cgf_safe_q_head = old->quarantine_next;
        if (!cgf_safe_q_head)
            cgf_safe_q_tail = NULL;
        cgf_safe_q_count--;
        cgf_safe_q_bytes -= hdr->size;
        remove_registry_locked(old);
        hdr->magic = 0;
        old->quarantine_next = NULL;
        *tail = old;
        tail = &old->quarantine_next;
    }
    return evicted;
}

static void *safe_allocate(size_t size, size_t alignment, int zero,
                           uint32_t site_id)
{
    size_t total, overhead;
    unsigned char *raw, *user;
    CgfSafePrefix *prefix;
    CgfSafeHdr *hdr;
    uintptr_t minimum;

    if (!is_power_of_two(alignment) || alignment < _Alignof(max_align_t)) {
        errno = EINVAL;
        return NULL;
    }
    if (add_overflow_size(sizeof(CgfSafePrefix), sizeof(CgfSafeHdr),
                          &overhead) ||
        add_overflow_size(overhead, alignment - 1U, &overhead) ||
        add_overflow_size(overhead, size, &overhead) ||
        add_overflow_size(overhead, sizeof(uint64_t), &total)) {
        errno = ENOMEM;
        return NULL;
    }
    raw = __real_malloc(total);
    if (!raw)
        return NULL;

    minimum = (uintptr_t)(raw + sizeof(CgfSafePrefix) + sizeof(CgfSafeHdr));
    user = (unsigned char *)align_up_uintptr(minimum, alignment);
    hdr = (CgfSafeHdr *)(void *)(user - sizeof(CgfSafeHdr));
    prefix = (CgfSafePrefix *)(void *)raw;
    prefix->hdr = hdr;
    prefix->user = user;
    prefix->raw_size = total;
    prefix->quarantine_next = NULL;
    hdr->magic = CGF_SAFE_MAGIC;
    hdr->size = size;
    hdr->state = CGF_SAFE_LIVE;
    hdr->site_id = site_id;
    hdr->pad = (uint64_t)(uintptr_t)raw;
    if (zero && size)
        memset(user, 0, size);
    canary_store(hdr);

    lock_registry();
    {
        size_t bucket = ((uintptr_t)user >> 4U) & (CGF_SAFE_HASH_BUCKETS - 1U);

        prefix->hash_next = cgf_safe_hash[bucket];
        cgf_safe_hash[bucket] = prefix;
    }
    prefix->all_previous = NULL;
    prefix->all_next = cgf_safe_all;
    if (cgf_safe_all)
        cgf_safe_all->all_previous = prefix;
    cgf_safe_all = prefix;
    atomic_fetch_add_explicit(&cgf_safe_generation, 1, memory_order_release);
    unlock_registry();
    return user;
}

static void safe_free(void *ptr)
{
    CgfSafeHdr *hdr;
    CgfSafePrefix *prefix;
    uint32_t site;
    uint64_t size;
    CgfSafePrefix *evicted;

    if (!ptr)
        return;
    lock_registry();
    prefix = find_prefix_exact_locked(ptr);
    if (!prefix) {
        CgfSafePrefix *interior = find_containing_locked(ptr);

        if (interior) {
            if (!header_valid(interior)) {
                unlock_registry();
                cgf_safe_fail_free("allocation-header corruption", 0, 0);
            }
            site = interior->hdr->site_id;
            size = interior->hdr->size;
            unlock_registry();
            cgf_safe_fail_free("invalid-free: interior pointer", site, size);
        }
        unlock_registry();
        __real_free(ptr);
        return;
    }
    hdr = prefix->hdr;
    if (!header_valid(prefix)) {
        /* Registry membership is stronger evidence than the magic.  A bad
         * magic here means our own metadata was overwritten, not a foreign
         * pointer collision. */
        unlock_registry();
        cgf_safe_fail_free("allocation-header corruption", 0, 0);
    }
    site = hdr->site_id;
    size = hdr->size;
    if (hdr->state != CGF_SAFE_LIVE) {
        unlock_registry();
        cgf_safe_fail_free("double-free", site, size);
    }
    if (!canary_ok(hdr)) {
        unlock_registry();
        cgf_safe_fail_free("heap-overflow-at-free: trailing canary corrupted",
                           site, size);
    }
    hdr->state = CGF_SAFE_FREED;
    if (size)
        memset(user_of(hdr), 0xDE, (size_t)size);
    prefix->quarantine_next = NULL;
    if (cgf_safe_q_tail)
        cgf_safe_q_tail->quarantine_next = prefix;
    else
        cgf_safe_q_head = prefix;
    cgf_safe_q_tail = prefix;
    cgf_safe_q_count++;
    cgf_safe_q_bytes += size;
    evicted = quarantine_evict_locked();
    atomic_fetch_add_explicit(&cgf_safe_generation, 1, memory_order_release);
    unlock_registry();
    while (evicted) {
        CgfSafePrefix *next = evicted->quarantine_next;

        __real_free(evicted);
        evicted = next;
    }
}

void cgf_safe_check(const void *ptr, size_t access_size, int kind,
                    uint32_t site_id)
{
    CgfSafeHdr *hdr;
    CgfSafePrefix *prefix;
    uintptr_t addr, base;
    uint64_t alloc_size;
    uint32_t alloc_site;
    int freed, bad_canary, out_of_bounds;

    if (access_size == 0)
        return;
    if (!ptr) {
        cgf_safe_fail_null(access_size, kind, site_id);
    }

    {
        uint64_t generation = (uint64_t)atomic_load_explicit(
            &cgf_safe_generation, memory_order_acquire);

        addr = (uintptr_t)ptr;
        if (cgf_safe_last_live.valid &&
            cgf_safe_last_live.generation == generation &&
            addr >= cgf_safe_last_live.base &&
            addr - cgf_safe_last_live.base <= cgf_safe_last_live.size &&
            access_size <=
                cgf_safe_last_live.size - (addr - cgf_safe_last_live.base))
            return;
    }

    lock_registry();
    prefix = find_containing_locked(ptr);
    if (!prefix) {
        unlock_registry();
        return; /* Unknown provenance is never a runtime error. */
    }
    hdr = prefix->hdr;
    if (!header_valid(prefix)) {
        unlock_registry();
        cgf_safe_fail_free("allocation-header corruption", 0, 0);
    }
    addr = (uintptr_t)ptr;
    base = (uintptr_t)user_of(hdr);
    alloc_size = hdr->size;
    alloc_site = hdr->site_id;
    freed = hdr->state != CGF_SAFE_LIVE;
    bad_canary = !canary_ok(hdr);
    out_of_bounds = addr < base || addr - base > alloc_size ||
                    access_size > alloc_size - (addr - base);
    if (!freed && !out_of_bounds) {
        cgf_safe_last_live.base = base;
        cgf_safe_last_live.size = alloc_size;
        cgf_safe_last_live.generation = (uint64_t)atomic_load_explicit(
            &cgf_safe_generation, memory_order_relaxed);
        cgf_safe_last_live.valid = 1;
        unlock_registry();
        return;
    }
    unlock_registry();
    cgf_safe_fail_access(freed ? "use-after-free" : "out-of-bounds", addr, base,
                         access_size, kind, site_id, alloc_site, alloc_size,
                         freed, bad_canary);
}

void *__wrap_malloc(size_t size)
{
    void *result;
    uint32_t site_id;

    if (cgf_safe_in_wrapper)
        return __real_malloc(size);
    site_id = take_pending_site();
    cgf_safe_in_wrapper++;
    result = safe_allocate(size, _Alignof(max_align_t), 0, site_id);
    cgf_safe_in_wrapper--;
    return result;
}

void *__wrap_calloc(size_t count, size_t size)
{
    size_t total;
    void *result;
    uint32_t site_id;

    if (cgf_safe_in_wrapper)
        return __real_calloc(count, size);
    site_id = take_pending_site();
    if (mul_overflow_size(count, size, &total)) {
        errno = ENOMEM;
        return NULL;
    }
    cgf_safe_in_wrapper++;
    result = safe_allocate(total, _Alignof(max_align_t), 1, site_id);
    cgf_safe_in_wrapper--;
    return result;
}

void __wrap_free(void *ptr)
{
    if (cgf_safe_in_wrapper) {
        __real_free(ptr);
        return;
    }
    cgf_safe_in_wrapper++;
    safe_free(ptr);
    cgf_safe_in_wrapper--;
}

void cgf_safe_free(void *ptr)
{
    __wrap_free(ptr);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    CgfSafeHdr *hdr;
    CgfSafePrefix *prefix;
    size_t old_size;
    void *result;
    uint32_t site_id;

    if (cgf_safe_in_wrapper)
        return __real_realloc(ptr, size);
    site_id = take_pending_site();
    if (!ptr) {
        cgf_safe_in_wrapper++;
        result = safe_allocate(size, _Alignof(max_align_t), 0, site_id);
        cgf_safe_in_wrapper--;
        return result;
    }

    lock_registry();
    prefix = find_prefix_exact_locked(ptr);
    if (!prefix) {
        CgfSafePrefix *interior = find_containing_locked(ptr);

        if (interior) {
            uint32_t site;
            uint64_t old;

            if (!header_valid(interior)) {
                unlock_registry();
                cgf_safe_fail_free("allocation-header corruption", 0, 0);
            }
            site = interior->hdr->site_id;
            old = interior->hdr->size;
            unlock_registry();
            cgf_safe_fail_free("invalid-realloc: interior pointer", site, old);
        }
        unlock_registry();
        cgf_safe_in_wrapper++;
        result = __real_realloc(ptr, size);
        cgf_safe_in_wrapper--;
        return result;
    }
    hdr = prefix->hdr;
    if (!header_valid(prefix)) {
        unlock_registry();
        cgf_safe_fail_free("allocation-header corruption", 0, 0);
    }
    if (hdr->state != CGF_SAFE_LIVE) {
        uint32_t site = hdr->site_id;
        uint64_t old = hdr->size;

        unlock_registry();
        cgf_safe_fail_free("realloc-after-free", site, old);
    }
    old_size = (size_t)hdr->size;
    unlock_registry();

    cgf_safe_in_wrapper++;
    result = safe_allocate(size, _Alignof(max_align_t), 0, site_id);
    if (result) {
        if (old_size && size)
            memcpy(result, ptr, old_size < size ? old_size : size);
        safe_free(ptr);
    }
    cgf_safe_in_wrapper--;
    return result;
}

void *__wrap_reallocarray(void *ptr, size_t count, size_t size)
{
    size_t total;

    if (mul_overflow_size(count, size, &total)) {
        (void)take_pending_site();
        errno = ENOMEM;
        return NULL;
    }
    return __wrap_realloc(ptr, total);
}

void *__wrap_strdup(const char *s)
{
    size_t n = strlen(s) + 1U;
    char *copy = __wrap_malloc(n);

    if (copy)
        memcpy(copy, s, n);
    return copy;
}

void *__wrap_strndup(const char *s, size_t limit)
{
    size_t n = 0;
    char *copy;

    while (n < limit && s[n])
        n++;
    copy = __wrap_malloc(n + 1U);
    if (copy) {
        if (n)
            memcpy(copy, s, n);
        copy[n] = '\0';
    }
    return copy;
}

void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
    void *result;
    uint32_t site_id;

    site_id = take_pending_site();
    if (!is_power_of_two(alignment) || alignment < sizeof(void *) ||
        size % alignment != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (cgf_safe_in_wrapper)
        return __real_aligned_alloc(alignment, size);
    cgf_safe_in_wrapper++;
    result = safe_allocate(
        size,
        alignment < _Alignof(max_align_t) ? _Alignof(max_align_t) : alignment,
        0, site_id);
    cgf_safe_in_wrapper--;
    return result;
}

int __wrap_posix_memalign(void **memptr, size_t alignment, size_t size)
{
    void *result;
    uint32_t site_id;

    site_id = take_pending_site();
    if (!memptr || !is_power_of_two(alignment) || alignment < sizeof(void *))
        return EINVAL;
    if (cgf_safe_in_wrapper)
        return __real_posix_memalign(memptr, alignment, size);
    cgf_safe_in_wrapper++;
    result = safe_allocate(
        size,
        alignment < _Alignof(max_align_t) ? _Alignof(max_align_t) : alignment,
        0, site_id);
    cgf_safe_in_wrapper--;
    if (!result)
        return errno == EINVAL ? EINVAL : ENOMEM;
    *memptr = result;
    return 0;
}
