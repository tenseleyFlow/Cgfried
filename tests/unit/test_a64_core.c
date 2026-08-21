#include "cg/arm64/mir.h"
#include "lower/f128.h"
#include "unit.h"

static u64 splitmix64(u64 *state);

static u64 test_mask(unsigned bits)
{
    return bits == 64 ? ~(u64)0 : (((u64)1 << bits) - 1);
}

static u64 test_ror(u64 value, unsigned rotate, unsigned width)
{
    u64 mask = test_mask(width);

    rotate &= width - 1;
    if (!rotate)
        return value & mask;
    return ((value >> rotate) | (value << (width - rotate))) & mask;
}

static u64 test_replicate(u64 elem, unsigned esize, unsigned width)
{
    u64 value = 0;
    unsigned bit;

    for (bit = 0; bit < width; bit += esize)
        value |= elem << bit;
    return value & test_mask(width);
}

static bool test_decode_logical(u32 packed, unsigned width, u64 *value)
{
    unsigned n = (packed >> 12) & 1;
    unsigned immr = (packed >> 6) & 63;
    unsigned imms = packed & 63;
    unsigned marker = (n << 6) | ((~imms) & 63);
    unsigned len = 0, esize, ones;

    if (!marker)
        return false;
    while (marker >>= 1)
        len++;
    esize = 1u << len;
    if (esize > width || (width == 32 && n))
        return false;
    ones = (imms & (esize - 1)) + 1;
    if (ones == esize)
        return false;
    *value =
        test_replicate(test_ror(test_mask(ones), immr, esize), esize, width);
    return true;
}

void test_a64_logical_immediates(TestCtx *t)
{
    unsigned esize, ones, rotate;
    unsigned constructed = 0;
    unsigned rejected = 0;
    u64 state = 0x47a64b17f00dull;
    u32 packed;

    T_ASSERT(t, !a64_logical_imm_encode(0, 64, &packed));
    T_ASSERT(t, !a64_logical_imm_encode(~(u64)0, 64, &packed));
    T_ASSERT(t, !a64_logical_imm_encode(0x1234, 64, &packed));
    T_ASSERT(t, !a64_logical_imm_encode(1ull << 32, 32, &packed));
    for (esize = 2; esize <= 64; esize *= 2) {
        for (ones = 1; ones < esize; ones++) {
            for (rotate = 0; rotate < esize; rotate++) {
                u64 elem = test_ror(test_mask(ones), rotate, esize);
                u64 want = test_replicate(elem, esize, 64);
                u64 got = 0;

                constructed++;
                T_ASSERT(t, a64_logical_imm_encode(want, 64, &packed));
                T_ASSERT(t, test_decode_logical(packed, 64, &got));
                T_ASSERT_EQ_INT(t, got, want);
            }
        }
    }
    T_ASSERT_EQ_INT(t, constructed, 5334);
    T_ASSERT(t, a64_logical_imm_encode(0x00ff00ff00ff00ffull, 64, &packed));
    T_ASSERT(t, a64_logical_imm_encode(0x5555555555555555ull, 64, &packed));
    T_ASSERT(t, a64_logical_imm_encode(0x00ff00ffu, 32, &packed));
    T_ASSERT(t, !(packed & (1u << 12)));
    for (rotate = 0; rotate < 10000; rotate++) {
        u64 value = splitmix64(&state);
        u64 decoded = 0;

        if (!a64_logical_imm_encode(value, 64, &packed)) {
            rejected++;
            continue;
        }
        T_ASSERT(t, test_decode_logical(packed, 64, &decoded));
        T_ASSERT_EQ_INT(t, decoded, value);
    }
    T_ASSERT(t, rejected >= 9990);
}

static u64 interpret_mov(const A64MovSynth *seq, u32 n)
{
    u64 value = 0;
    u32 i;

    for (i = 0; i < n; i++) {
        u64 field = (u64)seq[i].imm16 << seq[i].shift;

        switch (seq[i].kind) {
        case A64_MOV_ORR:
            return 0; /* handled by mov_roundtrip before this interpreter */
        case A64_MOV_MOVZ:
            value = field;
            break;
        case A64_MOV_MOVN:
            value = ~field;
            break;
        case A64_MOV_MOVK:
            value = (value & ~((u64)0xffff << seq[i].shift)) | field;
            break;
        }
    }
    return value;
}

static bool mov_roundtrip(const A64MovSynth *seq, u32 n, u64 *value)
{
    if (n == 1 && seq[0].kind == A64_MOV_ORR)
        return test_decode_logical(seq[0].logical, 64, value);
    *value = interpret_mov(seq, n);
    return true;
}

static bool mov_roundtrip_width(const A64MovSynth *seq, u32 n, unsigned width,
                                u64 *value)
{
    u64 mask = width == 32 ? 0xffffffffu : ~(u64)0;

    if (n == 1 && seq[0].kind == A64_MOV_ORR)
        return test_decode_logical(seq[0].logical, width, value);
    *value = interpret_mov(seq, n) & mask;
    return true;
}

static u32 mov_optimal_count(u64 value)
{
    unsigned zeros = 0, ones = 0, i;
    u32 packed;

    if (a64_logical_imm_encode(value, 64, &packed))
        return 1;
    for (i = 0; i < 4; i++) {
        u16 half = (u16)(value >> (i * 16));

        zeros += half == 0;
        ones += half == 0xffff;
    }
    if (zeros == 4 || ones == 4)
        return 1;
    return 4 - (zeros > ones ? zeros : ones);
}

