// XFAIL(audit): IR-H-08 symbolic indirect calls fail optimized IR round-trip
// A local function pointer remains an indirect call after promotion. The IR
// printer spells its now-symbolic callee like a direct call, so reparsing text
// silently changes the call form and the driver's structural check ICEs.
typedef int (*IrH08Fn)(int);

static int increment(int value)
{
    return value + 1;
}

int main(void)
{
    IrH08Fn fn = increment;

    return fn(41) != 42;
}
