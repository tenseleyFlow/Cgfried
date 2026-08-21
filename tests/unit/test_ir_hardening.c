#include <string.h>

#include "ir/ir.h"
#include "unit.h"
#include "util/arena.h"

/* Sprint 20 hardening units: the volatile-count tripwire (including a
 * deliberately-broken "pass"), setjmp flag consistency, stackrestore
 * token discipline, atomic discipline, and the self-reference dominance
 * pins. Modules arrive as text — the parser is the pass-test front
 * door. */

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
    char msg[256];
} HFix;

static void h_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    HFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR) {
        if (f->errors == 0) {
            strncpy(f->msg, d->message, sizeof(f->msg) - 1);
            f->msg[sizeof(f->msg) - 1] = '\0';
        }
        f->errors++;
    }
}

static IrModule *h_parse(HFix *f, const char *src)
{
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = h_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
    return ir_parse_module(&f->arena, f->dc, src, "<h>");
}

void test_volatile_count_tripwire(TestCtx *t)
{
    HFix f;
    IrModule *m = h_parse(&f, "func void @f(ptr %p) {\n"
                              "entry():\n"
                              "    %a = load i32, %p, align 4, volatile\n"
                              "    store i32 %a, %p, align 4, volatile\n"
                              "    %b = load i32, %p, align 4\n"
                              "    ret\n"
                              "}\n");
    u32 before[1];
    u32 bad = 99;

    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT_EQ_INT(t, ir_count_volatile_ops(&m->funcs[0]), 2);
    ir_snapshot_volatile(m, before);
    T_ASSERT(t, ir_volatile_counts_match(m, before, &bad));
    /* The deliberately-broken pass: dropping a volatile flag must trip
     * the snapshot compare naming the function. */
    m->funcs[0].blocks[0].first->flags = 0;
    T_ASSERT(t, !ir_volatile_counts_match(m, before, &bad));
    T_ASSERT_EQ_INT(t, bad, 0);
    arena_free_all(&f.arena);
}

void test_setjmp_flag_consistency(TestCtx *t)
{
    HFix f;
    IrModule *m;
    static const char *const names[] = {"setjmp", "_setjmp", "sigsetjmp",
                                        "__sigsetjmp"};
    static const char *const near_names[] = {"setjmpx", "__sigsetjmp_chk",
                                             "my_sigsetjmp"};
    int i;
    char src[256];

    /* Every exact returns-twice identity satisfies check 11 when marked. */
    for (i = 0; i < 4; i++) {
        snprintf(src, sizeof(src),
                 "func void @f(ptr %%b) setjmp {\n"
                 "entry():\n"
                 "    %%r = call i32 @%s(ptr %%b)\n"
                 "    ret\n"
                 "}\n",
                 names[i]);
        m = h_parse(&f, src);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        T_ASSERT(t, m->funcs[0].calls_setjmp);
        arena_free_all(&f.arena);
    }
    /* Near names neither require nor justify the marker. */
    for (i = 0; i < 3; i++) {
        snprintf(src, sizeof(src),
                 "func void @f(ptr %%b) {\n"
                 "entry():\n"
                 "    %%r = call i32 @%s(ptr %%b)\n"
                 "    ret\n"
                 "}\n",
                 near_names[i]);
        m = h_parse(&f, src);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        T_ASSERT(t, !m->funcs[0].calls_setjmp);
        arena_free_all(&f.arena);

        snprintf(src, sizeof(src),
                 "func void @f(ptr %%b) setjmp {\n"
                 "entry():\n"
                 "    %%r = call i32 @%s(ptr %%b)\n"
                 "    ret\n"
                 "}\n",
                 near_names[i]);
        m = h_parse(&f, src);
        T_ASSERT(t, m && !ir_verify(f.dc, m));
        T_ASSERT(t, strstr(f.msg, "ir verify [11]") != NULL);
        arena_free_all(&f.arena);
    }
    /* ...an UNMARKED caller fails check 11 in the other direction. */
    m = h_parse(&f, "func void @f(ptr %b) {\n"
                    "entry():\n"
                    "    %r = call i32 @setjmp(ptr %b)\n"
                    "    ret\n"
                    "}\n");
    T_ASSERT(t, m && !ir_verify(f.dc, m));
    T_ASSERT(t, strstr(f.msg, "ir verify [11]") != NULL);
    arena_free_all(&f.arena);
}

