// XFAIL(audit): DRV-M-01 assembler signal death is reported as ordinary rejection
// Reproduce with:
//   CGF_AS_PATH=tests/audit-regressions/support/drv-m-01/as-signal.sh \
//     build/cgfried -c tests/audit-regressions/drv-m-01.c -o /tmp/drv-m-01.o
// Expected after remediation: exit 1 and a diagnostic naming signal 15.
// Baseline: exit 1 but only says that the assembler "rejected" the text.
__asm__("");

int main(void) { return 0; }
