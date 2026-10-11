// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1
/* A braced array-of-pointers member still initializes pointer elements.  The
 * nested braces do not turn a string literal into a character-array image in
 * the first pointer slot.  OpenSSL's redirect table exposed the wrong image
 * as the invalid pointer whose little-endian bytes spell "http://s". */
int printf(const char *, ...);

struct redirect_case {
    const char *url;
    const char *redirects[4];
    int success;
};

static const struct redirect_case cases[] = {
    {"https://server/start", {"http://server/end"}, 0},
    {"http://server/start", {"/relative", "https://server/end"}, 1},
};

static int equal(const char *left, const char *right)
{
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static int valid_string(const char *value, const char *expected)
{
    /* Avoid dereferencing the known corrupt image so the regression reports
     * an ordinary failure instead of a target-dependent crash. */
    if ((unsigned long)value == 0x732f2f3a70747468ul)
        return 0;
    return equal(value, expected);
}

int main(void)
{
    int first = valid_string(cases[0].redirects[0], "http://server/end") &&
                cases[0].redirects[1] == 0;
    int second = valid_string(cases[1].redirects[0], "/relative") &&
                 valid_string(cases[1].redirects[1], "https://server/end") &&
                 cases[1].redirects[2] == 0;
    int fields = cases[0].success == 0 && cases[1].success == 1 &&
                 valid_string(cases[1].url, "http://server/start");

    printf("%d %d %d\n", first, second, fields);
    return !(first && second && fields);
}
