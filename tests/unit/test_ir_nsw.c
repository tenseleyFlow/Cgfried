#include <string.h>

#include "lower/lower.h"
#include "parse/parse.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
} NswFix;

static void nsw_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    NswFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        f->errors++;
}

static void nsw_fix_init(NswFix *f)
{
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = nsw_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
}

static int text_count(const char *text, const char *needle)
{
    int count = 0;
    size_t n = strlen(needle);

    while ((text = strstr(text, needle)) != NULL) {
        count++;
        text += n;
    }
    return count;
}

void test_ir_nsw_roundtrip(TestCtx *t)
{
    static const char source[] = "func i32 @f(i32 %a, i32 %b) {\n"
                                 "entry():\n"
                                 "    %x = iadd nsw i32 %a, %b\n"
                                 "    %y = isub nsw i32 %x, %b\n"
                                 "    %z = imul nsw i32 %y, %a\n"
                                 "    ret i32 %z\n"
                                 "}\n";
    NswFix f;
    IrModule *m, *round;
    Buf text;

    nsw_fix_init(&f);
    m = ir_parse_module(&f.arena, f.dc, source, "nsw.ir");
    T_ASSERT(t, m != NULL);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    if (m) {
        const IrInst *in = m->funcs[0].blocks[0].first;

        T_ASSERT(t, in && (in->flags & IRF_NSW));
        T_ASSERT(t, in && in->next && (in->next->flags & IRF_NSW));
        T_ASSERT(t, in && in->next && in->next->next &&
                        (in->next->next->flags & IRF_NSW));
    }
    buf_init(&text);
    if (m)
        ir_print_module_buf(&text, m);
    buf_push_u8(&text, 0);
    T_ASSERT_EQ_INT(t, text_count((const char *)text.data, ", nsw"), 3);
    round = ir_parse_module(&f.arena, f.dc, (const char *)text.data,
                            "nsw-round.ir");
    T_ASSERT(t, round != NULL);
    T_ASSERT(t, m && round && ir_module_struct_eq(m, round));
    buf_free(&text);
    arena_free_all(&f.arena);
}

void test_ir_nsw_invalid_placement_rejected(TestCtx *t)
{
    NswFix f;
    IrModule *m;
    IrFunc *fn;
    IrBuilder b;
    BlockId entry;

    nsw_fix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "f", IRT_VOID, NULL, 0);
    entry = ir_block_new(m, fn, "entry");
    ir_builder_at(&b, m, fn, entry);
    (void)ir_build2_flags(&b, IR_AND, IRT_I32, ir_op_iconst(IRT_I32, 1),
                          ir_op_iconst(IRT_I32, 2), IRF_NSW);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, f.errors > 0);
    arena_free_all(&f.arena);
}

VEC_DECL(NswPpVec, PpToken);

static Buf lower_text(NswFix *f, const char *source, bool fwrapv)
{
    Interner interner;
    Preprocessor pp;
    Parser parser;
    Sema sema;
    LangOpts lang;
    TargetSpec target;
    SourceFile *sf;
    NswPpVec toks = {NULL, 0, 0};
    PpToken tok;
    TokenList converted;
    AstNode *tu;
    IrModule *m;
    Buf text;

    intern_init(&interner, &f->arena);
    pp_init(&pp, &f->arena, f->dc, &interner);
    memset(&lang, 0, sizeof(lang));
    lang.std = STD_C17;
    lang.fwrapv = fwrapv;
    target.kind = CGF_TARGET_X86_64_LINUX_GNU;
    sf = pp_source_add_buffer(&pp, "nsw.c", source, strlen(source));
    pp_begin(&pp, sf, NULL);
    while (pp_next(&pp, &tok))
        NswPpVec_push(&toks, tok);
    converted =
        lex_convert(&pp, toks.data, (u32)toks.len, &lang, target, &f->arena);
    NswPpVec_free(&toks);
    parse_init(&parser, &converted, &pp, f->dc, &f->arena, &lang);
    tu = parse_translation_unit(&parser);
    sema_init(&sema, &f->arena, f->dc, &interner, &lang, target);
    sema_run(&sema, tu);
    m = f->errors ? NULL : lower_translation_unit(&f->arena, f->dc, &sema, tu);
    buf_init(&text);
    if (m)
        ir_print_module_buf(&text, m);
    buf_push_u8(&text, 0);
    pp_end(&pp);
    intern_free(&interner);
    pp_loc_free(&pp.loc);
    strmap_free(&pp.macros);
    return text;
}

void test_ir_nsw_lowering_respects_signedness_and_fwrapv(TestCtx *t)
{
    static const char signed_source[] =
        "int s(int a, int b) { a++; a *= 3; return -(a + b); }\n";
    static const char unsigned_source[] =
        "unsigned u(unsigned a, unsigned b) { return (a + b) * (a - b); }\n";
    NswFix strict, unsigned_fix, wrap;
    Buf strict_text, unsigned_text, wrap_text;

    nsw_fix_init(&strict);
    strict_text = lower_text(&strict, signed_source, false);
    T_ASSERT_EQ_INT(t, strict.errors, 0);
    T_ASSERT_EQ_INT(t, text_count((const char *)strict_text.data, ", nsw"), 4);

    nsw_fix_init(&unsigned_fix);
    unsigned_text = lower_text(&unsigned_fix, unsigned_source, false);
    T_ASSERT_EQ_INT(t, unsigned_fix.errors, 0);
    T_ASSERT_EQ_INT(t, text_count((const char *)unsigned_text.data, ", nsw"),
                    0);

    nsw_fix_init(&wrap);
    wrap_text = lower_text(&wrap, signed_source, true);
    T_ASSERT_EQ_INT(t, wrap.errors, 0);
    T_ASSERT_EQ_INT(t, text_count((const char *)wrap_text.data, ", nsw"), 0);

    buf_free(&strict_text);
    buf_free(&unsigned_text);
    buf_free(&wrap_text);
    arena_free_all(&strict.arena);
    arena_free_all(&unsigned_fix.arena);
    arena_free_all(&wrap.arena);
}
