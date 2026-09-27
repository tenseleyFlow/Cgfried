/* Cgfried compiles this CALLER; clang compiles align32_defs.c.  Apple keeps
 * an anonymous aggregate's 32-byte source alignment in the variadic stack
 * area even though the public SP contract is only 16 bytes.
 *
 * The two VLA sizes move SP by amounts that differ by exactly 16 modulo 32.
 * Therefore one call begins at each possible incoming SP parity: a caller
 * that merely rounds its relative argument offset, without realigning SP,
 * must fail one of them. */
struct __attribute__((aligned(32))) V32 { /* check_bans allow: ABI fixture */
    double a, b, c, d;
};

int read_v32(int skip, ...);

static int invoke(unsigned bytes)
{
    volatile unsigned char pad[bytes];
    struct V32 value = {1.25, 2.5, 4.75, 8.0};
    int rc;

    pad[0] = (unsigned char)bytes;
    rc = read_v32(1, 0, value);
    if (pad[0] != (unsigned char)bytes)
        return 16;
    return rc;
}

/* Keep the two runtime sizes in one uninlined function at every optimization
 * level; otherwise constant propagation can turn the VLAs into unrelated
 * static frames and cease to prove the two-SP-parity invariant. */
static int (*volatile dispatch)(unsigned) = invoke;

int main(void)
{
    int first = dispatch(1);
    int other_parity = dispatch(17);

    return first ? first : other_parity;
}
