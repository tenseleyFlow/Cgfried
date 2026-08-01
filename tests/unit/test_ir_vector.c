#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cg/x86_64/mir.h"
#include "ir/ir.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
} VecFix;

static void vec_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    VecFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        f->errors++;
}

static void vec_init(VecFix *f)
{
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = vec_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
}

static IrModule *parse_vec(VecFix *f, const char *body)
{
    return ir_parse_module(&f->arena, f->dc, body, "<vector-unit>");
}

static u32 count_text(const char *s, const char *needle)
{
    u32 n = 0;
    size_t len = strlen(needle);

    while ((s = strstr(s, needle)) != NULL) {
        n++;
        s += len;
    }
    return n;
}

void test_ir_vector_type_helpers(TestCtx *t)
{
    static const IrType vectors[] = {IRT_V16I8, IRT_V8I16, IRT_V4I32,
                                     IRT_V2I64, IRT_V4F32, IRT_V2F64};
    static const IrType elems[] = {IRT_I8,  IRT_I16, IRT_I32,
                                   IRT_I64, IRT_F32, IRT_F64};
    static const u32 lanes[] = {16, 8, 4, 2, 4, 2};
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(vectors); i++) {
        T_ASSERT(t, ir_type_is_vector(vectors[i]));
        T_ASSERT_EQ_INT(t, ir_vector_elem_type(vectors[i]), elems[i]);
        T_ASSERT_EQ_INT(t, ir_vector_lanes(vectors[i]), lanes[i]);
        T_ASSERT_EQ_INT(t, ir_type_size(vectors[i]), 16);
    }
    T_ASSERT(t, !ir_type_is_vector(IRT_I64));
    T_ASSERT_EQ_INT(t, ir_vector_elem_type(IRT_I64), IRT_VOID);
}

void test_ir_vector_roundtrip_and_verify(TestCtx *t)
{
    static const char src[] = "func i32 @vec() {\n"
                              "entry():\n"
                              "    %p = alloca 32, align 16\n"
                              "    %a = vsplat v4i32 i32 7\n"
                              "    store v4i32 %a, %p, align 16\n"
                              "    %b = load v4i32, %p, align 16\n"
                              "    %c = iadd v4i32 %b, %a\n"
                              "    %d = vextract v4i32 %c, 3\n"
                              "    ret i32 %d\n"
                              "}\n";
    VecFix f;
    IrModule *m, *m2;
    Buf out;

    vec_init(&f);
    m = parse_vec(&f, src);
    T_ASSERT(t, m != NULL);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    buf_init(&out);
    if (m)
        ir_print_module_buf(&out, m);
    buf_push_u8(&out, 0);
    m2 = parse_vec(&f, (const char *)out.data);
    T_ASSERT(t, m2 != NULL);
    T_ASSERT(t, m && m2 && ir_module_struct_eq(m, m2));
    buf_free(&out);
    arena_free_all(&f.arena);
}

void test_ir_vector_verifier_matrix(TestCtx *t)
{
    static const char bad_mul[] = "sym @x\nfunc void @f() {\nentry():\n"
                                  "  %a = load v4i32, @x, align 16\n"
                                  "  %b = imul v4i32 %a, %a\nret\n}\n";
    static const char bad_lane[] = "sym @x\nfunc void @f() {\nentry():\n"
                                   "  %a = load v2i64, @x, align 16\n"
                                   "  %b = vextract v2i64 %a, 2\nret\n}\n";
    static const char bad_volatile[] =
        "sym @x\nfunc void @f() {\nentry():\n"
        "  %a = load v4f32, @x, align 16, volatile\nret\n}\n";
    static const char bad_abi[] =
        "sym @x\nfunc v2f64 @f() {\nentry():\n"
        "  %a = load v2f64, @x, align 16\nret v2f64 %a\n}\n";
    static const char bad_select[] = "func void @f() {\nentry():\n"
                                     "  %a = vsplat v4i32 i32 1\n"
                                     "  %b = select 1, v4i32 %a, %a\nret\n}\n";
    const char *cases[] = {bad_mul, bad_lane, bad_volatile, bad_abi,
                           bad_select};
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(cases); i++) {
        VecFix f;
        IrModule *m;

        vec_init(&f);
        m = parse_vec(&f, cases[i]);
        T_ASSERT(t, m != NULL);
        T_ASSERT(t, m && !ir_verify(f.dc, m));
        arena_free_all(&f.arena);
    }
}

