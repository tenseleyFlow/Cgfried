// FLAGS: -std=gnu17
// OPT_EQ: all
// EXIT_CODE: 0
/* A read-write output appends a tied input internally, but its lvalue
 * expression is still one C operand and must be evaluated exactly once. */
static int calls;
static int value = 37;

static int *target(void)
{
    calls++;
    return &value;
}

int main(void)
{
    __asm__("" : "+r"(*target()));
    if (calls != 1)
        return 1;
    return value != 37 ? 2 : 0;
}