static u64 splitmix64(u64 *state)
{
    u64 z = (*state += 0x9e3779b97f4a7c15ull);

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

void test_a64_mov_synthesis(TestCtx *t)
{
    static const u64 table[] = {
        0x2a,
        0xffffffffffff0000ull,
        0x0000000100000000ull,
        0x00ff00ff00ff00ffull,
        0x123456789abcdef0ull,
        0,
        ~(u64)0,
    };
    u64 state = 0x47a64c0defacedull;
    A64MovSynth seq[4];
    unsigned i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        u64 got = 0;
        u32 n = a64_synth_mov(table[i], seq);

        T_ASSERT_EQ_INT(t, n, mov_optimal_count(table[i]));
        T_ASSERT(t, mov_roundtrip(seq, n, &got));
        T_ASSERT_EQ_INT(t, got, table[i]);
    }
    for (i = 0; i < 1000000; i++) {
        u64 value = splitmix64(&state);
        u64 got = 0;
        u32 n = a64_synth_mov(value, seq);

        T_ASSERT(t, n >= 1 && n <= 4);
        T_ASSERT_EQ_INT(t, n, mov_optimal_count(value));
        T_ASSERT(t, mov_roundtrip(seq, n, &got));
        T_ASSERT_EQ_INT(t, got, value);
    }
    for (i = 0; i < 50000; i++) {
        u32 value = (u32)splitmix64(&state);
        u64 got = 0;
        u32 n = a64_synth_mov_width(value, 32, seq);

        T_ASSERT(t, n >= 1 && n <= 2);
        T_ASSERT(t, mov_roundtrip_width(seq, n, 32, &got));
        T_ASSERT_EQ_INT(t, got, value);
    }
    {
        u64 got = 0;
        u32 n = a64_synth_mov_width(0xffffffffu, 32, seq);

        T_ASSERT_EQ_INT(t, n, 1);
        T_ASSERT_EQ_INT(t, seq[0].kind, A64_MOV_MOVN);
        T_ASSERT(t, mov_roundtrip_width(seq, n, 32, &got));
        T_ASSERT_EQ_INT(t, got, 0xffffffffu);
    }
}

void test_a64_immediate_and_address_tables(TestCtx *t)
{
    A64AddSubImm imm;
    u8 fpimm;

    T_ASSERT(t, a64_addsub_imm(4095, &imm));
    T_ASSERT(t, !imm.is_sub && imm.imm12 == 4095 && imm.shift == 0);
    T_ASSERT(t, a64_addsub_imm(-8, &imm));
    T_ASSERT(t, imm.is_sub && imm.imm12 == 8 && imm.shift == 0);
    T_ASSERT(t, a64_addsub_imm(0xfff000, &imm));
    T_ASSERT(t, !imm.is_sub && imm.imm12 == 0xfff && imm.shift == 12);
    T_ASSERT(t, !a64_addsub_imm(4097, &imm));
    T_ASSERT(t, !a64_addsub_imm(-4097, &imm));
    T_ASSERT(t, !a64_addsub_imm((i64)0x8000000000000000ull, &imm));

    T_ASSERT_EQ_INT(t, a64_isel_addr(0, 8, false, false), A64_ADDR_SCALED);
    T_ASSERT_EQ_INT(t, a64_isel_addr(32760, 8, false, false), A64_ADDR_SCALED);
    T_ASSERT_EQ_INT(t, a64_isel_addr(-16, 8, false, false), A64_ADDR_UNSCALED);
    T_ASSERT_EQ_INT(t, a64_isel_addr(7, 8, false, false), A64_ADDR_UNSCALED);
    T_ASSERT_EQ_INT(t, a64_isel_addr(-256, 8, true, false), A64_ADDR_PRE);
    T_ASSERT_EQ_INT(t, a64_isel_addr(255, 8, false, true), A64_ADDR_POST);
    T_ASSERT_EQ_INT(t, a64_isel_addr(256, 8, false, true),
                    A64_ADDR_MATERIALIZE);
    T_ASSERT_EQ_INT(t, a64_isel_addr(0, 3, false, false), A64_ADDR_MATERIALIZE);
    T_ASSERT_EQ_INT(t, a64_addr_reg_mode(false, false, true), A64_ADDR_REG_LSL);
    T_ASSERT_EQ_INT(t, a64_addr_reg_mode(true, false, true), A64_ADDR_REG_UXTW);
    T_ASSERT_EQ_INT(t, a64_addr_reg_mode(true, true, false), A64_ADDR_REG_SXTW);

    T_ASSERT(t, a64_fp_imm_encode(0x3f800000u, 32, &fpimm));
    T_ASSERT(t, a64_fp_imm_encode(0x3ff0000000000000ull, 64, &fpimm));
    T_ASSERT(t, !a64_fp_imm_encode(0, 32, &fpimm));
    T_ASSERT(t, !a64_fp_imm_encode(1, 64, &fpimm));
}

typedef struct A64DiagCount {
    int count;
} A64DiagCount;

static void a64_count_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    A64DiagCount *count = user;
    (void)diag;
    (void)dc;
    count->count++;
}

static int verify_a64_inst(A64Inst inst)
{
    Arena arena;
    A64Block block = {0};
    A64Func func = {0};
    A64DiagCount count = {0};
    DiagCtx *dc;
    int bad;

    arena_init(&arena);
    block.insts = &inst;
    block.n = 1;
    block.cap = 1;
    func.name = "reg31";
    func.allocated = true;
    func.arena = &arena;
    func.blocks = &block;
    func.nblocks = 1;
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, (DiagSink){a64_count_sink, &count});
    bad = a64_mir_verify(&func, dc);
    arena_free_all(&arena);
    return bad;
}

static A64Operand preg(A64PhysReg reg)
{
    return (A64Operand){.kind = A64O_REG, .reg = a64_phys(reg)};
}

static A64Operand pimm(i64 value)
{
    return (A64Operand){.kind = A64O_IMM, .imm = value};
}

static A64Inst three_reg_inst(A64Op op, A64PhysReg a, A64PhysReg b,
                              A64PhysReg c)
{
    A64Inst inst = {.op = op, .sf = A64_SF64, .src_sf = A64_SF64, .nops = 3};

    inst.ops[0] = preg(a);
    inst.ops[1] = preg(b);
    inst.ops[2] = preg(c);
    return inst;
}

