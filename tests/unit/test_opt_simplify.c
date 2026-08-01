#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} SimplifyFix;

static void simplify_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void simplify_fix_init(SimplifyFix *f)
{
    DiagSink sink = {simplify_silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *simplify_parse(SimplifyFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<simplify-test>");
}

static char *simplify_print(IrModule *m, Buf *out)
{
    buf_init(out);
    ir_print_module_buf(out, m);
    buf_push_u8(out, 0);
    return (char *)out->data;
}

static u32 simplify_count_op(const IrModule *m, IrOp op)
{
    u32 fi, bi, n = 0;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next)
                if (in->op == op)
                    n++;
        }
    return n;
}

void test_opt_fold_inst_uses_width_and_undef_rules(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    IrOperand out;
    const IrInst *in;

    simplify_fix_init(&f);
    m = simplify_parse(&f,
                       "func void @f() {\n"
                       "entry():\n"
                       "    %wrap = iadd i8 255, 2\n"
                       "    %shift = shl i32 1, 32\n"
                       "    %overflow = sdiv i32 2147483648, 4294967295\n"
                       "    %too_large = fptosi f64 0x4072C00000000000 to i8\n"
                       "    %negative = fptoui f64 0xBFF0000000000000 to i8\n"
                       "    ret\n"
                       "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    if (m) {
        in = m->funcs[0].blocks[0].first;
        T_ASSERT(t, opt_fold_inst(in, &out, &cfg));
        T_ASSERT_EQ_INT(t, out.kind, IROP_ICONST);
        T_ASSERT_EQ_INT(t, out.type, IRT_I8);
        T_ASSERT_EQ_INT(t, out.a, 1);
        in = in->next;
        T_ASSERT(t, opt_fold_inst(in, &out, &cfg));
        T_ASSERT_EQ_INT(t, out.kind, IROP_UNDEF);
        in = in->next;
        T_ASSERT(t, opt_fold_inst(in, &out, &cfg));
        T_ASSERT_EQ_INT(t, out.kind, IROP_UNDEF);
        in = in->next;
        T_ASSERT(t, opt_fold_inst(in, &out, &cfg));
        T_ASSERT_EQ_INT(t, out.kind, IROP_UNDEF);
        in = in->next;
        T_ASSERT(t, opt_fold_inst(in, &out, &cfg));
        T_ASSERT_EQ_INT(t, out.kind, IROP_UNDEF);
    }
    arena_free_all(&f.arena);
}

void test_opt_fold_inst_f80_f128_exact_bits(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    IrOperand out;
    const IrInst *in;

    simplify_fix_init(&f);
    m = simplify_parse(&f, "func void @f() {\n"
                           "entry():\n"
                           "    %a = fadd f80 0x3FFF:0x8000000000000000, "
                           "0x3FC0:0x8000000000000000\n"
                           "    %b = fadd f128 "
                           "0x3FFF000000000000:0x0000000000000000, "
                           "0x3F9B000000000000:0x0000000000000000\n"
                           "    ret\n"
                           "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    if (m) {
        in = m->funcs[0].blocks[0].first;
        T_ASSERT(t, opt_fold_inst(in, &out, &cfg));
        T_ASSERT_EQ_INT(t, out.kind, IROP_FCONST);
        T_ASSERT(t, out.b == 0x3fffull);
        T_ASSERT(t, out.a == 0x8000000000000001ull);
        in = in->next;
        T_ASSERT(t, opt_fold_inst(in, &out, &cfg));
        T_ASSERT_EQ_INT(t, out.kind, IROP_FCONST);
        T_ASSERT(t, out.b == 0x3fff000000000000ull);
        T_ASSERT(t, out.a == 0x1000ull);
    }
    arena_free_all(&f.arena);
}

