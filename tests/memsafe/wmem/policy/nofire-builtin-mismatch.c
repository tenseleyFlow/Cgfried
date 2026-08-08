// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: function returns on this path without releasing it
//
// A summary row is a contract about a SHAPE, not merely about a name. Both
// memory tables (libc_summaries and alloc_families) were selected by strcmp
// on the callee alone, so a call that merely forgot an #include -- whose
// implicit declaration returns int -- had the row's POINTER facts attached to
// an INTEGER result. The alias service's validator refused that, as an ICE,
// on ordinary C: `strcpy`, `strchr` and `getcwd` hit "return-alias seed is not
// a pointer call" and `malloc` hit "owned call result is not a pointer".
// Frontend fuzzer, seed 64271; pre-existing since at least Sprint 47.
//
// gcc rejects the same mismatch by name (-Wbuiltin-declaration-mismatch), and
// Sprint 39's format table already selects rows on a compatible signature.
//
// BOTH DIRECTIONS ARE PINNED HERE ON PURPOSE. Dropping a row is the safe
// answer, so a gate that is too strict switches the tables off and every
// memory diagnostic silently disappears -- which no fixture asserting only
// the absence of an ICE could tell from success. `calloc` below is declared
// compatibly and must still fire.

/* Not the libc functions these are named after, whatever they are called. */
int malloc(unsigned long);
int strcpy(char *, const char *);
int strchr(const char *, int);

/* Compatible, so the same TU proves the tables are still live. */
void *calloc(unsigned long, unsigned long);

void mismatch_is_not_the_builtin(void)
{
    char buf[8];

    /* An allocator that does not return a pointer is not an allocation
     * site, so there is nothing to leak and nothing to prove about it. */
    (void)malloc(8);
    /* A result that cannot hold a pointer cannot alias an argument. */
    (void)strcpy(buf, "");
    (void)strchr(buf, 'x');
}

void compatible_still_fires(void)
{
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    void *p = calloc(1, 8);

    (void)p;
}
