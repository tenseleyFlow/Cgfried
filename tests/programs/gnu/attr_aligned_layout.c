// EXIT_CODE: 0
// Every number was read off gcc before it was written down
// (.docs/audits/aligned-layout.md). Freestanding on purpose: glibc's
// <sys/cdefs.h> does `#define __attribute__(xyz)` while __GNUC__ is undefined,
// so a hosted fixture would have its attributes deleted by the preprocessor
// and pass no matter what layout does.
#define OFF(T, f) __builtin_offsetof(T, f)
#define A(n) __attribute__((aligned(n)))

struct R1 {
    char a;
    int b;
} A(16);

/* No argument: the target's biggest alignment. Measured as 16 on x86-64 AND
 * arm64-linux, so the fixture holds on both without a selector. */
struct R2 {
    char a;
} __attribute__((aligned));

struct R3 {
    char a;
    int b A(8);
};

/* THE RULE that separates `aligned` from `_Alignas`: it only ever RAISES.
 * R6 asks for 4 where the natural alignment is 8 and is silently declined --
 * a constraint violation for _Alignas, a no-op here. */
struct R6 {
    char a;
    long b;
} A(4);

/* ...which is why `aligned(1)` on a member is NOT a spelling of `packed`:
 * `b` stays at its natural offset 4. */
struct S {
    char a;
    int b A(1);
};

/* Composes with packed rather than conflicting: packed forces the offsets,
 * aligned sets the record's alignment, and the size rounds up to it. */
struct R4 {
    char a;
    int b;
} __attribute__((packed, aligned(8)));
struct R5 {
    char a;
    int b;
} __attribute__((packed)) A(2);

/* Between the keyword and the tag binds to the record; LEADING is ignored,
 * the same positional rule packed follows. */
struct A(16) T4 {
    char a;
};
A(16) struct T3 {
    char a;
};

union U1 {
    char a;
} A(8);
/* A union MEMBER's alignment raises the union's -- layout_union never read
 * align_override at all, so this was silently ignored for both spellings
 * until the layout differential generated it. */
union U2 {
    char a;
    int b A(16);
};

/* A constant EXPRESSION, not just a literal: gcc accepts it and headers use
 * it, which is why the argument is folded in sema rather than read as a
 * token. */
struct E1 {
    char a;
} A(4 * 8);

_Static_assert(sizeof(struct R1) == 16 && _Alignof(struct R1) == 16, "R1");
_Static_assert(sizeof(struct R2) == 16 && _Alignof(struct R2) == 16, "R2 bare");
_Static_assert(sizeof(struct R3) == 16 && _Alignof(struct R3) == 8, "R3");
_Static_assert(OFF(struct R3, b) == 8, "R3 member offset");
_Static_assert(sizeof(struct R6) == 16 && _Alignof(struct R6) == 8,
               "R6 never lowers");
_Static_assert(sizeof(struct S) == 8 && OFF(struct S, b) == 4,
               "aligned(1) is not packed");
_Static_assert(sizeof(struct R4) == 8 && _Alignof(struct R4) == 8, "R4");
_Static_assert(OFF(struct R4, b) == 1, "R4 packed offset survives");
_Static_assert(sizeof(struct R5) == 6 && _Alignof(struct R5) == 2, "R5");
_Static_assert(OFF(struct R5, b) == 1, "R5 packed offset survives");
_Static_assert(sizeof(struct T4) == 16, "T4 keyword-adjacent binds");
_Static_assert(sizeof(struct T3) == 1, "T3 leading is IGNORED");
_Static_assert(sizeof(union U1) == 8 && _Alignof(union U1) == 8, "U1");
_Static_assert(sizeof(union U2) == 16 && _Alignof(union U2) == 16, "U2 member");
_Static_assert(sizeof(struct E1) == 32 && _Alignof(struct E1) == 32, "E1 expr");

int main(void)
{
    return 0;
}
