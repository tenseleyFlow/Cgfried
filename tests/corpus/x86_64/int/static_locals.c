// Local statics persist across calls; internal linkage lands in bss.
// EXIT_CODE: 6
static int bump(void)
{
    static int counter;
    counter += 1;
    return counter;
}
int main(void)
{
    return bump() + bump() + bump();
}
