// OPT_EQ: all
// Exit-code plumbing, the whole way down.
// EXIT_CODE: 42
// ASM_CHECK(x86_64-linux-gnu): movl $42
int main(void)
{
    return 42;
}