void test_stackrestore_token_discipline(TestCtx *t)
{
    HFix f;
    IrModule *m = h_parse(&f, "func void @f() {\n"
                              "entry():\n"
                              "    %tok = stacksave\n"
                              "    %p = alloca %tok, align 8\n"
                              "    stackrestore %tok\n"
                              "    ret\n"
                              "}\n");

    /* A real token verifies... (the alloca consumes it as a size only to
     * keep the fixture small; the restore is what is under test) */
    T_ASSERT(t, m != NULL);
    if (m) {
        /* size operand is a ptr — swap for a legal i64 first */
        m->funcs[0].blocks[0].first->next->ops[0] = ir_op_iconst(IRT_I64, 8);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);

    /* ...a forged token (an alloca result) fails check 12. */
    m = h_parse(&f, "func void @f() {\n"
                    "entry():\n"
                    "    %p = alloca 8, align 8\n"
                    "    stackrestore %p\n"
                    "    ret\n"
                    "}\n");
    T_ASSERT(t, m && !ir_verify(f.dc, m));
    T_ASSERT(t, strstr(f.msg, "ir verify [12]") != NULL);
    arena_free_all(&f.arena);
}

void test_atomic_discipline_and_selfref(TestCtx *t)
{
    HFix f;
    IrModule *m;

    /* Float atomicrmw: check 13. */
    m = h_parse(&f, "func void @f(ptr %p) {\n"
                    "entry():\n"
                    "    %o = atomicrmw add f64 %p, "
                    "0x3FF0000000000000, seq_cst\n"
                    "    ret\n"
                    "}\n");
    T_ASSERT(t, m && !ir_verify(f.dc, m));
    T_ASSERT(t, strstr(f.msg, "ir verify [13]") != NULL);
    arena_free_all(&f.arena);

    /* A missing seq_cst: also check 13. */
    m = h_parse(&f, "func void @f(ptr %p) {\n"
                    "entry():\n"
                    "    %o = cmpxchg i32 %p, 0, 1\n"
                    "    ret\n"
                    "}\n");
    T_ASSERT(t, m && !ir_verify(f.dc, m));
    T_ASSERT(t, strstr(f.msg, "ir verify [13]") != NULL);
    arena_free_all(&f.arena);

    /* Self-reference dominance: a block passing its own param on a
     * backedge is LEGAL and verifies. */
    m = h_parse(&f, "func void @f(i32 %c) {\n"
                    "entry():\n"
                    "    br loop(i32 0)\n"
                    "loop(i32 %i):\n"
                    "    condbr %c, again(), out()\n"
                    "again():\n"
                    "    br loop(i32 %i)\n"
                    "out():\n"
                    "    ret\n"
                    "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    arena_free_all(&f.arena);
}

void test_verify_report_summary(TestCtx *t)
{
    HFix f;
    IrModule *m = h_parse(&f, "func void @broken() {\n"
                              "entry():\n"
                              "    %p = alloca 8, align 6\n"
                              "    ret\n"
                              "}\n");
    char why[128];

    T_ASSERT(t, m != NULL);
    if (m) {
        T_ASSERT(t, !ir_verify_report(f.dc, m, why, sizeof(why)));
        T_ASSERT(t, strstr(why, "check 8") != NULL);
        T_ASSERT(t, strstr(why, "@broken") != NULL);
    }
    arena_free_all(&f.arena);
}
