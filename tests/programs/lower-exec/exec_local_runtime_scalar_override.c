// EXIT_CODE: 0
// A later constant initializer supersedes an earlier deferred store to the
// same automatic struct member, including its runtime side effect.
static int calls;
static int pointer_calls;
static int target;

static int runtime_value(void)
{
    calls++;
    return 7;
}

static int *runtime_pointer(void)
{
    pointer_calls++;
    return &target;
}

int main(void)
{
    struct Value {
        int member;
    } value = {
        .member = runtime_value(),
        .member = 3,
    };
    struct Pointer {
        int *member;
    } pointer = {
        .member = runtime_pointer(),
        .member = &target,
    };

    if (calls != 0)
        return 1;
    if (value.member != 3)
        return 2;
    if (pointer_calls != 0)
        return 3;
    if (pointer.member != &target)
        return 4;
    return 0;
}