void test_ir_vector_atoms_and_lane_hardening(TestCtx *t)
{
    static const char *const malformed[] = {
        "sym @x\nfunc void @f() {\nentry():\n"
        "  %a = iadd v4i32 @x, undef\nret\n}\n",
        "func void @f() {\nentry():\n"
        "  %a = iadd v4i32 1, undef\nret\n}\n",
        "func void @f() {\nentry():\n"
        "  %a = vsplat v16i8 i8 1\n"
        "  %b = vextract v16i8 %a, 256\nret\n}\n",
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(malformed); i++) {
        VecFix f;
        IrModule *m;

        vec_init(&f);
        m = parse_vec(&f, malformed[i]);
        T_ASSERT(t, m == NULL);
        T_ASSERT(t, f.errors > 0);
        arena_free_all(&f.arena);
    }
    for (i = 0; i < 2; i++) {
        static const char src[] = "func void @f() {\nentry():\n"
                                  "  %a = vsplat v4i32 i32 1\n"
                                  "  %b = iadd v4i32 %a, %a\nret\n}\n";
        VecFix f;
        IrModule *m;
        IrInst *add;

        vec_init(&f);
        m = parse_vec(&f, src);
        T_ASSERT(t, m != NULL);
        add = m ? m->funcs[0].blocks[0].first->next : NULL;
        if (add) {
            add->ops[0] = i == 0 ? ir_op_symbol(IRT_V4I32, ir_sym(m, "x"), 0)
                                 : ir_op_iconst(IRT_V4I32, 1);
            T_ASSERT(t, !ir_verify(f.dc, m));
        }
        arena_free_all(&f.arena);
    }
    for (i = 0; i < 2; i++) {
        static const char src[] = "sym @x\nfunc void @f() {\nentry():\n"
                                  "  %a = vsplat v4i32 i32 1\n"
                                  "  br join(v4i32 %a)\n"
                                  "join(v4i32 %v):\nret\n}\n";
        VecFix f;
        IrModule *m;
        IrInst *br;

        vec_init(&f);
        m = parse_vec(&f, src);
        T_ASSERT(t, m != NULL);
        br = m ? m->funcs[0].blocks[0].last : NULL;
        if (br) {
            br->edges[0].args[0] =
                i == 0 ? ir_op_symbol(IRT_V4I32, ir_sym(m, "x"), 0)
                       : ir_op_iconst(IRT_V4I32, 1);
            T_ASSERT(t, !ir_verify(f.dc, m));
        }
        arena_free_all(&f.arena);
    }
}

static void bad_vextract_builder_child(void)
{
    Arena arena;
    DiagCtx *dc;
    IrModule *m;
    IrFunc *f;
    IrBuilder b;
    BlockId entry;
    ValueId v;

    close(STDERR_FILENO);
    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    m = ir_module_new(&arena, dc);
    f = ir_func_new(m, "f", IRT_VOID, NULL, 0);
    entry = ir_block_new(m, f, "entry");
    ir_builder_at(&b, m, f, entry);
    v = ir_build_vsplat(&b, IRT_V16I8, ir_op_iconst(IRT_I8, 1));
    (void)ir_build_vextract(&b, ir_op_value(f, v), 256);
    _exit(0);
}

void test_ir_vector_builder_rejects_lane_256_before_narrowing(TestCtx *t)
{
    pid_t pid;
    int status = 0;

    fflush(NULL);
    pid = fork();
    T_ASSERT(t, pid >= 0);
    if (pid == 0)
        bad_vextract_builder_child();
    if (pid < 0)
        return;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    T_ASSERT(t, WIFEXITED(status));
    T_ASSERT_EQ_INT(t, WEXITSTATUS(status), 4);
}

