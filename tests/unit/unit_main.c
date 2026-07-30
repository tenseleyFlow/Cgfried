/* Sprint 0 provisional unit binary: a plain main() of checks. Replaced by the
 * real harness (registry, runner, HARNESS_SKIP) in Sprint 1 — registration
 * will use an explicit table, never GNU constructor attributes (strict C11). */
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "util/arena.h"
#include "util/buf.h"
#include "util/intern.h"
#include "util/sort.h"
#include "util/strmap.h"
#include "util/vec.h"

static int checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            fprintf(stderr, "unit FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                                  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

static void test_arena(void)
{
    Arena a;
    void *p1, *p16;
    char *s;

    arena_init(&a);
    p1 = arena_alloc(&a, 1, 1);
    CHECK(p1 != NULL);
    p16 = arena_alloc(&a, 32, 16);
    CHECK(((uintptr_t)p16 & 15u) == 0);
    /* Oversize allocation forces a fresh block and still aligns. */
    p16 = arena_alloc(&a, 200 * 1024, 64);
    CHECK(((uintptr_t)p16 & 63u) == 0);
    memset(p16, 0xAB, 200 * 1024);
    s = arena_strdup(&a, "hello");
    CHECK(strcmp(s, "hello") == 0);
    s = arena_strndup(&a, "worldly", 5);
    CHECK(strcmp(s, "world") == 0);
    arena_free_all(&a);
    CHECK(a.head == NULL);
}

VEC_DECL(VecInt, int);

static void test_vec(void)
{
    VecInt v = { 0, 0, 0 };
    int i;

    for (i = 0; i < 100; i++)
        VecInt_push(&v, i);
    CHECK(v.len == 100);
    CHECK(v.cap >= 100); /* 8 -> 16 -> 32 -> 64 -> 128: >3 regrowths */
    for (i = 0; i < 100; i++)
        CHECK(v.data[i] == i);
    VecInt_free(&v);
    CHECK(v.data == NULL && v.len == 0 && v.cap == 0);
}

static void test_strmap(void)
{
    Strmap m;
    char key[32];
    int i;
    enum { N = 10000 };

    strmap_init(&m);
    for (i = 0; i < N; i++) {
        int n = snprintf(key, sizeof(key), "key%d", i);
        CHECK(strmap_put(&m, key, (size_t)n, (void *)(uintptr_t)(i + 1)) ==
              NULL);
    }
    CHECK(strmap_len(&m) == N);
    for (i = 0; i < N; i++) {
        int n = snprintf(key, sizeof(key), "key%d", i);
        CHECK(strmap_get(&m, key, (size_t)n) == (void *)(uintptr_t)(i + 1));
    }
    CHECK(strmap_get(&m, "absent", 6) == NULL);
    CHECK(!strmap_has(&m, "absent", 6));

    /* THE determinism property: iteration is exact insertion order. */
    {
        StrmapIter it = strmap_iter(&m);
        const char *k;
        size_t klen;
        void *v;

        for (i = 0; strmap_iter_next(&it, &k, &klen, &v); i++) {
            int n = snprintf(key, sizeof(key), "key%d", i);
            CHECK((size_t)n == klen && memcmp(k, key, klen) == 0);
            CHECK(v == (void *)(uintptr_t)(i + 1));
        }
        CHECK(i == N);
    }

    /* Overwrite keeps the original insertion position. */
    CHECK(strmap_put(&m, "key0", 4, (void *)(uintptr_t)777) ==
          (void *)(uintptr_t)1);
    {
        StrmapIter it = strmap_iter(&m);
        const char *k;
        size_t klen;
        void *v;

        CHECK(strmap_iter_next(&it, &k, &klen, &v));
        CHECK(klen == 4 && memcmp(k, "key0", 4) == 0);
        CHECK(v == (void *)(uintptr_t)777);
    }
    CHECK(strmap_len(&m) == N);
    strmap_free(&m);
}

static void test_intern(void)
{
    Arena a;
    Interner in;
    u32 id_foo, id_bar, id_foo2;

    arena_init(&a);
    intern_init(&in, &a);
    id_foo = intern_cstr(&in, "foo");
    id_bar = intern_cstr(&in, "bar");
    id_foo2 = intern(&in, "foobar", 3); /* length-bounded: "foo" */
    CHECK(id_foo == 1); /* id 0 is reserved invalid */
    CHECK(id_bar == 2);
    CHECK(id_foo2 == id_foo);
    CHECK(strcmp(intern_str(&in, id_foo), "foo") == 0);
    CHECK(strcmp(intern_str(&in, id_bar), "bar") == 0);
    CHECK(intern_count(&in) == 2);
    intern_free(&in);
    arena_free_all(&a);
}

typedef struct {
    int key;
    int seq;
} Pair;

static int pair_cmp(const void *a, const void *b, void *ctx)
{
    const Pair *pa = a, *pb = b;

    (*(int *)ctx)++;
    return pa->key < pb->key ? -1 : pa->key > pb->key ? 1 : 0;
}

static void test_sort_stable(void)
{
    Pair items[100];
    int i, cmp_calls = 0;

    for (i = 0; i < 100; i++) {
        items[i].key = i % 5;
        items[i].seq = i;
    }
    cgf_sort_stable(items, 100, sizeof(Pair), pair_cmp, &cmp_calls);
    CHECK(cmp_calls > 0); /* ctx pointer really threads through */
    for (i = 1; i < 100; i++) {
        CHECK(items[i - 1].key <= items[i].key);
        if (items[i - 1].key == items[i].key)
            CHECK(items[i - 1].seq < items[i].seq); /* stability */
    }
    /* Degenerate inputs. */
    cgf_sort_stable(items, 0, sizeof(Pair), pair_cmp, &cmp_calls);
    cgf_sort_stable(items, 1, sizeof(Pair), pair_cmp, &cmp_calls);
}

static void test_buf(void)
{
    Buf b;

    buf_init(&b);
    buf_append(&b, "ab", 2);
    buf_printf(&b, "%d-%s", 42, "x");
    buf_push_u8(&b, 0xFF);
    CHECK(b.len == 7);
    CHECK(memcmp(b.data, "ab42-x\xff", 7) == 0);
    buf_free(&b);
    CHECK(b.data == NULL && b.len == 0);
}

/* --- diag: structural capture via sink, then byte-exact rendering --- */

typedef struct {
    Diag last;
    int count;
} Capture;

static void capture_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    Capture *c = user;

    (void)dc;
    c->last = *d;
    c->count++;
}