void test_opt_fold_inst_preserves_nan_payload_operations(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    IrOperand out;
    const IrInst *in;
    u32 checked = 0;

    simplify_fix_init(&f);
    m = simplify_parse(&f, "func void @f() {\n"
                           "entry():\n"
                           "    %a = fadd f32 0x7F800001, 0x3F800000\n"
                           "    %b = fmul f32 0x7FC01234, 0x3F800000\n"
                           "    %c = fadd f64 0x7FF0000000000001, "
                           "0x3FF0000000000000\n"
                           "    %d = fdiv f64 0x7FF8000000001234, "
                           "0x3FF0000000000000\n"
                           "    %e = fneg f64 0x7FF0000000000001\n"
                           "    %f = fpext f32 0x7F800001 to f64\n"
                           "    %g = fptrunc f64 0x7FF8000000001234 to f32\n"
                           "    %h = fadd f80 0x7FFF:0xC000000000000001, "
                           "0x3FFF:0x8000000000000000\n"
                           "    %i = fadd f128 "
                           "0x7FFF800000000123:0x456789ABCDEF0123, "
                           "0x3FFF000000000000:0x0000000000000000\n"
                           "    %j = fdiv f64 0x0000000000000000, "
                           "0x0000000000000000\n"
                           "    ret\n"
                           "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    if (m)
        for (in = m->funcs[0].blocks[0].first; in && in->result.v;
             in = in->next) {
            T_ASSERT(t, !opt_fold_inst(in, &out, &cfg));
            checked++;
        }
    T_ASSERT_EQ_INT(t, checked, 10);
    arena_free_all(&f.arena);
}

void test_opt_simplify_integer_catalog(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;
    static const char src[] = "func i32 @catalog(i32 %x) {\n"
                              "entry():\n"
                              "    %a = iadd i32 %x, 0\n"
                              "    %b = isub i32 %a, 0\n"
                              "    %c = imul i32 %b, 1\n"
                              "    %d = imul i32 %c, 8, nsw\n"
                              "    %e = and i32 %d, 4294967295\n"
                              "    %f = or i32 %e, 0\n"
                              "    %g = xor i32 %f, 0\n"
                              "    %n1 = isub i32 0, %g\n"
                              "    %n2 = isub i32 0, %n1\n"
                              "    %x1 = xor i32 %n2, 85\n"
                              "    %x2 = xor i32 %x1, 85\n"
                              "    ret i32 %x2\n"
                              "}\n"
                              "func i32 @reflexive(i32 %x) {\n"
                              "entry():\n"
                              "    %a = isub i32 %x, %x\n"
                              "    %b = imul i32 %x, 0\n"
                              "    %r = iadd i32 %a, %b\n"
                              "    ret i32 %r\n"
                              "}\n"
                              "func i32 @cmp(i32 %x) {\n"
                              "entry():\n"
                              "    %r = icmp slt i32 7, %x\n"
                              "    ret i32 %r\n"
                              "}\n";

    simplify_fix_init(&f);
    m = simplify_parse(&f, src);
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_simplify(m, &cfg));
    if (m) {
        s = simplify_print(m, &text);
        T_ASSERT(t, strstr(s, "shl i32 %0, 3") != NULL);
        T_ASSERT(t, strstr(s, ", nsw") == NULL);
        T_ASSERT(t, strstr(s, "ret i32 0") != NULL);
        T_ASSERT(t, strstr(s, "icmp sgt i32 %0, 7") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_div_rem_and_shift_catalog(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;
    static const char src[] = "func i32 @signed_div(i32 %x) {\n"
                              "entry():\n"
                              "    %q = sdiv i32 %x, 8\n"
                              "    ret i32 %q\n"
                              "}\n"
                              "func i32 @signed_rem(i32 %x) {\n"
                              "entry():\n"
                              "    %r = srem i32 %x, 8\n"
                              "    ret i32 %r\n"
                              "}\n"
                              "func i32 @unsigned(i32 %x) {\n"
                              "entry():\n"
                              "    %q = udiv i32 %x, 8\n"
                              "    %r = urem i32 %x, 8\n"
                              "    %z = or i32 %q, %r\n"
                              "    ret i32 %z\n"
                              "}\n"
                              "func i32 @logical_pair(i32 %x) {\n"
                              "entry():\n"
                              "    %l = shl i32 %x, 5\n"
                              "    %r = lshr i32 %l, 5\n"
                              "    ret i32 %r\n"
                              "}\n"
                              "func i32 @real_sext(i8 %x) {\n"
                              "entry():\n"
                              "    %z = zext i8 %x to i32\n"
                              "    %l = shl i32 %z, 24\n"
                              "    %r = ashr i32 %l, 24\n"
                              "    ret i32 %r\n"
                              "}\n";

    simplify_fix_init(&f);
    m = simplify_parse(&f, src);
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_simplify(m, &cfg));
    if (m) {
        s = simplify_print(m, &text);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_SDIV), 0);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_SREM), 0);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_UDIV), 0);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_UREM), 0);
        T_ASSERT(t, strstr(s, "ashr i32 %0, 31") != NULL);
        T_ASSERT(t, strstr(s, "lshr i32") != NULL);
        T_ASSERT(t, strstr(s, "and i32 %0, 134217727") != NULL);
        T_ASSERT(t, strstr(s, "sext i8 %0 to i32") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_preserves_signed_min_divisors(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    simplify_fix_init(&f);
    m = simplify_parse(&f, "func i8 @div8(i8 %x) {\n"
                           "entry():\n"
                           "    %q = sdiv i8 %x, 128\n"
                           "    %r = srem i8 %x, 128\n"
                           "    %v = iadd i8 %q, %r\n"
                           "    ret i8 %v\n"
                           "}\n"
                           "func i32 @div32(i32 %x) {\n"
                           "entry():\n"
                           "    %q = sdiv i32 %x, 2147483648\n"
                           "    %r = srem i32 %x, 2147483648\n"
                           "    %v = iadd i32 %q, %r\n"
                           "    ret i32 %v\n"
                           "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && !opt_simplify(m, &cfg));
    if (m) {
        s = simplify_print(m, &text);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_SDIV), 2);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_SREM), 2);
        T_ASSERT(t, strstr(s, "sdiv i8 %0, 128") != NULL);
        T_ASSERT(t, strstr(s, "sdiv i32 %0, 2147483648") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_zext_compare_catalog(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    simplify_fix_init(&f);
    m = simplify_parse(&f, "func i32 @fits(i8 %x) {\n"
                           "entry():\n"
                           "    %z = zext i8 %x to i32\n"
                           "    %r = icmp ult i32 %z, 200\n"
                           "    ret i32 %r\n"
                           "}\n"
                           "func i32 @outside(i8 %x) {\n"
                           "entry():\n"
                           "    %z = zext i8 %x to i32\n"
                           "    %r = icmp ult i32 %z, 300\n"
                           "    ret i32 %r\n"
                           "}\n"
                           "func i32 @zero(i8 %x) {\n"
                           "entry():\n"
                           "    %z = zext i8 %x to i32\n"
                           "    %r = icmp ult i32 %z, 0\n"
                           "    ret i32 %r\n"
                           "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && opt_simplify(m, &cfg));
    if (m) {
        s = simplify_print(m, &text);
        T_ASSERT(t, strstr(s, "icmp ult i8 %0, 200") != NULL);
        T_ASSERT(t, strstr(s, "ret i32 1") != NULL);
        T_ASSERT(t, strstr(s, "ret i32 0") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_fp_exact_and_forbidden_identities(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    simplify_fix_init(&f);
    m = simplify_parse(&f, "func f64 @f(f64 %x) {\n"
                           "entry():\n"
                           "    %keep_add = fadd f64 %x, 0x0000000000000000\n"
                           "    %drop_add = fadd f64 %keep_add, "
                           "0x8000000000000000\n"
                           "    %keep_mul = fmul f64 %drop_add, "
                           "0x0000000000000000\n"
                           "    %drop_mul = fmul f64 %keep_mul, "
                           "0x3FF0000000000000\n"
                           "    %keep_sub = fsub f64 %drop_mul, %drop_mul\n"
                           "    %n1 = fneg f64 %keep_sub\n"
                           "    %n2 = fneg f64 %n1\n"
                           "    ret f64 %n2\n"
                           "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    T_ASSERT(t, m && opt_simplify(m, &cfg));
    if (m) {
        s = simplify_print(m, &text);
        T_ASSERT(t, strstr(s, "fadd f64 %0, 0x0000000000000000") != NULL);
        T_ASSERT(t, strstr(s, "fadd f64 %1, 0x8000000000000000") != NULL);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_FMUL), 2);
        T_ASSERT(t, strstr(s, "fsub f64") != NULL);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_FNEG), 1);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_fast_math_relicenses_fp_identities(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    simplify_fix_init(&f);
    m = simplify_parse(&f, "func f64 @f(f64 %x) {\n"
                           "entry():\n"
                           "    %a = fadd f64 %x, 0x0000000000000000\n"
                           "    %b = fmul f64 %a, 0x3FF0000000000000\n"
                           "    %c = fmul f64 %b, 0x0000000000000000\n"
                           "    %d = fsub f64 %x, %x\n"
                           "    %r = fadd f64 %c, %d\n"
                           "    ret f64 %r\n"
                           "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_OFAST);
    T_ASSERT(t, m && opt_simplify(m, &cfg));
    if (m) {
        s = simplify_print(m, &text);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_FADD), 0);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_FMUL), 0);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_FSUB), 0);
        T_ASSERT(t, strstr(s, "ret f64 0x0000000000000000") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_preserves_undef_derived_reflexive_and_div(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;

    simplify_fix_init(&f);
    m = simplify_parse(&f, "func i32 @f(i32 %c, i32 %x) {\n"
                           "entry():\n"
                           "    condbr %c, unknown(), defined()\n"
                           "unknown():\n"
                           "    br join(i32 undef)\n"
                           "defined():\n"
                           "    br join(i32 %x)\n"
                           "join(i32 %v):\n"
                           "    %same = isub i32 %v, %v\n"
                           "    %q = sdiv i32 %v, 8\n"
                           "    %r = iadd i32 %same, %q\n"
                           "    ret i32 %r\n"
                           "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    (void)(m && opt_simplify(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_ISUB), 1);
        T_ASSERT_EQ_INT(t, simplify_count_op(m, IR_SDIV), 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_folds_guaranteed_ub_and_tracks_possible_undef(TestCtx *t)
{
    SimplifyFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    simplify_fix_init(&f);
    m = simplify_parse(&f, "func i32 @f(i32 %x, i32 %y) {\n"
                           "entry():\n"
                           "    %divzero = sdiv i32 %x, 0\n"
                           "    %badshift = shl i32 %x, 32\n"
                           "    %maybe = sdiv i32 %x, %y\n"
                           "    %same = isub i32 %maybe, %maybe\n"
                           "    %r = xor i32 %same, %divzero\n"
                           "    ret i32 %r\n"
                           "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && opt_simplify(m, &cfg));
    if (m) {
        s = simplify_print(m, &text);
        T_ASSERT(t, strstr(s, "sdiv i32 %0, 0") == NULL);
        T_ASSERT(t, strstr(s, "shl i32 %0, 32") == NULL);
        T_ASSERT(t, strstr(s, "sdiv i32 %0, %1") != NULL);
        T_ASSERT(t, strstr(s, "isub i32 %2, %2") != NULL);
        T_ASSERT(t, strstr(s, "undef") != NULL);
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}
