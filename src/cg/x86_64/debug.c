#include "cg/x86_64/debug.h"

#include <string.h>

/* DWARF v4 line tables + the minimal CU DIE gdb needs, and x86-64
 * .eh_frame CFI. .debug_frame would serve debuggers only; .eh_frame serves
 * both debuggers and runtime unwinders, so it ships even without -g.
 *
 * The x86 FDE program relies on the v0.1.0 always-rbp prologue:
 *   push %rbp                 (1 byte)
 *   mov  %rsp, %rbp           (3 bytes)
 * -fomit-frame-pointer is therefore warn+ignored until a future CFI design.
 * Epilogue windows are deliberately not described in v0.1.0; ordinary C
 * backtraces sample call sites, where the rule is exact. */

typedef struct DebugFile {
    const char *path; /* identity: displayed path, as given */
    const char *name;
    u32 dir;
} DebugFile;

typedef struct DebugFiles {
    Arena *arena;
    const char *comp_dir;
    const char **dirs;
    u32 ndirs, cap;
    DebugFile *files;
    u32 nfiles;
} DebugFiles;

size_t x64_dwarf_uleb(u64 value, u8 out[10])
{
    size_t n = 0;

    do {
        u8 b = (u8)(value & 0x7f);

        value >>= 7;
        if (value)
            b |= 0x80;
        out[n++] = b;
    } while (value);
    return n;
}

size_t x64_dwarf_sleb(i64 value, u8 out[10])
{
    size_t n = 0;
    bool more = true;

    while (more) {
        u8 b = (u8)((u64)value & 0x7f);
        bool sign = (b & 0x40) != 0;

        value >>= 7;
        more = !((value == 0 && !sign) || (value == -1 && sign));
        if (more)
            b |= 0x80;
        out[n++] = b;
    }
    return n;
}

bool x64_dwarf_special(i64 line_delta, u64 address_delta, u8 *opcode)
{
    u64 adjusted;

    if (line_delta < -5 || line_delta > 8 || address_delta > 17)
        return false;
    adjusted = (u64)(line_delta + 5) + 14 * address_delta;
    if (adjusted + 13 > 255)
        return false;
    if (opcode)
        *opcode = (u8)(adjusted + 13);
    return true;
}

void x64_debug_prepare(X64Func *f)
{
    u32 bi, i, next = 0, previous = 0;

    f->debug_lines = true;
    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];

        for (i = 0; i < b->n; i++) {
            X64Inst *in = &b->insts[i];

            in->debug_label = 0;
            if (in->loc != previous) {
                if (in->loc || previous)
                    in->debug_label = ++next;
                previous = in->loc;
            }
        }
    }
}

static void emit_byte_list(Buf *out, const u8 *bytes, size_t n)
{
    size_t i;

    if (!n)
        return;
    buf_printf(out, "\t.byte\t");
    for (i = 0; i < n; i++)
        buf_printf(out, "%s%u", i ? "," : "", (u32)bytes[i]);
    buf_printf(out, "\n");
}

static void emit_cstr(Buf *out, const char *s)
{
    size_t n = strlen(s) + 1;
    size_t off = 0;

    while (off < n) {
        size_t k = n - off > 16 ? 16 : n - off;

        emit_byte_list(out, (const u8 *)s + off, k);
        off += k;
    }
}

static void emit_uleb(Buf *out, u64 value)
{
    u8 bytes[10];

    emit_byte_list(out, bytes, x64_dwarf_uleb(value, bytes));
}

static void emit_sleb(Buf *out, i64 value)
{
    u8 bytes[10];

    emit_byte_list(out, bytes, x64_dwarf_sleb(value, bytes));
}

static u32 add_dir(DebugFiles *d, const char *dir)
{
    u32 i;

    if (!dir || !*dir || strcmp(dir, ".") == 0)
        return 0;
    for (i = 0; i < d->ndirs; i++)
        if (strcmp(d->dirs[i], dir) == 0)
            return i + 1;
    if (d->ndirs >= d->cap)
        CGF_ICE("DWARF file table exceeded its prepared capacity");
    d->dirs[d->ndirs] = dir;
    return ++d->ndirs;
}