static void test_diag_sink(void)
{
    Arena a;
    DiagCtx *dc;
    Capture cap = { { DIAG_NOTE, { 0, 0, 0, 0 }, NULL }, 0 };
    DiagSink sink;
    Span sp;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = capture_sink;
    sink.user = &cap;
    diag_set_sink(dc, sink);

    sp.file_id = 0;
    sp.line = 0;
    sp.col = 0;
    sp.len = 0;
    diag_emit(dc, DIAG_WARNING, sp, "w=%d", 7);
    CHECK(cap.count == 1);
    CHECK(cap.last.level == DIAG_WARNING);
    CHECK(strcmp(cap.last.message, "w=7") == 0);
    CHECK(!diag_had_error(dc)); /* warnings are not errors */

    diag_emit(dc, DIAG_ERROR, sp, "x=%d", 42);
    CHECK(cap.count == 2);
    CHECK(cap.last.level == DIAG_ERROR);
    CHECK(cap.last.span.file_id == 0);
    CHECK(strcmp(cap.last.message, "x=42") == 0);
    CHECK(diag_had_error(dc));
    CHECK(diag_error_count(dc) == 1);
    arena_free_all(&a);
}

static void test_diag_render(void)
{
    /* Line 2 is tab-indented; the caret line must mirror the tab so the
     * caret aligns at any tab width. Col 6 = 'x' (1-based bytes). */
    static const char src[] = "int main;\n\tint x = y;\n";
    static const char expect[] =
        "t.c:2:6: error: no clue about 'x'\n"
        "\tint x = y;\n"
        "\t    ^~~\n";
    Arena a;
    DiagCtx *dc;
    u32 fid;
    Diag d;
    FILE *f;
    char got[256];
    size_t n;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    fid = diag_add_file(dc, "t.c", src, sizeof(src) - 1);
    CHECK(fid == 1);

    d.level = DIAG_ERROR;
    d.span.file_id = fid;
    d.span.line = 2;
    d.span.col = 6;
    d.span.len = 3;
    d.message = "no clue about 'x'";

    f = tmpfile();
    CHECK(f != NULL);
    diag_render(f, &d, dc, false);
    rewind(f);
    n = fread(got, 1, sizeof(got) - 1, f);
    got[n] = '\0';
    fclose(f);
    CHECK(strcmp(got, expect) == 0);

    /* Driver-level (no-span) rendering. */
    d.span.file_id = 0;
    f = tmpfile();
    CHECK(f != NULL);
    diag_render(f, &d, dc, false);
    rewind(f);
    n = fread(got, 1, sizeof(got) - 1, f);
    got[n] = '\0';
    fclose(f);
    CHECK(strcmp(got, "cgfried: error: no clue about 'x'\n") == 0);
    arena_free_all(&a);
}

int main(void)
{
    test_arena();
    test_vec();
    test_strmap();
    test_intern();
    test_sort_stable();
    test_buf();
    test_diag_sink();
    test_diag_render();
    printf("unit: all checks passed (%d assertions)\n", checks);
    return 0;
}
