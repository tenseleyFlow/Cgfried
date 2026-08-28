// FLAGS: -std=gnu17
// EXIT_CODE: 0

int aligned_object __attribute__((aligned(32)));

struct holder {
    char lead;
    int member __attribute__((aligned(16)));
};

struct __attribute__((packed)) packed_holder {
    char lead;
    int member;
};

static int calls;

static int side_effect(void)
{
    calls++;
    return 1;
}

_Static_assert(__alignof__(void) == 1, "GNU void alignment");
_Static_assert(__alignof__(int(void)) == 1, "GNU function-type alignment");
_Static_assert(__alignof__(side_effect) == 1,
               "GNU function-expression alignment");
_Static_assert(__alignof__((aligned_object)) == 32,
               "parentheses preserve declaration alignment");
_Static_assert(__alignof__(((struct holder *)0)->member) == 16,
               "member alignment is observable through an expression");
_Static_assert(__alignof__(((struct packed_holder *)0)->member) == 1,
               "packed members lower expression alignment");

int main(void)
{
    int local;

    if (__alignof__ local != _Alignof(int))
        return 1;
    if (__alignof__(side_effect()) != _Alignof(int))
        return 2;
    if (calls != 0)
        return 3;
    return 0;
}
