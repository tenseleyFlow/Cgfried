// FLAGS: -std=gnu89
// EXIT_CODE: 0

static const unsigned short file_values[] =
    (unsigned short[]){0x0D2B, 0x1234};

struct Pair {
    int values[2];
};

static struct Pair nested = {(int[]){7, 9}};

int main(void)
{
    static const int block_values[] = (const int[]){3, 5, 8};

    if (sizeof(file_values) / sizeof(file_values[0]) != 2)
        return 1;
    if (file_values[0] != 0x0D2B || file_values[1] != 0x1234)
        return 2;
    if (sizeof(block_values) / sizeof(block_values[0]) != 3)
        return 3;
    if (block_values[0] != 3 || block_values[1] != 5 || block_values[2] != 8)
        return 4;
    if (nested.values[0] != 7 || nested.values[1] != 9)
        return 5;
    return 0;
}
