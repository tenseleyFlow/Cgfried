#include "driver/driver.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cg/cg.h"
#include "cg/x86_64/debug.h"
#include "diag.h"
#include "driver/deps.h"
#include "driver/toolchain.h"
#include "ir/ir.h"
#include "lex/lex.h"
#include "lower/lower.h"
#include "opt/opt.h"
#include "parse/parse.h"
#include "pp/pp.h"
#include "sema/sema.h"
#include "target.h"
#include "util/arena.h"
#include "util/intern.h"

/* Hand-organized, no generated help (locked style); an ARRAY of segments
 * because ISO caps one string literal at 4095 bytes. Info-option
 * precedence as implemented: --help over --version over -dumpversion over
 * -dumpmachine over the -print-* family. "Last one wins" applies to -O and
 * -std= (documented here because configure scripts pass both). */
static const char *const help_text[] = {
    "Usage: cgfried [options] file...\n"
    "\n"
    "Modes:\n"
    "  -c                compile and assemble; one .o per input, in the\n"
    "                    current directory\n"
    "  -S                stop after codegen; <base>.s per input\n"
    "  -E                preprocess only, to stdout (-o redirects)\n"
    "  -o <file>         output name (forbidden with multiple inputs\n"
    "                    under -c/-S/-E)\n"
    "  -x <lang>         treat subsequent inputs as <lang>: 'c' or 'none'\n"
    "                    (restore extension dispatch); '-' reads stdin and\n"
    "                    requires an active -x c\n"
    "  -fsyntax-only     parse and report diagnostics; produce no output\n"
    "  Extension dispatch: .c preprocess+compile+assemble; .i compile;\n"
    "  .s assemble; .S preprocess then assemble; anything else is a\n"
    "  linker input, kept in command-line position.\n"
    "\n"
    "Preprocessor:\n"
    "  -I <dir>          include search dir (quote and angle forms)\n"
    "  -iquote <dir>     include search dir (\"...\" form only)\n"
    "  -isystem <dir>    system include dir (after -I, before builtin)\n"
    "  -include <file>   include <file> before line 1 of each input\n"
    "  -D name[=value]   predefine a macro (value defaults to 1)\n"
    "  -U name           undefine (processed in -D/-U order, later wins)\n"
    "  -nostdinc         do not search the system include directories\n"
    "  -trigraphs        enable trigraph translation (default off)\n"
    "  -P                omit linemarkers from -E output\n"
    "  -dM               with -E: dump macro definitions\n"
    "\n"
    "Dependencies (for make):\n"
    "  -M / -MM          write a depfile to stdout instead of compiling\n"
    "                    (-MM omits system headers); imply -E\n"
    "  -MD / -MMD        write <obj base>.d as a side effect of compiling\n"
    "  -MF <file>        depfile name\n"
    "  -MT <target>      dependency target, verbatim (repeatable)\n"
    "  -MQ <target>      like -MT but Make-quoted ($ -> $$, etc.)\n"
    "  -MP               emit a phony target per header\n"
    "\n",
    "Codegen / language:\n"
    "  -O<n>             optimization level: 0 (default) 1 2 3 s fast;\n"
    "                    -O means -O1; the LAST -O wins. -Ofast is -O3\n"
    "                    plus the bundle documented in docs/fast-math.md\n"
    "  -ftime-report     print per-pass invocation counts and wall time\n"
    "  -g[-level]        emit DWARF v4 line tables (-g1/-g2/-g3 all\n"
    "                    mean line-level debug info; -g0 disables)\n"
    "  -std=<std>        c89/c90, c99, c11, c17/c18 (+iso9899 spellings)\n"
    "                    and the gnu twins; the LAST -std= wins.\n"
    "                    Default c17 (gcc defaults to gnu17 — divergence\n"
    "                    is deliberate and documented here)\n"
    "  -W<name>/-Wno-<name>  enable/disable a warning (stored; the full\n"
    "                    machinery lands in a later sprint)\n"
    "  -w                suppress all warnings\n"
    "  -Werror[=name]    warnings become errors\n"
    "  -pedantic[-errors] ISO conformance diagnostics\n"
    "  -fmax-errors=N    stop after N errors (0 = unlimited, default);\n"
    "                    -ferror-limit=N is accepted as an alias\n"
    "  -fcommon          tentative definitions become COMMON symbols;\n"
    "                    the DEFAULT is -fno-common (gcc 10 semantics;\n"
    "                    gcc 8 defaulted to -fcommon — divergence is\n"
    "                    deliberate and documented here)\n"
    "  -ffreestanding    freestanding environment (-fhosted restores)\n"
    "  -fwrapv           signed overflow wraps\n"
    "  -fno-strict-aliasing  disable type-based aliasing\n"
    "  -ffast-math       enable the complete fast-math bundle\n"
    "  -fno-fast-math    disable it; component flags are accepted but\n"
    "                    bundled-only and warn (docs/fast-math.md)\n"
    "  Unknown -f/-W options warn and continue (configure-script parity);\n"
    "  any other unknown option is an error.\n"
    "\n"
    "Link:\n"
    "  -L <dir> / -l <name>  library search dir / library (position kept)\n"
    "  -Wl,a,b           comma-split args passed to the linker in position\n"
    "  -Xlinker <arg>    one raw linker arg in position\n"
    "  -static           static link\n"
    "  -nostdlib -nostartfiles -nodefaultlibs   omit default link inputs\n"
    "\n",
    "Introspection:\n"
    "  --help --version -dumpversion -dumpmachine\n"
    "  -print-prog-name=X -print-file-name=X -print-search-dirs\n"
    "  -v                print each subcommand before running it\n"
    "  -###              print subcommands, run nothing, exit 0\n"
    "  @file             read options from file (recursive, depth 16)\n"
    "\n"
    "Dumps (compiler development):\n"
    "  --dump-tokens --dump-ast -fdump-sema -fdump-layout -fdump-init\n"
    "  -emit-ir -emit-mir\n"
    "\n"
    "Environment:\n"
    "  NO_COLOR          disable diagnostic colors (any non-empty value)\n"
    "  CLICOLOR_FORCE    force diagnostic colors even when piped\n"
    "  CGF_AS            unset/1: use bundled afs-as (default); 0: system "
    "'as'\n"
    "  CGF_AS_PATH       use exactly this assembler (wins over CGF_AS)\n"
    "  CGF_LD            unset/0: use system 'ld' (default); 1: bundled\n"
    "                    afs-ld (-static ELF links are the supported\n"
    "                    lane; dynamic links are EXPERIMENTAL — data\n"
    "                    imports need COPY relocations, a later rung)\n"
    "  CGF_LD_PATH       use exactly this linker (wins over CGF_LD)\n"
    "  CGF_CRT_DIR       crt object discovery override\n"
    "  CGF_PP_DUMP_TOKENS  with -E: dump one token per line (testing)\n"
    "  CGF_PP_DUMP_GUARD   with -E: dump include-guard shapes (testing)\n"
    "  Empty-string values are treated as unset.\n",
};

VEC_DECL(PpTokVecD, PpToken);

/* Per-input work order: which pipeline slice runs and where the product
 * lands. driver_main builds one per input; the compile functions never
 * look at DriverArgs for paths again. */
typedef struct {
    const char *path; /* source path, or "-" for stdin */
    InputKind kind;
    const char *out;        /* product path (.s/.o) — NULL for stdout modes */
    Buf *pp_text;           /* non-NULL: -E token text lands here */
    bool pp_only;           /* preprocess only (mode -E or a .S first stage) */
    Buf *dep_text;          /* non-NULL: the -M depfile lands here */
    const char *dep_target; /* default depfile target (no -MT/-MQ given) */
} CompileJob;

