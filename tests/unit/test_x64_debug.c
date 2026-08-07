#include <string.h>

#include "cg/debug.h"
#include "cg/x86_64/debug.h"
#include "unit.h"
#include "util/arena.h"

static bool debug_has(const Buf *b, const char *needle)
{
    size_t n = strlen(needle);
    size_t i;

    for (i = 0; i + n <= b->len; i++)
        if (memcmp(b->data + i, needle, n) == 0)
            return true;
    return false;
}

static void assert_bytes(TestCtx *t, const u8 *got, size_t ngot, const u8 *want,
                         size_t nwant)
{
    T_ASSERT_EQ_INT(t, ngot, nwant);
    T_ASSERT(t, ngot == nwant && memcmp(got, want, nwant) == 0);
}

void test_x64_dwarf_leb_boundaries(TestCtx *t)
{
    static const u8 u0[] = {0};
    static const u8 u127[] = {127};
    static const u8 u128[] = {128, 1};
    static const u8 s63[] = {63};
    static const u8 s64[] = {192, 0};
    static const u8 sn8[] = {120};
    static const u8 sn64[] = {64};
    static const u8 sn65[] = {191, 127};
    u8 out[10];

    assert_bytes(t, out, cg_dwarf_uleb(0, out), u0, sizeof(u0));
    assert_bytes(t, out, cg_dwarf_uleb(127, out), u127, sizeof(u127));
    assert_bytes(t, out, cg_dwarf_uleb(128, out), u128, sizeof(u128));
    assert_bytes(t, out, cg_dwarf_sleb(63, out), s63, sizeof(s63));
    assert_bytes(t, out, cg_dwarf_sleb(64, out), s64, sizeof(s64));
    assert_bytes(t, out, cg_dwarf_sleb(-8, out), sn8, sizeof(sn8));
    assert_bytes(t, out, cg_dwarf_sleb(-64, out), sn64, sizeof(sn64));
    assert_bytes(t, out, cg_dwarf_sleb(-65, out), sn65, sizeof(sn65));
}

void test_cg_dwarf_special_opcode_boundaries(TestCtx *t)
{
    u8 op = 0;

    T_ASSERT(t, cg_dwarf_special(-5, 0, &op));
    T_ASSERT_EQ_INT(t, op, 13);
    T_ASSERT(t, cg_dwarf_special(-5, 17, &op));
    T_ASSERT_EQ_INT(t, op, 251);
    T_ASSERT(t, !cg_dwarf_special(-6, 0, &op));
    T_ASSERT(t, !cg_dwarf_special(9, 0, &op));
    T_ASSERT(t, !cg_dwarf_special(0, 17, &op));
    T_ASSERT(t, !cg_dwarf_special(0, 18, &op));
}

void test_x64_debug_label_transitions(TestCtx *t)
{
    X64Func f;
    X64Block block;
    X64Inst insts[6];

    memset(&f, 0, sizeof(f));
    memset(&block, 0, sizeof(block));
    memset(insts, 0, sizeof(insts));
    f.blocks = &block;
    f.nblocks = 1;
    block.insts = insts;
    block.n = CGF_ARRAY_LEN(insts);
    insts[2].loc = 1;
    insts[3].loc = 1;
    insts[5].loc = 2;
    x64_debug_prepare(&f);
    T_ASSERT(t, f.debug_lines);
    T_ASSERT_EQ_INT(t, insts[0].debug_label, 0);
    T_ASSERT_EQ_INT(t, insts[1].debug_label, 0);
    T_ASSERT_EQ_INT(t, insts[2].debug_label, 1);
    T_ASSERT_EQ_INT(t, insts[3].debug_label, 0);
    T_ASSERT_EQ_INT(t, insts[4].debug_label, 2); /* source -> line zero */
    T_ASSERT_EQ_INT(t, insts[5].debug_label, 3);
}

void test_x64_debug_section_bytes(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    IrModule *m;
    X64Func f;
    X64Func *funcs[1];
    X64Block block;
    X64Inst insts[3];
    Buf no_debug, debug;
    Span span = {0};
    TargetSpec target = {CGF_TARGET_X86_64_LINUX_GNU};

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    m = ir_module_new(&arena, dc);
    IrFunc *irf = ir_func_new(m, "probe", IRT_VOID, NULL, 0);
    span.file_id = diag_add_file(dc, "tests/debug/probe.c", "return;\n", 8);
    span.line = 1;
    span.col = 1;
    span.len = 6;
    m->locs = arena_alloc(&arena, sizeof(Span), _Alignof(Span));
    m->locs[0] = span;
    m->nlocs = 1;
    m->cap_locs = 1;
    irf->loc = 1;

    memset(&f, 0, sizeof(f));
    memset(&block, 0, sizeof(block));
    memset(insts, 0, sizeof(insts));
    f.name = "probe";
    f.allocated = true;
    f.blocks = &block;
    f.nblocks = 1;
    block.insts = insts;
    block.n = CGF_ARRAY_LEN(insts);
    insts[0].op = X64_OP_PUSH;
    insts[0].width = X64_Q;
    insts[0].a.kind = X64O_VREG;
    insts[0].a.r.v = X64_RBP + 1;
    insts[1].op = X64_OP_MOV;
    insts[1].width = X64_Q;
    insts[1].def.v = X64_RBP + 1;
    insts[1].a.kind = X64O_VREG;
    insts[1].a.r.v = X64_RSP + 1;
    insts[2].op = X64_OP_RET;
    insts[2].loc = 1;
    funcs[0] = &f;

    buf_init(&no_debug);
    x64_emit_debug_sections(target, &arena, m, funcs, 1, "tests/debug/probe.c",
                            "/work", false, &no_debug);
    T_ASSERT(t, debug_has(&no_debug, "\t.section\t.eh_frame,\"a\""));
    T_ASSERT(t, !debug_has(&no_debug, ".debug_line"));
    T_ASSERT(t,
             debug_has(&no_debug,
                       "\t.byte\t1,122,82,0,1,120,16,1,27,12,7,8,144,1,0,0\n"));
    T_ASSERT(t, debug_has(&no_debug,
                          "\t.byte\t0,65,14,16,134,2,67,13,6,0,0,0,0,0,0,0\n"));

    x64_debug_prepare(&f);
    buf_init(&debug);
    x64_emit_debug_sections(target, &arena, m, funcs, 1, "tests/debug/probe.c",
                            "/work", true, &debug);
    T_ASSERT(t, debug_has(&debug, "\t.section\t.debug_line,\"\""));
    T_ASSERT(t, debug_has(&debug, "\t.section\t.debug_abbrev,\"\""));
    T_ASSERT(t, debug_has(&debug, "\t.section\t.debug_info,\"\""));
    T_ASSERT(t, debug_has(&debug, "\t.byte\t1\n\t.byte\t0,9,2\n"));
    T_ASSERT(t, debug_has(&debug, "\t.quad\t.Lloc_0_1\n"));
    T_ASSERT(t, debug_has(&debug, "\t.byte\t10\n\t.byte\t1\n"));
    T_ASSERT(t, debug_has(&debug, "\t.quad\t.Lfe0_0\n"));
    buf_free(&no_debug);
    buf_free(&debug);
    arena_free_all(&arena);
}
