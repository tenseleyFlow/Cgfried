#include <string.h>

#include "ir/ir.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
} BoundsFix;

static void bounds_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    BoundsFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        f->errors++;
}

static void bounds_init(BoundsFix *f)
{
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = bounds_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
}

void test_ir_bounds_marker_roundtrip(TestCtx *t)
{
    static const char source[] = "func i32 @f(i32 %i, i32 %n) {\n"
                                 "entry():\n"
                                 "    %ok = icmp ult i32 %i, %n, bounds\n"
                                 "    ret i32 %ok\n"
                                 "}\n";
    BoundsFix f;
    IrModule *m, *round;
    Buf text;

    bounds_init(&f);
    m = ir_parse_module(&f.arena, f.dc, source, "bounds.cgfir");
    T_ASSERT(t, m != NULL);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && (m->funcs[0].blocks[0].first->flags & IRF_BOUNDS_CHECK));
    buf_init(&text);
    if (m)
        ir_print_module_buf(&text, m);
    buf_push_u8(&text, 0);
    T_ASSERT(t, strstr((const char *)text.data, ", bounds") != NULL);
    round = ir_parse_module(&f.arena, f.dc, (const char *)text.data,
                            "bounds-round.cgfir");
    T_ASSERT(t, round != NULL);
    T_ASSERT(t, m && round && ir_module_struct_eq(m, round));
    buf_free(&text);
    arena_free_all(&f.arena);
}

void test_ir_bounds_marker_invalid_placement_rejected(TestCtx *t)
{
    BoundsFix f;
    IrModule *m;
    IrFunc *fn;
    IrBuilder b;
    BlockId entry;

    bounds_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "f", IRT_VOID, NULL, 0);
    entry = ir_block_new(m, fn, "entry");
    ir_builder_at(&b, m, fn, entry);
    (void)ir_build2(&b, IR_IADD, IRT_I32, ir_op_iconst(IRT_I32, 1),
                    ir_op_iconst(IRT_I32, 2));
    fn->blocks[0].first->flags |= IRF_BOUNDS_CHECK;
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, f.errors > 0);
    arena_free_all(&f.arena);
}
