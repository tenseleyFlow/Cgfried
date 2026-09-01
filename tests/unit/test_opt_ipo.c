#include <stdint.h>
#include <stdio.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct Callgraph Callgraph;
Callgraph *ipo_callgraph_build(IrModule *m);
void ipo_callgraph_free(Callgraph *g);
u32 ipo_callgraph_node_count(const Callgraph *g);
u32 ipo_callgraph_edge_count(const Callgraph *g, u32 caller);
u32 ipo_callgraph_edge(const Callgraph *g, u32 caller, u32 ordinal);
bool ipo_callgraph_has_unknown_callees(const Callgraph *g, u32 node);
bool ipo_callgraph_address_taken(const Callgraph *g, u32 node);
u32 ipo_callgraph_scc_count(const Callgraph *g);
u32 ipo_callgraph_scc_of(const Callgraph *g, u32 node);
u32 ipo_callgraph_scc_size(const Callgraph *g, u32 scc);
u32 ipo_callgraph_scc_member(const Callgraph *g, u32 scc, u32 ordinal);
u32 ipo_callgraph_bottom_up_scc(const Callgraph *g, u32 ordinal);
bool opt_ipo(IrModule *m, const OptConfig *cfg);
extern const Pass OPT_PASS_IPO;

typedef struct {
    Arena arena;
    DiagCtx *dc;
} IpoFix;

static void silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void fix_init(IpoFix *f)
{
    DiagSink sink = {silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *parse(IpoFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<ipo-test>");
}

static const IrInst *first_call(const IrFunc *f)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == IR_CALL)
                return in;
    }
    return NULL;
}

