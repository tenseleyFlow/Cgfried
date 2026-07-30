#include "driver/driver.h"

#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "driver/toolchain.h"
#include "ir/ir.h"
#include "lex/lex.h"
#include "parse/parse.h"
#include "pp/pp.h"
#include "sema/sema.h"
#include "target.h"
#include "util/arena.h"
#include "util/intern.h"

/* One string, hand-organized — no generated help (locked style). Info-option
 * precedence as implemented: --help over --version over -dumpversion. */
static const char help_text[] =
    "Usage: cgfried [options] file...\n"
    "\n"
    "Modes:\n"
    "  -E                preprocess only, print tokens to stdout\n"
    "  --dump-tokens     run the lexer and dump one token per line\n"
    "  --dump-ast        parse and dump declarations (declarator shapes)\n"
    "  -fdump-sema       run semantic analysis and dump file-scope symbols\n"
    "  -fdump-layout     dump record layout (offsets, sizes, alignment)\n"
    "  -fdump-init       dump static initializer images as hex bytes\n"
    "  -fsyntax-only     parse and report diagnostics; produce no output\n"
    "  -emit-ir          parse, verify and reprint a .cgfir module\n"
    "                    (lowering C to IR lands in Sprint 18)\n"
    "  -P                omit linemarkers from -E output\n"
    "  (full compilation is not yet supported; the pipeline is under\n"
    "  construction sprint by sprint)\n"
    "\n"
    "Language:\n"
    "  -trigraphs        enable trigraph translation (default off)\n"
    "  -std=<std>        c89/gnu89/c99/c11/c17/gnu17 (default c17)\n"
    "  -pedantic         warn about non-ISO constructs\n"
    "  -fmax-errors=N    stop after N errors (0 = unlimited, the default);\n"
    "  -fcommon          tentative definitions become COMMON symbols\n"
    "                    (the default, matching gcc 8); -fno-common gives\n"
    "                    them zero-initialized definitions instead\n"
    "                    -ferror-limit=N is accepted as an alias\n"
    "  -D name[=value]   predefine a macro (value defaults to 1)\n"
    "  -U name           undefine a macro (processed in -D/-U order)\n"
    "\n"
    "Directories:\n"
    "  -I dir            add dir to the include search path\n"
    "  -iquote dir       add dir to the \"...\"-form-only search path\n"
    "  -nostdinc         do not search the system include directories\n"
    "  -v                print the include search list to stderr\n"
    "\n"
    "Output:\n"
    "  --help            print this help and exit\n"
    "  --version         print version and host target, then exit\n"
    "  -dumpversion      print the bare version number, then exit\n"
    "\n"
    "Diagnostics:\n"
    "  Diagnostics are colored when stderr is a terminal. If multiple info\n"
    "  options are given, --help wins over --version over -dumpversion.\n"
    "\n"
    "Environment:\n"
    "  NO_COLOR          disable diagnostic colors (any non-empty value)\n"
    "  CLICOLOR_FORCE    force diagnostic colors even when piped\n"
    "  CGF_AS            unset/1: use bundled afs-as (default); 0: system "
    "'as'\n"
    "  CGF_AS_PATH       use exactly this assembler (wins over CGF_AS)\n"
    "  CGF_LD            unset/0: use system 'ld' (default); 1: afs-ld "
    "(Sprint 27)\n"
    "  CGF_LD_PATH       use exactly this linker (wins over CGF_LD)\n"
    "  CGF_CRT_DIR       crt object discovery override (used from Sprint "
    "27)\n"
    "  CGF_PP_DUMP_TOKENS  with -E: dump one token per line (testing)\n"
    "  CGF_PP_DUMP_GUARD   with -E: dump include-guard shapes (testing)\n"
    "  Empty-string values are treated as unset.\n";

VEC_DECL(PpTokVecD, PpToken);

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
 * -D name means 1; -D name=val splits at the first '='. */
