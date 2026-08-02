#include <string.h>

#include "../runner/directive.h"
#include "unit.h"
#include "util/arena.h"

static DirectiveSet parse(Arena *a, const char *src)
{
    DirectiveSet ds;

    directive_parse(a, src, strlen(src), &ds);
    return ds;
}

void test_directive_basic(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// CHECK: hello\n// CHECK: world\n// EXIT_CODE: 3\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 2);
    T_ASSERT(t, ds.dirs[0].kind == DIR_CHECK);
    T_ASSERT_EQ_STR(t, ds.dirs[0].value, "hello");
    T_ASSERT_EQ_INT(t, ds.dirs[0].line, 1);
    T_ASSERT_EQ_STR(t, ds.dirs[1].value, "world");
    T_ASSERT_EQ_INT(t, ds.exit_code, 3);
    arena_free_all(&a);
}

void test_directive_defaults(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    /* Zero directives: defaults are exit 0, no output constraints. */
    ds = parse(&a, "int main(void) { return 0; }\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 0);
    T_ASSERT_EQ_INT(t, ds.exit_code, 0);
    T_ASSERT_EQ_INT(t, ds.timeout, 0);
    arena_free_all(&a);
}

void test_directive_unknown_name(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// CHEK: whoops\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "unknown directive 'CHEK'") != NULL);
    /* Lowercase comments are plain comments, not directives. */
    ds = parse(&a, "// check: not a directive\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    arena_free_all(&a);
}

void test_directive_unknown_selector(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// SKIP(sparc-solaris): nope\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "unknown target selector") != NULL);
    ds = parse(&a, "// SKIP(x86_64-linux-gnu): fine\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    ds = parse(&a, "// SKIP(*): fine\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    arena_free_all(&a);
}

void test_directive_xfail_id(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// XFAIL(*): XF-0042 some known bug\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    T_ASSERT(t, ds.dirs[0].kind == DIR_XFAIL);
    T_ASSERT_EQ_STR(t, ds.dirs[0].xf_id, "XF-0042");
    T_ASSERT_EQ_STR(t, ds.dirs[0].value, "some known bug");
    T_ASSERT_EQ_STR(t, ds.dirs[0].selector, "*");

    /* Missing/malformed id is a configuration error. */
    ds = parse(&a, "// XFAIL(*): no ledger id here\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "XF-NNNN") != NULL);
    ds = parse(&a, "// XFAIL(*): XF-0042\n"); /* id but no reason */
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    /* Selector is mandatory. */
    ds = parse(&a, "// XFAIL: XF-0042 reason\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    arena_free_all(&a);
}

void test_directive_reserved(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// OPT_EQ: -O0 -O1 -Os -Ofast\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.nopt_levels, 4);
    T_ASSERT_EQ_STR(t, ds.opt_levels[0], "-O0");
    T_ASSERT_EQ_STR(t, ds.opt_levels[1], "-O1");
    T_ASSERT_EQ_STR(t, ds.opt_levels[2], "-Os");
    T_ASSERT_EQ_STR(t, ds.opt_levels[3], "-Ofast");
    /* ASM_CHECK went live in Sprint 24; Sprint 25 gave it a target
     * selector (asm is inherently per-target). The OTHER CHECKs stay
     * selector-free until Sprint 49. */
    ds = parse(&a, "// ASM_CHECK: movq\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    T_ASSERT(t, ds.dirs[0].kind == DIR_ASM_CHECK);
    ds = parse(&a, "// ASM_CHECK(x86_64-linux-gnu): mov\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    T_ASSERT_EQ_STR(t, ds.dirs[0].selector, "x86_64-linux-gnu");
    ds = parse(&a, "// ASM_CHECK(bogus-target): mov\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// CHECK(x86_64-linux-gnu): out\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "Sprint 49") != NULL);
    /* IR_CHECK was reserved here until Sprint 17 landed it; it now
     * parses as a value directive with CHECK semantics. */
    ds = parse(&a, "// IR_CHECK: iadd\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    T_ASSERT(t, ds.dirs[0].kind == DIR_IR_CHECK);
    T_ASSERT_EQ_STR(t, ds.dirs[0].value, "iadd");
    /* Empty value and ERROR_EXPECTED-conflict rules apply to it too. */
    ds = parse(&a, "// IR_CHECK:\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// ERROR_EXPECTED: bad\n// IR_CHECK: iadd\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    arena_free_all(&a);
}

void test_directive_opt_eq_validation(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// OPT_EQ: all\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.nopt_levels, 6);
    T_ASSERT_EQ_STR(t, ds.opt_levels[0], "-O0");
    T_ASSERT_EQ_STR(t, ds.opt_levels[5], "-Ofast");

    ds = parse(&a, "// OPT_EQ: -O0\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "at least two") != NULL);
    ds = parse(&a, "// OPT_EQ: -O0 -Og\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "unknown OPT_EQ level '-Og'") != NULL);
    ds = parse(&a, "// OPT_EQ: -O0 -O0\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "duplicate OPT_EQ level '-O0'") != NULL);
    ds = parse(&a, "// OPT_EQ: -O0  -O1\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// OPT_EQ: -O0 -O1\n// OPT_EQ: -O2 -O3\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "duplicate OPT_EQ directive") != NULL);
    arena_free_all(&a);
}

void test_directive_ofast_divergence_validation(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// OPT_EQ: -O0 -Ofast\n"
                   "// OFAST_DIVERGENCE_OK: fp-reduction-reassoc\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_STR(t, ds.ofast_divergence_reason, "fp-reduction-reassoc");
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    T_ASSERT(t, ds.dirs[0].kind == DIR_OFAST_DIVERGENCE_OK);

    ds = parse(&a, "// OPT_EQ: -Ofast -O3\n"
                   "// OFAST_DIVERGENCE_OK: finite-math-fold\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_STR(t, ds.ofast_divergence_reason, "finite-math-fold");

    ds = parse(&a, "// OPT_EQ: -O0 -O3\n"
                   "// OFAST_DIVERGENCE_OK: fp-reduction-reassoc\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "requires OPT_EQ containing -Ofast") !=
                    NULL);
    ds = parse(&a, "// OPT_EQ: -O0 -Ofast\n// OFAST_DIVERGENCE_OK:\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// OPT_EQ: -O0 -Ofast\n"
                   "// OFAST_DIVERGENCE_OK: typo\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg,
                       "unknown OFAST_DIVERGENCE_OK reason 'typo'") != NULL);
    ds = parse(&a, "// OPT_EQ: -O0 -Ofast\n"
                   "// OFAST_DIVERGENCE_OK: finite-math-fold extra\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// OPT_EQ: -O0 -Ofast\n"
                   "// OFAST_DIVERGENCE_OK: finite-math-fold\n"
                   "// OFAST_DIVERGENCE_OK: finite-math-fold\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg,
                       "duplicate OFAST_DIVERGENCE_OK directive") != NULL);
    arena_free_all(&a);
}

void test_directive_placement_and_spacing(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    /* Directive after code on the same line: rejected. */
    ds = parse(&a, "int x; // CHECK: y\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "must start its line") != NULL);
    /* Leading whitespace is fine. */
    ds = parse(&a, "  // CHECK: y\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    /* Sloppy spacing is a config error, not a silent comment. */
    ds = parse(&a, "//CHECK: y\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "//  CHECK: y\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    /* URLs with uppercase-colon after "//" must not false-positive. */
    ds = parse(&a, "const char *u = \"http://EXAMPLE\";\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    arena_free_all(&a);
}

void test_directive_crlf(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// CHECK: hello\r\n// EXIT_CODE: 2\r\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    /* The \r is a line terminator, never part of the value. */
    T_ASSERT_EQ_STR(t, ds.dirs[0].value, "hello");
    T_ASSERT_EQ_INT(t, ds.exit_code, 2);
    arena_free_all(&a);
}

void test_directive_cross_constraints(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    /* CHECK with ERROR_EXPECTED is contradictory. */
    ds = parse(&a, "// ERROR_EXPECTED: bad\n// CHECK: out\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    /* Duplicate EXIT_CODE / TIMEOUT. */
    ds = parse(&a, "// EXIT_CODE: 1\n// EXIT_CODE: 2\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// TIMEOUT: 5\n// TIMEOUT: 6\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    /* Range checks. */
    ds = parse(&a, "// EXIT_CODE: 300\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// TIMEOUT: 0\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    /* Selector on CHECK is reserved until Sprint 49. */
    ds = parse(&a, "// CHECK(arm64-linux): x\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    arena_free_all(&a);
}

void test_directive_flags_env(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// FLAGS: -E -trigraphs\n// ENV: A=1\n// ENV: B=x=y\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_STR(t, ds.flags, "-E -trigraphs");
    T_ASSERT_EQ_INT(t, ds.ndirs, 2); /* the two ENV entries */
    T_ASSERT(t, ds.dirs[0].kind == DIR_ENV);
    T_ASSERT_EQ_STR(t, ds.dirs[0].value, "A=1");
    T_ASSERT_EQ_STR(t, ds.dirs[1].value, "B=x=y"); /* first '=' splits */

    /* Duplicate FLAGS is a config error; malformed ENV likewise. */
    ds = parse(&a, "// FLAGS: -E\n// FLAGS: -E\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// ENV: NOVALUE\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// ENV: =x\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// FLAGS: \n"); /* trailing space, empty value */
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    arena_free_all(&a);
}

void test_directive_warn_assertions(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// WARN_CHECK: unused-variable unused variable 'x'\n"
                   "// WARN_COUNT: 2\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    T_ASSERT(t, ds.dirs[0].kind == DIR_WARN_CHECK);
    T_ASSERT_EQ_INT(t, ds.dirs[0].line, 1);
    T_ASSERT_EQ_STR(t, ds.dirs[0].warn_flag, "unused-variable");
    T_ASSERT_EQ_STR(t, ds.dirs[0].value, "unused variable 'x'");
    T_ASSERT(t, ds.has_warn_check);
    T_ASSERT(t, ds.has_warn_count);
    T_ASSERT_EQ_INT(t, ds.warn_count, 2);

    ds = parse(&a, "// WARN_COUNT: 0\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT(t, ds.has_warn_count);
    T_ASSERT_EQ_INT(t, ds.warn_count, 0);

    ds = parse(&a, "// WARN_CHECK: -Wunused-variable message\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "without the '-W' prefix") != NULL);
    ds = parse(&a, "// WARN_CHECK: Unused message\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// WARN_CHECK: unused-variable\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// WARN_CHECK: unused-variable  message\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// WARN_COUNT: -1\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// WARN_COUNT: 10000\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// WARN_COUNT: 1\n// WARN_COUNT: 1\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "duplicate WARN_COUNT") != NULL);
    arena_free_all(&a);
}

void test_directive_gcc8_divergence_metadata(TestCtx *t)
{
    Arena a;
    DirectiveSet ds;

    arena_init(&a);
    ds = parse(&a, "// DIVERGES(gcc-8): GCC omits the warning group\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 0);
    T_ASSERT_EQ_INT(t, ds.ndirs, 1);
    T_ASSERT(t, ds.dirs[0].kind == DIR_DIVERGES_GCC8);
    T_ASSERT_EQ_STR(t, ds.dirs[0].selector, "gcc-8");
    T_ASSERT_EQ_STR(t, ds.dirs[0].value, "GCC omits the warning group");

    ds = parse(&a, "// DIVERGES(clang-7): wrong oracle\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    ds = parse(&a, "// DIVERGES: missing selector\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    arena_free_all(&a);
}
