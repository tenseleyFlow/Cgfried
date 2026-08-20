/* Deterministic oracle for the public alias-analysis contract.
 *
 * The analysis result is checked against a deliberately independent concrete
 * model.  The model knows only two objects, byte offsets, and the selector
 * environment; it never inspects alias-analysis object ids.  OPT-H-04 is the
 * single expected exception: opposite correlated selects currently collapse
 * to the same abstract footprint and are reported MUST.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "ir/ir.h"
#include "opt/alias.h"
#include "util/arena.h"

typedef enum ConcreteObject { OBJECT_A, OBJECT_B } ConcreteObject;

typedef enum PointerDesc {
    PTR_A0,
    PTR_A4,
    PTR_A8,
    PTR_B0,
    PTR_B4,
    PTR_SELECT_A0_A4,
    PTR_SELECT_A4_A0,
    PTR_SELECT_A0_B0,
    PTR_SELECT_B0_A0,
    PTR_BLOCK_A4,
    PTR_COUNT
} PointerDesc;

typedef struct ConcretePtr {
    ConcreteObject object;
    unsigned offset;
} ConcretePtr;

typedef struct Probe {
    Arena arena;
    DiagCtx *dc;
    IrModule *module;
    AliasCtx *alias;
    IrOperand ptrs[PTR_COUNT];
} Probe;

typedef struct Cell {
    const char *name;
    PointerDesc left;
    PointerDesc right;
    u64 left_size;
    u64 right_size;
    bool is_h04;
} Cell;

static const char ir_source[] =
    "func void @alias_oracle(i32 %choose) {\n"
    "entry():\n"
    "    %a = alloca 16, align 8\n"
    "    %b = alloca 16, align 8\n"
    "    %a4 = ptradd %a, 4\n"
    "    %a8 = ptradd %a, 8\n"
    "    %b4 = ptradd %b, 4\n"
    "    %direct = select %choose, ptr %a, %a4\n"
    "    %reverse = select %choose, ptr %a4, %a\n"
    "    %cross = select %choose, ptr %a, %b\n"
    "    %cross_reverse = select %choose, ptr %b, %a\n"
    "    br join(ptr %a4)\n"
    "join(ptr %block_a4):\n"
    "    ret\n"
    "}\n";

static const Cell cells[] = {
    {"must/direct", PTR_A0, PTR_A0, 4, 4, false},
    {"may/block-param", PTR_A4, PTR_BLOCK_A4, 4, 4, false},
    {"no/distinct-object", PTR_A0, PTR_B0, 4, 4, false},
    {"no/distinct-affine", PTR_A4, PTR_B4, 4, 4, false},
    {"no/adjacent-affine", PTR_A0, PTR_A8, 8, 4, false},
    {"may/partial-overlap", PTR_A0, PTR_A4, 8, 8, false},
    {"must/correlated-self", PTR_SELECT_A0_A4, PTR_SELECT_A0_A4, 4, 4, false},
    {"may/select-vs-direct", PTR_SELECT_A0_B0, PTR_A0, 4, 4, false},
    {"may/opposite-objects", PTR_SELECT_A0_B0, PTR_SELECT_B0_A0, 4, 4, false},
    {"h04/opposite-offsets", PTR_SELECT_A0_A4, PTR_SELECT_A4_A0, 4, 4, true},
};

static void silent_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    (void)user;
    (void)diag;
    (void)dc;
}

static IrInst *find_inst(IrFunc *func, IrOp op, u32 ordinal)
{
    u32 bi;

    for (bi = 0; bi < func->nblocks; bi++) {
        IrInst *inst;

        for (inst = func->blocks[bi].first; inst; inst = inst->next) {
            if (inst->op == op && ordinal-- == 0)
                return inst;
        }
    }
    return NULL;
}

static bool operand_for_inst(IrFunc *func, IrOp op, u32 ordinal, IrOperand *out)
{
    IrInst *inst = find_inst(func, op, ordinal);

    if (!inst || !inst->result.v)
        return false;
    *out = ir_op_value(func, inst->result);
    return true;
}

static bool probe_init(Probe *probe)
{
    DiagSink sink = {silent_sink, NULL};
    AliasConfig config;
    IrFunc *func;

    memset(probe, 0, sizeof(*probe));
    arena_init(&probe->arena);
    probe->dc = diag_ctx_new(&probe->arena);
    diag_set_sink(probe->dc, sink);
    probe->module =
        ir_parse_module(&probe->arena, probe->dc, ir_source, "<alias-oracle>");
    if (!probe->module || diag_had_error(probe->dc) ||
        !ir_verify(probe->dc, probe->module) || probe->module->nfuncs != 1)
        return false;
    func = &probe->module->funcs[0];
    if (!operand_for_inst(func, IR_ALLOCA, 0, &probe->ptrs[PTR_A0]) ||
        !operand_for_inst(func, IR_PTRADD, 0, &probe->ptrs[PTR_A4]) ||
        !operand_for_inst(func, IR_PTRADD, 1, &probe->ptrs[PTR_A8]) ||
        !operand_for_inst(func, IR_ALLOCA, 1, &probe->ptrs[PTR_B0]) ||
        !operand_for_inst(func, IR_PTRADD, 2, &probe->ptrs[PTR_B4]) ||
        !operand_for_inst(func, IR_SELECT, 0, &probe->ptrs[PTR_SELECT_A0_A4]) ||
        !operand_for_inst(func, IR_SELECT, 1, &probe->ptrs[PTR_SELECT_A4_A0]) ||
        !operand_for_inst(func, IR_SELECT, 2, &probe->ptrs[PTR_SELECT_A0_B0]) ||
        !operand_for_inst(func, IR_SELECT, 3, &probe->ptrs[PTR_SELECT_B0_A0]) ||
        func->nblocks != 2 || func->blocks[1].nparams != 1)
        return false;
    probe->ptrs[PTR_BLOCK_A4] = ir_op_value(func, func->blocks[1].params[0]);
    memset(&config, 0, sizeof(config));
    config.func = func;
    probe->alias = alias_build(probe->module, &config);
    return probe->alias != NULL;
}

static void probe_destroy(Probe *probe)
{
    alias_free(probe->alias);
    arena_free_all(&probe->arena);
}

static ConcretePtr concrete_ptr(PointerDesc desc, bool choose)
{
    ConcretePtr ptr = {OBJECT_A, 0};

    switch (desc) {
    case PTR_A0:
        break;
    case PTR_A4:
    case PTR_BLOCK_A4:
        ptr.offset = 4;
        break;
    case PTR_A8:
        ptr.offset = 8;
        break;
    case PTR_B0:
        ptr.object = OBJECT_B;
        break;
    case PTR_B4:
        ptr.object = OBJECT_B;
        ptr.offset = 4;
        break;
    case PTR_SELECT_A0_A4:
        ptr.offset = choose ? 0 : 4;
        break;
    case PTR_SELECT_A4_A0:
        ptr.offset = choose ? 4 : 0;
        break;
    case PTR_SELECT_A0_B0:
        ptr.object = choose ? OBJECT_A : OBJECT_B;
        break;
    case PTR_SELECT_B0_A0:
        ptr.object = choose ? OBJECT_B : OBJECT_A;
        break;
    case PTR_COUNT:
        break;
    }
    return ptr;
}

static bool intervals_overlap(ConcretePtr a, u64 asize, ConcretePtr b,
                              u64 bsize)
{
    u64 a_end = (u64)a.offset + asize;
    u64 b_end = (u64)b.offset + bsize;

    return a.object == b.object && (u64)a.offset < b_end &&
           (u64)b.offset < a_end;
}

static bool intervals_equal(ConcretePtr a, u64 asize, ConcretePtr b, u64 bsize)
{
    return a.object == b.object && a.offset == b.offset && asize == bsize;
}

static const char *result_name(AliasResult result)
{
    switch (result) {
    case ALIAS_NO:
        return "NO";
    case ALIAS_MAY:
        return "MAY";
    case ALIAS_MUST:
        return "MUST";
    }
    return "INVALID";
}

static int check_cell(Probe *probe, const Cell *cell, bool *saw_h04)
{
    MemLoc left = alias_memloc(probe->alias, probe->ptrs[cell->left],
                               cell->left_size, ETYPE_CHAR);
    MemLoc right = alias_memloc(probe->alias, probe->ptrs[cell->right],
                                cell->right_size, ETYPE_CHAR);
    AliasResult forward = alias_query(probe->alias, left, right);
    AliasResult reverse = alias_query(probe->alias, right, left);
    bool any_overlap = false;
    bool all_equal = true;
    unsigned environment;
    bool sound;
    bool expected_h04 = false;
    bool accepted;

    for (environment = 0; environment < 2; environment++) {
        ConcretePtr a = concrete_ptr(cell->left, environment != 0);
        ConcretePtr b = concrete_ptr(cell->right, environment != 0);

        any_overlap |=
            intervals_overlap(a, cell->left_size, b, cell->right_size);
        all_equal &= intervals_equal(a, cell->left_size, b, cell->right_size);
    }
    sound = forward == ALIAS_MAY || (forward == ALIAS_NO && !any_overlap) ||
            (forward == ALIAS_MUST && all_equal);
    if (cell->is_h04 && forward == ALIAS_MUST && !all_equal) {
        *saw_h04 = true;
        expected_h04 = true;
    }
    accepted = (sound || expected_h04) && forward == reverse;
    printf("%-24s %-4s %s\n", cell->name, result_name(forward),
           accepted ? (expected_h04 ? "expected" : "sound") : "FAIL");
    return accepted ? 0 : 1;
}

int main(int argc, char **argv)
{
    Probe probe;
    bool saw_h04 = false;
    size_t i;
    int failures = 0;

    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: alias_oracle\n");
        return 2;
    }
    if (!probe_init(&probe)) {
        fprintf(stderr, "alias_oracle: cannot parse or analyze probe IR\n");
        probe_destroy(&probe);
        return 2;
    }
    for (i = 0; i < sizeof(cells) / sizeof(cells[0]); i++)
        failures += check_cell(&probe, &cells[i], &saw_h04);
    if (!saw_h04) {
        fprintf(stderr, "alias_oracle: OPT-H-04 was not reproduced\n");
        failures++;
    }
    printf("alias_oracle: %zu cells, %d unexpected\n",
           sizeof(cells) / sizeof(cells[0]), failures);
    probe_destroy(&probe);
    return failures == 0 ? 0 : 1;
}
