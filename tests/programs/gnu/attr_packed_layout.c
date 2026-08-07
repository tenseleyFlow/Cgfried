// EXIT_CODE: 0
// Every number here was read off gcc before it was written down
// (.docs/audits/packed-layout.md). None of it is hand-computed: the two rows
// this file exists for -- S3 and the alignments -- are exactly the ones a
// plausible implementation gets wrong.
//
// NO HEADERS, deliberately. glibc's <sys/cdefs.h> does
// `#define __attribute__(xyz)` when __GNUC__ is undefined, and __GNUC__ stays
// undefined until the end of Sprint 55 -- so a hosted fixture would have its
// attributes erased by the preprocessor and pass no matter what layout does.
#define OFF(T, f) __builtin_offsetof(T, f)

struct S1 {
    char a;
    int b;
} __attribute__((packed));

/* Between the keyword and the tag: also packs. */
struct __attribute__((packed)) S2 {
    char a;
    int b;
};

/* THE NEGATIVE. A LEADING attribute, before the keyword, is silently ignored
 * by gcc -- it binds to the declaration, not to the record. Getting this
 * wrong packs types the author never asked to pack. */
__attribute__((packed)) struct S3 {
    char a;
    int b;
};

/* Member-level: the same rule applied to one member (rule 4). */
struct S5 {
    char a;
    __attribute__((packed)) int b;
};

/* A prefix attribute in MEMBER-specifier position covers every sibling
 * declarator; a suffix one covers only its own. */
struct S6 {
    char a;
    int b, c __attribute__((packed));
};
struct S7 {
    char a;
    __attribute__((packed)) int b, c;
};

/* A union loses only its ALIGNMENT: every member still starts at 0, so no
 * member can be misplaced and the size is still the widest one. */
union U1 {
    char a;
    int b;
} __attribute__((packed));
union P8 {
    char a;
    double d;
} __attribute__((packed));

/* Alignment 1 propagates: S1 is 1-aligned, so an ordinary struct containing
 * one packs against it without being packed itself. */
struct S8 {
    char a;
    struct S1 s;
};

struct P1 {
    char a;
    short b;
    long c;
} __attribute__((packed));
struct P3 {
    int a[3];
    char c;
} __attribute__((packed));
/* A nested aggregate is PLACED at alignment 1 and keeps its own layout. */
struct P4 {
    char c;
    struct {
        int x;
        int y;
    } in;
} __attribute__((packed));

/* Rule 2, the half that is easy to miss: the record's OWN alignment drops to
 * 1 as well. Force the offsets without that and every offset below still
 * passes while sizeof keeps its tail padding. */
_Static_assert(sizeof(struct S1) == 5, "S1 size");
_Static_assert(_Alignof(struct S1) == 1, "S1 align");
_Static_assert(OFF(struct S1, b) == 1, "S1 offset");

_Static_assert(sizeof(struct S2) == 5 && _Alignof(struct S2) == 1, "S2");
_Static_assert(OFF(struct S2, b) == 1, "S2 offset");

_Static_assert(sizeof(struct S3) == 8 && _Alignof(struct S3) == 4, "S3");
_Static_assert(OFF(struct S3, b) == 4, "S3 offset: leading is ignored");

_Static_assert(sizeof(struct S5) == 5 && _Alignof(struct S5) == 1, "S5");
_Static_assert(OFF(struct S5, b) == 1, "S5 offset");

_Static_assert(sizeof(struct S6) == 12 && _Alignof(struct S6) == 4, "S6");
_Static_assert(OFF(struct S6, b) == 4 && OFF(struct S6, c) == 8, "S6 offsets");

_Static_assert(sizeof(struct S7) == 9 && _Alignof(struct S7) == 1, "S7");
_Static_assert(OFF(struct S7, b) == 1 && OFF(struct S7, c) == 5, "S7 offsets");

_Static_assert(sizeof(union U1) == 4 && _Alignof(union U1) == 1, "U1");
_Static_assert(sizeof(union P8) == 8 && _Alignof(union P8) == 1, "P8");

_Static_assert(sizeof(struct S8) == 6 && _Alignof(struct S8) == 1, "S8");
_Static_assert(OFF(struct S8, s) == 1, "S8 offset");

_Static_assert(sizeof(struct P1) == 11 && OFF(struct P1, c) == 3, "P1");
_Static_assert(sizeof(struct P3) == 13 && OFF(struct P3, c) == 12, "P3");
_Static_assert(sizeof(struct P4) == 9 && OFF(struct P4, in) == 1, "P4");

int main(void)
{
    return 0;
}
