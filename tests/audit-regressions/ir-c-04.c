// RESOLVED(audit): IR-C-04 backward goto before a VLA declaration leaks stack space
// The label is outside the VLA object's scope because that scope begins after
// its declarator. Each trip through the declaration therefore creates a new
// object, and the backward edge must first release the previous allocation.
int cycle(int n)
{
    int count = 0;

again:
    ;
    int values[n];
    values[0] = count++;
    if (count < 3)
        goto again;
    return values[0];
}
