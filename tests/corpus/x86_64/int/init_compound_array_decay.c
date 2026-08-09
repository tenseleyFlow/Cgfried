// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 2 18 52 4660 1 2 3 4
/* A COMPOUND LITERAL OF ARRAY TYPE, initializing a pointer member, inside
 * ANOTHER compound literal, inside a local aggregate initializer. That
 * nesting is the whole bug: the flat form
 *
 *     struct iovec v = { .iov_base = (u8[]){ a, b }, .iov_len = 2 };
 *
 * lowers fine, because sema materializes the array-to-pointer decay as a
 * cast node and lowering has a rule for that at AST_EXPR_CAST. One level
 * deeper the decay arrives with no cast node, lower_scalar_convert was handed
 * from->kind == TY_ARRAY, and the compiler ICEd in lower_irtype.
 *
 * The rule now lives in lower_scalar_convert itself, so every caller inherits
 * it rather than the two that happened to be known.
 *
 * PRE-EXISTING and reachable by ordinary C -- no asm, no attributes, no
 * extension of any kind. It went unnoticed because no corpus program nested
 * compound literals this way; musl's res_msend.c does, and it only started
 * reaching lowering when extended asm made its includes parse. Every
 * expectation below is gcc-verified. */
extern int printf(const char *, ...);

typedef unsigned char u8;
struct iovec {
    void *iov_base;
    unsigned long iov_len;
};
struct msghdr {
    void *msg_name;
    struct iovec *msg_iov;
    int msg_iovlen;
};

static unsigned long header_len(const u8 *q, int ql)
{
    struct msghdr mh = {.msg_name = 0,
                        .msg_iov =
                            (struct iovec[2]){
                                {.iov_base = (u8[]){ql >> 8, ql},
                                 .iov_len = 2},
                                {.iov_base = (void *)q,
                                 .iov_len = (unsigned long)ql},
                            },
                        .msg_iovlen = 2};
    const u8 *hdr = mh.msg_iov[0].iov_base;
    const u8 *body = mh.msg_iov[1].iov_base;

    /* Read BOTH literals back: an address that merely lowered without ICEing
     * can still point at the wrong bytes, which no compile-only check sees. */
    printf("%lu %d %d %lu %d %d %d %d\n", mh.msg_iov[0].iov_len, hdr[0], hdr[1],
           mh.msg_iov[1].iov_len, body[0], body[1], body[2], body[3]);
    return mh.msg_iov[0].iov_len;
}

int main(void)
{
    static const u8 buf[4] = {1, 2, 3, 4};

    return header_len(buf, 0x1234) == 2 ? 0 : 1;
}
