#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/driver.h"
#include "driver/toolchain.h"
#include "unit.h"
#include "util/arena.h"

/* Sprint 27 link-line units: the canonical argv sequence from
 * toolchain_build_link_argv (live crt probe — any host that can link
 * has one; the existing crt unit already pins that), and the injectable
 * probe core's precedence ladder with temp dirs in the CWD (never under
 * build/ — the F-S26-RSPCWD lesson). */

static int argv_index(const VecStr *v, const char *needle)
{
    size_t i;

    for (i = 0; i < v->len && v->data[i]; i++)
        if (strcmp(v->data[i], needle) == 0)
            return (int)i;
    return -1;
}

static int argv_index_suffix(const VecStr *v, const char *suffix)
{
    size_t i, n = strlen(suffix);

    for (i = 0; i < v->len && v->data[i]; i++) {
        size_t l = strlen(v->data[i]);

        if (l >= n && strcmp(v->data[i] + l - n, suffix) == 0)
            return (int)i;
    }
    return -1;
}

static int argv_index_prefix(const VecStr *v, const char *prefix)
{
    size_t i, n = strlen(prefix);

    for (i = 0; i < v->len && v->data[i]; i++)
        if (strncmp(v->data[i], prefix, n) == 0)
            return (int)i;
    return -1;
}

static void push_link(DriverArgs *a, u8 kind, const char *val)
{
    LinkInput li;

    li.kind = kind;
    li.val = val;
    VecLink_push(&a->link_inputs, li);
}

/* Shared skeleton: x.o, -lm, --trace in exact order; -Lldir; -o prog. */
static void fill_args(DriverArgs *a)
{
    memset(a, 0, sizeof(*a));
    a->output = "prog";
    VecStr_push(&a->lib_dirs, "ldir");
    push_link(a, LINK_OBJ, "x.o");
    push_link(a, LINK_LIB, "m");
    push_link(a, LINK_RAW, "--trace");
}

void test_link_argv_default_sequence(TestCtx *t)
{
    Arena ar;
    DriverArgs a;
    VecStr v = {0};
    int idl, io, i1, ii, ix, im, itr, isg, ilc, ieg, in_;

    arena_init(&ar);
    fill_args(&a);
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    /* Canonical order: -dynamic-linker BEFORE -o; crt1/crti before the
     * stream; the stream in argv order; -lc after it; crtn last. */
    idl = argv_index(&v, "-dynamic-linker");
    io = argv_index(&v, "-o");
    i1 = argv_index_suffix(&v, "/crt1.o");
    ii = argv_index_suffix(&v, "/crti.o");
    ix = argv_index(&v, "x.o");
    im = argv_index(&v, "-lm");
    itr = argv_index(&v, "--trace");
    isg = argv_index(&v, "--start-group");
    ilc = argv_index(&v, "-lc");
    ieg = argv_index(&v, "--end-group");
    in_ = argv_index_suffix(&v, "/crtn.o");
    T_ASSERT(t, idl > 0 && io > idl);
    T_ASSERT(t, argv_index(&v, "/lib64/ld-linux-x86-64.so.2") == idl + 1);
    T_ASSERT(t, argv_index(&v, "-Lldir") > io);
    T_ASSERT(t, i1 > io && ii == i1 + 1);
    T_ASSERT(t, ix > ii && im == ix + 1 && itr == im + 1);
    T_ASSERT(t, isg > itr && ilc > isg && ieg > ilc && in_ > ieg);
    T_ASSERT(t, argv_index(&v, "-static") < 0);
    /* NULL-terminated for exec. */
    T_ASSERT(t, v.len > 0 && v.data[v.len - 1] == NULL);
    VecStr_free(&v);
    args_free(&a);
    arena_free_all(&ar);
}

