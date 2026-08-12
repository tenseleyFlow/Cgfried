// FLAGS: -O2
// EXIT_CODE: 0
// A large local aggregate takes the memcpy-template path. Its long zero tail
// triggers prefix splitting, but the prefix's relocation is still part of
// the template; dropping it turns the indirect call into a jump to address 0.
struct Big {
    unsigned char prefix[72];
    int (*call)(int);
    unsigned char zero_tail[84];
};

static int add_one(int value)
{
    return value + 1;
}

int main(void)
{
    struct Big value = {.call = add_one};

    return value.call && value.call(41) == 42 && value.zero_tail[83] == 0 ? 0
                                                                          : 1;
}
