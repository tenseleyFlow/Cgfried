// The GNU type-query extensions name their sprint.
// ERROR_EXPECTED: lands in Sprint 55
int f(void)
{
    return __builtin_types_compatible_p(int, long);
}
