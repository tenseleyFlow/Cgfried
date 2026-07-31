// -o with -c and MULTIPLE compiled inputs: the error text is verbatim
// from the sprint contract (the runner supplies the -o; the aux file
// makes it two inputs).
// FLAGS: -c tests/fixtures/driver/aux_two.c
// ERROR_EXPECTED: cannot specify -o with -c, -S or -E with multiple files
int main(void)
{
    return 0;
}