static SourceFile *build_cmdline_file(Preprocessor *pp, const DriverArgs *a)
{
    Buf b;
    SourceFile *sf;
    int i;

    if (a->n_defs == 0)
        return NULL;
    buf_init(&b);
    for (i = 0; i < a->n_defs; i++) {
        const char *d = a->defs[i];
        const char *eq = strchr(d, '=');

        if (a->def_is_undef[i]) {
            buf_printf(&b, "#undef %s\n", d);
        } else if (eq) {
            buf_printf(&b, "#define %.*s %s\n", (int)(eq - d), d, eq + 1);
        } else {
            buf_printf(&b, "#define %s 1\n", d);
        }
    }
    sf =
        pp_source_add_buffer(pp, "<command-line>", (const char *)b.data, b.len);
    buf_free(&b);
    return sf;
}

/* -emit-ir on a .cgfir file: parse -> verify -> print. The path does more
 * than print, deliberately: it re-parses its own output, demands
 * structural equality, prints AGAIN and demands byte equality — so every
 * .cgfir fixture that runs through the driver proves the round-trip AND
 * print-determinism invariants for free. A failure in either is an ICE
 * (our printer/parser disagree = our bug, never the user's). */
static int run_emit_ir(Arena *arena, DiagCtx *dc, const DriverArgs *a)
{
    FILE *f = fopen(a->input, "rb");
    char *src;
    long len;
    size_t rd;
    IrModule *m, *m2;
    Buf b1, b2;

    if (!f) {
        fprintf(stderr, "cgfried: error: cannot open '%s'\n", a->input);
        return CGF_EXIT_IO;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fprintf(stderr, "cgfried: error: cannot read '%s'\n", a->input);
        return CGF_EXIT_IO;
    }
    src = arena_alloc(arena, (size_t)len + 1, 1);
    rd = fread(src, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) {
        fprintf(stderr, "cgfried: error: cannot read '%s'\n", a->input);
        return CGF_EXIT_IO;
    }
    src[len] = '\0';
    if (memchr(src, '\0', (size_t)len) != NULL) {
        fprintf(stderr, "cgfried: error: '%s' contains a NUL byte\n", a->input);
        return CGF_EXIT_COMPILE;
    }

    m = ir_parse_module(arena, dc, src, a->input);
    if (!m)
        return CGF_EXIT_COMPILE;
    /* Hand-written IR that fails the verifier is a USER error (exit 1);
     * only generated IR failing it is an ICE (Sprint 18 wires that). Either
     * way CGF_DUMP_BAD_IR=path captures the offending module — and the dump
     * is itself parseable .cgfir, pinned by test. */
    if (!ir_verify(dc, m)) {
        const char *dump = cgf_env("CGF_DUMP_BAD_IR");

        if (dump) {
            FILE *df = fopen(dump, "wb");

            if (df) {
                ir_print_module(df, m);
                fclose(df);
            }
        }
        return CGF_EXIT_COMPILE;
    }

    buf_init(&b1);
    ir_print_module_buf(&b1, m);
    buf_push_u8(&b1, 0); /* the parser wants a C string */
    m2 = ir_parse_module(arena, dc, (const char *)b1.data, "<reprint>");
    b1.len--; /* drop the NUL again for the byte-compare and the output */
    if (!m2 || !ir_module_struct_eq(m, m2))
        CGF_ICE("-emit-ir round-trip broke: parse(print(M)) != M for '%s'",
                a->input);
    buf_init(&b2);
    ir_print_module_buf(&b2, m2);
    if (b1.len != b2.len || memcmp(b1.data, b2.data, b1.len) != 0)
        CGF_ICE("-emit-ir determinism broke: two prints differ for '%s'",
                a->input);
    fwrite(b1.data, 1, b1.len, stdout);
    buf_free(&b1);
    buf_free(&b2);
    return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
}

/* -E: preprocess through the directive engine, printed with SPACE/BOL-
 * faithful spacing (exact line-count fidelity is Sprint 6's problem). */
