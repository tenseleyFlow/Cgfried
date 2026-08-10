#include <string.h>

#include "ir/ir.h"
#include "unit.h"
#include "util/arena.h"

/* IR core units: builder structure (the sprint's §2 loop), operand
 * constructors, the 48-byte instruction budget, structural equality, and
 * the printer/parser round-trip with byte-pinned determinism. */

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
    char first_msg[512];
    u32 first_line;
    u32 first_col;
} IrFix;

static void ir_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    IrFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL) {
        if (f->errors == 0) {
            strncpy(f->first_msg, d->message, sizeof(f->first_msg) - 1);
            f->first_msg[sizeof(f->first_msg) - 1] = '\0';
            f->first_line = d->span.line;
            f->first_col = d->span.col;
        }
        f->errors++;
    }
}

static void fix_init(IrFix *f)
{
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = ir_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
}

static void fix_free(IrFix *f)
{
    arena_free_all(&f->arena);
}

/* Build the sprint's §2 sum_to loop through the builder. */
static IrModule *build_sum_to(IrFix *f)
{
    IrModule *m = ir_module_new(&f->arena, f->dc);
    IrType params[1] = {IRT_I32};
    IrFunc *fn = ir_func_new(m, "sum_to", IRT_I32, params, 1);
    BlockId entry = ir_block_new(m, fn, "entry");
    BlockId loop = ir_block_new(m, fn, "loop");
    BlockId body = ir_block_new(m, fn, "body");
    BlockId done = ir_block_new(m, fn, "done");
    ValueId i = ir_block_param(m, fn, loop, IRT_I32);
    ValueId acc = ir_block_param(m, fn, loop, IRT_I32);
    ValueId r;
    IrBuilder b;
    IrOperand args[2];
    ValueId c, acc2, i2;
    IrOperand rv;

    /* Values are created in DOCUMENT order (done's param comes after
     * body's instructions) so the renumbering printer round-trips this
     * module id-for-id — the order lowering naturally produces anyway. */
    ir_builder_at(&b, m, fn, entry);
    args[0] = ir_op_iconst(IRT_I32, 0);
    args[1] = ir_op_iconst(IRT_I32, 0);
    ir_build_br(&b, loop, args, 2);

    ir_builder_at(&b, m, fn, loop);
    c = ir_build_icmp(&b, ICMP_SLT, ir_op_value(fn, i),
                      ir_op_value(fn, fn->param_vals[0]));
    args[0] = ir_op_value(fn, acc);
    ir_build_condbr(&b, ir_op_value(fn, c), body, NULL, 0, done, args, 1);

    ir_builder_at(&b, m, fn, body);
    acc2 = ir_build2(&b, IR_IADD, IRT_I32, ir_op_value(fn, acc),
                     ir_op_value(fn, i));
    i2 = ir_build2(&b, IR_IADD, IRT_I32, ir_op_value(fn, i),
                   ir_op_iconst(IRT_I32, 1));
    args[0] = ir_op_value(fn, i2);
    args[1] = ir_op_value(fn, acc2);
    ir_build_br(&b, loop, args, 2);

    r = ir_block_param(m, fn, done, IRT_I32);
    ir_builder_at(&b, m, fn, done);
    rv = ir_op_value(fn, r);
    ir_build_ret(&b, &rv);
    return m;
}

void test_ir_inst_budget(TestCtx *t)
{
    /* The DoD line item, pinned at compile time AND here so the test log
     * carries the number. */
    T_ASSERT(t, sizeof(IrInst) <= 48);
    T_ASSERT(t, sizeof(IrOperand) <= 24);
}

