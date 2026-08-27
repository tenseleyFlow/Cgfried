// FLAGS: -std=gnu17
// EXIT_CODE: 0
struct Inner {
    int value;
};

struct Outer {
    int prefix;
    struct Inner inner;
    int values[2];
};

static struct Outer global = {
    inner: { value: 7 },
    values: { [1] = 9 },
};

int main(void)
{
    struct Outer local = { prefix: 1, inner: { value: 2 } };
    struct Outer compound = (struct Outer){ prefix: 3, values: { 4, 5 } };

    return global.inner.value == 7 && global.values[1] == 9 &&
                   local.prefix == 1 && local.inner.value == 2 &&
                   compound.prefix == 3 && compound.values[0] == 4 &&
                   compound.values[1] == 5
               ? 0
               : 1;
}
