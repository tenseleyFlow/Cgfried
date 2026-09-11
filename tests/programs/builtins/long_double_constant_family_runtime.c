// The long-double suffixes use the source target's format: x87-80 on x86,
// IEEE binary128 on arm64-linux, and binary64 on arm64-macos.
// EXIT_CODE: 0

static long double static_inf = __builtin_infl();
static long double static_huge = __builtin_huge_vall();
static long double static_nan = __builtin_nanl("0x1");

static long double runtime_inf(void)
{
    return __builtin_infl();
}

static long double runtime_huge(void)
{
    return __builtin_huge_vall();
}

static long double runtime_nan(void)
{
    return __builtin_nanl("");
}

int main(void)
{
    long double inf = runtime_inf();
    long double huge = runtime_huge();
    long double nan = runtime_nan();

    _Static_assert(_Generic(__builtin_infl(), long double: 1, default: 0),
                   "infl returns long double");
    _Static_assert(_Generic(__builtin_huge_vall(), long double: 1, default: 0),
                   "huge_vall returns long double");
    _Static_assert(_Generic(__builtin_nanl(""), long double: 1, default: 0),
                   "nanl returns long double");

    if (inf != huge || static_inf != static_huge)
        return 1;
    if (!(inf > __LDBL_MAX__) || !(static_inf > __LDBL_MAX__))
        return 2;
    if (!(nan != nan) || !(static_nan != static_nan))
        return 3;
    return 0;
}