void test_ir_builder_source_locations(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    IrFunc *fn;
    BlockId entry;
    IrBuilder b;
    Span a = {0}, z = {0}, got;
    IrInst *first;

    fix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "located", IRT_VOID, NULL, 0);
    entry = ir_block_new(m, fn, "entry");
    ir_builder_at(&b, m, fn, entry);

    a.file_id = diag_add_file(f.dc, "physical.c", "x\n", 2);
    a.line = 1;
    a.col = 1;
    a.len = 1;
    a.presumed_path = "logical.c";
    a.presumed_line = 40;
    fn->loc = ir_intern_span(m, a);
    ir_builder_set_span(&b, a);
    ir_build_store(&b, ir_op_iconst(IRT_I32, 1),
                   ir_op_symbol(IRT_PTR, ir_sym(m, "x"), 0), 4, 0);
    ir_build_ret(&b, NULL);

    first = fn->blocks[0].first;
    T_ASSERT(t, first->loc != 0);
    T_ASSERT_EQ_INT(t, fn->loc, first->loc); /* function/inst table dedup */
    T_ASSERT_EQ_INT(t, first->loc, first->next->loc); /* table dedup */
    {
        Span distinct = a;

        distinct.seq = 1;
        T_ASSERT(t, ir_intern_span(m, distinct) != first->loc);
        distinct = a;
        distinct.origin = SPAN_ORIGIN_ANY_MACRO;
        T_ASSERT(t, ir_intern_span(m, distinct) != first->loc);
    }
    got = ir_debug_loc(m, first->loc);
    T_ASSERT_EQ_INT(t, got.file_id, a.file_id);
    T_ASSERT_EQ_INT(t, got.line, 1);
    T_ASSERT_EQ_INT(t, got.presumed_line, 40);
    T_ASSERT_EQ_STR(t, diag_span_path(f.dc, got), "logical.c");
    T_ASSERT_EQ_INT(t, ir_inst_span(m, first).col, 1);
    T_ASSERT_EQ_INT(t, ir_debug_loc(m, 0).file_id, z.file_id);
    T_ASSERT_EQ_INT(t, sizeof(IrInst), 48);
    {
        Buf text;
        IrModule *parsed;

        buf_init(&text);
        ir_print_module_buf(&text, m);
        buf_push_u8(&text, 0);
        parsed = ir_parse_module(&f.arena, f.dc, (const char *)text.data,
                                 "<locless>");
        T_ASSERT(t, parsed != NULL);
        if (parsed) {
            T_ASSERT(t, ir_module_struct_eq(m, parsed));
            T_ASSERT_EQ_INT(t, parsed->nlocs, 0);
            T_ASSERT_EQ_INT(t, parsed->funcs[0].blocks[0].first->loc, 0);
        }
        buf_free(&text);
    }
    fix_free(&f);
}

void test_ir_module_clone_is_deep_and_preserves_provenance(TestCtx *t)
{
    IrFix f;
    Arena clones;
    IrModule *source, *copy;
    IrFunc *sf, *cf;
    Span loc = {0};
    const IrByteRange member_ranges[] = {{0, 1}, {4, 8}};

    fix_init(&f);
    arena_init(&clones);
    source = build_sum_to(&f);
    sf = &source->funcs[0];
    loc.file_id = diag_add_file(f.dc, "clone.c", "x\n", 2);
    loc.line = 1;
    loc.col = 1;
    loc.len = 1;
    loc.seq = 9;
    loc.origin = SPAN_ORIGIN_ANY_MACRO;
    sf->loc = ir_intern_span(source, loc);
    ir_mem_layout_register(source, loc, 8, member_ranges,
                           CGF_ARRAY_LEN(member_ranges), false);
    ir_func_record_removed_span(sf, (BlockId){3}, loc, IR_CFG_REMOVED_CONFIG);
    loc.seq = 10;
    ir_func_record_removed_span(sf, (BlockId){4}, loc, IR_CFG_REMOVED_CONFIG);
    loc.origin = SPAN_ORIGIN_SYSTEM_MACRO | SPAN_ORIGIN_ANY_MACRO;
    ir_func_record_removed_span(sf, (BlockId){4}, loc, IR_CFG_REMOVED_CONFIG);

    copy = ir_module_clone(&clones, source);
    T_ASSERT(t, copy != NULL);
    T_ASSERT(t, copy && ir_module_struct_eq(source, copy));
    if (copy) {
        cf = &copy->funcs[0];
        T_ASSERT(t, cf->module == copy);
        T_ASSERT(t, cf->blocks != sf->blocks);
        T_ASSERT(t, cf->blocks[0].first != sf->blocks[0].first);
        T_ASSERT(t, cf->blocks[0].first->edges != sf->blocks[0].first->edges);
        T_ASSERT(t, cf->blocks[0].first->edges[0].args !=
                        sf->blocks[0].first->edges[0].args);
        T_ASSERT_EQ_INT(t, cf->ncfg_removed, 3);
        T_ASSERT_EQ_INT(t, copy->nmem_layouts, 1);
        T_ASSERT(t, copy->mem_layouts != source->mem_layouts);
        T_ASSERT(t,
                 copy->mem_layouts[0].ranges != source->mem_layouts[0].ranges);
        T_ASSERT_EQ_INT(t, copy->mem_layouts[0].nranges, 2);
        T_ASSERT_EQ_INT(t, copy->mem_layouts[0].ranges[1].lo, 4);
        T_ASSERT_EQ_INT(t, copy->mem_layouts[0].ranges[1].hi, 8);
        T_ASSERT_EQ_INT(t, cf->cfg_removed[0].loc.seq, 9);
        T_ASSERT_EQ_INT(t, cf->cfg_removed[0].loc.origin,
                        SPAN_ORIGIN_ANY_MACRO);
        T_ASSERT_EQ_INT(t, cf->cfg_removed[1].loc.seq, 10);
        T_ASSERT_EQ_INT(t, cf->cfg_removed[1].loc.origin,
                        SPAN_ORIGIN_ANY_MACRO);
        T_ASSERT_EQ_INT(t, cf->cfg_removed[2].loc.seq, 10);
        T_ASSERT_EQ_INT(t, cf->cfg_removed[2].loc.origin,
                        SPAN_ORIGIN_SYSTEM_MACRO | SPAN_ORIGIN_ANY_MACRO);
        cf->blocks[0].first->edges[0].args[0].a = 99;
        T_ASSERT_EQ_INT(t, sf->blocks[0].first->edges[0].args[0].a, 0);
        T_ASSERT(t, !ir_module_struct_eq(source, copy));
    }
    arena_free_all(&clones);
    fix_free(&f);
}

