// FLAGS: -fsyntax-only
int fold_signed_min_no_crash(void)
{
    if ((-9223372036854775807LL - 1) / -1)
        return 1;
    return 0;
}
