// EXIT_CODE: 0
#define BE __attribute__((scalar_storage_order("big-endian")))

typedef __CHAR16_TYPE__ char16_type;

struct Wide {
    char16_type text[3];
} BE;

struct Pointer {
    int *p;
    unsigned int value;
} BE;

static int anchor;
static struct Wide global_wide = {u"AB"};
static struct Pointer global_pointer = {&anchor, 0x12345678};

int main(void)
{
    struct Wide local_wide = {u"CD"};
    unsigned char *g = (unsigned char *)&global_wide;
    unsigned char *l = (unsigned char *)&local_wide;

    if (global_wide.text[0] != 'A' || global_wide.text[1] != 'B' ||
        global_wide.text[2] != 0 || g[0] != 0 || g[1] != 'A' || g[2] != 0 ||
        g[3] != 'B' || g[4] != 0 || g[5] != 0)
        return 1;
    if (local_wide.text[0] != 'C' || local_wide.text[1] != 'D' ||
        local_wide.text[2] != 0 || l[0] != 0 || l[1] != 'C' || l[2] != 0 ||
        l[3] != 'D' || l[4] != 0 || l[5] != 0)
        return 2;
    if (global_pointer.p != &anchor || global_pointer.value != 0x12345678)
        return 3;
    return 0;
}
