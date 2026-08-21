// IR-C-01: a zero-width struct bitfield is an allocation barrier, not an
// occupied INTEGER ABI class. Exercise the f80 return and memory argument in
// one self-hosted runtime path; the ABI differential pins mixed compilers.
// CHECK: abi_f80_zero_width: ok
#include <stdio.h>

struct Value {
    int :0;
    long double value;
};

static struct Value make_value(long double value)
{
    struct Value out = {value};
    return out;
}

static long double read_value(struct Value value) { return value.value; }

int main(void)
{
    struct Value value = make_value(2.5L);

    if (read_value(value) != 2.5L)
        return 1;
    puts("abi_f80_zero_width: ok");
    return 0;
}