static int run_preprocess(Arena *arena, DiagCtx *dc, const DriverArgs *a)
{
    Interner interner;
    Preprocessor pp;
    SourceFile *sf, *cmdline;
    PpToken t;
    PpToken prev_tok;
    bool dump = cgf_env("CGF_PP_DUMP_TOKENS") != NULL;
    bool dump_guard = cgf_env("CGF_PP_DUMP_GUARD") != NULL;
    bool first = true;

    memset(&prev_tok, 0, sizeof(prev_tok));
    int i;

    intern_init(&interner, arena);
    pp_init(&pp, arena, dc, &interner);
    pp.trigraphs = a->trigraphs;
    pp.verbose = a->verbose;
    pp.std = (CStd)a->std;
    (void)a->no_linemarkers; /* -P: we never emit linemarkers (Sprint 7
                                owns line fidelity); accepted so oracle
                                command lines stay symmetric */
    pp.gnu_mode = pp.std >= STD_GNU89;
    for (i = 0; i < a->n_include; i++)
        pp.include_dirs[pp.n_include++] = a->include_dirs[i];
    for (i = 0; i < a->n_iquote; i++)
        pp.iquote_dirs[pp.n_iquote++] = a->iquote_dirs[i];
    if (!a->nostdinc) {
        /* Our shipped freestanding headers (stddef.h et al.) come FIRST —
         * glibc's own headers include them and gcc likewise places its own
         * include dir ahead of /usr/include. Dev tree: build/../include;
         * installed: <prefix>/bin/../include. */
        static char shipped[4096];
        if (cgf_exe_relative("/../include", shipped, sizeof(shipped)))
            pp.system_dirs[pp.n_system++] = shipped;
        pp.n_system += cgf_target_system_include_dirs(
            cgf_target_host(), pp.system_dirs + pp.n_system,
            PP_MAX_DIRS - pp.n_system);
    }

    sf = pp_source_load(&pp, a->input);
    if (!sf) {
        intern_free(&interner);
        pp_loc_free(&pp.loc);
        strmap_free(&pp.macros);
        return CGF_EXIT_IO;
    }
    cmdline = build_cmdline_file(&pp, a);
    pp_begin(&pp, sf, cmdline);

    if (a->dump_tokens || a->dump_ast || a->dump_sema || a->dump_layout ||
        a->dump_init || a->syntax_only) {
        /* Phase 5-7: collect the pp-token stream, convert, dump. */
        PpTokVecD collected = {NULL, 0, 0};
        LangOpts lang;
        TokenList tl;
        u32 k;

        memset(&lang, 0, sizeof(lang));
        lang.std = (CStd)a->std;
        lang.gnu_mode = lang.std >= STD_GNU89;
        lang.pedantic = a->pedantic;
        while (pp_next(&pp, &t))
            PpTokVecD_push(&collected, t);
        tl = lex_convert(&pp, collected.data, (u32)collected.len, &lang,
                         cgf_target_host(), arena);
        if (a->dump_tokens)
            for (k = 0; k < tl.n; k++)
                dump_token(&tl.toks[k]);
        if (a->dump_ast || a->dump_sema || a->dump_layout || a->dump_init ||
            a->syntax_only) {
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
                a->syntax_only) {
                Sema sema;

                sema_install_renderer();
                sema_init(&sema, arena, dc, &interner, &lang,
                          cgf_target_host());
                /* gcc 8's default is -fcommon; gcc 10 flipped it. We
                 * follow the 2018 parity baseline. */
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
            }
        }
        PpTokVecD_free(&collected);
        pp_end(&pp);
        intern_free(&interner);
        pp_loc_free(&pp.loc);
        strmap_free(&pp.macros);
        return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
    }

    while (pp_next(&pp, &t)) {
        if (dump) {
            FileId f;
            u32 line, col;

            pp_loc_resolve(&pp.loc, t.loc, &f, &line, &col);
            printf("%s %s %s:%u:%u%s%s\n", pp_tok_kind_name((PpTokKind)t.kind),
                   t.spelling, pp.files[f - 1]->path, (unsigned)line,
                   (unsigned)col, (t.flags & PPTOK_F_BOL) ? " BOL" : "",
                   (t.flags & PPTOK_F_SPACE) ? " SPACE" : "");
        } else {
            if (!first && (t.flags & PPTOK_F_BOL)) {
                putchar('\n');
            } else if (!first) {
                /* Explicit spacing, or gcc's avoid-paste rule: adjacent
                 * tokens that would re-lex as one must stay two. */
                if ((t.flags & PPTOK_F_SPACE) ||
                    pp_tokens_would_merge(&pp, &prev_tok, &t))
                    putchar(' ');
            }
            fputs(t.spelling, stdout);
            prev_tok = t;
            first = false;
        }
    }
    if (!dump && !first)
        putchar('\n');
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

    pp_end(&pp);
    intern_free(&interner);
    pp_loc_free(&pp.loc);
    strmap_free(&pp.macros);
    return diag_had_error(dc) ? CGF_EXIT_COMPILE : CGF_EXIT_OK;
}