static u32 add_file(DebugFiles *d, const char *path)
{
    const char *shown = path && *path ? path : "<unknown>";
    const char *split = shown;
    const char *slash;
    size_t comp_len = strlen(d->comp_dir);
    u32 i;

    for (i = 0; i < d->nfiles; i++)
        if (strcmp(d->files[i].path, shown) == 0)
            return i + 1;
    if (shown[0] == '/' && comp_len &&
        strncmp(shown, d->comp_dir, comp_len) == 0 && shown[comp_len] == '/')
        split = shown + comp_len + 1;
    slash = strrchr(split, '/');
    d->files[d->nfiles].path = shown;
    if (!slash) {
        d->files[d->nfiles].name = shown;
        d->files[d->nfiles].dir = 0;
    } else {
        const char *dir;

        d->files[d->nfiles].name = arena_strdup(d->arena, slash + 1);
        if (slash == split)
            dir = "/";
        else
            dir = arena_strndup(d->arena, split, (size_t)(slash - split));
        d->files[d->nfiles].dir = add_dir(d, dir);
    }
    return ++d->nfiles;
}

static u32 row_file(DebugFiles *d, const IrModule *m, u32 loc,
                    const char *fallback)
{
    Span sp = diag_span_for_debug(m->dc, ir_debug_loc(m, loc));
    const char *path = diag_span_path(m->dc, sp);

    return add_file(d, path ? path : fallback);
}

static void collect_files(DebugFiles *d, const IrModule *m,
                          X64Func *const *funcs, u32 nfuncs, const char *input)
{
    u32 fi, bi, i;

    (void)add_file(d, input);
    for (fi = 0; fi < nfuncs; fi++)
        for (bi = 0; bi < funcs[fi]->nblocks; bi++) {
            const X64Block *b = &funcs[fi]->blocks[bi];

            for (i = 0; i < b->n; i++)
                if (b->insts[i].debug_label && b->insts[i].loc)
                    (void)row_file(d, m, b->insts[i].loc, input);
        }
}

static void emit_line_header(Buf *out, const DebugFiles *d)
{
    static const u8 fixed[] = {1, 1, 1, 251, 14, 13, 0, 1, 1,
                               1, 1, 0, 0,   0,  1,  0, 0, 1};
    u32 i;

    /* fixed: min-inst, max-ops, default-is-stmt, line-base(-5), range,
     * opcode-base, then standard_opcode_lengths[1..12]. */
    emit_byte_list(out, fixed, sizeof(fixed));
    for (i = 0; i < d->ndirs; i++)
        emit_cstr(out, d->dirs[i]);
    buf_printf(out, "\t.byte\t0\n");
    for (i = 0; i < d->nfiles; i++) {
        emit_cstr(out, d->files[i].name);
        emit_uleb(out, d->files[i].dir);
        emit_uleb(out, 0); /* mtime: determinism forbids stat() */
        emit_uleb(out, 0); /* length: likewise deliberately unknown */
    }
    buf_printf(out, "\t.byte\t0\n");
}

static void emit_set_address(Buf *out, const char *prefix, u32 a, u32 b)
{
    buf_printf(out, "\t.byte\t0,9,2\n\t.quad\t%s%u_%u\n", prefix, a, b);
}

