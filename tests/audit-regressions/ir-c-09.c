// XFAIL(audit): IR-C-09 16-byte-aligned Linux AAPCS64 composites ignore the even-register rule
// AAPCS64 rule C.10 rounds the next general-purpose register number up to an
// even number before passing a 16-byte-aligned composite.  With `tag` in x0,
// Linux must therefore leave x1 unused and pass `value` in x2:x3.  Cgfried
// currently uses x1:x2.  Apple's arm64 ABI deliberately omits this rule, so
// x1:x2 is the target-specific control rather than a second failure.
//
// The same rule applies when Linux va_arg fetches this type from the general-
// purpose register save area: __gr_offs must first be rounded to a 16-byte
// boundary.  Cgfried advances the unaligned offset directly.
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

static long fixed_sink(long tag, struct pair16 value)
{
    return tag + pair_sum(value);
}

long fixed_probe(void)
{
    struct pair16 value = make_pair(11, 13);
    return fixed_sink(7, value);
}

static long variadic_sink(long tag, ...)
{
    __builtin_va_list ap;
    struct pair16 value;

    __builtin_va_start(ap, tag);
    value = __builtin_va_arg(ap, struct pair16);
    __builtin_va_end(ap);
    return tag + pair_sum(value);
}

int main(void)
{
    struct pair16 value = make_pair(17, 19);
    return fixed_probe() != 31 || variadic_sink(5, value) != 41;
}