void test_a64_reg31_verifier_matrix(TestCtx *t)
{
    A64Inst inst;
    u32 i;

    /* add/sub immediate and extended: Rd/Rn encode SP. */
    inst = three_reg_inst(A64_OP_ADD, A64_SP, A64_SP, A64_X0);
    inst.ops[2] = pimm(8);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_XZR);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst = three_reg_inst(A64_OP_ADD, A64_XZR, A64_XZR, A64_X0);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst = three_reg_inst(A64_OP_ADD, A64_SP, A64_SP, A64_X0);
    inst.nops = 4;
    inst.ops[3] = pimm(0); /* extended-register form */
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);

    /* Flag-setting immediate forms keep Rd as XZR and only Rn as SP. */
    inst = three_reg_inst(A64_OP_SUBS, A64_XZR, A64_SP, A64_X0);
    inst.ops[2] = pimm(5);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst.ops[0] = preg(A64_XZR);
    inst.ops[1] = preg(A64_XZR);
    T_ASSERT(t, verify_a64_inst(inst) > 0);

    /* Logical-immediate Rd names SP except ANDS; every Rn names XZR. */
    inst = three_reg_inst(A64_OP_ORR, A64_SP, A64_XZR, A64_X0);
    inst.ops[2] = pimm(0xff);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_XZR);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst.ops[0] = preg(A64_SP);
    inst.ops[1] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst = three_reg_inst(A64_OP_ANDS, A64_XZR, A64_XZR, A64_X0);
    inst.ops[2] = pimm(1);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);

    /* Memory base encoding 31 is SP; data/index encoding 31 is XZR. */
    inst = (A64Inst){
        .op = A64_OP_LOAD, .sf = A64_SF64, .src_sf = A64_SF64, .nops = 2};
    inst.ops[0] = preg(A64_XZR);
    inst.ops[1] = (A64Operand){
        .kind = A64O_MEM,
        .mem = {.base = a64_phys(A64_SP), .size = 8, .mode = A64_ADDR_SCALED}};
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst.ops[0] = preg(A64_XZR);
    inst.ops[1].mem.base = a64_phys(A64_XZR);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst.ops[1].mem.base = a64_phys(A64_X0);
    inst.ops[1].mem.index = a64_phys(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);

    /* Shifted arithmetic, selects, multiply and MOV synthesis use XZR. */
    inst = three_reg_inst(A64_OP_MUL, A64_XZR, A64_XZR, A64_XZR);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[2] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst = three_reg_inst(A64_OP_CSEL, A64_XZR, A64_XZR, A64_XZR);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[1] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst = (A64Inst){.op = A64_OP_MOVZ,
                     .sf = A64_SF64,
                     .src_sf = A64_SF64,
                     .nops = 2,
                     .ops = {preg(A64_XZR), pimm(42)}};
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);

    /* Scalar FP arithmetic has no SP/XZR spelling. FMOV alone accepts XZR
     * on either GP transfer side (GP source is the architectural +0.0 idiom);
     * conversions likewise allow XZR only on their integer side. */
    inst = (A64Inst){.op = A64_OP_FADD,
                     .sf = A64_SF64,
                     .src_sf = A64_SF64,
                     .nops = 3,
                     .ops = {preg(A64_XZR), preg(A64_V0), preg(A64_V1)}};
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst = three_reg_inst(A64_OP_FCSEL, A64_V0, A64_V1, A64_V2);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    for (i = 0; i < 3; i++) {
        inst = three_reg_inst(A64_OP_FCSEL, A64_V0, A64_V1, A64_V2);
        inst.ops[i] = preg(A64_XZR);
        T_ASSERT(t, verify_a64_inst(inst) > 0);
    }
    inst = (A64Inst){.op = A64_OP_FMOV,
                     .sf = A64_SF64,
                     .src_sf = A64_SF64,
                     .nops = 2,
                     .ops = {preg(A64_V0), preg(A64_XZR)}};
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.sf = A64_SF32;
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[1] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst.ops[0] = preg(A64_XZR);
    inst.ops[1] = preg(A64_V0);
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.sf = A64_SF64;
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_SP);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst = (A64Inst){.op = A64_OP_SCVTF,
                     .sf = A64_SF64,
                     .src_sf = A64_SF32,
                     .nops = 2,
                     .ops = {preg(A64_V0), preg(A64_XZR)}};
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[0] = preg(A64_XZR);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
    inst = (A64Inst){.op = A64_OP_FCVTZS,
                     .sf = A64_SF64,
                     .src_sf = A64_SF32,
                     .nops = 2,
                     .ops = {preg(A64_XZR), preg(A64_V0)}};
    T_ASSERT_EQ_INT(t, verify_a64_inst(inst), 0);
    inst.ops[1] = preg(A64_XZR);
    T_ASSERT(t, verify_a64_inst(inst) > 0);
}

void test_a64_isel_bulk_memory_address_materialization(TestCtx *t)
{
    static const char source[] = "func void @copy(ptr %dst, ptr %src) {\n"
                                 "entry():\n"
                                 "    memcpy %dst, %src, 32776, align 8\n"
                                 "    ret\n"
                                 "}\n";
    Arena arena;
    DiagCtx *dc;
    IrModule *module;
    A64Func *func;
    u32 bi, ii;
    u32 at_scaled_limit = 0;
    u32 materialized_32768 = 0;
    bool all_legal = true;

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    module = ir_parse_module(&arena, dc, source, "<a64-bulk-address>");
    T_ASSERT(t, module != NULL && !diag_had_error(dc));
    T_ASSERT(t, module && ir_verify(dc, module));
    if (!module || diag_had_error(dc)) {
        arena_free_all(&arena);
        return;
    }
    func = a64_isel_function(module, &module->funcs[0], &arena);
    T_ASSERT_EQ_INT(t, a64_mir_verify(func, dc), 0);
    for (bi = 0; bi < func->nblocks; bi++) {
        const A64Block *block = &func->blocks[bi];

        for (ii = 0; ii < block->n; ii++) {
            const A64Inst *inst = &block->insts[ii];
            u32 oi;

            if (inst->op == A64_OP_ADD && inst->nops == 3 &&
                inst->ops[2].kind == A64O_IMM && inst->ops[2].imm == 32768)
                materialized_32768++;
            for (oi = 0; oi < inst->nops; oi++) {
                const A64Operand *op = &inst->ops[oi];

                if (op->kind != A64O_MEM)
                    continue;
                if (op->mem.mode == A64_ADDR_MATERIALIZE ||
                    op->mem.mode != a64_isel_addr(op->mem.offset, op->mem.size,
                                                  false, false))
                    all_legal = false;
                if (op->mem.offset == 32760)
                    at_scaled_limit++;
            }
        }
    }
    T_ASSERT(t, all_legal);
    T_ASSERT_EQ_INT(t, at_scaled_limit, 2);
    T_ASSERT_EQ_INT(t, materialized_32768, 2);
    arena_free_all(&arena);
}