void test_ir_builder_loop_structure(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    IrFunc *fn;
    const IrBlock *entry, *loop, *body, *done;
    const IrInst *in;

    fix_init(&f);
    m = build_sum_to(&f);
    T_ASSERT_EQ_INT(t, m->nfuncs, 1);
    fn = &m->funcs[0];
    T_ASSERT_EQ_STR(t, fn->name, "sum_to");
    T_ASSERT_EQ_INT(t, fn->ret, IRT_I32);
    T_ASSERT_EQ_INT(t, fn->nparams, 1);
    T_ASSERT_EQ_INT(t, fn->nblocks, 4);
    /* 1 fparam + 3 block params + 3 inst results = 7 values */
    T_ASSERT_EQ_INT(t, fn->nvals, 7);

    entry = &fn->blocks[0];
    loop = &fn->blocks[1];
    body = &fn->blocks[2];
    done = &fn->blocks[3];
    T_ASSERT_EQ_INT(t, entry->nparams, 0);
    T_ASSERT_EQ_INT(t, loop->nparams, 2);
    T_ASSERT_EQ_INT(t, done->nparams, 1);
    T_ASSERT_EQ_INT(t, entry->ninsts, 1);
    T_ASSERT_EQ_INT(t, loop->ninsts, 2);
    T_ASSERT_EQ_INT(t, body->ninsts, 3);
    T_ASSERT_EQ_INT(t, done->ninsts, 1);

    /* entry: br loop(0, 0) */
    in = entry->first;
    T_ASSERT_EQ_INT(t, in->op, IR_BR);
    T_ASSERT_EQ_INT(t, in->nedges, 1);
    T_ASSERT_EQ_INT(t, in->edges[0].target.v, 2);
    T_ASSERT_EQ_INT(t, in->edges[0].nargs, 2);
    T_ASSERT_EQ_INT(t, in->edges[0].args[0].kind, IROP_ICONST);

    /* loop: icmp then condbr with per-edge args */
    in = loop->first;
    T_ASSERT_EQ_INT(t, in->op, IR_ICMP);
    T_ASSERT_EQ_INT(t, in->subop, ICMP_SLT);
    T_ASSERT_EQ_INT(t, in->type, IRT_I32);
    in = in->next;
    T_ASSERT_EQ_INT(t, in->op, IR_CONDBR);
    T_ASSERT_EQ_INT(t, in->nedges, 2);
    T_ASSERT_EQ_INT(t, in->edges[0].nargs, 0);
    T_ASSERT_EQ_INT(t, in->edges[1].nargs, 1);
    T_ASSERT_EQ_INT(t, in->edges[1].target.v, 4);

    /* value bookkeeping: defs recorded where they happened */
    T_ASSERT_EQ_INT(t, fn->vals[0].def_kind, VDEF_FPARAM);
    T_ASSERT_EQ_INT(t, fn->vals[1].def_kind, VDEF_BPARAM);
    T_ASSERT_EQ_INT(t, fn->vals[1].def_block.v, 2);
    T_ASSERT_EQ_INT(t, ir_value_type(fn, fn->param_vals[0]), IRT_I32);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    fix_free(&f);
}

