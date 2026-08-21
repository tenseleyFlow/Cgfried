/* A64-M-04: both returns keep a value live across a call, forcing a
 * callee-saved register and two independently described epilogues. */
extern long external_call(long);

long a64_callee_saved_unwind(long value, int alternate)
{
    long saved = value + 17;

    if (alternate)
        return external_call(value) + saved;
    return external_call(value + 1) + saved;
}