/* fwrite of a possibly-empty Buf: glibc declares fwrite's first arg
 * nonnull, so an empty Buf (data NULL, len 0) is UB — the recurring
 * zero-length-UB family (F-S26-FWRITE0; UBSan on CI's glibc caught it
 * on an empty -E output, the local glibc's headers did not). */
static void buf_fwrite(const Buf *b, FILE *f)
{
    if (b->len)
        fwrite(b->data, 1, b->len, f);
}

/* One line per token, stable and greppable: golden --dump-tokens files
 * assert on these. Constants print their CLASSIFIED type, which is the
 * whole point of the phase-7 conversion. */
static void dump_token(const Token *t)
{
    switch ((TokenKind)t->kind) {
    case TOK_KEYWORD:
        printf("KEYWORD %s\n", t->spelling);
        break;
    case TOK_IDENT:
        printf("IDENT %s\n", t->spelling);
        break;
    case TOK_PUNCT:
        printf("PUNCT %s\n", t->spelling);
        break;
    case TOK_INT_CONST:
        printf("INT_CONST %llu %s\n", (unsigned long long)t->int_val,
               lex_int_type_name((IntConstType)t->int_type));
        break;
    case TOK_CHAR_CONST:
        printf("CHAR_CONST %lld %s\n", (long long)t->int_val,
               lex_int_type_name((IntConstType)t->int_type));
        break;
    case TOK_FLOAT_CONST: {
        static const char *const fnames[] = {"float", "double", "long double"};
        printf("FLOAT_CONST %s %s\n", t->spelling, fnames[t->float_type]);
        break;
    }
    case TOK_STRING: {
        static const char *const enames[] = {"", "L", "u", "U", "u8"};
        u32 i;
        printf("STRING %s%u bytes:", enames[t->str.enc], t->str.nbytes);
        for (i = 0; i < t->str.nbytes; i++)
            printf(" %02x", t->str.bytes[i]);
        printf("\n");
        break;
    }
    case TOK_EOF:
        printf("EOF\n");
        break;
    }
}

static void dump_decl(const AstNode *n, int depth);

static void indent(int depth)
{
    int i;

    for (i = 0; i < depth; i++)
        printf("  ");
}

/* Statements print as an indented tree, expressions FULLY PARENTHESIZED.
 * The parenthesization is the point: it makes a precedence or
 * associativity mistake visible in the golden rather than hidden behind a
 * flat reprint of the source. */
static void dump_expr_line(const char *label, const AstNode *e, int depth)
{
    Buf b;

    buf_init(&b);
    ast_expr_render(e, &b);
    indent(depth);
    printf("%s ", label);
    fwrite(b.data, 1, b.len, stdout);
    if (e && e->sem_type) {
        Buf tb;

        buf_init(&tb);
        ast_sem_type_render(e, &tb);
        printf(" : ");
        fwrite(tb.data, 1, tb.len, stdout);
        buf_free(&tb);
        if (e->is_lvalue)
            printf(" [lvalue]");
    }
    if (e && e->unevaluated)
        printf(" [unevaluated]");
    printf("\n");
    buf_free(&b);
}

static void dump_stmt(const AstNode *s, int depth)
{
    u32 i;

    if (!s)
        return;
    switch (s->kind) {
    case AST_STMT_COMPOUND:
        indent(depth);
        printf("BLOCK\n");
        for (i = 0; i < s->nitems; i++)
            dump_stmt(s->items[i], depth + 1);
        return;
    case AST_STMT_DECL:
        dump_decl(s->lhs, depth);
        return;
    case AST_STMT_EXPR:
        dump_expr_line("EXPR", s->lhs, depth);
        return;
    case AST_STMT_NULL:
        indent(depth);
        printf("NULLSTMT\n");
        return;
    case AST_STMT_IF:
        dump_expr_line("IF", s->lhs, depth);
        dump_stmt(s->body, depth + 1);
        if (s->rhs) {
            indent(depth);
            printf("ELSE\n");
            dump_stmt(s->rhs, depth + 1);
        }
        return;
    case AST_STMT_SWITCH:
        dump_expr_line("SWITCH", s->lhs, depth);
        dump_stmt(s->body, depth + 1);
        return;
    case AST_STMT_WHILE:
        dump_expr_line("WHILE", s->lhs, depth);
        dump_stmt(s->body, depth + 1);
        return;
    case AST_STMT_DO:
        indent(depth);
        printf("DO\n");
        dump_stmt(s->body, depth + 1);
        dump_expr_line("DOWHILE", s->lhs, depth);
        return;
    case AST_STMT_FOR:
        indent(depth);
        printf("FOR\n");
        /* The init clause is either an expression statement or a
         * declaration — the c99 for-init form. */
        if (s->lhs)
            dump_stmt(s->lhs, depth + 1);
        if (s->mid)
            dump_expr_line("FORCOND", s->mid, depth + 1);
        if (s->rhs)
            dump_expr_line("FORSTEP", s->rhs, depth + 1);
        dump_stmt(s->body, depth + 1);
        return;
    case AST_STMT_RETURN:
        if (s->lhs) {
            dump_expr_line("RETURN", s->lhs, depth);
        } else {
            indent(depth);
            printf("RETURN\n");
        }
        return;
    case AST_STMT_GOTO:
        indent(depth);
        printf("GOTO %s\n", s->name ? s->name : "?");
        return;
    case AST_STMT_BREAK:
        indent(depth);
        printf("BREAK\n");
        return;
    case AST_STMT_CONTINUE:
        indent(depth);
        printf("CONTINUE\n");
        return;
    case AST_STMT_LABEL:
        indent(depth);
        printf("LABEL %s\n", s->name ? s->name : "?");
        dump_stmt(s->body, depth + 1);
        return;
    case AST_STMT_CASE:
        dump_expr_line("CASE", s->lhs, depth);
        dump_stmt(s->body, depth + 1);
        return;
    case AST_STMT_DEFAULT:
        indent(depth);
        printf("DEFAULT\n");
        dump_stmt(s->body, depth + 1);
        return;
    default:
        dump_decl(s, depth);
        return;
    }
}

/* One line per top-level declaration: name, rendered declarator, and the
 * storage class. This IS the declarator round-trip proof — the chain is
 * built inside-out by the parser and printed outside-in here. */
static void dump_decl(const AstNode *n, int depth)
{
    Buf b;
    u32 i;

    if (!n)
        return;
    buf_init(&b);
    switch (n->kind) {
    case AST_FUNC_DEF:
    case AST_DECL:
        for (i = 0; i < (u32)depth; i++)
            printf("  ");
        printf("%s %s: ", n->kind == AST_FUNC_DEF ? "FUNCDEF" : "DECL",
               n->name ? n->name : "<abstract>");
        ast_type_render(n->type, &b);
        fwrite(b.data, 1, b.len, stdout);
        if (n->storage & AST_SC_TYPEDEF)
            printf(" [typedef]");
        if (n->storage & AST_SC_STATIC)
            printf(" [static]");
        if (n->storage & AST_SC_EXTERN)
            printf(" [extern]");
        if (n->is_bitfield)
            printf(" [bitfield]");
        if (n->init)
            printf(" [init]");
        printf("\n");
        /* A scalar initializer is an expression, so render it — the
         * initializer goldens assert binding just like statements do. */
        if (n->init && n->init->kind != AST_INIT_LIST)
            dump_expr_line("INIT", n->init, depth + 1);
        if (n->kind == AST_FUNC_DEF && n->body)
            dump_stmt(n->body, depth + 1);
        break;
    case AST_ENUMERATOR:
        for (i = 0; i < (u32)depth; i++)
            printf("  ");
        printf("ENUMERATOR %s%s\n", n->name, n->init ? " = [expr]" : "");
        break;
    case AST_STATIC_ASSERT:
        for (i = 0; i < (u32)depth; i++)
            printf("  ");
        printf("STATIC_ASSERT\n");
        break;
    case AST_EMPTY_DECL:
        /* `struct S { ... };` declares no object, but the TAG it introduces
         * is the whole point of the line — render it. */
        for (i = 0; i < (u32)depth; i++)
            printf("  ");
        printf("EMPTY_DECL");
        if (n->type) {
            printf(" ");
            ast_type_render(n->type, &b);
            fwrite(b.data, 1, b.len, stdout);
        }
        printf("\n");
        break;
    default:
        break;
    }
    buf_free(&b);
    /* Sibling declarators from the same specifier list. */
    for (i = 0; i < n->nitems; i++)
        dump_decl(n->items[i], depth);
    /* Record members, so struct shapes are visible in goldens. */
    if (n->type && n->type->kind == ATY_BASE && n->type->record &&
        n->type->record->is_definition) {
        u32 m;
        for (m = 0; m < n->type->record->nmembers; m++)
            dump_decl(n->type->record->members[m], depth + 1);
    }
}

