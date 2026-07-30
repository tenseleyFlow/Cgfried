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
    ds = parse(&a, "// OPT_EQ: O0,O2 => stdout\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "Sprint 30") != NULL);
    ds = parse(&a, "// ASM_CHECK(x86_64-linux-gnu): mov\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "Sprint 24") != NULL);
    ds = parse(&a, "// IR_CHECK: iadd\n");
    T_ASSERT_EQ_INT(t, ds.nerrs, 1);
    T_ASSERT(t, strstr(ds.errs[0].msg, "Sprint 17") != NULL);
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