void test_x64_vector_transport_and_forced_spill(TestCtx *t)
{
    static const char src[] = "func i32 @vec() {\n"
                              "entry():\n"
                              "    %p = alloca 32, align 16\n"
                              "    %a = vsplat v4i32 i32 7\n"
                              "    store v4i32 %a, %p, align 16\n"
                              "    %b = load v4i32, %p, align 16\n"
                              "    %c = iadd v4i32 %b, %a\n"
                              "    %d = vreduce_add v4i32 %c\n"
                              "    ret i32 %d\n"
                              "}\n";
    VecFix f;
    IrModule *m;
    X64Func *xf;
    Buf asm_text;

    vec_init(&f);
    m = parse_vec(&f, src);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    xf = m ? x64_isel_function(m, &m->funcs[0], &f.arena) : NULL;
    T_ASSERT(t, xf != NULL);
    if (xf) {
        u32 v;
        bool saw_wide = false;

        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, f.dc), 0);
        for (v = 1; v <= xf->nvregs; v++)
            if (x64_vwidth(xf, v) == X64_X)
                saw_wide = true;
        T_ASSERT(t, saw_wide);
        setenv("CGF_SPILL_ALL", "1", 1);
        x64_regalloc(xf);
        unsetenv("CGF_SPILL_ALL");
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, f.dc), 0);
        buf_init(&asm_text);
        x64_emit_function(xf, m, 0, IRLINK_EXTERNAL, &asm_text);
        buf_push_u8(&asm_text, 0);
        T_ASSERT(t, strstr((const char *)asm_text.data, "movdqu") != NULL);
        T_ASSERT(t, strstr((const char *)asm_text.data, "pshufd") != NULL);
        T_ASSERT(t, strstr((const char *)asm_text.data, "paddd") != NULL);
        T_ASSERT(t, strstr((const char *)asm_text.data, "psrldq") != NULL);
        buf_free(&asm_text);
    }
    arena_free_all(&f.arena);
}

typedef struct {
    const char *vt;
    const char *et;
    const char *atom;
    const char *op;
    const char *mnemonic;
    bool reduce;
} VecCgCase;

static void check_vec_codegen_case(TestCtx *t, const VecCgCase *c)
{
    VecFix f;
    IrModule *m;
    X64Func *xf;
    Buf src, asm_text;

    vec_init(&f);
    buf_init(&src);
    buf_printf(&src, "func %s @f() {\nentry():\n", c->et);
    buf_printf(&src, "  %%a = vsplat %s %s %s\n", c->vt, c->et, c->atom);
    if (c->reduce) {
        buf_printf(&src, "  %%r = %s %s %%a\n", c->op, c->vt);
    } else {
        buf_printf(&src, "  %%b = vsplat %s %s %s\n", c->vt, c->et, c->atom);
        buf_printf(&src, "  %%v = %s %s %%a, %%b\n", c->op, c->vt);
        buf_printf(&src, "  %%r = vextract %s %%v, 0\n", c->vt);
    }
    buf_printf(&src, "  ret %s %%r\n}\n", c->et);
    buf_push_u8(&src, 0);
    m = parse_vec(&f, (const char *)src.data);
    T_ASSERT(t, m != NULL);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    xf = m ? x64_isel_function(m, &m->funcs[0], &f.arena) : NULL;
    T_ASSERT(t, xf != NULL);
    if (xf) {
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, f.dc), 0);
        x64_regalloc(xf);
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, f.dc), 0);
        buf_init(&asm_text);
        x64_emit_function(xf, m, 0, IRLINK_EXTERNAL, &asm_text);
        buf_push_u8(&asm_text, 0);
        T_ASSERT(t, strstr((const char *)asm_text.data, c->mnemonic) != NULL);
        buf_free(&asm_text);
    }
    buf_free(&src);
    arena_free_all(&f.arena);
}

