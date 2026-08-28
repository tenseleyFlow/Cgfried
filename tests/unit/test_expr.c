#include <string.h>

#include "parse/parse.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* The precedence matrix. Each case parses a single expression and asserts
 * its FULLY PARENTHESIZED rendering, so a wrong binding power or a wrong
 * associativity cannot hide — the shape of the tree is the assertion.
 * Adjacent levels of the 6.5 table are compared against each other, which
 * is the only arrangement that actually pins the ordering. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    int errors;
    int warnings;
} ExprFix;

static void efix_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    ExprFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

VEC_DECL(PpVecE, PpToken);

static AstNode *parse_src_e(ExprFix *f, const char *src, CStd std)
{
    DiagCtx *dc;
    DiagSink s;
    SourceFile *sf;
    PpVecE pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec target;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    s.handle = efix_sink;
    s.user = f;
    diag_set_sink(dc, s);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);

    memset(&lang, 0, sizeof(lang));
    lang.std = std;
    lang.gnu_mode = std >= STD_GNU89;
    lang.warnings = warn_ctx_new(&f->arena, dc);
    if (!std_is_c99_or_later(std))
        (void)warn_flag(lang.warnings, "declaration-after-statement");
    f->pp.warn = lang.warnings;
    target.kind = CGF_TARGET_X86_64_LINUX_GNU;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecE_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    PpVecE_free(&pv);

    parse_init(&f->ps, &tl, &f->pp, dc, &f->arena, &lang);
    return parse_translation_unit(&f->ps);
}