void test_opt_ipo_callgraph_direct_indirect_address_and_scc_order(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    Callgraph *g;
    u32 recursive_scc;

    fix_init(&f);
    m = parse(&f, "func void @leaf() internal {\n"
                  "entry():\n"
                  "    ret\n"
                  "}\n"
                  "func void @caller() {\n"
                  "entry():\n"
                  "    call void @leaf()\n"
                  "    ret\n"
                  "}\n"
                  "func void @left() internal {\n"
                  "entry():\n"
                  "    call void @right()\n"
                  "    ret\n"
                  "}\n"
                  "func void @right() internal {\n"
                  "entry():\n"
                  "    call void @left()\n"
                  "    ret\n"
                  "}\n"
                  "func void @escape(ptr %fp) {\n"
                  "entry():\n"
                  "    call void %fp()\n"
                  "    %addr = ptradd @leaf, 0\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    g = m ? ipo_callgraph_build(m) : NULL;
    T_ASSERT(t, g != NULL);
    if (g) {
        T_ASSERT_EQ_INT(t, ipo_callgraph_node_count(g), 5);
        T_ASSERT_EQ_INT(t, ipo_callgraph_edge_count(g, 1), 1);
        T_ASSERT_EQ_INT(t, ipo_callgraph_edge(g, 1, 0), 0);
        T_ASSERT(t, ipo_callgraph_has_unknown_callees(g, 4));
        T_ASSERT(t, ipo_callgraph_address_taken(g, 0));
        T_ASSERT_EQ_INT(t, ipo_callgraph_scc_count(g), 4);
        recursive_scc = ipo_callgraph_scc_of(g, 2);
        T_ASSERT_EQ_INT(t, recursive_scc, ipo_callgraph_scc_of(g, 3));
        T_ASSERT_EQ_INT(t, ipo_callgraph_scc_size(g, recursive_scc), 2);
        T_ASSERT_EQ_INT(t, ipo_callgraph_scc_member(g, recursive_scc, 0), 2);
        T_ASSERT_EQ_INT(t, ipo_callgraph_scc_member(g, recursive_scc, 1), 3);
        T_ASSERT(t, ipo_callgraph_scc_of(g, 0) < ipo_callgraph_scc_of(g, 1));
        T_ASSERT_EQ_INT(t, ipo_callgraph_bottom_up_scc(g, 0), 0);
        T_ASSERT_EQ_INT(t, ipo_callgraph_edge(g, 99, 0), UINT32_MAX);
    }
    ipo_callgraph_free(g);
    arena_free_all(&f.arena);
}

void test_opt_ipo_dead_and_constant_args_and_ignored_return(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *call;

    fix_init(&f);
    m = parse(&f, "func i32 @helper(i32 %x, i32 %dead) internal {\n"
                  "entry():\n"
                  "    %sum = iadd i32 %x, 1\n"
                  "    ret i32 %sum\n"
                  "}\n"
                  "func void @one() {\n"
                  "entry():\n"
                  "    %r = call i32 @helper(i32 7, i32 10)\n"
                  "    ret\n"
                  "}\n"
                  "func void @two() {\n"
                  "entry():\n"
                  "    %r = call i32 @helper(i32 7, i32 20)\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    T_ASSERT(t, m && opt_ipo(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, m->nfuncs, 3);
        T_ASSERT_EQ_INT(t, m->funcs[0].nparams, 0);
        T_ASSERT_EQ_INT(t, m->funcs[0].ret, IRT_VOID);
        T_ASSERT_EQ_INT(t, m->funcs[0].blocks[0].last->nops, 0);
        call = first_call(&m->funcs[1]);
        T_ASSERT(t, call != NULL);
        if (call) {
            T_ASSERT_EQ_INT(t, call->nops, 0);
            T_ASSERT_EQ_INT(t, call->type, IRT_VOID);
            T_ASSERT_EQ_INT(t, call->result.v, 0);
        }
        call = first_call(&m->funcs[2]);
        T_ASSERT(t, call && call->nops == 0 && call->type == IRT_VOID);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_ipo_keeps_differing_constant_argument(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func i32 @pick(i32 %x) internal {\n"
                  "entry():\n"
                  "    ret i32 %x\n"
                  "}\n"
                  "func i32 @one() {\n"
                  "entry():\n"
                  "    %r = call i32 @pick(i32 1)\n"
                  "    ret i32 %r\n"
                  "}\n"
                  "func i32 @two() {\n"
                  "entry():\n"
                  "    %r = call i32 @pick(i32 2)\n"
                  "    ret i32 %r\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    T_ASSERT(t, m && !opt_ipo(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, m->funcs[0].nparams, 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_ipo_dead_static_compacts_and_remaps_calls(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *call;
    const Pass *passes[] = {&OPT_PASS_IPO};

    fix_init(&f);
    m = parse(&f, "global @g size 4 align 4 internal\n"
                  "func void @dead() internal {\n"
                  "entry():\n"
                  "    ret\n"
                  "}\n"
                  "func void @live() internal {\n"
                  "entry():\n"
                  "    ret\n"
                  "}\n"
                  "func void @main() {\n"
                  "entry():\n"
                  "    call void @live()\n"
                  "    %v = load i32, @g, align 4, volatile\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    if (m) {
        m->funcs[1].opt_inline_growth_left = 17;
        m->funcs[1].opt_inline_growth_initialized = true;
        m->funcs[2].opt_inline_growth_left = 23;
        m->funcs[2].opt_inline_growth_initialized = true;
    }
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_run_pass_sequence(m, &cfg, passes, 1));
    if (m) {
        T_ASSERT_EQ_INT(t, m->nfuncs, 2);
        T_ASSERT_EQ_STR(t, m->funcs[0].name, "live");
        T_ASSERT_EQ_INT(t, m->funcs[0].opt_inline_growth_left, 17);
        T_ASSERT(t, m->funcs[0].opt_inline_growth_initialized);
        T_ASSERT_EQ_STR(t, m->funcs[1].name, "main");
        T_ASSERT_EQ_INT(t, m->funcs[1].opt_inline_growth_left, 23);
        T_ASSERT(t, m->funcs[1].opt_inline_growth_initialized);
        call = first_call(&m->funcs[1]);
        T_ASSERT(t, call != NULL);
        if (call)
            T_ASSERT_EQ_INT(t, call->callee, 0);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_ipo_address_taken_and_external_are_untouched(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];
    size_t n;

    fix_init(&f);
    m = parse(&f, "global @table size 8 align 8 internal "
                  "init x0000000000000000 reloc 0 @kept 0\n"
                  "func i32 @kept(i32 %unused) internal {\n"
                  "entry():\n"
                  "    ret i32 1\n"
                  "}\n"
                  "func i32 @visible(i32 %unused) {\n"
                  "entry():\n"
                  "    ret i32 2\n"
                  "}\n"
                  "func void @main() {\n"
                  "entry():\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_ipo(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, m->nfuncs, 3);
        T_ASSERT_EQ_INT(t, m->funcs[0].nparams, 1);
        T_ASSERT_EQ_INT(t, m->funcs[1].nparams, 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(log, 1, sizeof(log) - 1, report);
        log[n] = '\0';
        T_ASSERT(t, strstr(log, "ipo_addr_taken") != NULL);
        T_ASSERT(t, strstr(log, "ipo_external_linkage") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}

void test_opt_ipo_common_linkage_gate_and_descriptor(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[256];
    size_t n;

    fix_init(&f);
    m = parse(&f, "func void @mergeable() {\n"
                  "entry():\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m != NULL);
    if (m)
        m->funcs[0].linkage = IRLINK_COMMON;
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_ipo(m, &cfg));
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(log, 1, sizeof(log) - 1, report);
        log[n] = '\0';
        T_ASSERT(t, strstr(log, "ipo_common_symbol") != NULL);
        fclose(report);
    }
    T_ASSERT_EQ_STR(t, OPT_PASS_IPO.name, "ipo");
    T_ASSERT(t, OPT_PASS_IPO.run == opt_ipo);
    arena_free_all(&f.arena);
}

void test_opt_ipo_variadic_and_abi_return_signatures_are_guarded(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];
    size_t n;

    fix_init(&f);
    m = parse(&f, "func void @var(i32 %unused, ...) internal {\n"
                  "entry():\n"
                  "    ret\n"
                  "}\n"
                  "func void @aggregate(ptr %out, i32 %dead) internal "
                  "abi(sret) {\n"
                  "entry():\n"
                  "    ret\n"
                  "}\n"
                  "func i32 @old(i32 %x) unproto internal {\n"
                  "entry():\n"
                  "    ret i32 %x\n"
                  "}\n"
                  "func i32 @later(i32 %x) internal {\n"
                  "entry():\n"
                  "    ret i32 %x\n"
                  "}\n"
                  "func void @main(ptr %p) {\n"
                  "entry():\n"
                  "    call void @var(i32 1) va\n"
                  "    call void @aggregate(ptr %p sret(16), i32 9)\n"
                  "    %x = call i32 @old()\n"
                  "    %y = call i32 @later() unproto\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && opt_ipo(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, m->funcs[0].nparams, 1);
        T_ASSERT_EQ_INT(t, m->funcs[1].nparams, 1);
        T_ASSERT_EQ_INT(t, m->funcs[1].param_types[0], IRT_PTR);
        T_ASSERT_EQ_INT(t, m->funcs[2].nparams, 1);
        T_ASSERT_EQ_INT(t, m->funcs[3].nparams, 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(log, 1, sizeof(log) - 1, report);
        log[n] = '\0';
        T_ASSERT(t, strstr(log, "ipo_variadic_signature") != NULL);
        T_ASSERT(t, strstr(log, "ipo_abi_return_param") != NULL);
        T_ASSERT(t, strstr(log, "ipo_unprototyped_signature") != NULL);
        T_ASSERT(t, strstr(log, "ipo_unprototyped_call") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}

void test_opt_ipo_preserves_call_arg_annotation(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *nested;

    fix_init(&f);
    m = parse(&f, "func void @helper(i32 %x) internal {\n"
                  "entry():\n"
                  "    call void @sink(i32 %x byval(4))\n"
                  "    ret\n"
                  "}\n"
                  "func void @main() {\n"
                  "entry():\n"
                  "    call void @helper(i32 7)\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    T_ASSERT(t, m && opt_ipo(m, &cfg));
    if (m) {
        nested = first_call(&m->funcs[0]);
        T_ASSERT(t, nested != NULL);
        if (nested) {
            T_ASSERT_EQ_INT(t, nested->ops[0].kind, IROP_ICONST);
            T_ASSERT_EQ_INT(t, nested->ops[0].a, 7);
            T_ASSERT_EQ_INT(t, ir_arg_kind(nested->ops[0].b), IR_ARG_BYVAL);
            T_ASSERT_EQ_INT(t, ir_arg_size(nested->ops[0].b), 4);
        }
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_ipo_preserves_variadic_arg_provenance(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *nested;

    fix_init(&f);
    m = parse(&f, "func void @helper(i32 %x) internal {\n"
                  "entry():\n"
                  "    call void @sink(i32 0, i32 %x anon, i32 9 anon) va\n"
                  "    ret\n"
                  "}\n"
                  "func void @main() {\n"
                  "entry():\n"
                  "    call void @helper(i32 7)\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    T_ASSERT(t, m && opt_ipo(m, &cfg));
    if (m) {
        nested = first_call(&m->funcs[0]);
        T_ASSERT(t, nested != NULL);
        if (nested) {
            T_ASSERT_EQ_INT(t, nested->ops[0].argflags, 0);
            T_ASSERT_EQ_INT(t, nested->ops[1].kind, IROP_ICONST);
            T_ASSERT_EQ_INT(t, nested->ops[1].a, 7);
            T_ASSERT_EQ_INT(t, nested->ops[1].argflags, IROPF_ANON);
            T_ASSERT_EQ_INT(t, nested->ops[2].argflags, IROPF_ANON);
        }
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_ipo_common_global_read_logs_conservative_bail(TestCtx *t)
{
    IpoFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[256];
    size_t n;

    fix_init(&f);
    m = parse(&f, "global @shared size 4 align 4 common tentative\n"
                  "func i32 @read() internal {\n"
                  "entry():\n"
                  "    %v = load i32, @shared, align 4\n"
                  "    ret i32 %v\n"
                  "}\n"
                  "func i32 @main() {\n"
                  "entry():\n"
                  "    %v = call i32 @read()\n"
                  "    ret i32 %v\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_ipo(m, &cfg));
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(log, 1, sizeof(log) - 1, report);
        log[n] = '\0';
        T_ASSERT(t, strstr(log, "ipo_common_symbol") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}