void test_x64_vector_full_sse2_opcode_type_matrix(TestCtx *t)
{
#define B(V, E, A, O, M) {V, E, A, O, M, false}
#define R(V, E, A, O, M)                                                       \
    {                                                                          \
        V, E, A, O, M, true                                                    \
    }
    static const VecCgCase cases[] = {
        B("v16i8", "i8", "1", "iadd", "paddb"),
        B("v16i8", "i8", "1", "isub", "psubb"),
        B("v16i8", "i8", "1", "and", "pand"),
        B("v16i8", "i8", "1", "or", "por"),
        B("v16i8", "i8", "1", "xor", "pxor"),
        B("v8i16", "i16", "1", "iadd", "paddw"),
        B("v8i16", "i16", "1", "isub", "psubw"),
        B("v8i16", "i16", "1", "imul", "pmullw"),
        B("v8i16", "i16", "1", "and", "pand"),
        B("v8i16", "i16", "1", "or", "por"),
        B("v8i16", "i16", "1", "xor", "pxor"),
        B("v4i32", "i32", "1", "iadd", "paddd"),
        B("v4i32", "i32", "1", "isub", "psubd"),
        B("v4i32", "i32", "1", "and", "pand"),
        B("v4i32", "i32", "1", "or", "por"),
        B("v4i32", "i32", "1", "xor", "pxor"),
        B("v2i64", "i64", "1", "iadd", "paddq"),
        B("v2i64", "i64", "1", "isub", "psubq"),
        B("v2i64", "i64", "1", "and", "pand"),
        B("v2i64", "i64", "1", "or", "por"),
        B("v2i64", "i64", "1", "xor", "pxor"),
        B("v4f32", "f32", "0x3f800000", "fadd", "addps"),
        B("v4f32", "f32", "0x3f800000", "fsub", "subps"),
        B("v4f32", "f32", "0x3f800000", "fmul", "mulps"),
        B("v4f32", "f32", "0x3f800000", "fdiv", "divps"),
        B("v2f64", "f64", "0x3ff0000000000000", "fadd", "addpd"),
        B("v2f64", "f64", "0x3ff0000000000000", "fsub", "subpd"),
        B("v2f64", "f64", "0x3ff0000000000000", "fmul", "mulpd"),
        B("v2f64", "f64", "0x3ff0000000000000", "fdiv", "divpd"),
        R("v16i8", "i8", "1", "vreduce_add", "paddb"),
        R("v16i8", "i8", "1", "vreduce_and", "pand"),
        R("v16i8", "i8", "1", "vreduce_or", "por"),
        R("v16i8", "i8", "1", "vreduce_xor", "pxor"),
        R("v8i16", "i16", "1", "vreduce_add", "paddw"),
        R("v8i16", "i16", "1", "vreduce_mul", "pmullw"),
        R("v8i16", "i16", "1", "vreduce_and", "pand"),
        R("v8i16", "i16", "1", "vreduce_or", "por"),
        R("v8i16", "i16", "1", "vreduce_xor", "pxor"),
        R("v4i32", "i32", "1", "vreduce_add", "paddd"),
        R("v4i32", "i32", "1", "vreduce_and", "pand"),
        R("v4i32", "i32", "1", "vreduce_or", "por"),
        R("v4i32", "i32", "1", "vreduce_xor", "pxor"),
        R("v2i64", "i64", "1", "vreduce_add", "paddq"),
        R("v2i64", "i64", "1", "vreduce_and", "pand"),
        R("v2i64", "i64", "1", "vreduce_or", "por"),
        R("v2i64", "i64", "1", "vreduce_xor", "pxor"),
        R("v4f32", "f32", "0x3f800000", "vreduce_add", "addps"),
        R("v4f32", "f32", "0x3f800000", "vreduce_mul", "mulps"),
        R("v2f64", "f64", "0x3ff0000000000000", "vreduce_add", "addpd"),
        R("v2f64", "f64", "0x3ff0000000000000", "vreduce_mul", "mulpd"),
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(cases); i++)
        check_vec_codegen_case(t, &cases[i]);
#undef R
#undef B
}

void test_x64_vector_edge_copy_cycle(TestCtx *t)
{
    static const char src[] = "func i32 @f() {\n"
                              "entry():\n"
                              "  %a = vsplat v4i32 i32 1\n"
                              "  %b = vsplat v4i32 i32 2\n"
                              "  br loop(v4i32 %a, v4i32 %b)\n"
                              "loop(v4i32 %x, v4i32 %y):\n"
                              "  condbr 0, loop(v4i32 %y, v4i32 %x), exit()\n"
                              "exit():\n"
                              "  ret i32 0\n"
                              "}\n";
    VecFix f;
    IrModule *m;
    X64Func *xf;
    Buf mir, asm_text;

    vec_init(&f);
    m = parse_vec(&f, src);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    xf = m ? x64_isel_function(m, &m->funcs[0], &f.arena) : NULL;
    T_ASSERT(t, xf != NULL);
    if (xf) {
        buf_init(&mir);
        x64_mir_print(xf, &mir);
        buf_push_u8(&mir, 0);
        T_ASSERT(t, count_text((const char *)mir.data, "vmov.x") >= 5);
        buf_free(&mir);
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, f.dc), 0);
        x64_regalloc(xf);
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, f.dc), 0);
        buf_init(&asm_text);
        x64_emit_function(xf, m, 0, IRLINK_EXTERNAL, &asm_text);
        buf_push_u8(&asm_text, 0);
        T_ASSERT(t, count_text((const char *)asm_text.data, "movdqu") >= 5);
        buf_free(&asm_text);
    }
    arena_free_all(&f.arena);
}

