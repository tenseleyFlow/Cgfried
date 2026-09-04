// UTF-8 source bytes remain bytes in ordinary and u8 literals, but wide,
// UTF-16, and UTF-32 literals decode them into Unicode scalar values.
typedef __WCHAR_TYPE__ wchar_type;
typedef __CHAR16_TYPE__ char16_type;
typedef __CHAR32_TYPE__ char32_type;
typedef __UINTPTR_TYPE__ uintptr_type;

static unsigned char ordinary[] = "¢你😀";
static unsigned char utf8[] = u8"¢你😀";
static wchar_type wide[] = L"¢你😀";
static char16_type utf16[] = u"¢你😀";
static char32_type utf32[] = U"¢你😀";
static wchar_type mixed[] = "¢"
                            L"你😀";
static char *pool_pad = "x";
static wchar_type *wide_pointer = L"¢你😀";

static int bytes_are_utf8(const unsigned char *s)
{
    static const unsigned char want[] = {
        0xC2, 0xA2, 0xE4, 0xBD, 0xA0, 0xF0, 0x9F, 0x98, 0x80, 0,
    };
    unsigned i;

    for (i = 0; i < sizeof(want); i++) {
        if (s[i] != want[i])
            return 0;
    }
    return 1;
}

static int scalars_are_wide(const wchar_type *s)
{
    return s[0] == 0xA2 && s[1] == 0x4F60 && s[2] == 0x1F600 && s[3] == 0;
}

int main(void)
{
    wchar_type local_wide[] = L"¢你😀";
    wchar_type local_wide_padded[5] = L"¢你😀";
    char16_type local_utf16[] = u"¢你😀";

    if (sizeof(ordinary) != 10 || !bytes_are_utf8(ordinary))
        return 1;
    if (sizeof(utf8) != 10 || !bytes_are_utf8(utf8))
        return 2;
    if (sizeof(wide) != 4 * sizeof(wchar_type) || !scalars_are_wide(wide))
        return 3;
    if (sizeof(utf16) != 5 * sizeof(char16_type) || utf16[0] != 0xA2 ||
        utf16[1] != 0x4F60 || utf16[2] != 0xD83D || utf16[3] != 0xDE00 ||
        utf16[4] != 0)
        return 4;
    if (sizeof(utf32) != 4 * sizeof(char32_type) || utf32[0] != 0xA2 ||
        utf32[1] != 0x4F60 || utf32[2] != 0x1F600 || utf32[3] != 0)
        return 5;
    if (sizeof(mixed) != 4 * sizeof(wchar_type) || !scalars_are_wide(mixed))
        return 6;
    if (L'¢' != 0xA2 || u'你' != 0x4F60 || U'😀' != 0x1F600)
        return 7;
    if (*pool_pad != 'x' || !scalars_are_wide(wide_pointer) ||
        (uintptr_type)wide_pointer % _Alignof(wchar_type) != 0)
        return 8;
    if (sizeof(local_wide) != 4 * sizeof(wchar_type) ||
        !scalars_are_wide(local_wide))
        return 9;
    if (sizeof(local_utf16) != 5 * sizeof(char16_type) ||
        local_utf16[0] != 0xA2 || local_utf16[1] != 0x4F60 ||
        local_utf16[2] != 0xD83D || local_utf16[3] != 0xDE00 ||
        local_utf16[4] != 0)
        return 10;
    if (sizeof(local_wide_padded) != 5 * sizeof(wchar_type) ||
        !scalars_are_wide(local_wide_padded) || local_wide_padded[4] != 0)
        return 11;
    return 0;
}
