// FLAGS: -S -Wunused-variable
// WARN_COUNT: 1
/* The file-scope volatile exemption stops at block scope: gcc still warns
 * for an unused LOCAL volatile, because a local one names no external agent.
 * This is the negative half of nofire-file-scope-volatile.c and exists so
 * that widening the exemption cannot pass silently. */
void unused_local_volatile(void)
{
    // WARN_CHECK: unused-variable unused variable 'scratch'
    volatile int scratch;
}
