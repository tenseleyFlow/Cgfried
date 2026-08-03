#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/driver.h"
#include "unit.h"
#include "util/arena.h"

/* Sprint 26 args_parse units: pure except the response-file tests, which
 * write their fixture files under build/. Every ARG_EITHER flag is
 * exercised in both forms; the unknown-flag policy has one assertion per
 * documented row. */

#define PARSE(a, ar, ...)                                                      \
    do {                                                                       \
        static char *t_argv[] = {(char *)"cgf", __VA_ARGS__};                  \
        a = args_parse(ar, (int)CGF_ARRAY_LEN(t_argv), t_argv);                \
    } while (0)

void test_args_joined_vs_separate(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-Idir1", (char *)"-I", (char *)"dir2",
          (char *)"-DFOO=1", (char *)"-D", (char *)"BAR", (char *)"-Ldl",
          (char *)"-L", (char *)"dl2", (char *)"-lm", (char *)"-l", (char *)"c",
          (char *)"-ofile.o", (char *)"t.c");
    T_ASSERT_EQ_INT(t, (int)a.include_dirs.len, 2);
    T_ASSERT_EQ_STR(t, a.include_dirs.data[0], "dir1");
    T_ASSERT_EQ_STR(t, a.include_dirs.data[1], "dir2");
    T_ASSERT_EQ_INT(t, (int)a.defs.len, 2);
    T_ASSERT_EQ_STR(t, a.defs.data[0].val, "FOO=1");
    T_ASSERT_EQ_STR(t, a.defs.data[1].val, "BAR");
    T_ASSERT_EQ_INT(t, (int)a.lib_dirs.len, 2);
    T_ASSERT_EQ_INT(t, (int)a.link_inputs.len, 3); /* -lm, -lc, t.c's slot */
    T_ASSERT_EQ_INT(t, a.link_inputs.data[0].kind, LINK_LIB);
    T_ASSERT_EQ_STR(t, a.link_inputs.data[0].val, "m");
    T_ASSERT_EQ_INT(t, a.link_inputs.data[1].kind, LINK_LIB);
    T_ASSERT_EQ_STR(t, a.link_inputs.data[1].val, "c");
    T_ASSERT_EQ_STR(t, a.output, "file.o");
    T_ASSERT(t, !a.unknown_opt && !a.missing_arg);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_optimization_controls(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-O1", (char *)"-ftime-report", (char *)"-Os",
          (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_OS);
    T_ASSERT(t, a.time_report);
    T_ASSERT(t, !a.unknown_opt && !a.bad_value);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_cgf_safe_exact_flag(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-fcgf-safe", (char *)"t.c");
    T_ASSERT(t, a.fcgf_safe);
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 0);
    T_ASSERT(t, !a.unknown_opt && !a.bad_value);
    args_free(&a);

    /* The generic -f prefix must not steal the exact spelling. */
    PARSE(a, &ar, (char *)"-fcgf-safety", (char *)"t.c");
    T_ASSERT(t, !a.fcgf_safe);
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 1);
    T_ASSERT_EQ_STR(t, a.warn_unrecognized.data[0], "-fcgf-safety");
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_fast_math_bundle_and_order(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-Ofast", (char *)"-O3", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_O3);
    T_ASSERT(t, !a.fast_math);
    args_free(&a);

    PARSE(a, &ar, (char *)"-O3", (char *)"-Ofast", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_OFAST);
    T_ASSERT(t, a.fast_math);
    args_free(&a);

    PARSE(a, &ar, (char *)"-O2", (char *)"-ffast-math", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_O2);
    T_ASSERT(t, a.fast_math);
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 0);
    args_free(&a);

    PARSE(a, &ar, (char *)"-Ofast", (char *)"-fno-fast-math", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_OFAST);
    T_ASSERT(t, !a.fast_math);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_fast_math_components_are_recognized_bundled_only(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-fassociative-math", (char *)"-fno-signed-zeros",
          (char *)"-ffinite-math-only", (char *)"-freciprocal-math",
          (char *)"-fno-math-errno", (char *)"-ffp-contract=off",
          (char *)"t.c");
    T_ASSERT(t, !a.fast_math);
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 0);
    T_ASSERT_EQ_INT(t, (int)a.warn_fast_math.len, 6);
    T_ASSERT_EQ_STR(t, a.warn_fast_math.data[0], "-fassociative-math");
    T_ASSERT_EQ_STR(t, a.warn_fast_math.data[1], "-fno-signed-zeros");
    T_ASSERT_EQ_STR(t, a.warn_fast_math.data[2], "-ffinite-math-only");
    T_ASSERT_EQ_STR(t, a.warn_fast_math.data[3], "-freciprocal-math");
    T_ASSERT_EQ_STR(t, a.warn_fast_math.data[4], "-fno-math-errno");
    T_ASSERT_EQ_STR(t, a.warn_fast_math.data[5], "-ffp-contract=off");
    args_free(&a);

    PARSE(a, &ar, (char *)"-ffast-math", (char *)"-fno-associative-math",
          (char *)"-fsigned-zeros", (char *)"-fno-finite-math-only",
          (char *)"-fno-reciprocal-math", (char *)"-fmath-errno",
          (char *)"-ffp-contract=on", (char *)"-ffp-contract=fast",
          (char *)"-ffp-contract=fast-honor-pragmas", (char *)"t.c");
    T_ASSERT(t, a.fast_math); /* components warn; they do not split it */
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 0);
    T_ASSERT_EQ_INT(t, (int)a.warn_fast_math.len, 8);
    args_free(&a);

    PARSE(a, &ar, (char *)"-ffp-contract=surprise", (char *)"t.c");
    T_ASSERT_EQ_STR(t, a.bad_value, "-ffp-contract=");
    T_ASSERT_EQ_INT(t, (int)a.warn_fast_math.len, 0);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_either_forms_mf_mt_mq_x_b(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-MFdep.d", (char *)"-MT", (char *)"tgt",
          (char *)"-MQa$b", (char *)"-xc", (char *)"-Bpfx", (char *)"-B",
          (char *)"pfx2", (char *)"-iquoteqd", (char *)"-isystem", (char *)"sd",
          (char *)"t.c");
    T_ASSERT_EQ_STR(t, a.dep_file, "dep.d");
    T_ASSERT_EQ_INT(t, (int)a.dep_targets.len, 2);
    T_ASSERT_EQ_STR(t, a.dep_targets.data[0], "tgt");
    T_ASSERT_EQ_STR(t, a.dep_targets.data[1], "a$$b"); /* -MQ pre-quoted */
    T_ASSERT_EQ_INT(t, (int)a.prefix_dirs.len, 2);
    T_ASSERT_EQ_INT(t, (int)a.iquote_dirs.len, 1);
    T_ASSERT_EQ_INT(t, (int)a.isystem_dirs.len, 1);
    T_ASSERT_EQ_INT(t, (int)a.inputs.len, 1);
    T_ASSERT_EQ_INT(t, a.inputs.data[0].kind, IN_C);
    args_free(&a);
    arena_free_all(&ar);
}

