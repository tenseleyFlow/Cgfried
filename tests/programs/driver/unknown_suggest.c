// Unknown flag: error with a did-you-mean when edit distance <= 2.
// FLAGS: -dumpversio
// ERROR_EXPECTED: unrecognized command-line option '-dumpversio'; did you mean '-dumpversion'?
int main(void)
{
    return 0;
}
