#include <string.h>

#include "ir/ir.h"
#include "lower/lower.h"
#include "parse/parse.h"
#include "unit.h"

#include "util/arena.h"
#include "warn/warn.h"

#define TEST_GNU_ATTRIBUTE                                                     \
    "__attribute__" /* check_bans allow: compiler input, not host C */

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
} MetaFix;

static void meta_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    MetaFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        f->errors++;
}

static void meta_init(MetaFix *f)
{
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = meta_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
}

VEC_DECL(MetaPpVec, PpToken);

static IrModule *meta_lower_std(MetaFix *f, const char *src, CStd std)
{
    SourceFile *sf;
    MetaPpVec pv = {NULL, 0, 0};
    PpToken ppt;
    TokenList tl;
    LangOpts lang;
    TargetSpec target;
    AstNode *tu;

    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);
    memset(&lang, 0, sizeof(lang));
    lang.std = std;
    lang.gnu_mode = std >= STD_GNU89;
    lang.warnings = warn_ctx_new(&f->arena, f->dc);
    f->pp.warn = lang.warnings;
    target.kind = CGF_TARGET_X86_64_LINUX_GNU;
    sf = pp_source_add_buffer(&f->pp, "meta.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &ppt))
        MetaPpVec_push(&pv, ppt);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    MetaPpVec_free(&pv);
    parse_init(&f->ps, &tl, &f->pp, f->dc, &f->arena, &lang);
    tu = parse_translation_unit(&f->ps);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &lang, target);
    sema_run(&f->sema, tu);
    if (f->errors)
        return NULL;
    return lower_translation_unit(&f->arena, f->dc, &f->sema, tu);
}

static IrModule *meta_lower(MetaFix *f, const char *src)
{
    return meta_lower_std(f, src, STD_C17);
}

static const IrInst *meta_first_load(const IrFunc *fn, IrType type)
{
    u32 bi;

    for (bi = 0; bi < fn->nblocks; bi++) {
        const IrInst *in;

        for (in = fn->blocks[bi].first; in; in = in->next)
            if (in->op == IR_LOAD && in->type == type)
                return in;
    }
    return NULL;
}

static void meta_pp_free(MetaFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
}

void test_ir_effective_type_roundtrip(TestCtx *t)
{
    MetaFix f;
    IrModule *m;
    IrModule *parsed;
    IrFunc *fn;
    IrType pt = IRT_PTR;
    BlockId entry;
    IrBuilder b;
    ValueId p;
    const IrInst *in;
    Buf text;

    meta_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "typed", IRT_VOID, &pt, 1);
    fn->param_annots = arena_alloc(&f.arena, sizeof(u64), _Alignof(u64));
    fn->param_annots[0] = IR_PARAM_RESTRICT;
    entry = ir_block_new(m, fn, "entry");
    ir_builder_at(&b, m, fn, entry);
    p = ir_build_alloca_typed(&b, ir_op_iconst(IRT_I64, 8), 8, ETYPE_UNION);
    (void)ir_build_load_typed(&b, IRT_I32, ir_op_value(fn, p), 4, 0, ETYPE_I32);
    ir_build_store_typed(&b, ir_op_iconst(IRT_I32, 1), ir_op_value(fn, p), 4,
                         IRF_VOLATILE, ETYPE_CHAR);
    ir_build_memset(&b, ir_op_value(fn, p), ir_op_iconst(IRT_I32, 0),
                    ir_op_iconst(IRT_I64, 8), 1, 0);
    ir_build_ret(&b, NULL);

    T_ASSERT_EQ_INT(t, sizeof(IrInst), 48);
    T_ASSERT(t, ir_param_is_restrict(fn->param_annots[0]));
    in = fn->blocks[0].first;
    T_ASSERT_EQ_INT(t, in->subop, ETYPE_UNION);
    T_ASSERT_EQ_INT(t, in->next->subop, ETYPE_I32);
    T_ASSERT_EQ_INT(t, in->next->next->subop, ETYPE_CHAR);
    T_ASSERT_EQ_INT(t, in->next->next->next->subop, ETYPE_CHAR);

    buf_init(&text);
    ir_print_module_buf(&text, m);
    buf_push_u8(&text, 0);
    T_ASSERT(t, strstr((const char *)text.data, "ptr restrict %0") != NULL);
    T_ASSERT(t, strstr((const char *)text.data, "etype union") != NULL);
    T_ASSERT(t, strstr((const char *)text.data, "etype i32") != NULL);
    T_ASSERT(t, strstr((const char *)text.data, "etype char") != NULL);
    parsed =
        ir_parse_module(&f.arena, f.dc, (const char *)text.data, "meta.ir");
    T_ASSERT(t, parsed != NULL);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    if (parsed) {
        T_ASSERT(t, ir_module_struct_eq(m, parsed));
        T_ASSERT(t, ir_param_is_restrict(parsed->funcs[0].param_annots[0]));
        T_ASSERT(t, ir_verify(f.dc, parsed));
    }
    buf_free(&text);
    arena_free_all(&f.arena);
}

