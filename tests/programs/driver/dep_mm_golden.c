// -MM depfile golden: source first, header prereq, -MP phony rule for
// the header (never the source). System headers are OMITTED by -MM —
// IR_CHECK-NOT pins that our shipped stddef.h stays out.
// FLAGS: -E -MM -MP -Itests/fixtures/driver
// IR_CHECK: dep_mm_golden.o: tests/programs/driver/dep_mm_golden.c
// IR_CHECK: tests/fixtures/driver/dep_aux.h
// IR_CHECK: tests/fixtures/driver/dep_aux.h:
// IR_CHECK-NOT: stddef.h
#include <stddef.h>
#include "dep_aux.h"
size_t s;
int main(void)
{
    return 0;
}