/* THE pitfall: -o consumes the next argv even if it looks like a flag. */
void test_args_o_eats_flaglike(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-o", (char *)"-v", (char *)"t.c");
    T_ASSERT_EQ_STR(t, a.output, "-v");
    T_ASSERT(t, !a.verbose);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_du_order_preserved(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-DX", (char *)"-UX", (char *)"-DX=2", (char *)"t.c");
    T_ASSERT_EQ_INT(t, (int)a.defs.len, 3);
    T_ASSERT(t, !a.defs.data[0].is_undef);
    T_ASSERT(t, a.defs.data[1].is_undef);
    T_ASSERT(t, !a.defs.data[2].is_undef);
    T_ASSERT_EQ_STR(t, a.defs.data[2].val, "X=2");
    args_free(&a);
    arena_free_all(&ar);
}

/* The four unknown-flag policy rows. */
void test_args_unknown_policy(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    /* -Wno-<unknown>: silently accepted (recorded for Sprint 37). */
    PARSE(a, &ar, (char *)"-Wno-bogus-thing", (char *)"t.c");
    T_ASSERT(t, !a.unknown_opt);
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 0);
    T_ASSERT_EQ_INT(t, (int)a.warn_opts.len, 1);
    T_ASSERT_EQ_STR(t, a.warn_opts.data[0].name, "no-bogus-thing");
    args_free(&a);

    /* -W<unknown> and -f<unknown>: warning, continue. */
    PARSE(a, &ar, (char *)"-Wbogus", (char *)"-fbogus", (char *)"t.c");
    T_ASSERT(t, !a.unknown_opt);
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 2);
    T_ASSERT_EQ_STR(t, a.warn_unrecognized.data[0], "-Wbogus");
    T_ASSERT_EQ_STR(t, a.warn_unrecognized.data[1], "-fbogus");
    args_free(&a);

    /* -W<known> stays silent. */
    PARSE(a, &ar, (char *)"-Wall", (char *)"-Wextra", (char *)"t.c");
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 0);
    T_ASSERT_EQ_INT(t, (int)a.warn_opts.len, 2);
    args_free(&a);

    /* Any other unknown: error, with a suggestion when close. */
    PARSE(a, &ar, (char *)"-dumpversio", (char *)"t.c");
    T_ASSERT(t, a.unknown_opt != NULL);
    T_ASSERT_EQ_STR(t, a.unknown_opt, "-dumpversio");
    T_ASSERT_EQ_STR(t, a.suggest, "-dumpversion");
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_o_multi_conflict(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-c", (char *)"-o", (char *)"x.o", (char *)"a.c",
          (char *)"b.c");
    T_ASSERT(t, a.o_multi_conflict);
    args_free(&a);

    /* Link inputs do not count: -c a.c b.o -o x.o is legal. */
    PARSE(a, &ar, (char *)"-c", (char *)"-o", (char *)"x.o", (char *)"a.c",
          (char *)"b.o");
    T_ASSERT(t, !a.o_multi_conflict);
    args_free(&a);

    /* Linking with -o and many inputs is of course fine. */
    PARSE(a, &ar, (char *)"-o", (char *)"p", (char *)"a.c", (char *)"b.c");
    T_ASSERT(t, !a.o_multi_conflict);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_x_none_restores(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-x", (char *)"c", (char *)"weird.txt", (char *)"-x",
          (char *)"none", (char *)"obj.whatever", (char *)"t.c");
    T_ASSERT_EQ_INT(t, (int)a.inputs.len, 3);
    T_ASSERT_EQ_INT(t, a.inputs.data[0].kind, IN_C);    /* forced */
    T_ASSERT_EQ_INT(t, a.inputs.data[1].kind, IN_LINK); /* restored */
    T_ASSERT_EQ_INT(t, a.inputs.data[2].kind, IN_C);
    args_free(&a);

    /* Bad -x value errors; stdin without -x errors naming -x. */
    PARSE(a, &ar, (char *)"-x", (char *)"fortran", (char *)"t.c");
    T_ASSERT(t, a.bad_value != NULL);
    args_free(&a);
    PARSE(a, &ar, (char *)"-");
    T_ASSERT(t, a.stdin_no_x != NULL);
    args_free(&a);
    PARSE(a, &ar, (char *)"-x", (char *)"c", (char *)"-");
    T_ASSERT(t, !a.stdin_no_x);
    T_ASSERT_EQ_INT(t, (int)a.inputs.len, 1);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_extension_dispatch(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"a.c", (char *)"b.i", (char *)"c.s", (char *)"d.S",
          (char *)"e.o", (char *)"f.a", (char *)"g.cgfir", (char *)"noext");
    T_ASSERT_EQ_INT(t, (int)a.inputs.len, 8);
    T_ASSERT_EQ_INT(t, a.inputs.data[0].kind, IN_C);
    T_ASSERT_EQ_INT(t, a.inputs.data[1].kind, IN_CPP_OUT);
    T_ASSERT_EQ_INT(t, a.inputs.data[2].kind, IN_ASM);
    T_ASSERT_EQ_INT(t, a.inputs.data[3].kind, IN_ASM_PP);
    T_ASSERT_EQ_INT(t, a.inputs.data[4].kind, IN_LINK);
    T_ASSERT_EQ_INT(t, a.inputs.data[5].kind, IN_LINK);
    T_ASSERT_EQ_INT(t, a.inputs.data[6].kind, IN_CGFIR);
    T_ASSERT_EQ_INT(t, a.inputs.data[7].kind, IN_LINK);
    args_free(&a);
    arena_free_all(&ar);
}

