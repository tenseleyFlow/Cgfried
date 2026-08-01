// OPT_EQ: all
// 6 dense cases: the jump table, executed.  The volatile function pointer
// keeps this a backend-shape fixture after Sprint 33's direct-call inliner.
// EXIT_CODE: 21
// ASM_CHECK(x86_64-linux-gnu): jmp *(
static int pick(int n)
{
    switch (n) {
    case 0:
        return 1;
    case 1:
        return 2;
    case 2:
        return 4;
    case 3:
        return 8;
    case 4:
        return 16;
    case 5:
        return 32;
    default:
        return 0;
    }
}

static int (*volatile pick_fn)(int) = pick;

int main(void)
{
    return pick_fn(0) + pick_fn(2) + pick_fn(4) + pick_fn(9);
}
