void reject_integer_semantics(void)
{
    __uint128_t value = {0};
    (void)(value + value);
}
