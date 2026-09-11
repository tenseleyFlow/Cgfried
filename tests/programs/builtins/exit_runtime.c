// __builtin_exit uses the hosted libc symbol, converts its argument to int,
// evaluates that argument once, and never reaches the following statement.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 7
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}exit
static int calls;

static unsigned char status(void)
{
    calls++;
    return calls == 1 ? 7 : 9;
}

static int terminate(void)
{
    __builtin_exit(status());
    calls = 99;
}

int main(void)
{
    return terminate();
}