static void emit_debug_line(Buf *out, DebugFiles *files, const IrModule *m,
                            X64Func *const *funcs, u32 nfuncs,
                            const char *input)
{
    u32 fi, bi, i;

    buf_printf(out,
               "\t.section\t.debug_line,\"\",@progbits\n"
               ".Ldebug_line0:\n"
               "\t.long\t.Ldebug_line_end-.Ldebug_line_start\n"
               ".Ldebug_line_start:\n"
               "\t.short\t4\n"
               "\t.long\t.Ldebug_line_header_end-.Ldebug_line_header_start\n"
               ".Ldebug_line_header_start:\n");
    emit_line_header(out, files);
    buf_printf(out, ".Ldebug_line_header_end:\n");
    for (fi = 0; fi < nfuncs; fi++) {
        u32 current_file = 1;
        i64 current_line = 0;

        emit_set_address(out, ".Lloc_", fi, 0);
        buf_printf(out, "\t.byte\t3\n");
        emit_sleb(out, -1);
        buf_printf(out, "\t.byte\t1\n"); /* function entry: line 0 */

        for (bi = 0; bi < funcs[fi]->nblocks; bi++) {
            const X64Block *b = &funcs[fi]->blocks[bi];

            for (i = 0; i < b->n; i++) {
                const X64Inst *in = &b->insts[i];
                Span sp;
                u32 file;
                i64 line;

                if (!in->debug_label)
                    continue;
                emit_set_address(out, ".Lloc_", fi, in->debug_label);
                if (!in->loc) {
                    line = 0;
                    file = current_file;
                } else {
                    sp = diag_span_for_debug(m->dc, ir_debug_loc(m, in->loc));
                    file = row_file(files, m, in->loc, input);
                    line = sp.presumed_line ? sp.presumed_line : sp.line;
                }
                if (file != current_file) {
                    buf_printf(out, "\t.byte\t4\n");
                    emit_uleb(out, file);
                    current_file = file;
                }
                if (line != current_line) {
                    buf_printf(out, "\t.byte\t3\n");
                    emit_sleb(out, line - current_line);
                    current_line = line;
                }
                buf_printf(out, "\t.byte\t1\n"); /* DW_LNS_copy */
            }
        }
        emit_set_address(out, ".Lfe", fi, 0);
        buf_printf(out, "\t.byte\t0,1,1\n"); /* end_sequence */
    }
    buf_printf(out, ".Ldebug_line_end:\n");
}

static void emit_debug_abbrev(Buf *out)
{
    static const u8 abbrev[] = {
        1,    0x11, 0,  /* abbrev 1, DW_TAG_compile_unit, no children */
        0x25, 0x08,     /* producer: string */
        0x13, 0x0b,     /* language: data1 */
        0x03, 0x08,     /* name: string */
        0x1b, 0x08,     /* comp_dir: string */
        0x11, 0x01,     /* low_pc: addr */
        0x12, 0x07,     /* high_pc: data8 offset (DWARF v4 form) */
        0x10, 0x17,     /* stmt_list: sec_offset */
        0,    0,    0}; /* attr terminator, abbrev-table terminator */

    buf_printf(out, "\t.section\t.debug_abbrev,\"\",@progbits\n"
                    ".Ldebug_abbrev0:\n");
    emit_byte_list(out, abbrev, sizeof(abbrev));
}

static void emit_debug_info(Buf *out, u32 nfuncs, const IrModule *m,
                            const char *input, const char *comp_dir)
{
    (void)m;
    buf_printf(out, "\t.section\t.debug_info,\"\",@progbits\n"
                    ".Ldebug_info0:\n"
                    "\t.long\t.Ldebug_info_end-.Ldebug_info_start\n"
                    ".Ldebug_info_start:\n"
                    "\t.short\t4\n"
                    "\t.long\t.Ldebug_abbrev0\n"
                    "\t.byte\t8\n"
                    "\t.byte\t1\n");
    emit_cstr(out, "cgfried 0.1.0");
    buf_printf(out, "\t.byte\t12\n"); /* DW_LANG_C99 */
    emit_cstr(out, input);
    emit_cstr(out, comp_dir);
    if (nfuncs) {
        buf_printf(out, "\t.quad\t%s\n", m->funcs[0].name);
        buf_printf(out, "\t.quad\t.Lfe%u_0-%s\n", nfuncs - 1, m->funcs[0].name);
    } else {
        buf_printf(out, "\t.quad\t0\n\t.quad\t0\n");
    }
    buf_printf(out, "\t.long\t.Ldebug_line0\n.Ldebug_info_end:\n");
}

