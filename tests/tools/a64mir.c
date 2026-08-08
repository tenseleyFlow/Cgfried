#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cg/arm64/mir.h"
#include "diag.h"
#include "ir/ir.h"
#include "util/arena.h"
#include "util/buf.h"

static void die(const char *path, const char *why)
{
    fprintf(stderr, "a64mir: %s: %s\n", path, why);
    exit(1);
}

static char *read_all(const char *path)
{
    FILE *fp;
    char *text;
    long end = 0;

    fp = fopen(path, "rb");
    if (!fp)
        die(path, strerror(errno));
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0)
        die(path, "cannot determine file size");
    text = malloc((size_t)end + 1);
    if (!text)
        die(path, "out of memory");
    if ((size_t)end != fread(text, 1, (size_t)end, fp) || fclose(fp) != 0)
        die(path, "read failed");
    text[end] = '\0';
    return text;
}

static A64Operand reg(A64PhysReg r)
{
    A64Operand op = {0};
    op.kind = A64O_REG;
    op.reg = a64_phys(r);
    return op;
}

static A64Operand imm(i64 value)
{
    A64Operand op = {0};
    op.kind = A64O_IMM;
    op.imm = value;
    return op;
}

static A64Operand mem(A64PhysReg base, i64 offset, u8 size)
{
    A64Operand op = {0};
    op.kind = A64O_MEM;
    op.mem.base = a64_phys(base);
    op.mem.offset = offset;
    op.mem.size = size;
    op.mem.mode = (u8)a64_isel_addr(offset, size, false, false);
    return op;
}

static void append3(A64Func *f, A64Block *b, A64Op op, A64Operand a,
                    A64Operand x, A64Operand y, u8 flags)
{
    A64Inst in = {0};
    in.op = (u16)op;
    in.sf = A64_SF64;
    in.flags = flags;
    in.nops = 3;
    in.ops[0] = a;
    in.ops[1] = x;
    in.ops[2] = y;
    a64_block_append(f, b, in);
}

/* Architectural encoding 31 has two names.  These hand-built instructions
 * pin the legal spellings in positions that IR cannot naturally expose
 * before AAPCS64/frame lowering arrives in Sprint 48. */
static int print_reg31(Arena *arena, DiagCtx *dc, Buf *out)
{
    A64Func f = {0};
    A64Block block = {0};
    A64Inst in = {0};

    f.name = "reg31_legal";
    f.arena = arena;
    f.blocks = &block;
    f.nblocks = 1;
    append3(&f, &block, A64_OP_ADD, reg(A64_X0), reg(A64_SP), imm(0), 0);
    append3(&f, &block, A64_OP_ADD, reg(A64_SP), reg(A64_X0), imm(16), 0);
    append3(&f, &block, A64_OP_SUB, reg(A64_SP), reg(A64_SP), imm(16), 0);
    append3(&f, &block, A64_OP_SUBS, reg(A64_XZR), reg(A64_X0), imm(5),
            A64IF_DEFS_NZCV);
    append3(&f, &block, A64_OP_ANDS, reg(A64_XZR), reg(A64_X0), imm(255),
            A64IF_DEFS_NZCV);
    append3(&f, &block, A64_OP_ORR, reg(A64_SP), reg(A64_X0), imm(255), 0);
    append3(&f, &block, A64_OP_ORR, reg(A64_XZR), reg(A64_X0), reg(A64_X1), 0);
    in.op = A64_OP_LOAD;
    in.sf = A64_SF64;
    in.nops = 2;
    in.ops[0] = reg(A64_X0);
    in.ops[1] = mem(A64_SP, 0, 8);
    a64_block_append(&f, &block, in);
    in.op = A64_OP_STORE;
    in.ops[0] = reg(A64_XZR);
    in.ops[1] = mem(A64_SP, -8, 8);
    a64_block_append(&f, &block, in);
    if (a64_mir_verify(&f, dc) != 0)
        return 0;
    a64_mir_print(&f, out);
    return 1;
}

int main(int argc, char **argv)
{
    bool reg31_mode = false;
    bool asm_mode = false;
    const char *path;
    char *source;
    Arena arena;
    DiagCtx *dc;
    IrModule *module;
    Buf out;
    u32 i;

    if (argc == 3 && strcmp(argv[1], "--reg31") == 0) {
        reg31_mode = true;
        path = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--asm") == 0) {
        /* Sprint 49: allocate and emit real assembly. The driver cannot
         * select arm64 on an x86 host until --target lands in Sprint 51, so
         * this is how arm64 emission is exercised and assembled here. */
        asm_mode = true;
        path = argv[2];
    } else if (argc == 2) {
        path = argv[1];
    } else {
        fprintf(stderr, "usage: a64mir [--reg31|--asm] input.cgfir\n");
        return 2;
    }
    source = read_all(path);
    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    module = ir_parse_module(&arena, dc, source, path);
    if (!module || diag_had_error(dc) || !ir_verify(dc, module)) {
        free(source);
        arena_free_all(&arena);
        return 1;
    }
    buf_init(&out);
    for (i = 0; i < module->nfuncs; i++) {
        A64Func *f = a64_isel_function(module, &module->funcs[i], &arena);
        if (!f || a64_mir_verify(f, dc) != 0) {
            buf_free(&out);
            free(source);
            arena_free_all(&arena);
            return 1;
        }
        if (asm_mode) {
            a64_regalloc(f);
            if (a64_mir_verify(f, dc) != 0) {
                buf_free(&out);
                free(source);
                arena_free_all(&arena);
                return 1;
            }
            a64_emit_function(f, module, i, module->funcs[i].linkage, &out);
            continue;
        }
        a64_mir_print(f, &out);
    }
    if (asm_mode)
        a64_emit_globals(module, &out, false);
    if (reg31_mode && !print_reg31(&arena, dc, &out)) {
        buf_free(&out);
        free(source);
        arena_free_all(&arena);
        return 1;
    }
    if (out.len && fwrite(out.data, 1, out.len, stdout) != out.len)
        die("stdout", "write failed");
    buf_free(&out);
    free(source);
    arena_free_all(&arena);
    return 0;
}