/* Callee-side mirror of the Linux scalar-stack rule: after v0-v7 are full,
 * the ninth double arrives at incoming+0 and the following binary128 scalar
 * is rounded past the incoming+8 hole to incoming+16. */
void test_a64_isel_aligns_stacked_binary128_scalar(TestCtx *t)
{
    static const char source[] =
        "func void @callee(f64 %d0, f64 %d1, f64 %d2, f64 %d3, "
        "f64 %d4, f64 %d5, f64 %d6, f64 %d7, f64 %stacked_d, "
        "f128 %stacked_q) {\n"
        "entry():\n"
        "    ret\n"
        "}\n";
    Arena arena;
    DiagCtx *dc;
    IrModule *module;
    A64Func *func;
    bool saw_double = false, saw_binary128 = false, loaded_hole = false;
    u32 bi, ii;

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    module = ir_parse_module(&arena, dc, source, "<a64-stacked-binary128>");
    T_ASSERT(t, module != NULL && !diag_had_error(dc));
    T_ASSERT(t, module && ir_verify(dc, module));
    if (!module || diag_had_error(dc)) {
        arena_free_all(&arena);
        return;
    }
    func = a64_isel_function(module, &module->funcs[0], &arena);
    T_ASSERT_EQ_INT(t, a64_mir_verify(func, dc), 0);
    for (bi = 0; bi < func->nblocks; bi++) {
        const A64Block *block = &func->blocks[bi];

        for (ii = 0; ii < block->n; ii++) {
            const A64Inst *inst = &block->insts[ii];
            const A64Mem *mem;

            if (inst->op != A64_OP_LOAD || inst->ops[1].kind != A64O_MEM ||
                inst->ops[1].mem.mode != A64_ADDR_INCOMING)
                continue;
            mem = &inst->ops[1].mem;
            if (mem->offset == 0 && mem->size == 8)
                saw_double = true;
            if (mem->offset == 16 && mem->size == 16)
                saw_binary128 = true;
            if (mem->offset < 16 && mem->offset + mem->size > 8)
                loaded_hole = true;
        }
    }
    T_ASSERT(t, saw_double);
    T_ASSERT(t, saw_binary128);
    T_ASSERT(t, !loaded_hole);
    arena_free_all(&arena);
}

void test_a64_isel_opaque_ops_clobber_cached_compare_flags(TestCtx *t)
{
    static const struct {
        const char *name;
        const char *source;
        A64Op clobber_op;
    } cases[] = {
        {"call",
         "sym @clobber\n"
         "func i32 @pick(i32 %a, i32 %b) {\n"
         "entry():\n"
         "    %c = icmp slt i32 %a, %b\n"
         "    call void @clobber()\n"
         "    %r = select %c, i32 %a, %b\n"
         "    ret i32 %r\n"
         "}\n",
         A64_OP_CALL},
        {"asm",
         "func i32 @pick(i32 %a, i32 %b) {\n"
         "entry():\n"
         "    %c = icmp slt i32 %a, %b\n"
         "    asm volatile \"\"\n"
         "    %r = select %c, i32 %a, %b\n"
         "    ret i32 %r\n"
         "}\n",
         A64_OP_ASM},
        {"cas",
         "func i32 @pick(ptr %p, i32 %a, i32 %b) {\n"
         "entry():\n"
         "    %c = icmp slt i32 %a, %b\n"
         "    %old = cmpxchg i32 %p, %a, %b, seq_cst\n"
         "    %r = select %c, i32 %a, %b\n"
         "    ret i32 %r\n"
         "}\n",
         A64_OP_ATOMIC_CAS},
    };
    u32 ci;

    for (ci = 0; ci < CGF_ARRAY_LEN(cases); ci++) {
        Arena arena;
        DiagCtx *dc;
        IrModule *module;
        A64Func *func;
        const A64Block *block;
        const A64Inst *clobber = NULL;
        const A64Inst *select = NULL;
        u32 clobber_index = 0;
        u32 select_index = 0;
        u32 i;

        arena_init(&arena);
        dc = diag_ctx_new(&arena);
        module = ir_parse_module(&arena, dc, cases[ci].source, cases[ci].name);
        T_ASSERT(t, module != NULL && !diag_had_error(dc));
        T_ASSERT(t, module && ir_verify(dc, module));
        if (!module || diag_had_error(dc)) {
            arena_free_all(&arena);
            continue;
        }
        func = a64_isel_function(module, &module->funcs[0], &arena);
        T_ASSERT_EQ_INT(t, func ? func->nblocks : 0, 1);
        if (!func || func->nblocks != 1) {
            arena_free_all(&arena);
            continue;
        }
        block = &func->blocks[0];
        for (i = 0; i < block->n; i++) {
            if (block->insts[i].op == cases[ci].clobber_op) {
                clobber = &block->insts[i];
                clobber_index = i;
            }
            if (block->insts[i].op == A64_OP_CSEL) {
                select = &block->insts[i];
                select_index = i;
            }
        }
        T_ASSERT(t, clobber != NULL);
        T_ASSERT(t, clobber && (clobber->flags & A64IF_DEFS_NZCV));
        T_ASSERT(t, select != NULL);
        T_ASSERT(t, select && (select->flags & A64IF_USES_NZCV));
        T_ASSERT(t, select && select_index > clobber_index);
        T_ASSERT(t, select && select->flags_src > clobber_index);
        T_ASSERT(t, select && select->flags_src < select_index);
        T_ASSERT(t,
                 select && block->insts[select->flags_src].op == A64_OP_SUBS);
        T_ASSERT_EQ_INT(t, a64_mir_verify(func, dc), 0);
        arena_free_all(&arena);
    }
}

