// OPT_EQ: all
// Packed structs, EXECUTED. This file is in the shared corpus rather than
// only in tests/programs/gnu because the arm64 e2e lane re-runs exactly this
// directory -- and the VLA reckoning is what taught that lesson: a corpus
// inherited from another target covers only what that target's authors
// happened to write, and arm64 ICEd on every VLA for two sprints because no
// program here used one.
//
// Every expectation below was verified against gcc before it was pinned.
//
// What each shape is here to reach:
//
// - `mem`: 13 bytes with `b` at 1 and `c` at 5. Too big for two eightbytes
//   AND unaligned, so SysV passes it in MEMORY and AAPCS64 by value on the
//   stack. Returned by value too, which is the other half of the same rule.
//
// - `sse`: two floats at their natural offsets. Packed drops the record's
//   alignment but misaligns nothing, so it stays SSE on x86 and an HFA on
//   arm64 -- the NEGATIVE that stops "packed" being read as "always MEMORY".
//
// - `gp`: one int, packed. Alignment 1, every field still natural, so it
//   travels in an ordinary integer register. gcc disproved the guess that a
//   1-aligned record is MEMORY by construction.
//
// - `mixed`: a char before two floats, which misaligns both. This one IS
//   MEMORY, and it is the pair that makes `sse` meaningful.
struct mem {
    char a;
    int b;
    long c;
} __attribute__((packed));

struct sse {
    float x;
    float y;
} __attribute__((packed));

struct gp {
    int b;
} __attribute__((packed));

struct mixed {
    char a;
    float x;
    float y;
} __attribute__((packed));

static long take_mem(struct mem m)
{
    return m.a * 1000000L + m.b * 1000L + m.c;
}

static long take_sse(struct sse s)
{
    return (long)(s.x * 1000 + s.y);
}

static long take_gp(struct gp g)
{
    return g.b;
}

static long take_mixed(struct mixed m)
{
    return (long)(m.a * 1000000L + m.x * 1000 + m.y);
}

static struct mem make_mem(char a, int b, long c)
{
    struct mem m;

    m.a = a;
    m.b = b;
    m.c = c;
    return m;
}

/* An array of packed structs: the stride is sizeof, so element k's `b` is at
 * 13k+1 and no two elements share an alignment. */
static struct mem tab[4];

int main(void)
{
    struct sse s;
    struct gp g;
    struct mixed x;
    struct mem r;
    volatile struct mem *vp;
    long acc = 0;
    int i;

    if (sizeof(struct mem) != 13 || sizeof(struct sse) != 8)
        return 1;
    if (sizeof(struct gp) != 4 || sizeof(struct mixed) != 9)
        return 2;
    if (__builtin_offsetof(struct mem, b) != 1)
        return 3;
    if (__builtin_offsetof(struct mem, c) != 5)
        return 4;
    if (__builtin_offsetof(struct mixed, x) != 1)
        return 5;

    r = make_mem(1, 20, 300);
    if (take_mem(r) != 1020300)
        return 6;

    s.x = 3.0f;
    s.y = 4.0f;
    if (take_sse(s) != 3004)
        return 7;

    g.b = 42;
    if (take_gp(g) != 42)
        return 8;

    x.a = 2;
    x.x = 3.0f;
    x.y = 4.0f;
    if (take_mixed(x) != 2003004)
        return 9;

    /* Read-modify-write through a volatile misaligned member: volatile keeps
     * the accesses alive at every level, so OPT_EQ is checking real loads and
     * stores rather than a constant the folder computed. */
    vp = &r;
    vp->b = vp->b + 1;
    vp->c = vp->c * 2;
    if (r.b != 21 || r.c != 600)
        return 10;

    for (i = 0; i < 4; i++) {
        tab[i] = make_mem((char)i, i * 100, i * 1000);
        acc += tab[i].a + tab[i].b + tab[i].c;
    }
    if (acc != 6606)
        return 11;
    return 0;
}