/* Builds the "<command-line>" pseudo-file from -D/-U flags, in order.
 * -D name means 1; -D name=val splits at the first '='. -include files
 * follow ALL the defines (gcc processes every -D/-U first), each as if
 * `#include "F"` stood before line 1 of the main file. */
static SourceFile *build_cmdline_file(Preprocessor *pp, const DriverArgs *a)
{
    Buf b;
    SourceFile *sf;
    size_t i;

    if (a->defs.len == 0 && a->pre_includes.len == 0)
        return NULL;
    buf_init(&b);
    for (i = 0; i < a->defs.len; i++) {
        const char *d = a->defs.data[i].val;
        const char *eq = strchr(d, '=');

        if (a->defs.data[i].is_undef) {
            buf_printf(&b, "#undef %s\n", d);
        } else if (eq) {
            buf_printf(&b, "#define %.*s %s\n", (int)(eq - d), d, eq + 1);
        } else {
            buf_printf(&b, "#define %s 1\n", d);
        }
    }
    for (i = 0; i < a->pre_includes.len; i++)
        buf_printf(&b, "#include \"%s\"\n", a->pre_includes.data[i]);
    sf =
        pp_source_add_buffer(pp, "<command-line>", (const char *)b.data, b.len);
    buf_free(&b);
    return sf;
}

/* Prints a module to stdout with the round-trip self-check: re-parse the
 * output demanding structural equality, print again demanding byte
 * equality. Every fixture through the driver proves round-trip AND
 * determinism; a failure in either is an ICE (our printer/parser
 * disagree = our bug, never the user's). */
static int emit_ir_print(Arena *arena, DiagCtx *dc, IrModule *m,
                         const char *input)
{
    Buf b1, b2;
    IrModule *m2;

    buf_init(&b1);
    ir_print_module_buf(&b1, m);
    buf_push_u8(&b1, 0); /* the parser wants a C string */
    m2 = ir_parse_module(arena, dc, (const char *)b1.data, "<reprint>");
    b1.len--; /* drop the NUL again for the byte-compare and the output */
    if (!m2 || !ir_module_struct_eq(m, m2))
        CGF_ICE("-emit-ir round-trip broke: parse(print(M)) != M for '%s'",
                input);
    buf_init(&b2);
    ir_print_module_buf(&b2, m2);
    if (b1.len != b2.len || memcmp(b1.data, b2.data, b1.len) != 0)
        CGF_ICE("-emit-ir determinism broke: two prints differ for '%s'",
                input);
    fwrite(b1.data, 1, b1.len, stdout);
    buf_free(&b1);
    buf_free(&b2);
    return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
}

static bool env_is_one(const char *name)
{
    const char *value = cgf_env(name);

    return value && strcmp(value, "1") == 0;
}

/* The same optimization boundary serves generated C IR and verified textual
 * IR. A bad input module is a user error before this call; any invalidity
 * after a pass is our bug and therefore an ICE. */
static void optimize_module(IrModule *m, const DriverArgs *a, const char *input)
{
    OptConfig cfg;
    char why[256];

    opt_config_init(&cfg, (OptLevel)a->opt_level);
    cfg.fast_math.reassoc = a->fast_math;
    cfg.fast_math.no_nans = a->fast_math;
    cfg.fast_math.no_infs = a->fast_math;
    cfg.fast_math.no_signed_zeros = a->fast_math;
    cfg.fast_math.reciprocal_math = a->fast_math;
    cfg.no_strict_aliasing = a->fno_strict_aliasing;
    cfg.fwrapv = a->fwrapv;
    cfg.debug_info = a->debug_level != 0;
    cfg.verify_after_each = env_is_one("CGF_VERIFY_AFTER_EACH");
    cfg.bail_log = env_is_one("CGF_OPT_BAIL_LOG");
    cfg.disable_unswitch = env_is_one("CGF_OPT_DISABLE_UNSWITCH");
    cfg.disable_bce = env_is_one("CGF_OPT_DISABLE_BCE");
    cfg.disable_fusion = env_is_one("CGF_OPT_DISABLE_FUSION");
    cfg.disable_vectorize = env_is_one("CGF_OPT_DISABLE_VECTORIZE");
    cfg.time_report = a->time_report;
    cfg.dump_bad_ir = cgf_env("CGF_DUMP_BAD_IR");
    (void)opt_run_pipeline(m, &cfg);
    if (!ir_verify_report(m->dc, m, why, sizeof(why))) {
        if (cfg.dump_bad_ir) {
            FILE *df = fopen(cfg.dump_bad_ir, "wb");

            if (df) {
                ir_print_module(df, m);
                fprintf(df, "// verify failed after optimization: %s\n", why);
                fclose(df);
            }
        }
        CGF_ICE("optimization produced IR the verifier rejects for '%s' "
                "(%s)",
                input, why);
    }
}

/* Preserve the shell's logical, as-given cwd (including symlink components)
 * when it honestly names '.'. PWD is untrusted process input, so validate
 * identity before emitting it into DW_AT_comp_dir; otherwise use getcwd's
 * physical path. Failure is an I/O error, never fabricated debug metadata. */
static bool debug_comp_dir(char *out, size_t cap)
{
    const char *logical = cgf_env("PWD");
    struct stat logical_st, dot_st;
    int n;

    if (logical && logical[0] == '/' && stat(logical, &logical_st) == 0 &&
        stat(".", &dot_st) == 0 && logical_st.st_dev == dot_st.st_dev &&
        logical_st.st_ino == dot_st.st_ino) {
        n = snprintf(out, cap, "%s", logical);
        if (n >= 0 && (size_t)n < cap)
            return true;
        errno = ERANGE;
        return false;
    }
    return getcwd(out, cap) != NULL;
}

/* -S / -c / link (Sprints 24-26): the full backend per function, then
 * AT&T text. The .s is a USER-FACING CONTRACT (gas-assemblable); other
 * modes assemble it through the routed assembler, and an assembler
 * rejection of OUR text is an ICE quoting the offending line. Linking
 * happens in driver_main AFTER every TU compiled — never here. */
