// FLAGS: -Werror -fno-signed-zeros
// ERROR_EXPECTED: option '-fno-signed-zeros' is bundled-only in v0.1.0; see docs/fast-math.md
int main(void)
{
    return 0;
}
