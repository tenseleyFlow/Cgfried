// XFAIL(audit): SEMA-H-06 a record of only zero-length arrays is sized to its alignment
// GCC gives both of these size 0; Cgfried gives them 4. The trailing-idiom
// shapes (`struct { int n; char p[0]; }`) agree, which is why real code never
// surfaced this -- only a record whose members are ALL zero-length diverges,
// and the error then propagates through nesting.
struct all_zero_length {
    int x[0];
};

// The same fixup exists on the union path, uncommented.
union all_zero_length_union {
    int x[0];
};

// Nesting propagates it: GCC lays this out at 4, Cgfried at 8, because the
// embedded member is 4 bytes wide here and 0 there.
struct nests_a_zero_length {
    struct all_zero_length a;
    int n;
};