static int run_emit_asm(Arena *arena, DiagCtx *dc, IrModule *m,
                        const DriverArgs *a, const CompileJob *job)
{
    Buf b;
    u32 i;
    X64Func **xfuncs = NULL;
    char s_path[528];
    char comp_dir[4096];
    FILE *f;

    buf_init(&b);
    if (m->nfuncs)
        xfuncs = arena_alloc(arena, m->nfuncs * sizeof(X64Func *),
                             _Alignof(X64Func *));
    for (i = 0; i < m->nfuncs; i++) {
        X64Func *xf = x64_isel_function(m, &m->funcs[i], arena);

        if (x64_mir_verify(xf, dc))
            CGF_ICE("isel produced MIR the verifier rejects for '@%s'",
                    m->funcs[i].name);
        x64_regalloc_entry(CG_O0)(xf);
        if (x64_mir_verify(xf, dc))
            CGF_ICE("regalloc produced MIR the verifier rejects for '@%s'",
                    m->funcs[i].name);
        if (a->debug_level)
            x64_debug_prepare(xf);
        xfuncs[i] = xf;
        x64_emit_function(xf, m, i, m->funcs[i].linkage, &b);
    }
    x64_emit_globals(m, &b);
    comp_dir[0] = '\0';
    if (a->debug_level && !debug_comp_dir(comp_dir, sizeof(comp_dir))) {
        fprintf(stderr,
                "cgfried: error: cannot determine current directory: %s\n",
                strerror(errno));
        buf_free(&b);
        return CGF_EXIT_IO;
    }
    x64_emit_debug_sections(cgf_target_host(), arena, m, xfuncs, m->nfuncs,
                            job->path, comp_dir, a->debug_level != 0, &b);

    if (diag_had_error(dc)) {
        buf_free(&b);
        return CGF_EXIT_COMPILE;
    }
    if (a->emit_asm) {
        f = fopen(job->out, "wb");
        if (!f) {
            fprintf(stderr, "cgfried: error: cannot write '%s'\n", job->out);
            buf_free(&b);
            return CGF_EXIT_IO;
        }
        fwrite(b.data, 1, b.len, f);
        fclose(f);
        buf_free(&b);
        return CGF_EXIT_OK;
    }
    /* -c / link-mode object: write the .s beside the output
     * (deterministic name; kept on failure so the ICE is reproducible),
     * assemble, clean up. */
    snprintf(s_path, sizeof(s_path), "%s.cgf.s", job->out);
    f = fopen(s_path, "wb");
    if (!f) {
        fprintf(stderr, "cgfried: error: cannot write '%s'\n", s_path);
        buf_free(&b);
        return CGF_EXIT_IO;
    }
    fwrite(b.data, 1, b.len, f);
    fclose(f);
    {
        u32 bad_line = 0;
        ToolResult res = cgf_run_assembler(s_path, job->out, &bad_line);

        if (res.kind == TOOL_SPAWN_FAILED) {
            fprintf(stderr, "cgfried: error: %s\n",
                    cgf_tool_missing_hint(TOOL_AS));
            buf_free(&b);
            return CGF_EXIT_IO;
        }
        if (res.kind != TOOL_EXITED || res.exit_code != 0) {
            /* Quote the offending line from the buffer we just wrote. */
            char quoted[256];
            u32 ln = 1;
            size_t p = 0, q;

            quoted[0] = '\0';
            if (bad_line) {
                while (p < b.len && ln < bad_line) {
                    if (b.data[p] == '\n')
                        ln++;
                    p++;
                }
                q = 0;
                while (p < b.len && b.data[p] != '\n' && q + 1 < sizeof(quoted))
                    quoted[q++] = b.data[p++];
                quoted[q] = '\0';
            }
            CGF_ICE("the assembler rejected cgfried-generated assembly "
                    "(kept at '%s', line %u: \"%s\") — this is a cgf "
                    "emission bug",
                    s_path, bad_line, quoted);
        }
    }
    unlink(s_path);
    buf_free(&b);
    return CGF_EXIT_OK;
}

static int emit_mir_print(Arena *arena, DiagCtx *dc, IrModule *m)
{
    u32 i;
    Buf b;

    buf_init(&b);
    for (i = 0; i < m->nfuncs; i++) {
        X64Func *xf = x64_isel_function(m, &m->funcs[i], arena);

        if (x64_mir_verify(xf, dc))
            CGF_ICE("isel produced MIR the verifier rejects for '@%s'",
                    m->funcs[i].name);
        /* Sprint 22: -emit-mir shows the ALLOCATED form — physical
         * registers, spill code, prologue/epilogue — verified again
         * post-RA (vreg survival, canonical two-address, markers gone). */
        x64_regalloc_entry(CG_O0)(xf);
        if (x64_mir_verify(xf, dc))
            CGF_ICE("regalloc produced MIR the verifier rejects for '@%s'",
                    m->funcs[i].name);
        x64_mir_print(xf, &b);
    }
    fwrite(b.data, 1, b.len, stdout);
    buf_free(&b);
    return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
}

/* -emit-ir on a .cgfir file: parse -> verify -> print. */
static int run_emit_ir(Arena *arena, DiagCtx *dc, const DriverArgs *a,
                       const CompileJob *job)
{
    FILE *f = fopen(job->path, "rb");
    char *src;
    long len;
    size_t rd;
    IrModule *m;

    if (!f) {
        fprintf(stderr, "cgfried: error: cannot open '%s'\n", job->path);
        return CGF_EXIT_IO;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fprintf(stderr, "cgfried: error: cannot read '%s'\n", job->path);
        return CGF_EXIT_IO;
    }
    src = arena_alloc(arena, (size_t)len + 1, 1);
    rd = fread(src, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) {
        fprintf(stderr, "cgfried: error: cannot read '%s'\n", job->path);
        return CGF_EXIT_IO;
    }
    src[len] = '\0';
    if (memchr(src, '\0', (size_t)len) != NULL) {
        fprintf(stderr, "cgfried: error: '%s' contains a NUL byte\n",
                job->path);
        return CGF_EXIT_COMPILE;
    }

    m = ir_parse_module(arena, dc, src, job->path);
    if (!m)
        return CGF_EXIT_COMPILE;
    /* Hand-written IR that fails the verifier is a USER error (exit 1);
     * generated IR failing it is an ICE (see run_preprocess). Either way
     * CGF_DUMP_BAD_IR=path captures the offending module — and the dump
     * is itself parseable .cgfir, pinned by test. */
    {
        char why[256];

        if (!ir_verify_report(dc, m, why, sizeof(why))) {
            const char *dump = cgf_env("CGF_DUMP_BAD_IR");

            if (dump) {
                FILE *df = fopen(dump, "wb");

                if (df) {
                    ir_print_module(df, m);
                    /* Trailing comment: which check, which function —
                     * and the dump still re-parses (comments drop). */
                    fprintf(df, "// verify failed: %s\n", why);
                    fclose(df);
                }
            }
            return CGF_EXIT_COMPILE;
        }
    }
    optimize_module(m, a, job->path);
    if (a->emit_mir)
        return emit_mir_print(arena, dc, m);
    return emit_ir_print(arena, dc, m, job->path);
}

/* Reads all of stdin (for `-` inputs; requires -x c, enforced at parse). */
static bool read_stdin(Buf *b)
{
    char chunk[4096];
    size_t n;

    buf_init(b);
    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        size_t i;

        for (i = 0; i < n; i++)
            buf_push_u8(b, (u8)chunk[i]);
    }
    return !ferror(stdin);
}

/* The per-TU pipeline: preprocess, then as much of lex/parse/sema/lower/
 * codegen as the mode asks for. -E text lands in job->pp_text. */
