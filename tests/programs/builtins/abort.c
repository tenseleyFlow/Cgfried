// __builtin_abort has hosted abort semantics: it links through the real
// library symbol rather than becoming __builtin_trap. Keep the branch live
// in generated code without taking it in this positive execution fixture.

volatile int trigger_abort;

static void fatal(void)
{
    __builtin_abort();
}

int main(void)
{
    if (trigger_abort)
        fatal();
    return 0;
}