void test_ir_operand_constructors(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    IrOperand o;
    u32 s;

    fix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    o = ir_op_iconst(IRT_I64, -1);
    T_ASSERT_EQ_INT(t, o.kind, IROP_ICONST);
    T_ASSERT_EQ_INT(t, o.type, IRT_I64);
    T_ASSERT(t, o.a == 0xFFFFFFFFFFFFFFFFull);
    o = ir_op_fconst(IRT_F128, 0x123456789ABCDEF0ull, 0x3FFF000000000000ull);
    T_ASSERT_EQ_INT(t, o.kind, IROP_FCONST);
    T_ASSERT(t, o.a == 0x123456789ABCDEF0ull);
    T_ASSERT(t, o.b == 0x3FFF000000000000ull);
    s = ir_sym(m, "puts");
    T_ASSERT_EQ_INT(t, ir_sym(m, "puts"), s); /* interning dedupes */
    o = ir_op_symbol(IRT_PTR, s, -8);
    T_ASSERT_EQ_INT(t, o.kind, IROP_SYMBOL);
    T_ASSERT_EQ_INT(t, o.sym, s);
    T_ASSERT_EQ_INT(t, (i64)o.a, -8);
    o = ir_op_undef(IRT_F32);
    T_ASSERT_EQ_INT(t, o.kind, IROP_UNDEF);
    T_ASSERT_EQ_INT(t, o.type, IRT_F32);
    fix_free(&f);
}

void test_ir_struct_eq(TestCtx *t)
{
    IrFix f1, f2;
    IrModule *a, *b;

    fix_init(&f1);
    fix_init(&f2);
    a = build_sum_to(&f1);
    b = build_sum_to(&f2);
    T_ASSERT(t, ir_module_struct_eq(a, b));
    /* Perturb one constant: no longer equal. */
    b->funcs[0].blocks[0].first->edges[0].args[1].a = 7;
    T_ASSERT(t, !ir_module_struct_eq(a, b));
    b->funcs[0].blocks[0].first->edges[0].args[1].a = 0;
    T_ASSERT(t, ir_module_struct_eq(a, b));
    /* Perturb a block name: names compare by CONTENT. */
    b->funcs[0].blocks[2].name = "bodyx";
    T_ASSERT(t, !ir_module_struct_eq(a, b));
    fix_free(&f1);
    fix_free(&f2);
}

void test_ir_reserved_ops_named(TestCtx *t)
{
    /* The reserved band exists and sits after the terminators, so both
     * the builder trap and verifier check 10 key off IR_STACKSAVE. */
    T_ASSERT(t, IR_VA_ARG > IR_UNREACHABLE);
    T_ASSERT_EQ_INT(t, IR_OP_COUNT - IR_VA_ARG, 3);
}

/* --- printer/parser round-trip ------------------------------------------- */

static IrModule *parse_ok(TestCtx *t, IrFix *f, const char *src)
{
    IrModule *m = ir_parse_module(&f->arena, f->dc, src, "<test>");

    T_ASSERT(t, m != NULL);
    T_ASSERT_EQ_INT(t, f->errors, 0);
    return m;
}

/* parse -> print -> parse -> struct_eq, plus double-print byte compare.
 * The same invariant the driver enforces per fixture; here it guards the
 * builder-facing corners the fixtures cannot reach. */
static void roundtrip(TestCtx *t, IrFix *f, IrModule *m)
{
    Buf b1, b2;
    IrModule *m2;

    buf_init(&b1);
    ir_print_module_buf(&b1, m);
    buf_push_u8(&b1, 0);
    m2 = ir_parse_module(&f->arena, f->dc, (const char *)b1.data, "<rt>");
    T_ASSERT(t, m2 != NULL);
    if (m2)
        T_ASSERT(t, ir_module_struct_eq(m, m2));
    buf_init(&b2);
    if (m2)
        ir_print_module_buf(&b2, m2);
    buf_push_u8(&b2, 0);
    T_ASSERT(t, b1.len == b2.len && memcmp(b1.data, b2.data, b1.len) == 0);
    buf_free(&b1);
    buf_free(&b2);
}