static int run_preprocess(Arena *arena, DiagCtx *dc, const DriverArgs *a,
                          const CompileJob *job)
{
    Interner interner;
    Preprocessor pp;
    SourceFile *sf, *cmdline;
    PpToken t;
    PpToken prev_tok;
    bool dump = cgf_env("CGF_PP_DUMP_TOKENS") != NULL;
    bool dump_guard = cgf_env("CGF_PP_DUMP_GUARD") != NULL;
    bool first = true;
    size_t i;

    memset(&prev_tok, 0, sizeof(prev_tok));

    intern_init(&interner, arena);
    pp_init(&pp, arena, dc, &interner);
    pp.trigraphs = a->trigraphs;
    pp.freestanding = a->freestanding;
    pp.verbose = a->verbose;
    pp.std = (CStd)a->std;
    (void)a->no_linemarkers; /* -P: we never emit linemarkers (Sprint 7
                                owns line fidelity); accepted so oracle
                                command lines stay symmetric */
    pp.gnu_mode = pp.std >= STD_GNU89;
    for (i = 0; i < a->include_dirs.len; i++)
        pp.include_dirs[pp.n_include++] = a->include_dirs.data[i];
    for (i = 0; i < a->iquote_dirs.len; i++)
        pp.iquote_dirs[pp.n_iquote++] = a->iquote_dirs.data[i];
    /* -isystem dirs come AFTER -I and before the builtin chain; they
     * live at the front of the system list so classification (system
     * headers, -MM, warning suppression) sees them as system dirs. */
    for (i = 0; i < a->isystem_dirs.len; i++)
        pp.system_dirs[pp.n_system++] = a->isystem_dirs.data[i];
    if (!a->nostdinc) {
        /* Our shipped freestanding headers come FIRST among the system
         * dirs (after any -isystem): glibc's own headers include them,
         * and gcc likewise places its include dir ahead of /usr/include.
         * The dir holds EXACTLY the nine headers the standard makes the
         * compiler's, so it can never shadow libc's stdio.h et al.
         * Probe order: CGF_INCLUDE_DIR > installed layout > dev tree. */
        const char *shipped = cgf_shipped_include_dir();

        if (shipped)
            pp.system_dirs[pp.n_system++] = shipped;
        pp.n_system += cgf_target_system_include_dirs(
            cgf_target_host(), pp.system_dirs + pp.n_system,
            PP_MAX_DIRS - pp.n_system);
    }

    if (strcmp(job->path, "-") == 0) {
        Buf sb;

        if (!read_stdin(&sb)) {
            fprintf(stderr, "cgfried: error: cannot read stdin\n");
            intern_free(&interner);
            pp_loc_free(&pp.loc);
            strmap_free(&pp.macros);
            return CGF_EXIT_IO;
        }
        sf =
            pp_source_add_buffer(&pp, "<stdin>", (const char *)sb.data, sb.len);
        buf_free(&sb);
    } else {
        sf = pp_source_load(&pp, job->path);
    }
    if (!sf) {
        intern_free(&interner);
        pp_loc_free(&pp.loc);
        strmap_free(&pp.macros);
        return CGF_EXIT_IO;
    }
    cmdline = build_cmdline_file(&pp, a);
    pp_begin(&pp, sf, cmdline);

    if (!job->pp_only &&
        (a->dump_tokens || a->dump_ast || a->dump_sema || a->dump_layout ||
         a->dump_init || a->syntax_only || a->emit_ir || a->emit_mir ||
         a->emit_asm || a->compile_obj || a->link_exe)) {
        /* Phase 5-7: collect the pp-token stream, convert, dump. */
        PpTokVecD collected = {NULL, 0, 0};
        LangOpts lang;
        TokenList tl;
        u32 k;

        memset(&lang, 0, sizeof(lang));
        lang.std = (CStd)a->std;
        lang.gnu_mode = lang.std >= STD_GNU89;
        lang.pedantic = a->pedantic;
        lang.fwrapv = a->fwrapv;
        while (pp_next(&pp, &t))
            PpTokVecD_push(&collected, t);
        tl = lex_convert(&pp, collected.data, (u32)collected.len, &lang,
                         cgf_target_host(), arena);
        if (a->dump_tokens)
            for (k = 0; k < tl.n; k++)
                dump_token(&tl.toks[k]);
        if (a->dump_ast || a->dump_sema || a->dump_layout || a->dump_init ||
            a->syntax_only || a->emit_ir || a->emit_mir || a->emit_asm ||
            a->compile_obj || a->link_exe) {
            Parser ps;
            AstNode *tu;

            parse_init(&ps, &tl, &pp, dc, arena, &lang);
            tu = parse_translation_unit(&ps);
            if (a->dump_ast)
                for (k = 0; k < tu->ndecls; k++)
                    dump_decl(tu->decls[k], 0);
            /* Sema runs for -fsyntax-only too: a declaration that parses
             * but is semantically wrong must still be reported, and that
             * is what -fsyntax-only means. */
            if (a->dump_sema || a->dump_layout || a->dump_init ||
                a->syntax_only || a->emit_ir || a->emit_mir || a->emit_asm ||
                a->compile_obj || a->link_exe) {
                Sema sema;

                sema_install_renderer();
                sema_init(&sema, arena, dc, &interner, &lang,
                          cgf_target_host());
                /* -fno-common is the DEFAULT (gcc 10 semantics; the gcc 8
                 * parity baseline defaulted to -fcommon — Sprint 26
                 * locked the flip, documented in --help). */
                sema.fcommon = !a->fno_common;
                sema_run(&sema, tu);
                if (a->dump_layout)
                    layout_dump(&sema, stdout);
                if (a->dump_init)
                    constexpr_dump_initializers(&sema, tu, stdout);
                if (a->dump_sema) {
                    sema_dump(&sema, stdout);
                    /* Function bodies too, so the goldens can assert that
                     * implicit conversions are MATERIALIZED in the tree
                     * rather than left as rules for a later pass. */
                    for (k = 0; k < tu->ndecls; k++)
                        if (tu->decls[k] && tu->decls[k]->kind == AST_FUNC_DEF)
                            dump_decl(tu->decls[k], 0);
                }
                if ((a->emit_ir || a->emit_mir || a->emit_asm ||
                     a->compile_obj || a->link_exe) &&
                    !diag_had_error(dc)) {
                    /* AST -> IR. This module is GENERATED: a verifier
                     * failure here is an ICE, never a user error — the
                     * user's errors all ended in sema. CGF_DUMP_BAD_IR
                     * captures the module first so the bug is
                     * reportable. */
                    IrModule *m = lower_translation_unit(arena, dc, &sema, tu);

                    char why[256];

                    if (m && !ir_verify_report(dc, m, why, sizeof(why))) {
                        const char *dump_path = cgf_env("CGF_DUMP_BAD_IR");

                        if (dump_path) {
                            FILE *df = fopen(dump_path, "wb");

                            if (df) {
                                ir_print_module(df, m);
                                fprintf(df, "// verify failed: %s\n", why);
                                fclose(df);
                            }
                        }
                        CGF_ICE("lowering produced IR the verifier "
                                "rejects for '%s' (%s)",
                                job->path, why);
                    }
                    if (m)
                        optimize_module(m, a, job->path);
                    if (m && a->emit_mir) {
                        int rc = emit_mir_print(arena, dc, m);

                        (void)rc;
                    } else if (m &&
                               (a->emit_asm || a->compile_obj || a->link_exe)) {
                        int rc = run_emit_asm(arena, dc, m, a, job);

                        if (rc != CGF_EXIT_OK) {
                            PpTokVecD_free(&collected);
                            pp_end(&pp);
                            intern_free(&interner);
                            pp_loc_free(&pp.loc);
                            strmap_free(&pp.macros);
                            return rc;
                        }
                    } else if (m) {
                        int rc = emit_ir_print(arena, dc, m, job->path);

                        (void)rc;
                    }
                }
            }
        }
        if (job->dep_text && !diag_had_error(dc))
            cgf_deps_write(job->dep_text, a, arena, job->dep_target, &pp);
        PpTokVecD_free(&collected);
        pp_end(&pp);
        intern_free(&interner);
        pp_loc_free(&pp.loc);
        strmap_free(&pp.macros);
        return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
    }

    while (pp_next(&pp, &t)) {
        if (dump) {
            FileId fid;
            u32 line, col;

            pp_loc_resolve(&pp.loc, t.loc, &fid, &line, &col);
            printf("%s %s %s:%u:%u%s%s\n", pp_tok_kind_name((PpTokKind)t.kind),
                   t.spelling, pp.files[fid - 1]->path, (unsigned)line,
                   (unsigned)col, (t.flags & PPTOK_F_BOL) ? " BOL" : "",
                   (t.flags & PPTOK_F_SPACE) ? " SPACE" : "");
        } else {
            if (!first && (t.flags & PPTOK_F_BOL)) {
                buf_push_u8(job->pp_text, '\n');
            } else if (!first) {
                /* Explicit spacing, or gcc's avoid-paste rule: adjacent
                 * tokens that would re-lex as one must stay two. */
                if ((t.flags & PPTOK_F_SPACE) ||
                    pp_tokens_would_merge(&pp, &prev_tok, &t))
                    buf_push_u8(job->pp_text, ' ');
            }
            buf_printf(job->pp_text, "%s", t.spelling);
            prev_tok = t;
            first = false;
        }
    }
    if (!dump && !first)
        buf_push_u8(job->pp_text, '\n');
    if (a->dump_macros)
        pp_dump_macros(&pp);
    if (dump_guard) {
        /* Include-guard shape probe (Sprint 6 detector; Sprint 7 fast
         * path consumes guard_macro). One line per file, in load order. */
        size_t fi;
        for (fi = 0; fi < pp.nfiles; fi++)
            printf("GUARD %s %s\n", pp.files[fi]->path,
                   pp.files[fi]->guard_macro ? pp.files[fi]->guard_macro : "-");
    }

    if (job->dep_text && !diag_had_error(dc))
        cgf_deps_write(job->dep_text, a, arena, job->dep_target, &pp);
    pp_end(&pp);
    intern_free(&interner);
    pp_loc_free(&pp.loc);
    strmap_free(&pp.macros);
    return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
}

