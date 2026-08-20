// XFAIL(audit): IR-C-10 stacked 16-byte-aligned composites lose AAPCS64 stack alignment
// Eight scalar arguments occupy x0-x7 and `stacked` occupies the first stack
// slot.  AAPCS64 then rounds the stack offset from 8 to 16 before placing the
// 16-byte-aligned composite, whose two eightbytes belong at sp+16 and sp+24.
// Cgfried's caller stores them at sp+8 and sp+16, and its callee reads the same
// wrong offsets.  Both the Linux and Apple arm64 ABIs require this stack
// alignment even though they differ on the register rule covered by IR-C-09.
struct pair16 {
    _Alignas(16) long first;
    long second;
};

static struct pair16 make_pair(long first, long second)
{
    struct pair16 value;
    value.first = first;
    value.second = second;
    return value;
}

static long pair_sum(struct pair16 value)
{
    return value.first + value.second;
}

static long stacked_sink(long a0, long a1, long a2, long a3, long a4,
                         long a5, long a6, long a7, long stacked,
                         struct pair16 value)
{
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + stacked +
           pair_sum(value);
}

long stacked_probe(void)
{
    struct pair16 value = make_pair(10, 11);
    return stacked_sink(0, 1, 2, 3, 4, 5, 6, 7, 9, value);
}

int main(void) { return stacked_probe() != 58; }
