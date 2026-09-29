// FLAGS: -std=gnu17
// EXIT_CODE: 0
/* The intentionally narrow vector_size boundary: one 128-bit integer lane
 * carried in one 16-byte SIMD register.  This is GCC torture pr105613 plus a
 * return-value round trip, so caller and callee halves of the ABI stay live. */
typedef unsigned __int128 __attribute__((__vector_size__(16))) V;
typedef __int128 __attribute__((vector_size(16))) SV;

static V mask(V value) { return value != 0; }
static V echo(V value) { return value; }

int main(void)
{
    V value = (V){0x500000005ULL};
    V result = mask(echo(value));
    SV signed_value = (SV){-7};

    if (sizeof(V) != 16 || _Alignof(V) != 16)
        return 1;
    if (result[0] != ~(unsigned __int128)0)
        return 2;
    if (mask((V){0})[0] != 0)
        return 3;
    if (echo(value)[0] != (unsigned __int128)0x500000005ULL)
        return 4;
    if (signed_value[0] != -7)
        return 5;
    if (__builtin_classify_type(value) != 19)
        return 6;
    return 0;
}