/* ---- product naming ------------------------------------------------- */

/* <base>.<ext> in the CURRENT directory (gcc drops the source dir), from
 * the input's basename with its last extension replaced. */
static void product_path(const char *src, char ext, char *out, size_t sz)
{
    const char *base = strrchr(src, '/');
    const char *dot;
    size_t n;

    base = base ? base + 1 : src;
    dot = strrchr(base, '.');
    n = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    snprintf(out, sz, "%.*s.%c", (int)n, base, ext);
}

/* The -MD/-MMD side-depfile name: -MF wins; else the -o argument with
 * its last suffix replaced by .d (gcc keeps the directory); else
 * <src base>.d in the cwd. */
static void dep_side_path(const DriverArgs *a, const char *src, char *out,
                          size_t sz)
{
    if (a->dep_file) {
        snprintf(out, sz, "%s", a->dep_file);
        return;
    }
    if (a->output) {
        const char *base = strrchr(a->output, '/');
        const char *dot = strrchr(base ? base + 1 : a->output, '.');
        size_t n = dot ? (size_t)(dot - a->output) : strlen(a->output);

        snprintf(out, sz, "%.*s.d", (int)n, a->output);
        return;
    }
    product_path(src, 'd', out, sz);
}

/* ---- introspection -------------------------------------------------- */

static int print_prog_name(const DriverArgs *a)
{
    ToolchainConfig tc = cgf_toolchain_resolve(cgf_target_host());
    const char *name = a->print_prog;
    char resolved[4096];

    /* as/ld go through the CGF_* routing first — the answer is the tool
     * the driver would actually exec. */
    if (strcmp(name, "as") == 0 && tc.as_path)
        name = tc.as_path;
    else if (strcmp(name, "ld") == 0 && tc.ld_path)
        name = tc.ld_path;
    if (cgf_which(name, resolved, sizeof(resolved)))
        printf("%s\n", resolved);
    else
        printf("%s\n", a->print_prog); /* verbatim echo-back */
    return CGF_EXIT_OK;
}

static int print_file_name(const DriverArgs *a)
{
    char probe[4096], crtdiag[256];
    const char *crtdir;
    size_t i;

    for (i = 0; i < a->lib_dirs.len; i++) {
        snprintf(probe, sizeof(probe), "%s/%s", a->lib_dirs.data[i],
                 a->print_file);
        if (access(probe, R_OK) == 0) {
            printf("%s\n", probe);
            return CGF_EXIT_OK;
        }
    }
    crtdir = cgf_probe_crt_dir(crtdiag, sizeof(crtdiag));
    if (crtdir) {
        snprintf(probe, sizeof(probe), "%s/%s", crtdir, a->print_file);
        if (access(probe, R_OK) == 0) {
            printf("%s\n", probe);
            return CGF_EXIT_OK;
        }
    }
    printf("%s\n", a->print_file); /* autoconf relies on the echo-back */
    return CGF_EXIT_OK;
}

static int print_search_dirs(const DriverArgs *a)
{
    char dir[4096], crtdiag[256];
    const char *crtdir = cgf_probe_crt_dir(crtdiag, sizeof(crtdiag));
    bool have_exe = cgf_exe_relative("", dir, sizeof(dir));
    size_t i;

    /* Truthful, not gcc-shaped: exactly the resolved search order the
     * link path uses — -L dirs in flag order, the crt probe hit, then
     * the defaults (Sprint 27 section 6). */
    printf("install: %s/\n", have_exe ? dir : ".");
    printf("programs: %s\n", have_exe ? dir : ".");
    printf("libraries: =");
    for (i = 0; i < a->lib_dirs.len; i++)
        printf("%s:", a->lib_dirs.data[i]);
    if (crtdir)
        printf("%s:", crtdir);
    printf("/usr/lib:/lib\n");
    return CGF_EXIT_OK;
}

/* Shell-quoted argv on one line — the -v/-### contract. Every arg is
 * double-quoted (gcc's -### shape); embedded quotes get backslashes. */
static void echo_argv(const char *const argv[])
{
    int i;

    for (i = 0; argv[i]; i++) {
        const char *p = argv[i];

        if (i)
            fputc(' ', stderr);
        fputc('"', stderr);
        for (; *p; p++) {
            if (*p == '"' || *p == '\\')
                fputc('\\', stderr);
            fputc(*p, stderr);
        }
        fputc('"', stderr);
    }
    fputc('\n', stderr);
}

/* The internal compile step printed as a REAL re-invocation: the line
 * `"cgfried" "-S" "t.c" "-o" "t.s"` reproduces the step by itself. */
static void echo_compile_step(const char *mode, const char *src,
                              const char *out)
{
    const char *argv[6];
    int n = 0;

    argv[n++] = cgf_toolchain_argv0();
    argv[n++] = mode;
    argv[n++] = src;
    if (out) {
        argv[n++] = "-o";
        argv[n++] = out;
    }
    argv[n] = NULL;
    echo_argv(argv);
}

/* ---- driver_main ---------------------------------------------------- */