void test_ir_roundtrip_builder_loop(TestCtx *t)
{
    IrFix f;
    IrModule *m;

    fix_init(&f);
    m = build_sum_to(&f);
    /* The builder created values in document order here, so even the
     * renumbering printer round-trips it structurally. */
    roundtrip(t, &f, m);
    fix_free(&f);
}

void test_ir_exact_asm_symbol_roundtrip(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    static const char src[] =
        "sym @!collision$UNIX2003\n"
        "sym @collision$UNIX2003\n"
        "global @!collision$UNIX2003 size 4 align 4 external\n"
        "global @collision$UNIX2003 size 4 align 4 external\n";

    fix_init(&f);
    m = parse_ok(t, &f, src);
    if (m) {
        T_ASSERT_EQ_INT(t, m->nsyms, 2);
        T_ASSERT(t, ir_sym_name_is_exact_asm(m->syms[0]));
        T_ASSERT(t, !ir_sym_name_is_exact_asm(m->syms[1]));
        T_ASSERT_EQ_STR(t, ir_sym_asm_spelling(m->syms[0]),
                        "collision$UNIX2003");
        T_ASSERT(t, strcmp(m->syms[0], m->syms[1]) != 0);
        roundtrip(t, &f, m);
    }
    fix_free(&f);
}

void test_ir_parse_full_surface(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    static const char src[] =
        "sym @g\n"
        "sym @ext\n"
        "global @g size 8 align 8 external init x00ff10ab00000000 "
        "reloc 0 @g 4\n"
        "global @c size 4 align 4 common tentative\n"
        "func i32 @callee(i32 %x) {\n"
        "entry():\n"
        "    ret i32 %x\n"
        "}\n"
        "func void @main() {\n"
        "entry():\n"
        "    %p = alloca 16, align 8\n"
        "    store i64 -1, %p, align 8, volatile\n"
        "    %v = load i64, %p, align 8, seq_cst\n"
        "    %d = sitofp i64 %v to f64\n"
        "    %e = fadd f64 %d, 0x3FF0000000000000\n"
        "    %q = fcmp uno f64 %e, undef\n"
        "    %w = trunc i64 %v to i8\n"
        "    %ww = zext i8 %w to i32\n"
        "    %r1 = call i32 @callee(i32 %ww)\n"
        "    %r2 = call i32 @ext(ptr @g+8)\n"
        "    %fp = ptradd @g, -16\n"
        "    call void %fp(i32 %q)\n"
        "    memcpy %p, @g, 8, align 8, volatile\n"
        "    memset %p, 0, 16, align 1\n"
        "    %sel = select %q, f64 %e, undef\n"
        "    %b = bitcast f64 %sel to i64\n"
        "    switch i64 %b, done(), 1: one(), -2: done()\n"
        "one():\n"
        "    unreachable\n"
        "done():\n"
        "    ret\n"
        "}\n"
        "func void @hq(ptr %out) abi(hfa_f128,2) {\n"
        "entry():\n"
        "    ret\n"
        "}\n";
    const IrFunc *fn;
    const IrInst *in;

    fix_init(&f);
    m = parse_ok(t, &f, src);
    if (!m) {
        fix_free(&f);
        return;
    }
    T_ASSERT_EQ_INT(t, m->nglobals, 2);
    T_ASSERT_EQ_INT(t, m->nfuncs, 3);
    /* sym table: declared order preserved, funcs appended after */
    T_ASSERT_EQ_STR(t, m->syms[0], "g");
    T_ASSERT_EQ_STR(t, m->syms[1], "ext");
    T_ASSERT_EQ_INT(t, m->globals[0].nrelocs, 1);
    T_ASSERT_EQ_INT(t, m->globals[0].init[1], 0xff);
    T_ASSERT(t, m->globals[1].is_tentative);
    T_ASSERT_EQ_INT(t, m->globals[1].linkage, IRLINK_COMMON);

    fn = &m->funcs[1];
    in = fn->blocks[0].first; /* alloca */
    T_ASSERT_EQ_INT(t, in->op, IR_ALLOCA);
    T_ASSERT_EQ_INT(t, in->align, 8);
    in = in->next; /* store: volatile flag, -1 iconst */
    T_ASSERT_EQ_INT(t, in->flags, IRF_VOLATILE);
    T_ASSERT(t, in->ops[0].a == 0xFFFFFFFFFFFFFFFFull);
    in = in->next; /* load: seq_cst */
    T_ASSERT_EQ_INT(t, in->flags, IRF_SEQ_CST);
    in = in->next->next; /* fadd: fconst bits exact */
    T_ASSERT(t, in->ops[1].a == 0x3FF0000000000000ull);
    in = in->next; /* fcmp uno with undef operand */
    T_ASSERT_EQ_INT(t, in->subop, FCMP_UNO);
    T_ASSERT_EQ_INT(t, in->ops[1].kind, IROP_UNDEF);
    in = in->next->next->next; /* call @callee: internal, arity 1 */
    T_ASSERT_EQ_INT(t, in->op, IR_CALL);
    T_ASSERT_EQ_INT(t, in->subop, FUNCREF_INTERNAL);
    T_ASSERT_EQ_INT(t, in->callee, 0);
    in = in->next; /* call @ext: external via symbol, ptr arg w/ addend */
    T_ASSERT_EQ_INT(t, in->subop, FUNCREF_EXTERNAL);
    T_ASSERT_EQ_STR(t, m->syms[in->callee], "ext");
    T_ASSERT_EQ_INT(t, (i64)in->ops[0].a, 8);
    in = in->next; /* ptradd with negative offset */
    T_ASSERT_EQ_INT(t, (i64)in->ops[1].a, -16);
    in = in->next; /* indirect call: fp is ops[0] */
    T_ASSERT_EQ_INT(t, in->subop, FUNCREF_INDIRECT);
    T_ASSERT_EQ_INT(t, in->nops, 2);
    T_ASSERT_EQ_INT(t, in->result.v, 0); /* void: defines nothing */
    T_ASSERT_EQ_INT(t, m->funcs[2].abi_ret, IR_ABIRET_HFA_F128);
    T_ASSERT_EQ_INT(t, m->funcs[2].abi_ret_n, 2);

    roundtrip(t, &f, m);
    fix_free(&f);
}

