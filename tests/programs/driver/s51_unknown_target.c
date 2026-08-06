// FLAGS: --target=riscv64-linux -fsyntax-only
// ERROR_EXPECTED: unknown target 'riscv64-linux'
// The five names are matched EXACTLY -- there is no triple parser. A parser
// that half-understands an unfamiliar triple and degrades to a default would
// cross-compile silently for the wrong machine, which is the failure a
// closed enum exists to prevent. The diagnostic lists what IS known,
// because picking the wrong target is the expensive mistake here.
int main(void)
{
    return 0;
}