void test_a64_mixed_width_and_call_metadata(TestCtx *t)
{
    Arena arena;
    A64Block block = {0};
    A64Func func = {0};
    IrModule module = {0};
    IrFunc funcs[2] = {0};
    const char *syms[4] = {"a", "b", "c", "puts"};
    A64DiagCount count = {0};
    DiagCtx *dc;
    A64Inst inst = {0};
    A64Inst call = {0};
    A64CallInfo *ci;
    Buf first, second;
    u32 i;

    arena_init(&arena);
    func.name = "widths";
    func.allocated = true;
    func.arena = &arena;
    func.blocks = &block;
    func.nblocks = 1;
    funcs[0].name = "outer";
    funcs[1].name = "inside";
    module.funcs = funcs;
    module.nfuncs = 2;
    module.syms = syms;
    module.nsyms = 4;
    func.m = &module;

    inst = (A64Inst){.op = A64_OP_FCVT,
                     .sf = A64_SF64,
                     .src_sf = A64_SF32,
                     .nops = 2,
                     .ops = {preg(A64_V0), preg(A64_V1)}};
    a64_block_append(&func, &block, inst);
    inst = (A64Inst){.op = A64_OP_SCVTF,
                     .sf = A64_SF64,
                     .src_sf = A64_SF32,
                     .nops = 2,
                     .ops = {preg(A64_V2), preg(A64_X3)}};
    a64_block_append(&func, &block, inst);
    inst = (A64Inst){.op = A64_OP_FCVTZS,
                     .sf = A64_SF64,
                     .src_sf = A64_SF32,
                     .nops = 2,
                     .ops = {preg(A64_X4), preg(A64_V5)}};
    a64_block_append(&func, &block, inst);

    call.op = A64_OP_CALL;
    call.sf = A64_SF64;
    call.src_sf = A64_SF64;
    ci = a64_call_info_new(&func, &call, FUNCREF_EXTERNAL, 3, (A64Reg){0},
                           a64_phys(A64_X2), IRT_I64, IR_ABIRET_NONE, true,
                           false);
    for (i = 0; i < 6; i++)
        a64_call_add_arg(&func, ci, a64_phys((A64PhysReg)(A64_X0 + i)),
                         i & 1 ? IRT_F64 : IRT_I32,
                         i >= 4 ? (u8)IROPF_ANON : 0u,
                         i == 5 ? ir_arg_annot(IR_ARG_BYVAL, 24) : i);
    a64_block_append(&func, &block, call);
    call = (A64Inst){.op = A64_OP_CALL, .sf = A64_SF64, .src_sf = A64_SF64};
    (void)a64_call_info_new(&func, &call, FUNCREF_INTERNAL, 1, (A64Reg){0},
                            (A64Reg){0}, IRT_VOID, IR_ABIRET_NONE, false,
                            false);
    a64_block_append(&func, &block, call);
    call = (A64Inst){.op = A64_OP_CALL, .sf = A64_SF64, .src_sf = A64_SF64};
    (void)a64_call_info_new(&func, &call, FUNCREF_INDIRECT, 0, a64_phys(A64_X9),
                            (A64Reg){0}, IRT_VOID, IR_ABIRET_NONE, false,
                            false);
    a64_block_append(&func, &block, call);

    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, (DiagSink){a64_count_sink, &count});
    T_ASSERT_EQ_INT(t, a64_mir_verify(&func, dc), 0);
    T_ASSERT_EQ_INT(t, ci->nargs, 6);
    T_ASSERT_EQ_INT(t, ci->args[5].abi_annot, ir_arg_annot(IR_ARG_BYVAL, 24));
    /* The anonymous marker is independent of the ABI annotation word: arg 5
     * carries both, arg 4 only the marker, arg 3 neither. */
    T_ASSERT_EQ_INT(t, ci->args[3].argflags, 0);
    T_ASSERT_EQ_INT(t, ci->args[4].argflags, IROPF_ANON);
    T_ASSERT_EQ_INT(t, ci->args[5].argflags, IROPF_ANON);

    buf_init(&first);
    buf_init(&second);
    a64_mir_print(&func, &first);
    a64_mir_print(&func, &second);
    buf_push_u8(&first, 0);
    buf_push_u8(&second, 0);
    T_ASSERT_EQ_STR(t, (const char *)first.data, (const char *)second.data);
    T_ASSERT(t, strstr((const char *)first.data, "fcvt.d.s d0, s1") != NULL);
    T_ASSERT(t, strstr((const char *)first.data, "scvtf.d.w d2, w3") != NULL);
    T_ASSERT(t, strstr((const char *)first.data, "fcvtzs.x.s x4, s5") != NULL);
    T_ASSERT(t, strstr((const char *)first.data, "call @puts(") != NULL);
    T_ASSERT(t, strstr((const char *)first.data, "call @inside()") != NULL);
    T_ASSERT(t, strstr((const char *)first.data, "call x9()") != NULL);
    T_ASSERT(t, strstr((const char *)first.data, "variadic") != NULL);
    buf_free(&first);
    buf_free(&second);

    block.insts[0].src_sf = 2;
    T_ASSERT(t, a64_mir_verify(&func, dc) > 0);
    block.insts[0].src_sf = A64_SF32;
    ci->args[0].value = a64_phys(A64_SP);
    T_ASSERT(t, a64_mir_verify(&func, dc) > 0);
    arena_free_all(&arena);
}

