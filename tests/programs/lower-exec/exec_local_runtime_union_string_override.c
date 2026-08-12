// EXIT_CODE: 0
// Selecting a later union member invalidates a deferred store for the old
// member before the character-array string image is emitted.
int target;
static int calls;

static void *runtime_pointer(void)
{
    calls++;
    return &target;
}

union Value {
    void *pointer;
    char text[8];
};

int main(void)
{
    union Value value = {
        .pointer = runtime_pointer(),
        .text = "A",
    };

    if (calls != 0)
        return 1;
    if (value.text[0] != 'A' || value.text[1] != 0)
        return 2;
    return 0;
}
