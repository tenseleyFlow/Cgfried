// FLAGS: -fsyntax-only
// ERROR_EXPECTED: cannot cast from non-scalar type 'struct Mix'
// FUZZER FINDING (Sprint 51, seed 9241). C11 6.5.4p2 constrains BOTH sides
// of a cast: unless the type name is void, the TYPE NAME shall specify a
// scalar type AND THE OPERAND SHALL HAVE SCALAR TYPE. Only the target was
// checked, so casting a struct to an arithmetic type typed cleanly and then
// ICEd in lowering ("lower_irtype on non-scalar type kind 19").
//
// A cast to void is exempt and stays legal for any operand -- it discards
// the value whatever its type, and `(void)s` on a struct is ordinary C.
// tests/programs/sema13/cast_to_void_aggregate.c pins that half, because a
// constraint added without its exemption is how correct code starts getting
// rejected.
struct Mix {
    double d;
    long l;
};

struct Mix r_mix(void);

int f(void)
{
    return (int)r_mix();
}