void test_a64_mir_core(TestCtx *t)
{
    Arena arena;
    A64Block block = {0};
    A64Func func = {0};
    A64DiagCount count = {0};
    DiagCtx *dc;
    Buf text;
    A64Inst cmp = {0};
    A64Inst branch = {0};

    arena_init(&arena);
    func.name = "core";
    func.arena = &arena;
    func.blocks = &block;
    func.nblocks = 1;
    cmp.op = A64_OP_SUBS;
    cmp.sf = A64_SF64;
    cmp.flags = A64IF_DEFS_NZCV;
    cmp.nops = 3;
    cmp.ops[0] = (A64Operand){.kind = A64O_REG, .reg = a64_phys(A64_XZR)};
    cmp.ops[1] = (A64Operand){.kind = A64O_REG, .reg = a64_phys(A64_X0)};
    cmp.ops[2] = (A64Operand){.kind = A64O_IMM, .imm = 5};
    branch.op = A64_OP_BCOND;
    branch.sf = A64_SF64;
    branch.flags = A64IF_USES_NZCV;
    branch.cond = A64_CC_LT;
    branch.nops = 1;
    branch.ops[0] = (A64Operand){.kind = A64O_LABEL, .id = 1};
    branch.flags_src = 0;
    a64_block_append(&func, &block, cmp);
    a64_block_append(&func, &block, branch);

    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, (DiagSink){a64_count_sink, &count});
    T_ASSERT_EQ_INT(t, a64_mir_verify(&func, dc), 0);
    T_ASSERT_EQ_INT(t, count.count, 0);
    T_ASSERT_EQ_INT(t, a64_phys_encode(A64_SP), 31);
    T_ASSERT_EQ_INT(t, a64_phys_encode(A64_XZR), 31);
    T_ASSERT(t, A64_SP != A64_XZR);
    T_ASSERT_EQ_STR(t, a64_phys_name(A64_SP, A64_SF64), "sp");
    T_ASSERT_EQ_STR(t, a64_phys_name(A64_XZR, A64_SF32), "wzr");
    T_ASSERT_EQ_INT(t, a64_vclass(&func, a64_phys(A64_X0)), A64RC_GP);
    T_ASSERT_EQ_INT(t, a64_vclass(&func, a64_phys(A64_V31)), A64RC_FP);
    T_ASSERT_EQ_INT(t, a64_vclass(&func, a64_phys(A64_NZCV)), A64RC_NZCV);
    T_ASSERT_EQ_INT(t, a64_fp_cond_map[A64_FP_OLT].first, A64_CC_MI);
    T_ASSERT_EQ_INT(t, a64_fp_cond_map[A64_FP_OLE].first, A64_CC_LS);
    T_ASSERT_EQ_INT(t, a64_fp_cond_map[A64_FP_UNO].first, A64_CC_VS);

    buf_init(&text);
    a64_mir_print(&func, &text);
    buf_push_u8(&text, 0);
    T_ASSERT_EQ_STR(t, (const char *)text.data,
                    "mir.a64 @core (vregs=0)\n"
                    "bb1:\n"
                    "    subs.x xzr, x0, #5\n"
                    "    b.lt bb1\n");
    buf_free(&text);

    block.insts[1].flags_src = 1;
    T_ASSERT(t, a64_mir_verify(&func, dc) > 0);
    T_ASSERT(t, count.count > 0);
    block.insts[1].flags_src = 0;
    block.insts[0].ops[0].reg = a64_phys(A64_SP);
    T_ASSERT(t, a64_mir_verify(&func, dc) > 0);
    block.insts[0].ops[0].reg = a64_phys(A64_XZR);
    block.insts[0].ops[1].reg = a64_phys(A64_XZR);
    T_ASSERT(t, a64_mir_verify(&func, dc) > 0);
    arena_free_all(&arena);
}

void test_a64_emit_tentative_tls_is_not_common(TestCtx *t)
{
    IrGlobal globals[] = {
        {.name = "ordinary_common",
         .size = 4,
         .align = 4,
         .linkage = IRLINK_COMMON,
         .is_tentative = true},
        {.name = "tls_common",
         .size = 4,
         .align = 4,
         .linkage = IRLINK_COMMON,
         .is_tentative = true,
         .is_tls = true},
        {.name = "tls_static",
         .size = 8,
         .align = 8,
         .linkage = IRLINK_INTERNAL,
         .is_tls = true},
    };
    IrModule module = {.globals = globals, .nglobals = CGF_ARRAY_LEN(globals)};
    TargetSpec previous = cgf_target_selected();
    Buf text;

    T_ASSERT(t, cgf_target_select("arm64-linux"));
    buf_init(&text);
    a64_emit_tls_decls(&module, &text);
    a64_emit_globals(&module, &text, false);
    buf_push_u8(&text, 0);

    T_ASSERT(t, strstr((const char *)text.data,
                       "\t.comm\tordinary_common,4,4\n") != NULL);
    T_ASSERT(t,
             strstr((const char *)text.data, "\t.comm\ttls_common,") == NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\t.globl\ttls_common\n"
                       "\t.section\t.tbss,\"awT\",@nobits\n"
                       "\t.p2align\t2\n"
                       "\t.type\ttls_common, @tls_object\n"
                       "\t.size\ttls_common, 4\n"
                       "tls_common:\n"
                       "\t.zero\t4\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\t.local\ttls_static\n"
                       "\t.section\t.tbss,\"awT\",@nobits\n"
                       "\t.p2align\t3\n"
                       "\t.type\ttls_static, @tls_object\n"
                       "\t.size\ttls_static, 8\n"
                       "tls_static:\n"
                       "\t.zero\t8\n") != NULL);

    buf_free(&text);
    T_ASSERT(t, cgf_target_select(cgf_target_name(previous)));
}

