#include <stdio.h>

long load_signed_index(const long *base, int index);
int load_unsigned_index(const int *base, unsigned index);

int main(void)
{
    static const long signed_values[] = {11, 22, 33, 44};
    static const int unsigned_values[] = {5, 10, 15, 20};
    long signed_got = load_signed_index(&signed_values[2], -1);
    int unsigned_got = load_unsigned_index(unsigned_values, 3);

    if (signed_got != 22 || unsigned_got != 20) {
        printf("FAIL signed=%ld unsigned=%d\n", signed_got, unsigned_got);
        return 1;
    }
    puts("OK");
    return 0;
}
