// A .h listed among the inputs must reach the COMPILER, never the linker.
//
// Reported by a user: `cgf -Wall main.c keytab.h -o keytab` failed where the
// identical gcc command line worked, with
//     /usr/bin/ld:keytab.h: file format not recognized;
//         treating as linker script
// because the driver had no `.h` row in its extension dispatch and the
// catch-all sends anything unrecognized to the link stream in position.
//
// gcc compiles a .h as a header to PRECOMPILE and hands only the .c objects
// to the linker -- `gcc -### main.c keytab.h` shows two cc1 runs and one .o
// on the collect2 line. We do not implement precompiled headers, so the
// header is CHECKED and produces nothing; what this fixture pins is that the
// program still builds and runs, which is the half a user notices.
// FLAGS: tests/fixtures/driver/aux_header.h
// EXIT_CODE: 42
#include "../../fixtures/driver/aux_header.h"

int aux_header_answer(void)
{
    return AUX_HEADER_ANSWER;
}

int main(void)
{
    return aux_header_answer();
}
