// RESOLVED(audit): IR-C-03 atomic pointer increment is split into a load and store
// C17 6.5.2.4 defines postfix ++ in terms of += 1 except for the result value,
// and 6.5.16.2 requires compound assignment on an atomic object to be one
// sequentially consistent read-modify-write operation. Splitting the update
// loses increments when threads race even though both component accesses are
// individually seq_cst.
_Atomic(int *) cursor;

int *advance(void)
{
    return cursor++;
}
