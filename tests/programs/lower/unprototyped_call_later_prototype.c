// A call is constrained by the declaration visible at that point, not by a
// prototype introduced later in the translation unit. The first call may
// therefore have a different arity and carries explicit IR provenance; the
// second call sees the final prototype and remains strict.
// FLAGS: -emit-ir
// IR_CHECK: call i32 @pick() unproto
// IR_CHECK: call i32 @pick(i32 7)
static int pick();

int before(void) { return pick(); }

static int pick(int value) { return value; }

int after(void) { return pick(7); }
