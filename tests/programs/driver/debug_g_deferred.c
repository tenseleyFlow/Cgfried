// -g (all spellings: the joined family covers -ggdb/-g3 too) hard-errors
// until Sprint 29 lands line tables.
// FLAGS: -g
// ERROR_EXPECTED: option '-g' lands in Sprint 29
int main(void)
{
    return 0;
}
