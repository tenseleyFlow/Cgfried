/* Stdio-free diagnostics for the Sprint 44 heap-safety runtime.  This lives
 * in a separate translation unit so cgf_safe_check's common success path
 * does not reserve the diagnostic buffer or save the slow path's registers. */

#include "cgf_safe_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern long write(int, const void *, unsigned long);

typedef struct CgfMsg {
    char data[512];
    size_t len;
} CgfMsg;

static void msg_text(CgfMsg *m, const char *s)
{
    while (*s && m->len < sizeof(m->data))
        m->data[m->len++] = *s++;
}

static void msg_u64(CgfMsg *m, uint64_t value)
{
    char digits[32];
    size_t n = 0;

    do {
        digits[n++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0);
    while (n && m->len < sizeof(m->data))
        m->data[m->len++] = digits[--n];
}

static void msg_offset(CgfMsg *m, uintptr_t addr, uintptr_t base)
{
    if (addr < base) {
        msg_text(m, "p-");
        msg_u64(m, (uint64_t)(base - addr));
    } else {
        msg_text(m, "p+");
        msg_u64(m, (uint64_t)(addr - base));
    }
}

static void write_all(const char *p, size_t n)
{
    while (n != 0) {
        long wrote = write(2, p, (unsigned long)n);

        if (wrote > 0) {
            p += (size_t)wrote;
            n -= (size_t)wrote;
        } else if (wrote < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

static _Noreturn void fail_message(CgfMsg *m)
{
    const char *mode;

    msg_text(m, "CGF_SAFE_ABORT: aborting\n");
    write_all(m->data, m->len);
    mode = getenv("CGF_SAFE_ABORT");
    if (mode && strcmp(mode, "trap") == 0)
        __builtin_trap();
    abort();
}

_Noreturn void cgf_safe_fail_free(const char *reason, uint32_t site,
                                  uint64_t size)
{
    CgfMsg m = {{0}, 0};

    msg_text(&m, "cgf-safe: ");
    msg_text(&m, reason);
    msg_text(&m, "\n  site: allocation site #");
    msg_u64(&m, site);
    msg_text(&m, " (");
    msg_u64(&m, size);
    msg_text(&m, " bytes)\n");
    fail_message(&m);
}

_Noreturn void cgf_safe_fail_null(size_t access_size, int kind,
                                  uint32_t site_id)
{
    CgfMsg m = {{0}, 0};

    msg_text(&m, "cgf-safe: null-pointer-access: ");
    msg_u64(&m, access_size);
    msg_text(&m, kind == 0 ? "-byte read\n  site: access site #"
                           : "-byte write\n  site: access site #");
    msg_u64(&m, site_id);
    msg_text(&m, "\n");
    fail_message(&m);
}

_Noreturn void cgf_safe_fail_access(const char *reason, uintptr_t addr,
                                    uintptr_t base, size_t access_size,
                                    int kind, uint32_t access_site,
                                    uint32_t alloc_site, uint64_t alloc_size,
                                    int freed, int bad_canary)
{
    CgfMsg m = {{0}, 0};

    msg_text(&m, "cgf-safe: ");
    msg_text(&m, reason);
    msg_text(&m, ": ");
    msg_u64(&m, access_size);
    msg_text(&m, "-byte ");
    msg_text(&m, kind == 0 ? "read at " : "write at ");
    msg_offset(&m, addr, base);
    msg_text(&m, "\n  site: access site #");
    msg_u64(&m, access_site);
    msg_text(&m, ", allocation site #");
    msg_u64(&m, alloc_site);
    msg_text(&m, " (");
    msg_u64(&m, alloc_size);
    msg_text(&m, " bytes)\n  state: ");
    msg_text(&m, freed ? "freed (in quarantine)" : "live");
    if (bad_canary)
        msg_text(&m, ", trailing canary corrupted");
    msg_text(&m, "\n");
    fail_message(&m);
}

_Noreturn void cgf_safe_fail_transform(const char *reason, uint32_t access_site,
                                       uint32_t alloc_site, uint64_t alloc_size,
                                       int freed, int bad_canary)
{
    CgfMsg m = {{0}, 0};

    msg_text(&m, "cgf-safe: ");
    msg_text(&m, reason);
    msg_text(&m, "\n  site: access site #");
    msg_u64(&m, access_site);
    msg_text(&m, ", allocation site #");
    msg_u64(&m, alloc_site);
    msg_text(&m, " (");
    msg_u64(&m, alloc_size);
    msg_text(&m, " bytes)\n  state: ");
    msg_text(&m, freed ? "freed (in quarantine)" : "live");
    if (bad_canary)
        msg_text(&m, ", trailing canary corrupted");
    msg_text(&m, "\n");
    fail_message(&m);
}
