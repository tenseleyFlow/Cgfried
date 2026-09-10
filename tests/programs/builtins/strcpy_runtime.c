// __builtin_strcpy has the hosted libc signature and preserves both argument
// evaluations exactly once. Its result is the original destination pointer.
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}strcpy
static char buffer[16];
static int destination_calls;
static int source_calls;

static char *destination(void)
{
    destination_calls++;
    return buffer;
}

static const char *source(const char *s)
{
    source_calls++;
    return s;
}

int main(void)
{
    char *result = __builtin_strcpy(destination(), source("abcde"));

    if (result != buffer)
        return 1;
    if (destination_calls != 1 || source_calls != 1)
        return 2;
    if (__builtin_strcmp(buffer, "abcde") != 0)
        return 3;
    if (__builtin_strcpy(buffer, "") != buffer)
        return 4;
    if (buffer[0] != 0)
        return 5;
    return 0;
}