void test_ir_parse_forward_refs(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    /* A value used textually before its defining block appears — legal
     * SSA (mid dominates use), and the fixup machinery's reason to
     * exist. */
    static const char src[] = "func i32 @fwd() {\n"
                              "entry():\n"
                              "    br mid()\n"
                              "use():\n"
                              "    %y = iadd i32 %x, 1\n"
                              "    ret i32 %y\n"
                              "mid():\n"
                              "    %x = iadd i32 2, 3\n"
                              "    br use()\n"
                              "}\n";

    fix_init(&f);
    m = parse_ok(t, &f, src);
    if (m) {
        const IrInst *in = m->funcs[0].blocks[1].first;

        T_ASSERT_EQ_INT(t, in->ops[0].kind, IROP_VALUE);
        T_ASSERT_EQ_INT(t, in->ops[0].type, IRT_I32);
        roundtrip(t, &f, m);
    }
    fix_free(&f);
}

void test_ir_parse_f80_f128_bits(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    static const char src[] =
        "func void @f() {\n"
        "entry():\n"
        "    %a = fadd f80 0x3FFF:0x8000000000000000, "
        "0x0001:0x0000000000000001\n"
        "    %b = fadd f128 0x3FFF000000000000:0x0000000000000001, undef\n"
        "    %c = fpext f32 0x3F800000 to f64\n"
        "    ret\n"
        "}\n";

    fix_init(&f);
    m = parse_ok(t, &f, src);
    if (m) {
        const IrInst *in = m->funcs[0].blocks[0].first;

        T_ASSERT(t, in->ops[0].b == 0x3FFFull);
        T_ASSERT(t, in->ops[0].a == 0x8000000000000000ull);
        T_ASSERT(t, in->ops[1].b == 0x1ull);
        T_ASSERT(t, in->ops[1].a == 0x1ull);
        in = in->next;
        T_ASSERT(t, in->ops[0].b == 0x3FFF000000000000ull);
        in = in->next;
        T_ASSERT(t, in->ops[0].a == 0x3F800000ull);
        roundtrip(t, &f, m);
    }
    fix_free(&f);
}

