#ifndef CGF_TESTS_DEBUG_DWARF_LINES_H
#define CGF_TESTS_DEBUG_DWARF_LINES_H

static int s29_header_step(int value)
{
    return value + 2; /* A2L: header-step */
}

#define S29_MACRO_STEP(value) s29_leaf((value) + 3)

#endif