/* Position-sensitive link stream: -Wl, comma splits (empty = zero args),
 * -Xlinker single raw, objects and -l interleave in argv order. */
void test_args_link_stream_order(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-lm", (char *)"main.o", (char *)"-Wl,-q,-r",
          (char *)"-Wl,", (char *)"-Xlinker", (char *)"--trace", (char *)"b.c");
    T_ASSERT_EQ_INT(t, (int)a.link_inputs.len, 6);
    T_ASSERT_EQ_INT(t, a.link_inputs.data[0].kind, LINK_LIB);
    T_ASSERT_EQ_STR(t, a.link_inputs.data[0].val, "m");
    T_ASSERT_EQ_INT(t, a.link_inputs.data[1].kind, LINK_OBJ);
    T_ASSERT_EQ_STR(t, a.link_inputs.data[1].val, "main.o");
    T_ASSERT_EQ_INT(t, a.link_inputs.data[2].kind, LINK_RAW);
    T_ASSERT_EQ_STR(t, a.link_inputs.data[2].val, "-q");
    T_ASSERT_EQ_INT(t, a.link_inputs.data[3].kind, LINK_RAW);
    T_ASSERT_EQ_STR(t, a.link_inputs.data[3].val, "-r");
    T_ASSERT_EQ_INT(t, a.link_inputs.data[4].kind, LINK_RAW);
    T_ASSERT_EQ_STR(t, a.link_inputs.data[4].val, "--trace");
    /* b.c's reserved slot is last, val NULL until compiled. */
    T_ASSERT_EQ_INT(t, a.link_inputs.data[5].kind, LINK_OBJ);
    T_ASSERT(t, a.link_inputs.data[5].val == NULL);
    T_ASSERT_EQ_INT(t, a.inputs.data[1].link_slot, 5);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_last_one_wins(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-O2", (char *)"-O", (char *)"-Os", (char *)"-O3",
          (char *)"-std=c99", (char *)"-std=gnu11", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_O3);
    T_ASSERT_EQ_INT(t, a.std, 6); /* STD_GNU11 */
    args_free(&a);

    PARSE(a, &ar, (char *)"-O", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_O1);
    args_free(&a);
    PARSE(a, &ar, (char *)"-O7", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_O3); /* clamps, gcc parity */
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_deferred_flags(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-shared", (char *)"t.c");
    T_ASSERT(t, a.deferred && strcmp(a.deferred, "-shared") == 0);
    T_ASSERT(t, strstr(a.deferred_sprint, "Sprint 51") != NULL);
    args_free(&a);
    PARSE(a, &ar, (char *)"-fPIC", (char *)"t.c");
    T_ASSERT(t, a.deferred && strcmp(a.deferred, "-fPIC") == 0);
    args_free(&a);
    PARSE(a, &ar, (char *)"-flto", (char *)"t.c");
    T_ASSERT(t, a.deferred && strstr(a.deferred_sprint, "v0.1.0") != NULL);
    args_free(&a);
    /* -fomit-frame-pointer: warn+ignore, never an error. */
    PARSE(a, &ar, (char *)"-fomit-frame-pointer", (char *)"t.c");
    T_ASSERT(t, !a.deferred);
    T_ASSERT_EQ_INT(t, (int)a.warn_unrecognized.len, 1);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_debug_levels(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-g", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.debug_level, 2);
    T_ASSERT(t, !a.deferred);
    args_free(&a);
    PARSE(a, &ar, (char *)"-g1", (char *)"-g3", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.debug_level, 3);
    args_free(&a);
    PARSE(a, &ar, (char *)"-g3", (char *)"-g0", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.debug_level, 0);
    args_free(&a);
    PARSE(a, &ar, (char *)"-ggdb", (char *)"t.c");
    T_ASSERT(t, a.bad_value && strcmp(a.bad_value, "-g") == 0);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_dep_family(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-M", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.dep_mode, DEP_M);
    T_ASSERT(t, a.mode_E); /* -M implies -E */
    T_ASSERT(t, !a.dep_side);
    args_free(&a);
    PARSE(a, &ar, (char *)"-MMD", (char *)"-MP", (char *)"-c", (char *)"t.c");
    T_ASSERT_EQ_INT(t, a.dep_mode, DEP_MM);
    T_ASSERT(t, a.dep_side);
    T_ASSERT(t, a.dep_phony);
    T_ASSERT(t, !a.mode_E); /* -MD/-MMD do NOT imply -E */
    args_free(&a);
    arena_free_all(&ar);
}

/* The Make-quoting table rows from the sprint file. */
void test_args_make_quote_table(TestCtx *t)
{
    Arena ar;

    arena_init(&ar);
    T_ASSERT_EQ_STR(t, cgf_make_quote(&ar, "a$b"), "a$$b");
    T_ASSERT_EQ_STR(t, cgf_make_quote(&ar, "a b"), "a\\ b");
    T_ASSERT_EQ_STR(t, cgf_make_quote(&ar, "a#b"), "a\\#b");
    /* backslash-before-space doubles, then the space escapes. */
    T_ASSERT_EQ_STR(t, cgf_make_quote(&ar, "a\\ b"), "a\\\\\\ b");
    T_ASSERT_EQ_STR(t, cgf_make_quote(&ar, "plain"), "plain");
    arena_free_all(&ar);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

void test_args_response_files(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    /* Fixture files go in the CURRENT directory, not under build/ — the
     * sanitizer lane builds into build-san/ and a clean CI checkout has
     * no build/ at all, so a build/-relative fopen fails and the @file
     * silently degrades to a literal arg (F-S26-RSPCWD). Unlinked
     * before the asserts so a failure never litters the tree. */
    arena_init(&ar);
    write_file("t_args_rsp1.tmp",
               "-DRSP=1 'sp aced.c' -I \"quo ted\"\n@t_args_rsp2.tmp");
    write_file("t_args_rsp2.tmp", "-O2 esc\\ aped.c");
    PARSE(a, &ar, (char *)"@t_args_rsp1.tmp", (char *)"t.c");
    unlink("t_args_rsp1.tmp");
    unlink("t_args_rsp2.tmp");
    T_ASSERT(t, !a.rsp_error);
    T_ASSERT_EQ_INT(t, (int)a.defs.len, 1);
    T_ASSERT_EQ_STR(t, a.defs.data[0].val, "RSP=1");
    T_ASSERT_EQ_INT(t, (int)a.include_dirs.len, 1);
    T_ASSERT_EQ_STR(t, a.include_dirs.data[0], "quo ted");
    T_ASSERT_EQ_INT(t, a.opt_level, OPT_O2); /* from the nested file */
    T_ASSERT_EQ_INT(t, (int)a.inputs.len, 3);
    T_ASSERT_EQ_STR(t, a.inputs.data[0].path, "sp aced.c");
    T_ASSERT_EQ_STR(t, a.inputs.data[1].path, "esc aped.c");
    T_ASSERT_EQ_STR(t, a.inputs.data[2].path, "t.c");
    args_free(&a);

    /* An unreadable @file is a LITERAL argument (gcc parity). */
    PARSE(a, &ar, (char *)"@t_args_nonexistent.rsp", (char *)"t.c");
    T_ASSERT(t, !a.rsp_error);
    T_ASSERT_EQ_INT(t, (int)a.inputs.len, 2);
    T_ASSERT_EQ_STR(t, a.inputs.data[0].path, "@t_args_nonexistent.rsp");
    args_free(&a);

    /* Self-inclusion trips the depth cap, never an infinite loop. */
    write_file("t_args_rsp_loop.tmp", "@t_args_rsp_loop.tmp");
    PARSE(a, &ar, (char *)"@t_args_rsp_loop.tmp");
    unlink("t_args_rsp_loop.tmp");
    T_ASSERT(t, a.rsp_error != NULL);
    T_ASSERT_EQ_STR(t, a.rsp_error, "response files nested too deeply");
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_std_aliases(TestCtx *t)
{
    static const struct {
        const char *v;
        int std;
    } rows[] = {
        {"c89", 0},   {"c90", 0},   {"iso9899:1990", 0}, {"iso9899:199409", 0},
        {"c99", 1},   {"c9x", 1},   {"c11", 2},          {"c1x", 2},
        {"c17", 3},   {"c18", 3},   {"iso9899:2017", 3}, {"iso9899:2018", 3},
        {"gnu89", 4}, {"gnu90", 4}, {"gnu99", 5},        {"gnu11", 6},
        {"gnu17", 7}, {"gnu18", 7},
    };
    Arena ar;
    size_t i;

    arena_init(&ar);
    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        DriverArgs a;
        char flag[64];
        char *argv[3];

        snprintf(flag, sizeof(flag), "-std=%s", rows[i].v);
        argv[0] = (char *)"cgf";
        argv[1] = flag;
        argv[2] = (char *)"t.c";
        a = args_parse(&ar, 3, argv);
        T_ASSERT_EQ_INT(t, a.std, rows[i].std);
        args_free(&a);
    }
    {
        DriverArgs a;
        char *argv[3] = {(char *)"cgf", (char *)"-std=c++17", (char *)"t.c"};

        a = args_parse(&ar, 3, argv);
        T_ASSERT(t, a.bad_value != NULL);
        args_free(&a);
    }
    arena_free_all(&ar);
}

void test_args_separate_only_include(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    /* -include is SEPARATE-only: the joined spelling is unknown (with a
     * suggestion), exactly as gcc treats it. */
    PARSE(a, &ar, (char *)"-include", (char *)"pre.h", (char *)"t.c");
    T_ASSERT_EQ_INT(t, (int)a.pre_includes.len, 1);
    T_ASSERT_EQ_STR(t, a.pre_includes.data[0], "pre.h");
    T_ASSERT(t, !a.unknown_opt);
    args_free(&a);
    PARSE(a, &ar, (char *)"-includepre.h", (char *)"t.c");
    T_ASSERT(t, a.unknown_opt != NULL);
    args_free(&a);
    /* -Xlinker likewise consumes exactly the next argv. */
    PARSE(a, &ar, (char *)"-Xlinker", (char *)"t.c");
    T_ASSERT_EQ_INT(t, (int)a.link_inputs.len, 1);
    T_ASSERT_EQ_STR(t, a.link_inputs.data[0].val, "t.c");
    T_ASSERT_EQ_INT(t, (int)a.inputs.len, 0);
    args_free(&a);
    /* Missing value at argv end. */
    PARSE(a, &ar, (char *)"-l");
    T_ASSERT(t, a.missing_arg != NULL);
    args_free(&a);
    arena_free_all(&ar);
}

void test_args_warn_routing(TestCtx *t)
{
    Arena ar;
    DriverArgs a;

    arena_init(&ar);
    PARSE(a, &ar, (char *)"-w", (char *)"-Werror", (char *)"-Werror=shadow",
          (char *)"-pedantic-errors", (char *)"t.c");
    T_ASSERT(t, a.no_warnings);
    T_ASSERT(t, a.werror);
    T_ASSERT_EQ_INT(t, (int)a.warn_opts.len, 3);
    T_ASSERT_EQ_STR(t, a.warn_opts.data[0].name, "error");
    T_ASSERT_EQ_STR(t, a.warn_opts.data[1].name, "error=shadow");
    T_ASSERT_EQ_STR(t, a.warn_opts.data[2].name, "pedantic-errors");
    T_ASSERT(t, a.pedantic && a.pedantic_errors);
    args_free(&a);

    PARSE(a, &ar, (char *)"-Wfatal-errors", (char *)"-Wno-not-a-real-warning",
          (char *)"t.c");
    T_ASSERT(t, !a.unknown_opt);
    T_ASSERT_EQ_INT(t, (int)a.warn_opts.len, 2);
    T_ASSERT_EQ_INT(t, (int)a.warn_unknown_negative.len, 1);
    T_ASSERT_EQ_STR(t, a.warn_unknown_negative.data[0],
                    "-Wno-not-a-real-warning");
    args_free(&a);

    PARSE(a, &ar, (char *)"-Werror=not-a-real-warning", (char *)"t.c");
    T_ASSERT_EQ_STR(t, a.unknown_opt, "-Werror=not-a-real-warning");
    args_free(&a);

    PARSE(a, &ar, (char *)"-Wno-error=not-a-real-warning", (char *)"t.c");
    T_ASSERT_EQ_STR(t, a.unknown_opt, "-Wno-error=not-a-real-warning");
    args_free(&a);

    PARSE(a, &ar, (char *)"-Wformat=3", (char *)"t.c");
    T_ASSERT_EQ_STR(t, a.bad_value, "-Wformat=");
    args_free(&a);

    PARSE(a, &ar, (char *)"-Wno-format=2", (char *)"t.c");
    T_ASSERT_EQ_STR(t, a.bad_value, "-Wformat=");
    args_free(&a);
    arena_free_all(&ar);
}