void test_ir_parse_errors_have_locations(TestCtx *t)
{
    IrFix f;
    IrModule *m;

    /* Unknown instruction: file:line:col quality, not an assert-fest. */
    fix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @f() {\n"
                        "entry():\n"
                        "    frobnicate i32 1, 2\n"
                        "}\n",
                        "<test>");
    T_ASSERT(t, m == NULL);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    T_ASSERT_EQ_INT(t, f.first_line, 3);
    T_ASSERT_EQ_INT(t, f.first_col, 5);
    T_ASSERT(t, strstr(f.first_msg, "expected an instruction") != NULL);
    fix_free(&f);

    /* Use of a value that never gets defined -> fixup resolution error. */
    fix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @f() {\n"
                        "entry():\n"
                        "    %a = iadd i32 %ghost, 1\n"
                        "    ret\n"
                        "}\n",
                        "<test>");
    T_ASSERT(t, m == NULL);
    T_ASSERT(t, strstr(f.first_msg, "undefined value '%ghost'") != NULL);
    fix_free(&f);

    /* Redefinition of a value name. */
    fix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @f() {\n"
                        "entry():\n"
                        "    %a = iadd i32 1, 2\n"
                        "    %a = iadd i32 3, 4\n"
                        "    ret\n"
                        "}\n",
                        "<test>");
    T_ASSERT(t, m == NULL);
    T_ASSERT(t, strstr(f.first_msg, "redefinition") != NULL);
    fix_free(&f);

    /* Branch to a label that does not exist. */
    fix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @f() {\n"
                        "entry():\n"
                        "    br nowhere()\n"
                        "}\n",
                        "<test>");
    T_ASSERT(t, m == NULL);
    T_ASSERT(t, strstr(f.first_msg, "unknown block") != NULL);
    fix_free(&f);

    /* Decimal float constant: the no-host-float law at the parser door. */
    fix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @f() {\n"
                        "entry():\n"
                        "    %a = fadd f64 1, 2\n"
                        "    ret\n"
                        "}\n",
                        "<test>");
    T_ASSERT(t, m == NULL);
    T_ASSERT(t, strstr(f.first_msg, "exact bits") != NULL);
    fix_free(&f);

    /* Bad init image length. */
    fix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "global @g size 4 align 4 internal init x00ff\n",
                        "<test>");
    T_ASSERT(t, m == NULL);
    T_ASSERT(t, strstr(f.first_msg, "init image") != NULL);
    fix_free(&f);
}

void test_ir_parse_block_param_corners(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    /* 0 params and 8 params; condbr with both edges to the SAME block
     * carrying different args. */
    static const char src[] =
        "func i32 @corners(i32 %a) {\n"
        "entry():\n"
        "    condbr %a, wide(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, "
        "i32 7, i32 8), wide(i32 8, i32 7, i32 6, i32 5, i32 4, i32 3, "
        "i32 2, i32 1)\n"
        "wide(i32 %p0, i32 %p1, i32 %p2, i32 %p3, i32 %p4, i32 %p5, "
        "i32 %p6, i32 %p7):\n"
        "    ret i32 %p7\n"
        "}\n";

    fix_init(&f);
    m = parse_ok(t, &f, src);
    if (m) {
        const IrInst *in = m->funcs[0].blocks[0].first;

        T_ASSERT_EQ_INT(t, m->funcs[0].blocks[1].nparams, 8);
        T_ASSERT_EQ_INT(t, in->edges[0].target.v, in->edges[1].target.v);
        T_ASSERT_EQ_INT(t, (i64)in->edges[0].args[0].a, 1);
        T_ASSERT_EQ_INT(t, (i64)in->edges[1].args[0].a, 8);
        roundtrip(t, &f, m);
    }
    fix_free(&f);
}

void test_ir_switch_zero_cases(TestCtx *t)
{
    IrFix f;
    IrModule *m;
    static const char src[] = "func void @s(i32 %x) {\n"
                              "entry():\n"
                              "    switch i32 %x, done()\n"
                              "done():\n"
                              "    ret\n"
                              "}\n";

    fix_init(&f);
    m = parse_ok(t, &f, src);
    if (m) {
        const IrInst *in = m->funcs[0].blocks[0].first;

        T_ASSERT_EQ_INT(t, in->op, IR_SWITCH);
        T_ASSERT_EQ_INT(t, in->nedges, 1); /* just the default */
        roundtrip(t, &f, m);
    }
    fix_free(&f);
}
