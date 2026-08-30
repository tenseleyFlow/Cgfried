// FLAGS: -std=gnu17
// OPT_EQ: all
// EXIT_CODE: 0
/* GCC folds a non-volatile, top-level const scalar object's initializer when
 * its value appears in a later static initializer. Exercise arithmetic and
 * pointer values, chained constants, scalar braces, and block-scope const. */
const char small = 0x42;
const double promoted = (double)small;
double arithmetic = 1 + small;

static char text[] = "const-object";
char *const pointer = text;
static char *pointer_copy = pointer;

static int values[8];
const int offset = 3;
static int *offset_pointer = values + offset;

const int braced = { 5 };
const int chain = braced + 2;

static int local_static(void)
{
    const int automatic = 11;
    static const int copied = automatic;

    return copied;
}

int main(void)
{
    if (promoted != 66.0 || arithmetic != 67.0)
        return 1;
    if (pointer_copy != text || *pointer_copy != 'c')
        return 2;
    if (offset_pointer != &values[3])
        return 3;
    if (chain != 7 || local_static() != 11)
        return 4;
    return 0;
}
