// EXIT_CODE: 0
// GNU C accepted 0b integer spellings before C23 standardized them.  Chibicc
// uses them throughout its UTF-8 codec, including unsigned-width boundaries.
int main(void)
{
    unsigned int high = 0B10000000000000000000000000000000;

    return 0b101010 == 42 && 0b111u == 7u && high == 0x80000000u ? 0 : 1;
}
