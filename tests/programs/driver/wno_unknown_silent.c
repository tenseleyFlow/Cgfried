// -Wno-<unknown> is silently accepted (gcc parity — configure probes
// depend on it). It is retained only for gcc's deferred note if another
// diagnostic later fires.
// FLAGS: -Wno-bogus-flag-that-does-not-exist
// EXIT_CODE: 5
int main(void)
{
    return 5;
}
