// EXIT_CODE: 0
// A later union member initializer replaces the earlier representation,
// including an address relocation overlapped by a character-array string.
int target;

union Value {
    void *pointer;
    char text[8];
};

int main(void)
{
    union Value value = {
        .pointer = &target,
        .text = "A",
    };

    if (value.text[0] != 'A' || value.text[1] != 0)
        return 1;
    return 0;
}