int driver_main(int argc, char **argv)
{
    static const Span no_span = {0};
    Arena arena;
    DiagCtx *dc;
    DriverArgs a;
    int status = CGF_EXIT_OK;
    size_t k;
    bool command_line_warning_error;

    if (argc < 2) {
        fprintf(stderr, "usage: cgfried [options] file...\n");
        fprintf(stderr, "try 'cgfried --help' for the option list\n");
        return CGF_EXIT_COMPILE;
    }

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    a = args_parse(&arena, argc, argv);
    diag_set_max_errors(dc, a.max_errors);

    command_line_warning_error =
        a.werror && !a.no_warnings &&
        (a.warn_unrecognized.len != 0 || a.warn_fast_math.len != 0);

    /* Flag-parse warnings are subject to the already-live bare -Werror
     * contract.  Named warning policy remains Sprint 37 territory. */
    if (!a.no_warnings) {
        for (k = 0; k < a.warn_unrecognized.len; k++)
            fprintf(stderr,
                    "cgfried: %s: unrecognized command-line option "
                    "'%s'\n",
                    a.werror ? "error" : "warning",
                    a.warn_unrecognized.data[k]);
        for (k = 0; k < a.warn_fast_math.len; k++)
            fprintf(stderr,
                    "cgfried: %s: option '%s' is bundled-only in "
                    "v0.1.0; see docs/fast-math.md\n",
                    a.werror ? "error" : "warning", a.warn_fast_math.data[k]);
    }

    if (a.unknown_opt) {
        if (a.suggest[0])
            diag_emit(dc, DIAG_ERROR, no_span,
                      "unrecognized command-line option '%s'; did you mean "
                      "'%s'?",
                      a.unknown_opt, a.suggest);
        else
            diag_emit(dc, DIAG_ERROR, no_span,
                      "unrecognized command-line option '%s'", a.unknown_opt);
        status = CGF_EXIT_COMPILE;
    } else if (a.missing_arg) {
        diag_emit(dc, DIAG_ERROR, no_span, "option '%s' requires an argument",
                  a.missing_arg);
        status = CGF_EXIT_COMPILE;
    } else if (a.bad_value) {
        diag_emit(dc, DIAG_ERROR, no_span, "invalid value for option '%s'",
                  a.bad_value);
        status = CGF_EXIT_COMPILE;
    } else if (a.rsp_error) {
        diag_emit(dc, DIAG_ERROR, no_span, "%s", a.rsp_error);
        status = CGF_EXIT_COMPILE;
    } else if (a.deferred) {
        diag_emit(dc, DIAG_ERROR, no_span, "option '%s' %s", a.deferred,
                  a.deferred_sprint);
        status = CGF_EXIT_COMPILE;
    } else if (a.stdin_no_x) {
        diag_emit(dc, DIAG_ERROR, no_span,
                  "reading from stdin ('-') requires -x c");
        status = CGF_EXIT_COMPILE;
    } else if (a.o_multi_conflict) {
        diag_emit(dc, DIAG_ERROR, no_span,
                  "cannot specify -o with -c, -S or -E with multiple files");
        status = CGF_EXIT_COMPILE;
    } else if (command_line_warning_error) {
        /* The promoted diagnostics were rendered above; do not compile. */
        status = CGF_EXIT_COMPILE;
    } else if (a.show_help) {
        for (k = 0; k < CGF_ARRAY_LEN(help_text); k++)
            fputs(help_text[k], stdout);
    } else if (a.show_version) {
        printf("cgfried %s (%s)\n", CGF_VERSION,
               cgf_target_name(cgf_target_host()));
    } else if (a.show_dumpversion) {
        printf("%s\n", CGF_VERSION);
    } else if (a.show_dumpmachine) {
        printf("%s\n", cgf_target_name(cgf_target_host()));
    } else if (a.print_search_dirs) {
        status = print_search_dirs(&a);
    } else if (a.print_prog) {
        status = print_prog_name(&a);
    } else if (a.print_file) {
        status = print_file_name(&a);
    } else if (a.inputs.len == 0) {
        fprintf(stderr, "cgfried: no input files\n");
        status = CGF_EXIT_COMPILE;
    } else {
        bool stop_mode = a.mode_E || a.emit_asm || a.compile_obj ||
                         a.syntax_only || a.dump_tokens || a.dump_ast ||
                         a.dump_sema || a.dump_layout || a.dump_init ||
                         a.emit_ir || a.emit_mir;
        bool any_fail = false;
        VecStr temp_objs = {0};
        FILE *eout = NULL;
        const char *final_out = a.output ? a.output : "a.out";

        a.link_exe = !stop_mode;
        cgf_toolchain_set_echo(a.verbose);

        if (a.mode_E && a.output && !a.dry_run) {
            eout = fopen(a.output, "wb");
            if (!eout) {
                fprintf(stderr, "cgfried: error: cannot write '%s'\n",
                        a.output);
                status = CGF_EXIT_IO;
                goto done;
            }
        }

        /* Every TU is attempted even after one errors (gcc parity: the
         * user sees all their diagnostics); link is skipped if any
         * failed. */
        for (k = 0; k < a.inputs.len; k++) {
            DriverInput *in = &a.inputs.data[k];
            CompileJob job;
            char out_buf[512];
            int rc = CGF_EXIT_OK;

            memset(&job, 0, sizeof(job));
            job.path = in->path;
            job.kind = (InputKind)in->kind;
            cgf_ice_set_input(in->path);

            if (in->kind == IN_LINK) {
                if (!a.link_exe && !a.no_warnings)
                    fprintf(stderr,
                            "cgfried: warning: %s: linker input file unused "
                            "because linking is not done\n",
                            in->path);
                continue;
            }
            if (in->kind == IN_CGFIR) {
                if (a.emit_ir || a.emit_mir) {
                    rc = a.dry_run ? CGF_EXIT_OK
                                   : run_emit_ir(&arena, dc, &a, &job);
                } else {
                    fprintf(stderr,
                            "cgfried: error: %s: .cgfir input needs -emit-ir "
                            "or -emit-mir\n",
                            in->path);
                    rc = CGF_EXIT_COMPILE;
                }
                if (rc != CGF_EXIT_OK) {
                    any_fail = true;
                    if (status == CGF_EXIT_OK)
                        status = rc;
                }
                continue;
            }
            if (in->kind == IN_ASM && (a.mode_E || a.emit_asm)) {
                /* Nothing in this mode consumes a .s input. */
                if (!a.no_warnings)
                    fprintf(stderr, "cgfried: warning: %s: input file unused\n",
                            in->path);
                continue;
            }

            /* Where does this input's product land? */
            if (a.mode_E || a.syntax_only || a.dump_tokens || a.dump_ast ||
                a.dump_sema || a.dump_layout || a.dump_init || a.emit_ir ||
                a.emit_mir) {
                job.out = NULL;
            } else if (a.emit_asm) {
                if (a.output)
                    snprintf(out_buf, sizeof(out_buf), "%s", a.output);
                else
                    product_path(in->path, 's', out_buf, sizeof(out_buf));
                job.out = out_buf;
            } else if (a.compile_obj) {
                if (a.output)
                    snprintf(out_buf, sizeof(out_buf), "%s", a.output);
                else
                    product_path(in->path, 'o', out_buf, sizeof(out_buf));
                job.out = out_buf;
            } else {
                /* Link mode: a deterministic temp object per input. */
                snprintf(out_buf, sizeof(out_buf), "%s.cgf.%zu.o", final_out,
                         k);
                job.out = out_buf;
            }

            if (a.dry_run) {
                /* -###: print the plan, run nothing. */
                if (a.mode_E) {
                    echo_compile_step("-E", in->path, a.output);
                } else if (job.out == NULL) {
                    /* dump/-fsyntax-only shapes: one internal step. */
                    echo_compile_step("-fsyntax-only", in->path, NULL);
                } else if (in->kind == IN_ASM) {
                    cgf_echo_as_plan(in->path, job.out);
                } else if (a.emit_asm) {
                    echo_compile_step("-S", in->path, job.out);
                } else {
                    char s_tmp[528];

                    snprintf(s_tmp, sizeof(s_tmp), "%s.cgf.s", job.out);
                    echo_compile_step("-S", in->path, s_tmp);
                    cgf_echo_as_plan(s_tmp, job.out);
                }
                if (job.out && !a.emit_asm && !a.mode_E && a.link_exe &&
                    in->link_slot >= 0) {
                    size_t n = strlen(job.out);
                    char *dup = arena_alloc(&arena, n + 1, 1);

                    memcpy(dup, job.out, n + 1);
                    a.link_inputs.data[in->link_slot].val = dup;
                }
                continue;
            }

            if (in->kind == IN_ASM) {
                /* Assemble a user .s: assembler diagnostics are the
                 * user's diagnostics (exit 1, never an ICE). */
                ToolResult res = cgf_run_assembler(in->path, job.out, NULL);

                rc = cgf_tool_exit_code(TOOL_AS, &res, true);
                if (res.kind == TOOL_SPAWN_FAILED) {
                    fprintf(stderr, "cgfried: error: %s\n",
                            cgf_tool_missing_hint(TOOL_AS));
                    rc = CGF_EXIT_IO;
                }
            } else if (in->kind == IN_ASM_PP || a.mode_E) {
                /* Preprocess to text; .S then assembles it. */
                Buf text, deps;
                char tgt_buf[512];

                buf_init(&text);
                buf_init(&deps);
                job.pp_text = &text;
                job.pp_only = true;
                if (a.dep_mode != DEP_OFF && a.mode_E &&
                    in->kind != IN_ASM_PP) {
                    /* -M/-MM: the depfile REPLACES the -E text. */
                    product_path(in->path, 'o', tgt_buf, sizeof(tgt_buf));
                    job.dep_text = &deps;
                    job.dep_target = tgt_buf;
                }
                if (a.verbose)
                    echo_compile_step("-E", in->path, NULL);
                rc = run_preprocess(&arena, dc, &a, &job);
                if (rc == CGF_EXIT_OK && job.dep_text) {
                    if (a.dep_file) {
                        FILE *df = fopen(a.dep_file, "wb");

                        if (!df) {
                            fprintf(stderr,
                                    "cgfried: error: cannot write '%s'\n",
                                    a.dep_file);
                            rc = CGF_EXIT_IO;
                        } else {
                            buf_fwrite(&deps, df);
                            fclose(df);
                        }
                    } else {
                        FILE *dst = eout ? eout : stdout;

                        buf_fwrite(&deps, dst);
                    }
                } else if (rc == CGF_EXIT_OK && a.mode_E) {
                    FILE *dst = eout ? eout : stdout;

                    buf_fwrite(&text, dst);
                } else if (rc == CGF_EXIT_OK && in->kind == IN_ASM_PP &&
                           a.emit_asm) {
                    /* -S on a .S: the preprocessed text IS the product. */
                    FILE *sf = fopen(job.out, "wb");

                    if (!sf) {
                        fprintf(stderr, "cgfried: error: cannot write '%s'\n",
                                job.out);
                        rc = CGF_EXIT_IO;
                    } else {
                        buf_fwrite(&text, sf);
                        fclose(sf);
                    }
                } else if (rc == CGF_EXIT_OK && in->kind == IN_ASM_PP) {
                    char s_tmp[528];
                    FILE *sf;

                    snprintf(s_tmp, sizeof(s_tmp), "%s.cgf.s", job.out);
                    sf = fopen(s_tmp, "wb");
                    if (!sf) {
                        fprintf(stderr, "cgfried: error: cannot write '%s'\n",
                                s_tmp);
                        rc = CGF_EXIT_IO;
                    } else {
                        ToolResult res;

                        buf_fwrite(&text, sf);
                        fclose(sf);
                        res = cgf_run_assembler(s_tmp, job.out, NULL);
                        rc = cgf_tool_exit_code(TOOL_AS, &res, true);
                        if (res.kind == TOOL_SPAWN_FAILED) {
                            fprintf(stderr, "cgfried: error: %s\n",
                                    cgf_tool_missing_hint(TOOL_AS));
                            rc = CGF_EXIT_IO;
                        }
                        if (rc == CGF_EXIT_OK)
                            unlink(s_tmp);
                    }
                }
                buf_free(&text);
                buf_free(&deps);
            } else {
                Buf deps;
                char tgt_buf[512];

                buf_init(&deps);
                if (a.dep_side) {
                    /* -MD/-MMD: depfile beside compiling. The target
                     * derives from the OUTPUT object (gcc parity): the
                     * -o argument, or the object this job produces. */
                    if (a.link_exe)
                        snprintf(tgt_buf, sizeof(tgt_buf), "%s",
                                 a.output ? a.output : "a.out");
                    else
                        snprintf(tgt_buf, sizeof(tgt_buf), "%s", job.out);
                    job.dep_text = &deps;
                    job.dep_target = tgt_buf;
                }
                if (a.verbose && job.out) {
                    /* The internal step really is compile-to-.s; the
                     * assembler echoes its own line. */
                    if (a.emit_asm) {
                        echo_compile_step("-S", in->path, job.out);
                    } else {
                        char s_tmp[528];

                        snprintf(s_tmp, sizeof(s_tmp), "%s.cgf.s", job.out);
                        echo_compile_step("-S", in->path, s_tmp);
                    }
                }
                rc = run_preprocess(&arena, dc, &a, &job);
                if (rc == CGF_EXIT_OK && a.dep_side) {
                    char dpath[512];
                    FILE *df;

                    dep_side_path(&a, in->path, dpath, sizeof(dpath));
                    df = fopen(dpath, "wb");
                    if (!df) {
                        fprintf(stderr, "cgfried: error: cannot write '%s'\n",
                                dpath);
                        rc = CGF_EXIT_IO;
                    } else {
                        buf_fwrite(&deps, df);
                        fclose(df);
                    }
                }
                buf_free(&deps);
            }

            if (rc == CGF_EXIT_OK && a.link_exe && in->link_slot >= 0) {
                /* The object claims its argv position in the link line. */
                size_t n = strlen(job.out);
                char *dup = arena_alloc(&arena, n + 1, 1);

                memcpy(dup, job.out, n + 1);
                a.link_inputs.data[in->link_slot].val = dup;
                VecStr_push(&temp_objs, dup);
            }
            if (rc != CGF_EXIT_OK) {
                any_fail = true;
                if (status == CGF_EXIT_OK)
                    status = rc;
            }
        }
        if (eout)
            fclose(eout);

        if (a.link_exe && !any_fail && a.link_inputs.len > 0) {
            VecStr ldargv = {0};

            /* final_out is what the builder reads via a.output. */
            (void)final_out;
            if (!toolchain_build_link_argv(&a, cgf_target_host(), &arena,
                                           &ldargv)) {
                /* crt/route failure: link-phase error, exit 2 (the
                 * diagnostic named every probed path already). */
                if (status == CGF_EXIT_OK)
                    status = CGF_EXIT_LINK;
            } else if (a.dry_run) {
                cgf_toolchain_echo_argv((const char *const *)ldargv.data);
            } else {
                ToolResult lres =
                    cgf_run_tool((const char *const *)ldargv.data);
                int rc = cgf_tool_exit_code(TOOL_LD, &lres, false);

                if (lres.kind == TOOL_SPAWN_FAILED) {
                    fprintf(stderr, "cgfried: error: %s\n",
                            cgf_tool_missing_hint(TOOL_LD));
                } else if (rc != CGF_EXIT_OK) {
                    /* The linker's own stderr streamed through verbatim
                     * above; ONE trailer, then the contract exit. */
                    if (lres.kind == TOOL_EXITED)
                        fprintf(stderr,
                                "cgfried: error: linker command failed "
                                "with exit code %d (use -v to see "
                                "invocation)\n",
                                lres.exit_code);
                    else
                        fprintf(stderr,
                                "cgfried: error: linker command died with "
                                "signal %d (use -v to see invocation)\n",
                                lres.term_signal);
                    /* Sprint 27: afs-ld's dynamic ELF lane is
                     * experimental (data imports need COPY relocations,
                     * a later upstream rung — LD-ELF-001). */
                    if (!a.static_link &&
                        cgf_toolchain_resolve(cgf_target_host()).use_afs_ld)
                        fprintf(stderr, "cgfried: note: afs-ld's dynamic ELF "
                                        "lane is experimental; retry with "
                                        "-static or unset CGF_LD\n");
                }
                if (rc != CGF_EXIT_OK && status == CGF_EXIT_OK)
                    status = rc;
            }
            VecStr_free(&ldargv);
        } else if (a.link_exe && a.link_inputs.len == 0 && !any_fail) {
            /* All inputs consumed by warnings (e.g. only unused .s). */
            fprintf(stderr, "cgfried: no input files\n");
            status = CGF_EXIT_COMPILE;
        }
        for (k = 0; k < temp_objs.len; k++)
            unlink(temp_objs.data[k]);
        VecStr_free(&temp_objs);
    }
done:
    args_free(&a);
    arena_free_all(&arena);
    return status;
}