void test_x64_vector_spill_alignment_with_odd_saved_push(TestCtx *t)
{
    static const char src[] = "sym @sink\n"
                              "func i32 @f() {\nentry():\n"
                              "  %g = iadd i32 40, 2\n"
                              "  %v = vsplat v4i32 i32 3\n"
                              "  %c = call i32 @sink()\n"
                              "  %e = vextract v4i32 %v, 0\n"
                              "  %x = iadd i32 %g, %c\n"
                              "  %r = iadd i32 %x, %e\n"
                              "  ret i32 %r\n"
                              "}\n";
    VecFix f;
    IrModule *m;
    X64Func *xf;
    u32 bi, i, pushes = 0, vector_homes = 0;

    vec_init(&f);
    m = parse_vec(&f, src);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    xf = m ? x64_isel_function(m, &m->funcs[0], &f.arena) : NULL;
    T_ASSERT(t, xf != NULL);
    if (xf) {
        x64_regalloc(xf);
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, f.dc), 0);
        for (bi = 0; bi < xf->nblocks; bi++) {
            X64Block *b = &xf->blocks[bi];

            for (i = 0; i < b->n; i++) {
                X64Inst *in = &b->insts[i];

                if (in->op == X64_OP_PUSH)
                    pushes++;
                if (in->op == X64_OP_VLOAD && in->a.kind == X64O_MEM) {
                    T_ASSERT_EQ_INT(t, in->a.mem.disp % 16, 0);
                    vector_homes++;
                }
                if (in->op == X64_OP_VSTORE && in->b.kind == X64O_MEM) {
                    T_ASSERT_EQ_INT(t, in->b.mem.disp % 16, 0);
                    vector_homes++;
                }
            }
        }
        T_ASSERT(t, pushes > 1);
        T_ASSERT(t, ((pushes - 1) & 1u) == 1u);
        T_ASSERT(t, vector_homes >= 2);
    }
    arena_free_all(&f.arena);
}

void test_x64_align16_alloca_with_odd_saved_push(TestCtx *t)
{
    static const char src[] = "sym @sink\n"
                              "func i32 @f() {\nentry():\n"
                              "  %p = alloca 16, align 16\n"
                              "  store i32 42, %p, align 4\n"
                              "  %c = call i32 @sink()\n"
                              "  %x = load i32, %p, align 4\n"
                              "  %r = iadd i32 %c, %x\n"
                              "  ret i32 %r\n"
                              "}\n";
    VecFix f;
    IrModule *m;
    X64Func *xf;
    u32 bi, i, pushes = 0, allocas = 0, vector_mem = 0;

    vec_init(&f);
    m = parse_vec(&f, src);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    xf = m ? x64_isel_function(m, &m->funcs[0], &f.arena) : NULL;
    T_ASSERT(t, xf != NULL);
    if (xf) {
        x64_regalloc(xf);
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, f.dc), 0);
        for (bi = 0; bi < xf->nblocks; bi++) {
            X64Block *b = &xf->blocks[bi];

            for (i = 0; i < b->n; i++) {
                X64Inst *in = &b->insts[i];

                if (in->op == X64_OP_PUSH)
                    pushes++;
                if (in->op == X64_OP_LEA && in->a.kind == X64O_MEM &&
                    in->a.mem.base.v == (u32)X64_RBP + 1 &&
                    in->a.mem.disp < 0 && in->def.v != (u32)X64_RSP + 1) {
                    T_ASSERT_EQ_INT(t, in->a.mem.disp % 16, 0);
                    allocas++;
                }
                if (in->op == X64_OP_VLOAD || in->op == X64_OP_VSTORE)
                    vector_mem++;
            }
        }
        T_ASSERT(t, pushes > 1);
        T_ASSERT(t, ((pushes - 1) & 1u) == 1u);
        T_ASSERT_EQ_INT(t, allocas, 1);
        T_ASSERT_EQ_INT(t, vector_mem, 0);
    }
    arena_free_all(&f.arena);
}
