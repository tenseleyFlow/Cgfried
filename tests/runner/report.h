#ifndef CGF_TEST_REPORT_H
#define CGF_TEST_REPORT_H

#include <stddef.h>

/* Result buckets. XPASS and CONFIG count as failures for the exit code:
 * an XFAIL that passes must force its annotation off, and a typo'd
 * directive must never be green. */
typedef struct {
    int pass;
    int fail;
    int xfail;
    int xpass;
    int skip;
    int config;
} Counts;

int counts_total(const Counts *c);
int counts_exit_code(const Counts *c);

/* Deterministic output only: no durations, no pids, no absolute paths —
 * the meta-suite byte-compares two full runs. */
void report_summary(const Counts *c, const char *profile);

#endif