void test_link_argv_static_grouping(TestCtx *t)
{
    Arena ar;
    DriverArgs a;
    VecStr v = {0};
    int isg, ilc, ieg;

    arena_init(&ar);
    fill_args(&a);
    a.static_link = true;
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    T_ASSERT(t, argv_index(&v, "-static") == 1);
    T_ASSERT(t, argv_index(&v, "-dynamic-linker") < 0);
    isg = argv_index(&v, "--start-group");
    ilc = argv_index(&v, "-lc");
    ieg = argv_index(&v, "--end-group");
    T_ASSERT(t, isg > 0 && ilc > isg && ieg > ilc);
    T_ASSERT(t, argv_index_suffix(&v, "/crtn.o") == ieg + 1);
    VecStr_free(&v);
    args_free(&a);
    arena_free_all(&ar);
}

void test_link_argv_atomic16_dependency(TestCtx *t)
{
    Arena ar;
    DriverArgs a;
    VecStr v = {0};
    int ilg, isg, ila, ilc, ieg;

    arena_init(&ar);
    fill_args(&a);
    a.needs_libatomic = true;
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    ilg = argv_index_prefix(&v, "-L/usr/lib/gcc/");
    isg = argv_index(&v, "--start-group");
    ila = argv_index(&v, "-latomic");
    ilc = argv_index(&v, "-lc");
    ieg = argv_index(&v, "--end-group");
    T_ASSERT(t, ilg > 0 && isg > ilg && ila > isg && ilc > ila && ieg > ilc);
    VecStr_free(&v);
    args_free(&a);

    fill_args(&a);
    memset(&v, 0, sizeof(v));
    a.needs_libatomic = true;
    a.static_link = true;
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    isg = argv_index(&v, "--start-group");
    ila = argv_index(&v, "-latomic");
    ilc = argv_index(&v, "-lc");
    ieg = argv_index(&v, "--end-group");
    T_ASSERT(t, isg > 0 && ila > isg && ilc > ila && ieg > ilc);
    VecStr_free(&v);
    args_free(&a);
    arena_free_all(&ar);
}

void test_link_argv_cgf_safe_wrap_family(TestCtx *t)
{
    static const char *const wraps[] = {
        "--wrap=malloc",  "--wrap=calloc",        "--wrap=realloc",
        "--wrap=free",    "--wrap=reallocarray",  "--wrap=strdup",
        "--wrap=strndup", "--wrap=aligned_alloc", "--wrap=posix_memalign",
    };
    Arena ar;
    DriverArgs a;
    VecStr v = {0};
    int previous;
    size_t i;

    arena_init(&ar);
    fill_args(&a);
    a.fcgf_safe = true;
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    previous = argv_index(&v, "--eh-frame-hdr");
    for (i = 0; i < CGF_ARRAY_LEN(wraps); i++) {
        int at = argv_index(&v, wraps[i]);

        T_ASSERT(t, at == previous + 1);
        previous = at;
    }
    T_ASSERT(t, previous < argv_index(&v, "x.o"));
    VecStr_free(&v);
    args_free(&a);

    fill_args(&a);
    memset(&v, 0, sizeof(v));
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    T_ASSERT(t, argv_index(&v, "--wrap=malloc") < 0);
    VecStr_free(&v);
    args_free(&a);
    arena_free_all(&ar);
}

/* The subtraction table: user -l flags ALWAYS survive. */
void test_link_argv_subtraction(TestCtx *t)
{
    Arena ar;
    DriverArgs a;
    VecStr v;

    arena_init(&ar);

    fill_args(&a);
    a.nostartfiles = true;
    memset(&v, 0, sizeof(v));
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    T_ASSERT(t, argv_index_suffix(&v, "/crt1.o") < 0);
    T_ASSERT(t, argv_index_suffix(&v, "/crtn.o") < 0);
    T_ASSERT(t, argv_index(&v, "-lc") > 0);
    T_ASSERT(t, argv_index(&v, "-lm") > 0);
    VecStr_free(&v);
    args_free(&a);

    fill_args(&a);
    a.nodefaultlibs = true;
    a.fcgf_safe = true;
    memset(&v, 0, sizeof(v));
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    T_ASSERT(t, argv_index_suffix(&v, "/crt1.o") > 0);
    T_ASSERT(t, argv_index_suffix(&v, "/crtn.o") > 0);
    T_ASSERT(t, argv_index(&v, "-lc") < 0);
    T_ASSERT(t, argv_index(&v, "--wrap=malloc") < 0);
    T_ASSERT(t, argv_index(&v, "-lm") > 0); /* the user's, survives */
    VecStr_free(&v);
    args_free(&a);

    fill_args(&a);
    a.nostdlib = true;
    a.fcgf_safe = true;
    memset(&v, 0, sizeof(v));
    T_ASSERT(t, toolchain_build_link_argv(&a, cgf_target_host(), &ar, &v));
    T_ASSERT(t, argv_index_suffix(&v, "/crt1.o") < 0);
    T_ASSERT(t, argv_index(&v, "-lc") < 0);
    T_ASSERT(t, argv_index(&v, "--wrap=malloc") < 0);
    T_ASSERT(t, argv_index(&v, "-lm") > 0);
    VecStr_free(&v);
    args_free(&a);
    arena_free_all(&ar);
}