void test_lower_effective_type_classes(TestCtx *t)
{
    Arena arena;
    Lower lo;
    Sema sema;

    arena_init(&arena);
    memset(&lo, 0, sizeof(lo));
    memset(&sema, 0, sizeof(sema));
    sema.target.kind = CGF_TARGET_X86_64_LINUX_GNU;
    lo.sema = &sema;

    T_ASSERT_EQ_INT(t, lower_efftype(&lo, type_basic(TY_CHAR)), ETYPE_CHAR);
    T_ASSERT_EQ_INT(t, lower_efftype(&lo, type_basic(TY_SCHAR)), ETYPE_CHAR);
    T_ASSERT_EQ_INT(t, lower_efftype(&lo, type_basic(TY_UCHAR)), ETYPE_CHAR);
    T_ASSERT_EQ_INT(t, lower_efftype(&lo, type_basic(TY_INT)),
                    lower_efftype(&lo, type_basic(TY_UINT)));
    T_ASSERT_EQ_INT(t, lower_efftype(&lo, type_basic(TY_LONG)),
                    lower_efftype(&lo, type_basic(TY_ULONG)));
    T_ASSERT_EQ_INT(t, lower_efftype(&lo, type_basic(TY_FLOAT)), ETYPE_F32);
    T_ASSERT_EQ_INT(t, lower_efftype(&lo, type_ptr(&arena, type_basic(TY_INT))),
                    ETYPE_PTR);
    arena_free_all(&arena);
}

void test_lower_may_alias_types_use_wildcard_effective_type(TestCtx *t)
{
    static const char src[] =
        "typedef int alias_int " TEST_GNU_ATTRIBUTE "((may_alias));\n"
        "struct " TEST_GNU_ATTRIBUTE "((__may_alias__)) S { int x; };\n"
        "typedef int alias_array[2] " TEST_GNU_ATTRIBUTE "((may_alias));\n"
        "struct " TEST_GNU_ATTRIBUTE "((may_alias)) Forward;\n"
        "struct Forward { int x; };\n"
        "typedef int alias_unsized[] " TEST_GNU_ATTRIBUTE "((may_alias));\n"
        "alias_unsized completed = { 1 };\n"
        "struct " TEST_GNU_ATTRIBUTE "((may_alias)) Bits { unsigned x : 3; };\n"
        "struct PlainTag { int x; };\n"
        "typedef struct PlainTag TagAlias " TEST_GNU_ATTRIBUTE
        "((may_alias));\n"
        "int td(alias_int *p) { return *p; }\n"
        "int rec(struct S *p) { return p->x; }\n"
        "int arr(alias_array *p) { return (*p)[0]; }\n"
        "int complete(void) { return completed[0]; }\n"
        "int bits(struct Bits *p) { return p->x; }\n"
        "int fwd(struct Forward *p) { return p->x; }\n"
        "int tag_alias(TagAlias *p) { return p->x; }\n"
        "int plain(int *p) { return *p; }\n";
    MetaFix f;
    IrModule *m;
    const IrInst *load;
    u32 i;

    meta_init(&f);
    m = meta_lower_std(&f, src, STD_GNU17);
    T_ASSERT(t, m != NULL);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, m ? m->nfuncs : 0, 8);
    if (!m || m->nfuncs != 8) {
        meta_pp_free(&f);
        arena_free_all(&f.arena);
        return;
    }
    for (i = 0; i < 5; i++) {
        load = meta_first_load(&m->funcs[i], IRT_I32);
        T_ASSERT(t, load != NULL);
        T_ASSERT_EQ_INT(t, load ? load->subop : ETYPE_COUNT, ETYPE_UNKNOWN);
    }
    for (i = 5; i < 8; i++) {
        load = meta_first_load(&m->funcs[i], IRT_I32);
        T_ASSERT(t, load != NULL);
        T_ASSERT_EQ_INT(t, load ? load->subop : ETYPE_COUNT, ETYPE_I32);
    }
    T_ASSERT(t, ir_verify(f.dc, m));
    meta_pp_free(&f);
    arena_free_all(&f.arena);
}

void test_lower_effective_type_and_restrict_markers(TestCtx *t)
{
    static const char src[] =
        "union U { int i; float f; };\n"
        "int f(int *restrict p, unsigned *restrict q, char *c, union U *u) {\n"
        "  *p = 1; *q = 2; c[0] = 3; u->i = 4; return *p;\n"
        "}\n";
    MetaFix f;
    IrModule *m;
    Buf text;
    const char *s;

    meta_init(&f);
    m = meta_lower(&f, src);
    T_ASSERT(t, m != NULL);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    if (!m) {
        meta_pp_free(&f);
        arena_free_all(&f.arena);
        return;
    }
    buf_init(&text);
    ir_print_module_buf(&text, m);
    buf_push_u8(&text, 0);
    s = (const char *)text.data;
    T_ASSERT(t, strstr(s, "ptr restrict %0, ptr restrict %1") != NULL);
    T_ASSERT(t, strstr(s, "etype i32") != NULL);
    T_ASSERT(t, strstr(s, "etype char") != NULL);
    T_ASSERT(t, strstr(s, "etype union") != NULL);
    T_ASSERT(t, ir_verify(f.dc, m));
    buf_free(&text);
    meta_pp_free(&f);
    arena_free_all(&f.arena);
}

void test_ir_metadata_verifier_rejects_invalid(TestCtx *t)
{
    MetaFix f;
    IrModule *m;
    IrFunc *fn;
    IrType pt = IRT_I32;
    BlockId entry;
    IrBuilder b;
    IrInst *in;

    meta_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "bad", IRT_VOID, &pt, 1);
    fn->param_annots = arena_alloc(&f.arena, sizeof(u64), _Alignof(u64));
    fn->param_annots[0] = IR_PARAM_RESTRICT;
    entry = ir_block_new(m, fn, "entry");
    ir_builder_at(&b, m, fn, entry);
    (void)ir_build_load(&b, IRT_I32, ir_op_symbol(IRT_PTR, ir_sym(m, "x"), 0),
                        4, 0);
    ir_build_ret(&b, NULL);
    in = fn->blocks[0].first;
    in->subop = ETYPE_COUNT;

    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, f.errors >= 2);
    arena_free_all(&f.arena);
}
