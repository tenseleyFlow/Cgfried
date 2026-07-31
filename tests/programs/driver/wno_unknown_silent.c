// -Wno-<unknown> is silently accepted (gcc parity — configure probes
// depend on it); the unit suite pins that nothing lands in the warn
// list either.
// FLAGS: -Wno-bogus-flag-that-does-not-exist
// EXIT_CODE: 5
int main(void)
{
    return 5;
}
