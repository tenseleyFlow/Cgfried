// EXIT_CODE: 0
static const unsigned char table[] = {"\000ip\0\377raw"};

int main(void)
{
    unsigned char local[] = {"abc"};

    if (sizeof table != 9 || table[0] != 0 || table[1] != 'i' ||
        table[4] != 255 || table[7] != 'w' || table[8] != 0)
        return 1;
    if (sizeof local != 4 || local[0] != 'a' || local[2] != 'c' ||
        local[3] != 0)
        return 2;
    return 0;
}