void test_a64_emit_tls_addends_are_encodable(TestCtx *t)
{
    static const i64 addends[] = {
        0,         4095,      4096,      5000,       -5000,
        0xffffff,  -0xffffff, 0x1000000, -0x1000000, 0x0123456789abcdef,
        INT64_MAX, INT64_MIN};
    static const A64PhysReg regs[] = {A64_X0, A64_X1,  A64_X2,  A64_X3,
                                      A64_X4, A64_X5,  A64_X6,  A64_X7,
                                      A64_X8, A64_X11, A64_X12, A64_X13};
    const char *syms[] = {"tls_object", "external_arena"};
    IrModule module = {.syms = syms, .nsyms = CGF_ARRAY_LEN(syms)};
    TargetSpec previous = cgf_target_selected();
    Arena arena;
    A64Block block = {0};
    A64Func func = {0};
    Buf text;
    u32 i;

    T_ASSERT(t, cgf_target_select("arm64-linux"));
    arena_init(&arena);
    func.name = "tls_addends";
    func.arena = &arena;
    func.blocks = &block;
    func.nblocks = 1;
    func.allocated = true;
    for (i = 0; i < CGF_ARRAY_LEN(addends); i++) {
        A64Inst addr = {.op = A64_OP_TLSADDR, .sf = A64_SF64, .nops = 3};

        addr.ops[0] = (A64Operand){.kind = A64O_REG, .reg = a64_phys(regs[i])};
        addr.ops[1] = (A64Operand){.kind = A64O_SYM, .id = 1};
        addr.ops[2] = (A64Operand){.kind = A64O_IMM, .imm = addends[i]};
        a64_block_append(&func, &block, addr);
    }
    for (i = 0; i < 2; i++) {
        A64Inst addr = {.op = A64_OP_ADDR, .sf = A64_SF64, .nops = 3};

        addr.ops[0] =
            (A64Operand){.kind = A64O_REG, .reg = a64_phys(A64_X9 + i)};
        addr.ops[1] = (A64Operand){.kind = A64O_SYM, .id = 2};
        addr.ops[2] =
            (A64Operand){.kind = A64O_IMM, .imm = i ? -0x1000000 : 0x1000000};
        a64_block_append(&func, &block, addr);
    }

    buf_init(&text);
    a64_emit_set_pic(A64_PIC_FULL);
    a64_emit_function(&func, &module, 0, IRLINK_INTERNAL, &text);
    a64_emit_set_pic(A64_PIC_NONE);
    buf_push_u8(&text, 0);

    T_ASSERT(t,
             strstr((const char *)text.data, "\tadd\tx1, x1, #4095\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\tadd\tx2, x2, #1, lsl #12\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\tadd\tx3, x3, #904\n"
                       "\tadd\tx3, x3, #1, lsl #12\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\tsub\tx4, x4, #904\n"
                       "\tsub\tx4, x4, #1, lsl #12\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\tadd\tx5, x5, #4095\n"
                       "\tadd\tx5, x5, #4095, lsl #12\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\tsub\tx6, x6, #4095\n"
                       "\tsub\tx6, x6, #4095, lsl #12\n") != NULL);
    T_ASSERT(t,
             strstr((const char *)text.data, "\torr\tx12, xzr, #0x1000000\n"
                                             "\tadd\tx7, x7, x12\n") != NULL);
    T_ASSERT(t,
             strstr((const char *)text.data, "\torr\tx12, xzr, #0x1000000\n"
                                             "\tsub\tx8, x8, x12\n") != NULL);
    T_ASSERT(t,
             strstr((const char *)text.data, "\tmovz\tx12, #52719\n"
                                             "\tmovk\tx12, #35243, lsl #16\n"
                                             "\tmovk\tx12, #17767, lsl #32\n"
                                             "\tmovk\tx12, #291, lsl #48\n"
                                             "\tadd\tx11, x11, x12\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\torr\tx13, xzr, #0x7fffffffffffffff\n"
                       "\tadd\tx12, x12, x13\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\torr\tx12, xzr, #0x8000000000000000\n"
                       "\tsub\tx13, x13, x12\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\tadrp\tx9, :got:external_arena\n"
                       "\tldr\tx9, [x9, #:got_lo12:external_arena]\n"
                       "\torr\tx12, xzr, #0x1000000\n"
                       "\tadd\tx9, x9, x12\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data,
                       "\tadrp\tx10, :got:external_arena\n"
                       "\tldr\tx10, [x10, #:got_lo12:external_arena]\n"
                       "\torr\tx12, xzr, #0x1000000\n"
                       "\tsub\tx10, x10, x12\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data, "#5000") == NULL);
    T_ASSERT(t, strstr((const char *)text.data, "x12, x12, x12") == NULL);
    T_ASSERT(t, strstr((const char *)text.data, "x13, x13, x13") == NULL);

    buf_free(&text);
    arena_free_all(&arena);
    T_ASSERT(t, cgf_target_select(cgf_target_name(previous)));
}

void test_a64_emit_relaxes_nonlayout_conditional_branches(TestCtx *t)
{
    Arena arena;
    A64Block blocks[3] = {{0}};
    A64Block far_blocks[3] = {{0}};
    A64Func func = {0};
    A64Func far_func = {0};
    IrModule module = {0};
    A64Inst branch = {0};
    A64Inst padding = {0};
    Buf text;
    u32 i;

    arena_init(&arena);
    func.name = "long_cond";
    func.arena = &arena;
    func.blocks = blocks;
    func.nblocks = 3;
    func.allocated = true;

    branch.op = A64_OP_TBZ;
    branch.sf = A64_SF64;
    branch.nops = 4;
    branch.ops[0] = (A64Operand){.kind = A64O_REG, .reg = a64_phys(A64_X0)};
    branch.ops[1] = (A64Operand){.kind = A64O_IMM, .imm = 2};
    branch.ops[2] = (A64Operand){.kind = A64O_LABEL, .id = 3};
    branch.ops[3] = (A64Operand){.kind = A64O_LABEL, .id = 2};
    a64_block_append(&func, &blocks[0], branch);

    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_BCOND;
    branch.sf = A64_SF64;
    branch.cond = A64_CC_EQ;
    branch.nops = 2;
    branch.ops[0] = (A64Operand){.kind = A64O_LABEL, .id = 3};
    branch.ops[1] = (A64Operand){.kind = A64O_LABEL, .id = 1};
    a64_block_append(&func, &blocks[1], branch);

    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_CBZ;
    branch.sf = A64_SF32;
    branch.nops = 2;
    branch.ops[0] = (A64Operand){.kind = A64O_REG, .reg = a64_phys(A64_X1)};
    branch.ops[1] = (A64Operand){.kind = A64O_LABEL, .id = 1};
    a64_block_append(&func, &blocks[2], branch);

    buf_init(&text);
    a64_emit_function(&func, &module, 0, IRLINK_INTERNAL, &text);
    buf_push_u8(&text, 0);
    T_ASSERT(t, strstr((const char *)text.data, "\ttbz\tx0, #2, .Lf0_3\n") !=
                    NULL);
    T_ASSERT(t, strstr((const char *)text.data, ".Lf0_2:\n"
                                                "\tb.eq\t.Lf0_3\n"
                                                "\tb\t.Lf0_1\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data, ".Lf0_3:\n"
                                                "\tcbz\tw1, .Lf0_1\n") != NULL);
    T_ASSERT(t, strstr((const char *)text.data, ".Lbr") == NULL);
    buf_free(&text);

    far_func.name = "far_cond";
    far_func.arena = &arena;
    far_func.blocks = far_blocks;
    far_func.nblocks = 3;
    far_func.allocated = true;

    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_TBZ;
    branch.sf = A64_SF64;
    branch.nops = 4;
    branch.ops[0] = (A64Operand){.kind = A64O_REG, .reg = a64_phys(A64_X0)};
    branch.ops[1] = (A64Operand){.kind = A64O_IMM, .imm = 2};
    branch.ops[2] = (A64Operand){.kind = A64O_LABEL, .id = 3};
    branch.ops[3] = (A64Operand){.kind = A64O_LABEL, .id = 2};
    a64_block_append(&far_func, &far_blocks[0], branch);
    padding.op = A64_OP_RET;
    for (i = 0; i < 8192; i++)
        a64_block_append(&far_func, &far_blocks[1], padding);

    buf_init(&text);
    a64_emit_function(&far_func, &module, 1, IRLINK_INTERNAL, &text);
    buf_push_u8(&text, 0);
    T_ASSERT(t, strstr((const char *)text.data, "\ttbnz\tx0, #2, .Lbr1_0\n"
                                                "\tb\t.Lf1_3\n"
                                                ".Lbr1_0:\n") != NULL);
    buf_free(&text);
    arena_free_all(&arena);
}

/* The 16-byte constant pool. Deduping is not a nicety: every fneg in a
 * function would otherwise mint its own copy of the same sign mask, and the
 * .rodata bytes are part of the object differential. */
void test_a64_cpool_interning(TestCtx *t)
{
    Arena arena;
    A64Func func = {0};

    arena_init(&arena);
    func.name = "cpool";
    func.arena = &arena;

    T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 0, 0), 0);
    T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 1, 0), 1);
    T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 0, 1), 2);
    /* Both halves participate: same lo, different hi is a DIFFERENT
     * constant, and 1.5L vs 2.25L differ only in the high word. */
    T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 0, 0), 0);
    T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 1, 0), 1);
    T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 0, 1), 2);
    T_ASSERT_EQ_INT(t, (int)func.ncpool, 3);

    /* Past the initial capacity, so the growth path copies what is there. */
    {
        u64 i;

        for (i = 0; i < 64; i++)
            T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 100 + i, 0), (int)(3 + i));
        T_ASSERT_EQ_INT(t, (int)func.ncpool, 67);
        for (i = 0; i < 64; i++)
            T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 100 + i, 0), (int)(3 + i));
        T_ASSERT_EQ_INT(t, (int)func.ncpool, 67);
    }
    T_ASSERT_EQ_INT(t, a64_cpool_add(&func, 0, 0), 0);
    arena_free_all(&arena);
}

