// A quote include found beside its source remains a user dependency even
// when that directory is also configured as -isystem.
// FLAGS: -E -MM -isystem tests/programs/driver
// IR_CHECK: dep_mm_local_isystem.o: tests/programs/driver/dep_mm_local_isystem.c
// IR_CHECK: tests/programs/driver/dep_mm_local_isystem.h
#include "dep_mm_local_isystem.h"
