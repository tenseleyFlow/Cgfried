// ERROR_EXPECTED: const-qualified
void mutate_name(void)
{
    __func__[0] = 'M';
}