/* Every FP predicate must have a libcall plan.
 *
 * C's six relational and equality operators reach only eight of the
 * fourteen; the other six arise when the optimizer negates a condition, so
 * no fixture covers them and the failure mode is an ICE on valid input --
 * exactly the shape that survives a full test run. */
void test_f128_every_predicate_has_a_libcall(TestCtx *t)
{
    static const u8 preds[] = {FCMP_OEQ, FCMP_ONE, FCMP_OLT, FCMP_OLE, FCMP_OGT,
                               FCMP_OGE, FCMP_ORD, FCMP_UEQ, FCMP_UNE, FCMP_ULT,
                               FCMP_ULE, FCMP_UGT, FCMP_UGE, FCMP_UNO};
    u32 i;

    for (i = 0; i < sizeof(preds) / sizeof(preds[0]); i++)
        T_ASSERT(t, lower_f128_compare_libcall(preds[i]) != NULL);
    T_ASSERT_EQ_INT(t, (int)(sizeof(preds) / sizeof(preds[0])),
                    (int)FCMP_UNO + 1);

    /* The relations that share a call with their negation: ULT is !(a >= b),
     * so it asks __getf2 and tests the other way. Getting this backwards is
     * invisible without a NaN. */
    T_ASSERT_EQ_STR(t, lower_f128_compare_libcall(FCMP_OLT), "__lttf2");
    T_ASSERT_EQ_STR(t, lower_f128_compare_libcall(FCMP_UGE), "__lttf2");
    T_ASSERT_EQ_STR(t, lower_f128_compare_libcall(FCMP_OGE), "__getf2");
    T_ASSERT_EQ_STR(t, lower_f128_compare_libcall(FCMP_ULT), "__getf2");
    T_ASSERT_EQ_STR(t, lower_f128_compare_libcall(FCMP_ORD), "__unordtf2");
    T_ASSERT_EQ_STR(t, lower_f128_compare_libcall(FCMP_UNO), "__unordtf2");
    T_ASSERT(t, lower_f128_compare_libcall(200) == NULL);
}
