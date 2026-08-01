#include "dwarf_lines.h"

static int s29_leaf(int value);
static int s29_remapped(int value);

static int s29_leaf(int value)
{
    int result = value + 1; /* A2L: leaf-body */
    return result;          /* GDB_LINE: leaf-return */
}

static int s29_middle(int value)
{
    return S29_MACRO_STEP(s29_header_step(value)); /* A2L: macro-call */
}

static int s29_top(int value)
{
    int result = s29_middle(value); /* A2L: top-call */
    return result + 1;              /* GDB_LINE: top-return */
}

int main(void)
{
    int value = s29_top(1);      /* GDB_LINE: main-first */
    int mapped = s29_remapped(1); /* GDB_LINE: main-next */
    return value == 8 && mapped == 5 ? 0 : 1; /* A2L: main-return */
}

#line 200 "virtual/s29_remapped.c"
static int s29_remapped(int value)
{
    return value + 4; /* A2L: remapped-body */
}