static void efix_free(ExprFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

/* Wraps the expression in a function body, parses, and renders the first
 * expression statement. `typedef int T;` is always in scope so the
 * cast-vs-call cases have a typedef to consult. */
static void expr_is(TestCtx *t, const char *expr, const char *want)
{
    ExprFix f;
    char src[512];
    AstNode *tu;
    Buf b;

    snprintf(src, sizeof(src),
             "typedef int T;\nstruct S { int m; };\n"
             "int a,b,c,d,e,x,y; int *p; struct S *sp; int g(int);\n"
             "void fn(void) { %s; }\n",
             expr);
    tu = parse_src_e(&f, src, STD_C17);
    buf_init(&b);
    if (f.errors != 0) {
        t_fail(t, __FILE__, __LINE__, "%s: unexpected error", expr);
    } else {
        AstNode *fd = tu->decls[tu->ndecls - 1];
        AstNode *body = fd->body;

        if (!body || body->nitems < 1 ||
            body->items[0]->kind != AST_STMT_EXPR) {
            t_fail(t, __FILE__, __LINE__, "%s: no expression statement", expr);
        } else {
            char got[512];
            ast_expr_render(body->items[0]->lhs, &b);
            snprintf(got, sizeof(got), "%.*s", (int)b.len,
                     (const char *)b.data);
            if (strcmp(got, want) != 0)
                t_fail(t, __FILE__, __LINE__, "%s:\n  got  %s\n  want %s", expr,
                       got, want);
        }
    }
    t->assertions++;
    buf_free(&b);
    efix_free(&f);
}

/* Every ADJACENT pair in the 6.5 table: the tighter level must end up
 * deeper in the tree. */
void test_expr_precedence_ladder(TestCtx *t)
{
    /* 17 comma < 16 assignment */
    expr_is(t, "a = b, c = d", "((a = b) , (c = d))");
    /* 16 assignment < 15 conditional */
    expr_is(t, "a = b ? c : d", "(a = (b ? c : d))");
    /* 15 conditional < 14 logical OR */
    expr_is(t, "a || b ? c : d", "((a || b) ? c : d)");
    /* 14 || < 13 && */
    expr_is(t, "a || b && c", "(a || (b && c))");
    /* 13 && < 12 | */
    expr_is(t, "a && b | c", "(a && (b | c))");
    /* 12 | < 11 ^ */
    expr_is(t, "a | b ^ c", "(a | (b ^ c))");
    /* 11 ^ < 10 & */
    expr_is(t, "a ^ b & c", "(a ^ (b & c))");
    /* 10 & < 9 equality */
    expr_is(t, "a & b == c", "(a & (b == c))");
    /* 9 equality < 8 relational */
    expr_is(t, "a == b < c", "(a == (b < c))");
    /* 8 relational < 7 shift */
    expr_is(t, "a < b << c", "(a < (b << c))");
    /* 7 shift < 6 additive */
    expr_is(t, "a << b + c", "(a << (b + c))");
    /* 6 additive < 5 multiplicative */
    expr_is(t, "a + b * c", "(a + (b * c))");
    /* 5 multiplicative < 4 cast */
    expr_is(t, "a * (T)b", "(a * (cast<T> b))");
    /* 4 cast < 3 unary */
    expr_is(t, "(T)-a", "(cast<T> (- a))");
    /* 3 unary < 2 postfix */
    expr_is(t, "-a++", "(- (a post++))");
    expr_is(t, "*p++", "(* (p post++))");
    /* 2 postfix < 1 primary */
    expr_is(t, "g(a)", "(call g [1] a)");
}

void test_expr_associativity(TestCtx *t)
{
    /* Left-associative levels: the FIRST operator ends up deepest. */
    expr_is(t, "a - b - c", "((a - b) - c)");
    expr_is(t, "a / b / c", "((a / b) / c)");
    expr_is(t, "a << b << c", "((a << b) << c)");
    expr_is(t, "a < b < c", "((a < b) < c)");
    expr_is(t, "a & b & c", "((a & b) & c)");
    expr_is(t, "a && b && c", "((a && b) && c)");
    expr_is(t, "a, b, c", "((a , b) , c)");
    /* Right-associative: assignment and the conditional. */
    expr_is(t, "a = b = c", "(a = (b = c))");
    expr_is(t, "a += b -= c", "(a += (b -= c))");
    expr_is(t, "a ? b : c ? d : e", "(a ? b : (c ? d : e))");
    /* ...and NOT the other way: a?b:c is the condition of nothing. */
    expr_is(t, "a ? b ? c : d : e", "(a ? (b ? c : d) : e)");
    /* Prefix unary is right-associative by construction. */
    expr_is(t, "- - a", "(- (- a))");
    expr_is(t, "!~a", "(! (~ a))");
    /* Casts nest right-to-left. */
    expr_is(t, "(T)(char)a", "(cast<T> (cast<char> a))");
}

void test_expr_postfix_chains(TestCtx *t)
{
    expr_is(t, "p[a]", "(p[a])");
    expr_is(t, "p[a][b]", "((p[a])[b])");
    expr_is(t, "sp->m", "(sp->m)");
    expr_is(t, "(*sp).m", "((* sp).m)");
    expr_is(t, "g(a)(b)", "(call (call g [1] a) [1] b)");
    expr_is(t, "g(a)[b]", "((call g [1] a)[b])");
    expr_is(t, "a++ + ++b", "((a post++) + (++ b))");
    /* The comma inside an argument list is a SEPARATOR; a parenthesized
     * comma expression is one argument. Three arguments here, not one. */
    expr_is(t, "g(a, (b, c), d)", "(call g [3] a (b , c) d)");
    expr_is(t, "g()", "(call g [0])");
    /* Assignment inside an argument is fine — it is below the comma. */
    expr_is(t, "g(a = b, c)", "(call g [2] (a = b) c)");
    expr_is(t, "g(a, __builtin_va_arg_pack())",
            "(call g [2] a __builtin_va_arg_pack())");
    expr_is(t, "__builtin_va_arg_pack_len()", "__builtin_va_arg_pack_len()");
}

void test_expr_conditional_middle_is_full_expr(TestCtx *t)
{
    /* 6.5.15: the middle operand is an EXPRESSION, so the comma operator
     * is legal there without parentheses. */
    expr_is(t, "a ? b, c : d", "(a ? (b , c) : d)");
    /* But the third operand is a conditional-expression, so a comma there
     * belongs to the enclosing expression instead. */
    expr_is(t, "(a ? b : c), d", "((a ? b : c) , d)");
    /* gcc-style: `a ? b : c = d` PARSES as an assignment whose LHS is the
     * conditional, and fails the lvalue check in sema (Sprint 13) rather
     * than producing a syntax error here. */
    expr_is(t, "a ? b : c = d", "((a ? b : c) = d)");
}

void test_expr_cast_vs_call(TestCtx *t)
{
    /* T is a typedef, g is not: identical syntax, opposite meanings. */
    expr_is(t, "(T)(a)", "(cast<T> a)");
    expr_is(t, "(g)(a)", "(call g [1] a)");
    expr_is(t, "(T)-a", "(cast<T> (- a))");
    /* A cast is not a unary-expression, so prefix ++ cannot take one —
     * covered as an error case in test_expr_deferrals_and_errors. */
    expr_is(t, "(T)a + b", "((cast<T> a) + b)");
    expr_is(t, "sizeof(T)", "(sizeof<T>)");
    expr_is(t, "sizeof a", "(sizeof a)");
    expr_is(t, "sizeof (a)", "(sizeof a)");
    expr_is(t, "sizeof *p", "(sizeof (* p))");
    expr_is(t, "sizeof -1", "(sizeof (- 1))");
    expr_is(t, "_Alignof(T)", "(alignof<T>)");
    expr_is(t, "__alignof__(T)", "(alignof<T>)");
    expr_is(t, "__alignof__(a)", "(alignof a)");
    expr_is(t, "__alignof__ a", "(alignof a)");
}

void test_expr_compound_literals(TestCtx *t)
{
    /* `(T){...}` is a compound literal, not a cast — and it is a POSTFIX
     * expression, so further postfix operators apply to it. */
    expr_is(t, "(T){0}", "(complit<T>[1])");
    expr_is(t, "(int[]){1,2}[0]", "((complit<array of int>[2])[0])");
    expr_is(t, "&(struct S){0}", "(& (complit<struct S>[1]))");
    expr_is(t, "(struct S){0}.m", "((complit<struct S>[1]).m)");
    /* `sizeof (int){1}` is sizeof of the LITERAL, not of the type. */
    expr_is(t, "sizeof (int){1}", "(sizeof (complit<int>[1]))");
}

void test_expr_generic(TestCtx *t)
{
    expr_is(t, "_Generic(a, int: 1, char: 2)", "(_Generic a <int>:1 <char>:2)");
    /* `default` may appear ANYWHERE, not only last. */
    expr_is(t, "_Generic(a, default: 0, int: 1)",
            "(_Generic a default:0 <int>:1)");
    expr_is(t, "_Generic(a, int: _Generic(b, int: 1))",
            "(_Generic a <int>:(_Generic b <int>:1))");
    /* Association results are assignment-expressions, so a comma there
     * would end the list — this must be TWO associations. */
    expr_is(t, "_Generic(a, int: b = 1, char: 2)",
            "(_Generic a <int>:(b = 1) <char>:2)");
}

/* The controlling expression of _Generic and the operand of sizeof are
 * never evaluated (6.5.1.1p2, 6.5.3.4p2); the AST must say so or a later
 * pass will happily emit their side effects. */
void test_expr_unevaluated_flags(TestCtx *t)
{
    ExprFix f;
    AstNode *tu;
    AstNode *stmt;

    tu = parse_src_e(&f,
                     "int a; int g(int);\n"
                     "void fn(void) { a = _Generic(g(a), int: 1); }\n",
                     STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    stmt = tu->decls[tu->ndecls - 1]->body->items[0];
    /* rhs of the assignment is the _Generic; its controlling expr is the
     * call, which must be flagged. */
    T_ASSERT(t, stmt->lhs->rhs->kind == AST_EXPR_GENERIC);
    T_ASSERT(t, stmt->lhs->rhs->lhs->unevaluated);
    /* The association RESULT is evaluated (one of them is, at least), so
     * it must NOT be flagged. */
    T_ASSERT(t, !stmt->lhs->rhs->items[0]->lhs->unevaluated);
    efix_free(&f);

    tu = parse_src_e(&f, "int a; void fn(void) { a = sizeof (a++); }\n",
                     STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    stmt = tu->decls[tu->ndecls - 1]->body->items[0];
    T_ASSERT(t, stmt->lhs->rhs->kind == AST_EXPR_SIZEOF);
    T_ASSERT(t, stmt->lhs->rhs->unevaluated);
    T_ASSERT(t, stmt->lhs->rhs->lhs->unevaluated);
    efix_free(&f);
}

/* A compound literal's storage duration comes from the SCOPE it appears
 * in, with no keyword to read: static at file scope, automatic at block
 * scope (6.5.2.5p5). Sprint 19 lowers it and needs this recorded. */
void test_expr_compound_literal_storage(TestCtx *t)
{
    ExprFix f;
    AstNode *tu;

    tu = parse_src_e(
        &f,
        "struct S { int m; };\n"
        "struct S *fp = &(struct S){1};\n"
        "void fn(void) { struct S *q = &(struct S){2}; (void)q; }\n",
        STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    /* File scope: the initializer is `&(struct S){1}`. */
    T_ASSERT(t, tu->decls[1]->init->lhs->kind == AST_EXPR_COMPOUND_LIT);
    T_ASSERT(t, tu->decls[1]->init->lhs->is_static_storage);
    /* Block scope: same spelling, automatic storage. */
    {
        AstNode *d = tu->decls[2]->body->items[0]->lhs;
        T_ASSERT(t, d->init->lhs->kind == AST_EXPR_COMPOUND_LIT);
        T_ASSERT(t, !d->init->lhs->is_static_storage);
    }
    efix_free(&f);
}

static void expr_bad(TestCtx *t, const char *src)
{
    ExprFix f;

    (void)parse_src_e(&f, src, STD_C17);
    if (f.errors == 0)
        t_fail(t, __FILE__, __LINE__, "%s: should be rejected", src);
    t->assertions++;
    efix_free(&f);
}

static void expr_ok(TestCtx *t, const char *src)
{
    ExprFix f;

    (void)parse_src_e(&f, src, STD_C17);
    if (f.errors != 0)
        t_fail(t, __FILE__, __LINE__, "%s: should be accepted", src);
    t->assertions++;
    efix_free(&f);
}

void test_expr_deferrals_and_errors(TestCtx *t)
{
    /* Deferred GNU forms must hard-error naming their sprint — never
     * parse silently as something else. */
    /* `a ?: b` LANDED in Sprint 55 and now parses. The boundary that
     * remains is a -pedantic PEDWARN, not a parse error, so it cannot be
     * pinned here -- expr_bad asks whether the parser rejected the text.
     * It is pinned in tests/programs/parse/err_omitted_middle.c, together
     * with the __extension__ suppression, which is the half that would
     * otherwise regress silently. */
    expr_ok(t, "int f(int a) { return a ?: 1; }\n");
    /* `__label__` is REFUSED, deliberately and not pending: block-scoped
     * labels need mangling and our labels are interned with pointer
     * comparison. docs/gnu-extensions.md carries the reasoning. */
    expr_bad(t, "int f(int x){ __label__ d; if(x) goto d; d: return 1; }\n");
    /* Statement expressions LAND in Sprint 55 -- what stays an error is the
     * FILE-SCOPE use, which gcc rejects too ("braced-group within expression
     * allowed only inside a function"). The accepting cases are pinned by
     * tests/corpus/x86_64/int/stmt_expr.c, which EXECUTES them. */
    expr_bad(t, "int g = ({ 1; });\n");
    /* Only the __-spelled GNU operators accept an expression operand. */
    expr_bad(t, "int f(int a) { return _Alignof a; }\n");
    expr_bad(t, "void f(void *p) { goto *p; }\n"); /* computed goto */
    /* `&&lab` — the address-of-label operator. `&&` is a single token, so
     * it reaches primary rather than the unary path, and without an
     * explicit case it reports only "expected an expression". */
    expr_bad(t, "void f(void){ void *p = &&lab; lab: ; (void)p; }\n");

    /* `sizeof (T)(x)` — a cast-expression is not a unary-expression, so
     * sizeof stops at the type and the `(x)` is stray. gcc agrees. */
    expr_bad(t, "typedef int T; int x; int f(void){ return sizeof (T)(x); }\n");
    /* Prefix ++ takes a unary-expression, so a cast operand is a syntax
     * error rather than a silent acceptance. */
    expr_bad(t, "typedef int T; int x; int f(void){ return ++(T)x; }\n");
    /* _Generic constraints checkable at parse time. */
    expr_bad(
        t, "int a; int f(void){ return _Generic(a, default:1, default:2); }\n");
    expr_bad(t, "int a; int f(void){ return _Generic(a); }\n");
}

/* Every `__builtin_*` name defers to Sprint 28 — in BOTH positions.
 * `__builtin_va_list` lexes as a keyword and the rest as identifiers, so
 * the two paths are genuinely separate code. */
void test_expr_builtins_defer(TestCtx *t)
{
    /* __builtin_va_list went LIVE in Sprint 19; Sprint 28 landed the
     * table in src/builtins.def. A name with a table row parses and
     * types; a name WITHOUT one still defers loudly — that split is the
     * contract (there is deliberately no accept-anything fallback). */
    expr_ok(t, "typedef __builtin_va_list va_list;\n");
    expr_ok(t, "void f(void){ __builtin_va_list ap; (void)ap; }\n");
    expr_ok(t, "int f(void){ return __builtin_expect(1, 1); }\n");
    expr_ok(t, "void f(void){ __builtin_trap(); }\n");
    expr_ok(t, "double f(void){ return __builtin_inf(); }\n");
    expr_ok(t, "struct S { int a; double b; };\n"
               "unsigned long f(void){ "
               "return __builtin_offsetof(struct S, b); }\n");
    /* The GNU type queries LANDED in Sprint 55 and now parse. */
    expr_ok(t,
            "int f(void){ return __builtin_types_compatible_p(int, int); }\n");
    expr_ok(t, "int f(void){ return __builtin_choose_expr(1, 2, 3); }\n");
    /* No row: still deferred. (Arity and offsetof member checks are SEMA's
     * — this fixture only parses — so they live in
     * tests/programs/builtins/.) */
    expr_bad(t, "int f(void){ return __builtin_clz(8); }\n");
    /* A designator is required, not an arbitrary expression. */
    expr_bad(t, "struct S { int a; };\n"
                "unsigned long f(void){ "
                "return __builtin_offsetof(struct S, 1 + 1); }\n");
}

void test_stmt_control_flow_constraints(TestCtx *t)
{
    /* `case`/`default` need an enclosing switch; `continue` needs a LOOP,
     * which is why break and continue cannot share one counter. */
    expr_bad(t, "void f(void) { case 1: ; }\n");
    expr_bad(t, "void f(void) { default: ; }\n");
    expr_bad(t, "void f(void) { continue; }\n");
    expr_bad(t, "void f(void) { break; }\n");
    expr_bad(t, "void f(int x) { switch (x) { case 1: continue; } }\n");
    /* Labels: function scope, so an undefined one is knowable only at the
     * closing brace, and a duplicate is an error. */
    expr_bad(t, "void f(void) { goto nope; }\n");
    expr_bad(t, "void f(void) { a: ; a: ; }\n");
}

void test_stmt_shapes(TestCtx *t)
{
    ExprFix f;
    AstNode *tu;
    AstNode *body;

    /* Dangling else binds to the NEAREST if. */
    tu = parse_src_e(&f,
                     "int a,b,x;\n"
                     "void f(void){ if (a) if (b) x=1; else x=2; }\n",
                     STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    body = tu->decls[tu->ndecls - 1]->body;
    T_ASSERT(t, body->items[0]->kind == AST_STMT_IF);
    T_ASSERT(t, body->items[0]->rhs == NULL); /* outer if has NO else */
    T_ASSERT(t, body->items[0]->body->kind == AST_STMT_IF);
    T_ASSERT(t, body->items[0]->body->rhs != NULL); /* inner one has it */
    efix_free(&f);

    /* Duff's device: `case` labels inside a nested do-while, which is only
     * possible because case is an ordinary label rather than a member of
     * a switch-body production. */
    tu = parse_src_e(&f,
                     "void send(int *to, int *from, int count) {\n"
                     "  int n = (count + 7) / 8;\n"
                     "  switch (count % 8) {\n"
                     "  case 0: do { *to = *from++;\n"
                     "  case 7:      *to = *from++;\n"
                     "  case 1:      *to = *from++;\n"
                     "          } while (--n > 0);\n"
                     "  }\n}\n",
                     STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    body = tu->decls[tu->ndecls - 1]->body;
    T_ASSERT(t, body->items[1]->kind == AST_STMT_SWITCH);
    {
        /* switch body -> block -> case 0 -> do -> block -> [expr, case 7,
         * case 1] */
        AstNode *blk = body->items[1]->body;
        AstNode *case0 = blk->items[0];
        AstNode *dostmt, *doblk;

        T_ASSERT(t, case0->kind == AST_STMT_CASE);
        dostmt = case0->body;
        T_ASSERT(t, dostmt->kind == AST_STMT_DO);
        doblk = dostmt->body;
        T_ASSERT(t, doblk->kind == AST_STMT_COMPOUND);
        T_ASSERT(t, doblk->items[1]->kind == AST_STMT_CASE);
        T_ASSERT(t, doblk->items[2]->kind == AST_STMT_CASE);
    }
    efix_free(&f);

    /* A label and a variable of the same name coexist: separate
     * namespaces. */
    tu = parse_src_e(&f,
                     "int f(void){ int foo = 0; foo: foo = 1;"
                     " if (foo) goto foo; return foo; }\n",
                     STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    efix_free(&f);
}

/* A for-init declaration scopes over the condition, the step, and the
 * body — and ends with the loop. */
void test_stmt_for_init_scope(TestCtx *t)
{
    ExprFix f;
    AstNode *tu;

    tu = parse_src_e(
        &f,
        "typedef int T;\n"
        "void f(void){ for (int T = 0; T < 1; T++) ; T x; (void)x; }\n",
        STD_C17);
    /* Inside the loop `T` is a variable (so `T++` parses as an
     * expression); after the loop the typedef is visible again, so
     * `T x;` is a declaration. If the scope leaked, this would fail. */
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->decls[1]->body->nitems == 3);
    T_ASSERT(t, tu->decls[1]->body->items[1]->kind == AST_STMT_DECL);
    efix_free(&f);

    /* c89 has no for-init declarations. */
    (void)parse_src_e(&f, "void f(void){ for (int i = 0; i < 1; i++) ; }\n",
                      STD_C89);
    T_ASSERT(t, f.errors >= 1);
    efix_free(&f);

    /* c89 pedwarns on declarations after a statement; c17 does not. */
    (void)parse_src_e(
        &f, "void f(void){ int a = 0; a = 1; int b = a; (void)b; }\n", STD_C89);
    T_ASSERT(t, f.warnings >= 1);
    efix_free(&f);

    (void)parse_src_e(
        &f, "void f(void){ int a = 0; a = 1; int b = a; (void)b; }\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    efix_free(&f);
}

/* Block scope finally exists, so THE typedef pitfall the sprint-9 file
 * called pitfall 1 can be tested in its original form. */
void test_stmt_block_scope_typedef_pitfall(TestCtx *t)
{
    ExprFix f;
    AstNode *tu = NULL;

    /* `{ T T; T x; }` — the specifier of the first declaration still sees
     * the typedef, so it declares a VARIABLE T; from that declarator's
     * completion onward T is an ordinary identifier, so `T x;` is an
     * error rather than a second declaration. */
    (void)parse_src_e(&f, "typedef int T;\nvoid f(void){ T T; T x; }\n",
                      STD_C17);
    T_ASSERT(t, f.errors >= 1);
    efix_free(&f);

    /* Without the shadowing declaration, the same line is fine. */
    tu = parse_src_e(&f, "typedef int T;\nvoid f(void){ T x; (void)x; }\n",
                     STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    efix_free(&f);

    /* And the shadow is undone by the closing brace. */
    tu = parse_src_e(&f,
                     "typedef int T;\nvoid f(void){ { T T; (void)T; } T y;"
                     " (void)y; }\n",
                     STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->ndecls == 2);
    efix_free(&f);

    /* `T * p;` at block scope is a DECLARATION when T is a typedef and a
     * multiplication when it is not — the same tokens, decided by the
     * scope stack alone. */
    tu = parse_src_e(&f, "typedef int T; int p;\nvoid f(void){ T * p; }\n",
                     STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->decls[2]->body->items[0]->kind == AST_STMT_DECL);
    efix_free(&f);

    tu = parse_src_e(&f, "int T; int p;\nvoid f(void){ T * p; }\n", STD_C17);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, tu->decls[2]->body->items[0]->kind == AST_STMT_EXPR);
    efix_free(&f);
}
