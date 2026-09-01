// FLAGS: -fsyntax-only
// A linked declaration in an inner scope has the composite type there, while
// the outer declaration's spelling remains unchanged outside that scope.
extern int file_array[10];
extern int old_style();
static char internal_array[6];

void file_scope_cases(void)
{
    extern int file_array[];
    extern int old_style(int);
    int (*fp)(int) = old_style;

    _Static_assert(sizeof(file_array) == 10 * sizeof(int), "file bound");
    (void)fp;
}

void internal_linkage_case(void)
{
    extern char internal_array[];

    _Static_assert(sizeof(internal_array) == 6, "internal bound");
}

void nested_block_case(void)
{
    extern char block_array[10];

    {
        extern char block_array[];
        _Static_assert(sizeof(block_array) == 10, "block bound");
    }
}