static void mk_crt_dir(const char *dir, bool with_crt1)
{
    char path[256];

    mkdir(dir, 0755);
    if (with_crt1) {
        FILE *f;

        snprintf(path, sizeof(path), "%s/crt1.o", dir);
        f = fopen(path, "wb");
        if (f)
            fclose(f);
    }
}

static void rm_crt_dir(const char *dir)
{
    char path[256];

    snprintf(path, sizeof(path), "%s/crt1.o", dir);
    unlink(path);
    rmdir(dir);
}

/* The precedence ladder on the injectable core: override > -B (flag
 * order) > table; a missing OVERRIDE never falls through; every probed
 * path lands in the transcript. */
void test_link_probe_precedence(TestCtx *t)
{
    Buf searched;
    const char *hit;
    const char *bdirs1[] = {"t_link_b"};
    const char *bdirs2[] = {"t_link_empty", "t_link_b"};
    const char *table[] = {"t_link_a"};

    mk_crt_dir("t_link_a", true);
    mk_crt_dir("t_link_b", true);
    mk_crt_dir("t_link_empty", false);

    /* Override wins over everything. */
    buf_init(&searched);
    hit = cgf_probe_crt_dir_in("t_link_a", bdirs1, 1, table, 1, &searched);
    T_ASSERT_EQ_STR(t, hit, "t_link_a");
    buf_free(&searched);

    /* -B beats the table. */
    buf_init(&searched);
    hit = cgf_probe_crt_dir_in(NULL, bdirs1, 1, table, 1, &searched);
    T_ASSERT_EQ_STR(t, hit, "t_link_b");
    buf_free(&searched);

    /* -B dirs probe in flag order; the miss is in the transcript. */
    buf_init(&searched);
    hit = cgf_probe_crt_dir_in(NULL, bdirs2, 2, table, 1, &searched);
    T_ASSERT_EQ_STR(t, hit, "t_link_b");
    T_ASSERT(t, searched.len > 0);
    T_ASSERT(t, memchr(searched.data, 'e', searched.len) != NULL);
    buf_free(&searched);

    /* A missing override does NOT fall through to a table that would
     * hit: the user asked for exactly that dir. */
    buf_init(&searched);
    hit =
        cgf_probe_crt_dir_in("t_link_missing", bdirs1, 1, table, 1, &searched);
    T_ASSERT(t, hit == NULL);
    T_ASSERT(t, searched.len > 0);
    buf_free(&searched);

    /* All-miss: every probed path is named, one per line. */
    buf_init(&searched);
    hit = cgf_probe_crt_dir_in(NULL, bdirs2, 1, NULL, 0, &searched);
    T_ASSERT(t, hit == NULL);
    {
        size_t i, lines = 0;

        for (i = 0; i < searched.len; i++)
            if (searched.data[i] == '\n')
                lines++;
        T_ASSERT_EQ_INT(t, (int)lines, 1); /* one probed, one named */
    }
    buf_free(&searched);

    rm_crt_dir("t_link_a");
    rm_crt_dir("t_link_b");
    rm_crt_dir("t_link_empty");
}
