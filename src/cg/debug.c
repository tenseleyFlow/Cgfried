#include "cg/debug.h"

#include <string.h>

#include "diag.h"

/* The target-neutral half of Sprint 29's DWARF emitter. See cg/debug.h for
 * why CFI is not here. Everything below moved out of cg/x86_64/debug.c
 * unchanged except for the two functions that walked X64Func directly; those
 * now read a CgDebugRow sequence, which is the entire seam. */

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

size_t cg_dwarf_uleb(u64 value, u8 out[10])
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

size_t cg_dwarf_sleb(i64 value, u8 out[10])
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

bool cg_dwarf_special(i64 line_delta, u64 address_delta, u8 *opcode)
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

    emit_byte_list(out, bytes, cg_dwarf_uleb(value, bytes));
}

static void emit_sleb(Buf *out, i64 value)
{
    u8 bytes[10];

    emit_byte_list(out, bytes, cg_dwarf_sleb(value, bytes));
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

static void collect_files(DebugFiles *d, const IrModule *m,
                          const CgDebugFunc *funcs, u32 nfuncs,
                          const char *input)
{
    u32 fi, i;

    (void)add_file(d, input);
    for (fi = 0; fi < nfuncs; fi++) {
        Span function_span =
            diag_span_for_debug(m->dc, ir_debug_loc(m, m->funcs[fi].loc));

        if (function_span.file_id)
            (void)add_file(d, diag_span_path(m->dc, function_span));
        for (i = 0; i < funcs[fi].nrows; i++)
            if (funcs[fi].rows[i].label && funcs[fi].rows[i].loc)
                (void)row_file(d, m, funcs[fi].rows[i].loc, input);
    }
}

static void emit_debug_line(Buf *out, DebugFiles *files, const IrModule *m,
                            const CgDebugFunc *funcs, u32 nfuncs,
                            const char *input)
{
    u32 fi, i;

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
        bool prologue_end = false;
        Span function_span =
            diag_span_for_debug(m->dc, ir_debug_loc(m, m->funcs[fi].loc));
        u32 current_file = 1;
        i64 current_line = function_span.presumed_line
                               ? function_span.presumed_line
                               : function_span.line;

        emit_set_address(out, ".Lloc_", fi, 0);
        if (function_span.file_id) {
            current_file =
                add_file(files, diag_span_path(m->dc, function_span));
            if (current_file != 1) {
                buf_printf(out, "\t.byte\t4\n");
                emit_uleb(out, current_file);
            }
        }
        if (current_line != 1) {
            buf_printf(out, "\t.byte\t3\n");
            emit_sleb(out, current_line - 1);
        }
        buf_printf(out, "\t.byte\t1\n"); /* function definition row */

        for (i = 0; i < funcs[fi].nrows; i++) {
            const CgDebugRow *row = &funcs[fi].rows[i];
            Span sp;
            u32 file;
            i64 line;

            if (!row->label)
                continue;
            emit_set_address(out, ".Lloc_", fi, row->label);
            if (!row->loc) {
                line = 0;
                file = current_file;
            } else {
                sp = diag_span_for_debug(m->dc, ir_debug_loc(m, row->loc));
                file = row_file(files, m, row->loc, input);
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
            if (!prologue_end && line) {
                buf_printf(out, "\t.byte\t10\n"); /* prologue_end */
                prologue_end = true;
            }
            buf_printf(out, "\t.byte\t1\n"); /* DW_LNS_copy */
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

void cg_emit_debug_info(Arena *arena, const IrModule *m,
                        const CgDebugFunc *funcs, u32 nfuncs, const char *input,
                        const char *comp_dir, Buf *out)
{
    DebugFiles files;
    u32 max_files = 1;
    u32 fi;

    for (fi = 0; fi < nfuncs; fi++)
        max_files += funcs[fi].nrows;
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
