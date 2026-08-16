// XFAIL(audit): A64-M-04 unwind information omits x19 and epilogue state transitions
extern long external_call(long);

long preserve_across_call(long value) {
    long saved = value + 17;
    return external_call(value) + saved;
}