int driver_main(int argc, char **argv)
{
    static const Span no_span = {0};
    Arena arena;
    DiagCtx *dc;
    DriverArgs a;
    int status = CGF_EXIT_OK;

    if (argc < 2) {
        fprintf(stderr, "usage: cgfried [options] file...\n");
        fprintf(stderr, "try 'cgfried --help' for the option list\n");
        return CGF_EXIT_COMPILE;
    }

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    a = args_parse(argc, argv);
    diag_set_max_errors(dc, a.max_errors);

    if (a.unknown_opt) {
        diag_emit(dc, DIAG_ERROR, no_span, "unknown option '%s'",
                  a.unknown_opt);
        status = CGF_EXIT_COMPILE;
    } else if (a.missing_arg) {
        diag_emit(dc, DIAG_ERROR, no_span, "option '%s' requires an argument",
                  a.missing_arg);
        status = CGF_EXIT_COMPILE;
    } else if (a.bad_value) {
        diag_emit(dc, DIAG_ERROR, no_span,
                  "option '%s' needs a non-negative integer", a.bad_value);
        status = CGF_EXIT_COMPILE;
    } else if (a.too_many) {
        diag_emit(dc, DIAG_ERROR, no_span,
                  "too many '%s' options (fixed cap until Sprint 26)",
                  a.too_many);
        status = CGF_EXIT_COMPILE;
    } else if (a.show_help) {
        fputs(help_text, stdout);
    } else if (a.show_version) {
        printf("cgfried %s (%s)\n", CGF_VERSION,
               cgf_target_name(cgf_target_host()));
    } else if (a.show_dumpversion) {
        printf("%s\n", CGF_VERSION);
    } else if (a.extra_input) {
        diag_emit(dc, DIAG_ERROR, no_span,
                  "only one input file is supported for now ('%s' and '%s' "
                  "given)",
                  a.input, a.extra_input);
        status = CGF_EXIT_COMPILE;
    } else if (a.input) {
        size_t ilen = strlen(a.input);

        cgf_ice_set_input(a.input);
        if (a.emit_ir) {
            if (ilen >= 6 && strcmp(a.input + ilen - 6, ".cgfir") == 0) {
                status = run_emit_ir(&arena, dc, &a);
            } else {
                diag_emit(dc, DIAG_ERROR, no_span,
                          "-emit-ir takes a .cgfir file for now; lowering "
                          "C to IR lands in Sprint 18");
                status = CGF_EXIT_COMPILE;
            }
        } else if (a.mode_E || a.dump_tokens || a.dump_ast || a.dump_sema ||
                   a.dump_layout || a.dump_init || a.syntax_only) {
            status = run_preprocess(&arena, dc, &a);
        } else {
            diag_emit(dc, DIAG_ERROR, no_span,
                      "only -E, --dump-tokens, --dump-ast and "
                      "-fsyntax-only are supported so far: full "
                      "compilation lands sprint by sprint (next: Sprint "
                      "10, statements and expressions)");
            status = CGF_EXIT_COMPILE;
        }
    } else {
        /* Options only, none of them info options, nothing to do. */
        fprintf(stderr, "cgfried: no input files\n");
        status = CGF_EXIT_COMPILE;
    }

    arena_free_all(&arena);
    return status;
}