static void emit_eh_frame(TargetSpec target, const IrModule *m, u32 nfuncs,
                          Buf *out)
{
    static const u8 cie[] = {1,    'z',  'R', 0, 1,    0x78, 16, 1,
                             0x1b, 0x0c, 7,   8, 0x90, 1,    0,  0};
    static const u8 fde[] = {0,    0x41, 0x0e, 0x10, 0x86, 0x02, 0x43, 0x0d,
                             0x06, 0,    0,    0,    0,    0,    0,    0};
    u32 i;

    if (target.kind != CGF_TARGET_X86_64_LINUX_GNU &&
        target.kind != CGF_TARGET_X86_64_LINUX_MUSL &&
        target.kind != CGF_TARGET_X86_64_FREEBSD)
        CGF_ICE("x86-64 CFI encoder received non-x86 target %d",
                (int)target.kind);
    buf_printf(out, "\t.section\t.eh_frame,\"a\",@progbits\n"
                    "\t.p2align\t3\n"
                    ".Lcie0:\n"
                    "\t.long\t.Lcie_end-.Lcie_start\n"
                    ".Lcie_start:\n"
                    "\t.long\t0\n");
    emit_byte_list(out, cie, sizeof(cie));
    buf_printf(out, ".Lcie_end:\n");
    for (i = 0; i < nfuncs; i++) {
        buf_printf(out,
                   ".Lfde%u:\n"
                   "\t.long\t.Lfde%u_end-.Lfde%u_start\n"
                   ".Lfde%u_start:\n"
                   ".Lfde%u_cie:\n"
                   "\t.long\t.Lfde%u_cie-.Lcie0\n"
                   "\t.long\t%s-.\n"
                   "\t.long\t.Lfe%u_0-%s\n",
                   i, i, i, i, i, i, m->funcs[i].name, i, m->funcs[i].name);
        emit_byte_list(out, fde, sizeof(fde));
        buf_printf(out, ".Lfde%u_end:\n", i);
    }
}

static void verify_cfi_prologue(const X64Func *f)
{
    const X64Inst *push;
    const X64Inst *mov;

    if (!f->allocated || !f->nblocks || f->blocks[0].n < 2)
        CGF_ICE("CFI: function '%s' has no finalized rbp prologue", f->name);
    push = &f->blocks[0].insts[0];
    mov = &f->blocks[0].insts[1];
    if (push->op != X64_OP_PUSH || push->width != X64_Q ||
        push->a.kind != X64O_VREG || push->a.r.v != X64_RBP + 1 ||
        mov->op != X64_OP_MOV || mov->width != X64_Q ||
        mov->def.v != X64_RBP + 1 || mov->a.kind != X64O_VREG ||
        mov->a.r.v != X64_RSP + 1)
        CGF_ICE("CFI: function '%s' violates the push-rbp/mov-rsp-rbp law",
                f->name);
}

void x64_emit_debug_sections(TargetSpec target, Arena *arena, const IrModule *m,
                             X64Func *const *funcs, u32 nfuncs,
                             const char *input, const char *comp_dir,
                             bool emit_debug, Buf *out)
{
    DebugFiles files;
    u32 max_files = 1;
    u32 fi, bi;

    for (fi = 0; fi < nfuncs; fi++)
        verify_cfi_prologue(funcs[fi]);
    emit_eh_frame(target, m, nfuncs, out);
    if (!emit_debug)
        return;
    for (fi = 0; fi < nfuncs; fi++)
        for (bi = 0; bi < funcs[fi]->nblocks; bi++)
            max_files += funcs[fi]->blocks[bi].n;
    memset(&files, 0, sizeof(files));
    files.arena = arena;
    files.comp_dir = comp_dir;
    files.cap = max_files;
    files.dirs = arena_alloc(arena, max_files * sizeof(const char *),
                             _Alignof(const char *));
    files.files =
        arena_alloc(arena, max_files * sizeof(DebugFile), _Alignof(DebugFile));
    collect_files(&files, m, funcs, nfuncs, input);
    emit_debug_line(out, &files, m, funcs, nfuncs, input);
    emit_debug_abbrev(out);
    emit_debug_info(out, nfuncs, m, input, comp_dir);
}
