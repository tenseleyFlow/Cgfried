// Duff's device at runtime: the interleaved switch/loop copies exactly.
// EXIT_CODE: 0
static void duffcpy(char *to, const char *from, int count)
{
    int n = (count + 7) / 8;
    switch (count % 8) {
    case 0:
        do {
            *to++ = *from++;
        case 7:
            *to++ = *from++;
        case 6:
            *to++ = *from++;
        case 5:
            *to++ = *from++;
        case 4:
            *to++ = *from++;
        case 3:
            *to++ = *from++;
        case 2:
            *to++ = *from++;
        case 1:
            *to++ = *from++;
        } while (--n > 0);
    }
}
int main(void)
{
    char src[19], dst[19];
    int i;
    for (i = 0; i < 19; i++) {
        src[i] = (char)(i * 3 + 1);
        dst[i] = 0;
    }
    duffcpy(dst, src, 19);
    for (i = 0; i < 19; i++)
        if (dst[i] != src[i])
            return 1;
    return 0;
}
