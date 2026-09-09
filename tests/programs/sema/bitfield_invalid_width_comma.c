// FLAGS: -fsyntax-only
// ERROR_EXPECTED: 'bad_width' undeclared
typedef unsigned long ull;

struct widths {
    ull u40 : bad_width;
};

struct widths value;

void recover(void)
{
    (void)(0, value.u40);
}
