/* Sprint 51 D6: the ABI differential generator.
 *
 * Emits a random-but-DETERMINISTIC function signature and a matched
 * caller/callee pair. The pair is then compiled with the two halves swapped
 * between cgfried and the reference compiler and run; a disagreement is an
 * ABI bug. Signatures are weighted toward the known cliff edges -- sizes
 * 15-17, HFA leaf counts 4-5, alignment 16 -- because that is where psABI
 * rules change shape.
 *
 * TWO DESIGN DECISIONS, both deliberate departures from the sprint sketch.
 *
 * 1. The checksum folds FIELD VALUES, not raw object bytes. The sketch said
 *    bytes, and bytes would be wrong: padding content is unspecified, no ABI
 *    promises to transport it, and reading it would manufacture mismatches
 *    that are not bugs. Field values are exactly what an ABI does promise.
 *
 * 2. The caller computes the EXPECTED checksum locally with the same fold,
 *    rather than the generator baking in a constant. Baking a constant would
 *    require this program to model C conversion semantics exactly; computing
 *    it on both sides means only the ABI transport differs between them,
 *    which is precisely the variable under test.
 *
 * Two modes, so that MINIMIZATION is possible from the start (the sprint is
 * right that retrofitting it is misery):
 *
 *     abigen --seed N [--args M]      -> a signature DESCRIPTOR on stdout
 *     abigen --emit FILE --out DIR    -> sig.h, caller.c, callee.c
 *
 * The descriptor is a small text form, so the shrinking loop is plain text
 * surgery -- drop an argument line, replace a composite with one member --
 * and never has to re-derive the generator's random state.
 *
 * Descriptor grammar (one directive per line):
 *     R <type>     the return type; `v` is void
 *     A <type>     one per FIXED argument, in order
 *     V <type>     one per ANONYMOUS argument; any V makes the signature
 *                  variadic, and at least one A must precede them because
 *                  va_start has to name the last fixed parameter
 * Types:
 *     c h i l      char, short, int, long
 *     f d          float, double
 *     s(T,...)     struct
 *     u(T,...)     union
 *     a(N,T)       array of N T, always inside a struct
 *
 * Built with the HOST compiler: it is a test tool, not a compilation target.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MEM 6
#define MAX_ARGS 8
#define MAX_TYPES 512

typedef enum {
    K_CHAR,
    K_SHORT,
    K_INT,
    K_LONG,
    K_FLOAT,
    K_DOUBLE,
    K_STRUCT,
    K_UNION,
    K_ARRAY,
    K_VOID
} Kind;

typedef struct Type {
    Kind kind;
    int nmem;
    struct Type *mem[MAX_MEM];
    int count; /* K_ARRAY element count */
    int tag;   /* K_STRUCT/K_UNION: emitted typedef id (T<tag>) */
} Type;

static Type pool[MAX_TYPES];
static int npool;

static Type *tnew(Kind k)
{
    Type *t;

    if (npool == MAX_TYPES) {
        fprintf(stderr, "abigen: type pool exhausted\n");
        exit(2);
    }
    t = &pool[npool++];
    memset(t, 0, sizeof(*t));
    t->kind = k;
    return t;
}

/* splitmix64: the same generator the pp fuzzer uses, for the same reason --
 * a seed must reproduce a signature exactly, forever. */
static unsigned long long rng_state;

static unsigned long long rnd(void)
{
    unsigned long long z;

    rng_state += 0x9E3779B97F4A7C15ull;
    z = rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static int pick(int n)
{
    return (int)(rnd() % (unsigned long long)n);
}

static Type *gen_type(int depth, int allow_composite);

/* An HFA: 1-4 leaves of ONE floating type. Generated explicitly rather than
 * hoped for, because the counts that matter (4, and 5 to fall off the edge)
 * are exactly the ones a uniform random struct almost never produces. */
static Type *gen_hfa(void)
{
    Type *s = tnew(K_STRUCT);
    Kind leaf = pick(2) ? K_FLOAT : K_DOUBLE;
    int n = 1 + pick(5); /* 1..5 -- five is over the limit, on purpose */
    int i;

    if (n > MAX_MEM)
        n = MAX_MEM;
    for (i = 0; i < n; i++)
        s->mem[i] = tnew(leaf);
    s->nmem = n;
    return s;
}

/* A composite sized near the 16-byte cliff: 15, 16 or 17 bytes, which is
 * where SysV stops using registers and AAPCS64 switches to an indirect
 * copy. Built from chars so the size is exact. */
static Type *gen_cliff(void)
{
    Type *s = tnew(K_STRUCT);
    Type *arr = tnew(K_ARRAY);
    int size = 15 + pick(3);

    arr->count = size;
    arr->mem[0] = tnew(K_CHAR);
    arr->nmem = 1;
    s->mem[0] = arr;
    s->nmem = 1;
    return s;
}

static Type *gen_composite(int depth)
{
    int roll = pick(100);
    Type *s;
    int n, i;

    if (roll < 25)
        return gen_hfa();
    if (roll < 40)
        return gen_cliff();
    s = tnew(roll < 50 ? K_UNION : K_STRUCT);
    n = 1 + pick(MAX_MEM - 1);
    for (i = 0; i < n; i++)
        s->mem[i] = gen_type(depth + 1, depth < 2);
    s->nmem = n;
    return s;
}

static Type *gen_type(int depth, int allow_composite)
{
    static const Kind scalars[] = {K_CHAR, K_SHORT, K_INT,
                                   K_LONG, K_FLOAT, K_DOUBLE};

    if (allow_composite && pick(100) < 45)
        return gen_composite(depth);
    return tnew(scalars[pick(6)]);
}

/* An ANONYMOUS argument's type. Scalars are restricted to the ones that
 * survive the default argument promotions unchanged -- a `char` argument is
 * fetched with `va_arg(ap, int)`, so spelling it `c` in the descriptor would
 * name a type that never travels. Composites are not promoted and are the
 * interesting case anyway: Apple passes every anonymous argument on the
 * stack, so an aggregate there exercises a rule AAPCS64 does not have. */
static Type *gen_va_type(void)
{
    static const Kind scalars[] = {K_INT, K_LONG, K_DOUBLE};

    if (pick(100) < 35)
        return gen_composite(1);
    return tnew(scalars[pick(3)]);
}

/* --- descriptor text ----------------------------------------------------- */

static void print_type(FILE *f, const Type *t)
{
    int i;

    switch (t->kind) {
    case K_CHAR:
        fputc('c', f);
        return;
    case K_SHORT:
        fputc('h', f);
        return;
    case K_INT:
        fputc('i', f);
        return;
    case K_LONG:
        fputc('l', f);
        return;
    case K_FLOAT:
        fputc('f', f);
        return;
    case K_DOUBLE:
        fputc('d', f);
        return;
    case K_VOID:
        fputc('v', f);
        return;
    case K_ARRAY:
        fprintf(f, "a(%d,", t->count);
        print_type(f, t->mem[0]);
        fputc(')', f);
        return;
    default:
        fputc(t->kind == K_UNION ? 'u' : 's', f);
        fputc('(', f);
        for (i = 0; i < t->nmem; i++) {
            if (i)
                fputc(',', f);
            print_type(f, t->mem[i]);
        }
        fputc(')', f);
        return;
    }
}

static const char *pp;

static Type *parse_type(void)
{
    Type *t;

    while (*pp == ' ')
        pp++;
    switch (*pp) {
    case 'c':
        pp++;
        return tnew(K_CHAR);
    case 'h':
        pp++;
        return tnew(K_SHORT);
    case 'i':
        pp++;
        return tnew(K_INT);
    case 'l':
        pp++;
        return tnew(K_LONG);
    case 'f':
        pp++;
        return tnew(K_FLOAT);
    case 'd':
        pp++;
        return tnew(K_DOUBLE);
    case 'v':
        pp++;
        return tnew(K_VOID);
    case 'a':
        pp += 2; /* "a(" */
        t = tnew(K_ARRAY);
        t->count = (int)strtol(pp, (char **)&pp, 10);
        pp++; /* ',' */
        t->mem[0] = parse_type();
        t->nmem = 1;
        pp++; /* ')' */
        return t;
    case 's':
    case 'u':
        t = tnew(*pp == 'u' ? K_UNION : K_STRUCT);
        pp += 2; /* "s(" */
        for (;;) {
            t->mem[t->nmem++] = parse_type();
            if (*pp == ',') {
                pp++;
                continue;
            }
            break;
        }
        pp++; /* ')' */
        return t;
    default:
        fprintf(stderr, "abigen: bad descriptor at '%s'\n", pp);
        exit(2);
    }
}

/* A mask that reduces the 64-bit fold to a value the type represents
 * EXACTLY, so that a scalar return can be compared without the comparison
 * itself being lossy.
 *
 * The first run of the harness reported `R f / A l` -- return float, take
 * one long -- as an ABI disagreement in BOTH directions. It was this: the
 * callee returned `(float)h` for a 64-bit h, float carries 24 bits of
 * mantissa, and the caller then compared `(long)rv` against the full hash.
 * The conversion lost the value before the ABI ever saw it, and converting
 * an out-of-range float back to long is undefined besides.
 *
 * Failing in both directions is the tell. A real convention bug is usually
 * asymmetric -- one side reads the wrong register -- while a harness bug is
 * a property of the generated program and shows up whoever compiles it. */
static unsigned long long ret_mask(Kind k)
{
    switch (k) {
    case K_CHAR:
        return 0x3Full;
    case K_SHORT:
        return 0x3FFFull;
    case K_INT:
        return 0x3FFFFFFFull;
    case K_FLOAT:
        return 0xFFFFFFull; /* 24-bit mantissa */
    case K_DOUBLE:
        return 0x1FFFFFFFFFFFFFull; /* 53-bit mantissa */
    default:
        return 0x3FFFFFFFFFFFFFFFull;
    }
}

/* Load a descriptor file into a return type and an argument list. Shared by
 * --emit and --simplify so the two can never drift on the grammar. */
static void load_desc(const char *file, Type **ret, Type **args, int *na,
                      Type **va, int *nva)
{
    FILE *df = fopen(file, "r");
    char line[4096];
    int i;

    *ret = NULL;
    *na = 0;
    *nva = 0;
    if (!df) {
        fprintf(stderr, "abigen: cannot read %s\n", file);
        exit(2);
    }
    while (fgets(line, sizeof(line), df)) {
        char *p = line;

        while (*p == ' ')
            p++;
        if (*p == 'R') {
            pp = p + 1;
            *ret = parse_type();
        } else if (*p == 'A' && *na < MAX_ARGS) {
            pp = p + 1;
            args[(*na)++] = parse_type();
        } else if (*p == 'V' && *nva < MAX_ARGS) {
            pp = p + 1;
            va[(*nva)++] = parse_type();
        }
    }
    fclose(df);
    /* va_start names the LAST fixed parameter, so a variadic signature needs
     * at least one. The generator never emits such a descriptor; a
     * hand-edited or over-shrunk one would. */
    if (*nva && !*na) {
        fprintf(stderr,
                "abigen: variadic descriptor with no fixed argument "
                "in %s\n",
                file);
        exit(2);
    }
    /* An anonymous argument's type must survive the default argument
     * promotions, or `va_arg` names a type that never travelled and the
     * program is undefined -- `va_arg(ap, float)` after the caller promoted
     * it to double, most obviously. The GENERATOR never emits one, but the
     * minimizer unwraps composites and produced `V f` out of `V s(f,...)`.
     * Rejecting it here makes that reduction fail to emit, which the shrink
     * loop already reads as "do not take this step". */
    for (i = 0; i < *nva; i++) {
        Kind k = va[i]->kind;

        if (k == K_CHAR || k == K_SHORT || k == K_FLOAT) {
            fprintf(stderr,
                    "abigen: anonymous argument %d in %s has a type that "
                    "default argument promotion changes\n",
                    i, file);
            exit(2);
        }
    }
    if (!*ret) {
        fprintf(stderr, "abigen: descriptor has no return type\n");
        exit(2);
    }
    /* A bare array cannot be a parameter or a return type -- it decays. The
     * grammar keeps arrays inside a struct, so one here means a descriptor
     * was hand-edited or a reduction broke the invariant. Say so, rather
     * than crashing in the emitter and leaving the minimizer to interpret a
     * signal as agreement. */
    if ((*ret)->kind == K_ARRAY) {
        fprintf(stderr, "abigen: bare array return type in %s\n", file);
        exit(2);
    }
    for (i = 0; i < *na; i++) {
        if (args[i]->kind == K_ARRAY) {
            fprintf(stderr, "abigen: bare array argument %d in %s\n", i, file);
            exit(2);
        }
    }
}

/* One reduction step for the minimizer: replace the target'th composite,
 * counted in pre-order over the return type then each argument, with its
 * first member. An array collapses to its element type, which drops the
 * count as well.
 *
 * One level at a time rather than all the way down, because the shrink is
 * only kept when the failure SURVIVES it -- a composite that has to stay a
 * composite for the bug to reproduce is exactly the thing being located.
 *
 * An array is never the RESULT of a reduction: the grammar puts arrays only
 * inside a struct, because a bare array is not a C parameter type -- it
 * decays. Unwrapping `s(a(16,c))` therefore yields the element `c` rather
 * than the array, skipping a level to keep the invariant. The first draft
 * did not, and the generator segfaulted mid-minimization on exactly the
 * 16-byte-composite signature that had just been caught. */
static Type *simplify_walk(Type *t, int *idx, int target)
{
    Type *r;
    int i;

    if (t->kind != K_STRUCT && t->kind != K_UNION && t->kind != K_ARRAY)
        return t;
    if ((*idx)++ == target) {
        r = t->mem[0];
        return r->kind == K_ARRAY ? r->mem[0] : r;
    }
    for (i = 0; i < t->nmem; i++)
        t->mem[i] = simplify_walk(t->mem[i], idx, target);
    return t;
}

/* --- C emission ---------------------------------------------------------- */

static int tag_seq;

/* Composite types are named T0, T1, ... and defined depth-first so a member
 * type is always complete before its user. */
static void emit_typedefs(FILE *f, Type *t, int *id)
{
    int i;

    if (t->kind == K_ARRAY) {
        emit_typedefs(f, t->mem[0], id);
        return;
    }
    if (t->kind != K_STRUCT && t->kind != K_UNION)
        return;
    for (i = 0; i < t->nmem; i++)
        emit_typedefs(f, t->mem[i], id);
    t->tag = tag_seq++;
    fprintf(f, "typedef %s {", t->kind == K_UNION ? "union" : "struct");
    for (i = 0; i < t->nmem; i++) {
        Type *m = t->mem[i];

        if (m->kind == K_ARRAY) {
            fprintf(f, " ");
            switch (m->mem[0]->kind) {
            case K_CHAR:
                fprintf(f, "char");
                break;
            case K_SHORT:
                fprintf(f, "short");
                break;
            case K_INT:
                fprintf(f, "int");
                break;
            case K_LONG:
                fprintf(f, "long");
                break;
            case K_FLOAT:
                fprintf(f, "float");
                break;
            default:
                fprintf(f, "double");
                break;
            }
            fprintf(f, " m%d[%d];", i, m->count);
        } else if (m->kind == K_STRUCT || m->kind == K_UNION) {
            fprintf(f, " T%d m%d;", m->tag, i);
        } else {
            static const char *const names[] = {"char", "short", "int",
                                                "long", "float", "double"};
            fprintf(f, " %s m%d;", names[m->kind], i);
        }
    }
    fprintf(f, " } T%d;\n", t->tag);
    *id = t->tag;
}

static void type_name(char *buf, size_t n, const Type *t)
{
    static const char *const names[] = {"char", "short", "int",
                                        "long", "float", "double"};

    if (t->kind == K_STRUCT || t->kind == K_UNION)
        snprintf(buf, n, "T%d", t->tag);
    else if (t->kind == K_VOID)
        snprintf(buf, n, "void");
    else
        snprintf(buf, n, "%s", names[t->kind]);
}

/* The fold. Every scalar leaf contributes its VALUE (floats via a
 * round-trip through long, which is exact for the small integers the caller
 * builds) into an FNV-1a-shaped running hash. Padding never participates,
 * which is the whole point -- see the header comment. */
static void emit_fold(FILE *f, const Type *t, const char *expr, int depth)
{
    int i;
    char sub[512];

    if (depth > 8)
        return;
    switch (t->kind) {
    case K_STRUCT:
        for (i = 0; i < t->nmem; i++) {
            snprintf(sub, sizeof(sub), "%s.m%d", expr, i);
            emit_fold(f, t->mem[i], sub, depth + 1);
        }
        return;
    case K_UNION:
        /* Only member 0 is live: the others would read a value nobody
         * stored, and C says nothing useful about that. */
        if (t->nmem) {
            snprintf(sub, sizeof(sub), "%s.m0", expr);
            emit_fold(f, t->mem[0], sub, depth + 1);
        }
        return;
    case K_ARRAY:
        for (i = 0; i < t->count; i++) {
            snprintf(sub, sizeof(sub), "%s[%d]", expr, i);
            emit_fold(f, t->mem[0], sub, depth + 1);
        }
        return;
    case K_VOID:
        return;
    default:
        fprintf(f, "    h = fold(h, (long)(%s));\n", expr);
        return;
    }
}

/* Deterministic argument construction: every leaf gets a distinct small
 * integer, so a mismatch names WHICH leaf moved rather than just failing. */
static void emit_build(FILE *f, const Type *t, const char *expr, int *counter,
                       int depth)
{
    int i;
    char sub[512];

    if (depth > 8)
        return;
    switch (t->kind) {
    case K_STRUCT:
        for (i = 0; i < t->nmem; i++) {
            snprintf(sub, sizeof(sub), "%s.m%d", expr, i);
            emit_build(f, t->mem[i], sub, counter, depth + 1);
        }
        return;
    case K_UNION:
        if (t->nmem) {
            snprintf(sub, sizeof(sub), "%s.m0", expr);
            emit_build(f, t->mem[0], sub, counter, depth + 1);
        }
        return;
    case K_ARRAY:
        for (i = 0; i < t->count; i++) {
            snprintf(sub, sizeof(sub), "%s[%d]", expr, i);
            emit_build(f, t->mem[0], sub, counter, depth + 1);
        }
        return;
    case K_VOID:
        return;
    default:
        /* Small and positive: every leaf type can hold it exactly, so the
         * fold is comparing values and not conversion behaviour. */
        fprintf(f, "    %s = %d;\n", expr, 1 + (*counter)++ % 90);
        return;
    }
}

/* The parameter list, written identically into sig.h and callee.c. One
 * helper because a declaration and a definition that disagree about the
 * variadic tail is undefined behaviour, not a test result. */
static void emit_params(FILE *f, Type **args, int na, int nva)
{
    int i;

    for (i = 0; i < na; i++) {
        char an[64];

        type_name(an, sizeof(an), args[i]);
        fprintf(f, "%s%s a%d", i ? ", " : "", an, i);
    }
    if (nva)
        fprintf(f, ", ...");
    else if (!na)
        fprintf(f, "void");
}

/* Composite returns: rebuild the pattern the callee promised, fold both,
 * compare. Written as a helper because it needs two independent folds. */
static void long_return_check(FILE *ca, Type *ret)
{
    int counter = 1;

    fprintf(ca, "    {\n        long rh, rw;\n        T%d expect;\n\n",
            ret->tag);
    emit_build(ca, ret, "expect", &counter, 0);
    fprintf(ca, "        h = 0;\n");
    emit_fold(ca, ret, "rv", 0);
    fprintf(ca, "        rh = h;\n        h = 0;\n");
    emit_fold(ca, ret, "expect", 0);
    fprintf(ca, "        rw = h;\n");
    fprintf(ca, "        if (rh != rw) return 2;\n    }\n");
}

int main(int argc, char **argv)
{
    unsigned long long seed = 0;
    int nargs = 0;
    const char *emit_file = NULL;
    const char *outdir = NULL;
    int simplify = -1;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--args") == 0 && i + 1 < argc)
            nargs = atoi(argv[++i]);
        else if (strcmp(argv[i], "--emit") == 0 && i + 1 < argc)
            emit_file = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outdir = argv[++i];
        else if (strcmp(argv[i], "--simplify") == 0 && i + 1 < argc)
            simplify = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: abigen --seed N [--args M]\n"
                            "       abigen --emit DESC --out DIR\n"
                            "       abigen --simplify K --emit DESC\n");
            return 2;
        }
    }

    if (simplify >= 0) {
        Type *ret;
        Type *args[MAX_ARGS];
        Type *va[MAX_ARGS];
        int na, nva, j, idx = 0;

        if (!emit_file) {
            fprintf(stderr, "abigen: --simplify needs --emit\n");
            return 2;
        }
        load_desc(emit_file, &ret, args, &na, va, &nva);
        ret = simplify_walk(ret, &idx, simplify);
        for (j = 0; j < na; j++)
            args[j] = simplify_walk(args[j], &idx, simplify);
        for (j = 0; j < nva; j++)
            va[j] = simplify_walk(va[j], &idx, simplify);
        /* Out of range: the caller's loop uses this to know it is done. */
        if (simplify >= idx)
            return 1;
        printf("R ");
        print_type(stdout, ret);
        printf("\n");
        for (j = 0; j < na; j++) {
            printf("A ");
            print_type(stdout, args[j]);
            printf("\n");
        }
        for (j = 0; j < nva; j++) {
            printf("V ");
            print_type(stdout, va[j]);
            printf("\n");
        }
        return 0;
    }

    if (!emit_file) {
        Type *ret;
        int j;

        rng_state = seed * 0x2545F4914F6CDD1Dull + 1;
        if (nargs <= 0)
            nargs = 1 + pick(MAX_ARGS);
        /* A void return is common in real code and exercises the no-hidden-
         * pointer path, so it is not rare here either. */
        ret = pick(100) < 25 ? tnew(K_VOID) : gen_type(0, 1);
        printf("R ");
        print_type(stdout, ret);
        printf("\n");
        for (j = 0; j < nargs; j++) {
            Type *a = gen_type(0, 1);

            printf("A ");
            print_type(stdout, a);
            printf("\n");
        }
        /* A variadic tail on a quarter of signatures. It needs a fixed
         * argument for va_start to name, which nargs >= 1 guarantees. */
        if (pick(100) < 25) {
            int nv = 1 + pick(3);

            for (j = 0; j < nv; j++) {
                printf("V ");
                print_type(stdout, gen_va_type());
                printf("\n");
            }
        }
        return 0;
    }

    if (!outdir) {
        fprintf(stderr, "abigen: --emit needs --out\n");
        return 2;
    }
    {
        Type *ret;
        Type *args[MAX_ARGS];
        Type *va[MAX_ARGS];
        int na, nva;
        char path[1024];
        FILE *h, *ca, *ce;
        char nb[64];
        int dummy = 0;
        int counter;

        load_desc(emit_file, &ret, args, &na, va, &nva);

        snprintf(path, sizeof(path), "%s/sig.h", outdir);
        h = fopen(path, "w");
        if (!h) {
            fprintf(stderr, "abigen: cannot write %s\n", path);
            return 2;
        }
        fprintf(h, "/* generated by abigen -- do not edit */\n");
        fprintf(h, "#ifndef ABI_SIG_H\n#define ABI_SIG_H\n");
        emit_typedefs(h, ret, &dummy);
        for (i = 0; i < na; i++)
            emit_typedefs(h, args[i], &dummy);
        for (i = 0; i < nva; i++)
            emit_typedefs(h, va[i], &dummy);
        if (nva)
            fprintf(h, "#include <stdarg.h>\n");
        /* The fold lives in the header so BOTH sides compute it locally;
         * only the argument transport differs between them. */
        fprintf(h, "extern long seen;\n");
        fprintf(h, "static long fold(long h, long v)\n{\n"
                   "    return (h ^ v) * 1099511628211L + 0x9E3779B9L;\n}\n");
        type_name(nb, sizeof(nb), ret);
        fprintf(h, "%s abi_probe(", nb);
        emit_params(h, args, na, nva);
        fprintf(h, ");\n#endif\n");
        fclose(h);

        snprintf(path, sizeof(path), "%s/callee.c", outdir);
        ce = fopen(path, "w");
        fprintf(ce, "#include \"sig.h\"\n\n");
        type_name(nb, sizeof(nb), ret);
        fprintf(ce, "%s abi_probe(", nb);
        emit_params(ce, args, na, nva);
        fprintf(ce, ")\n{\n    long h = 0;\n");
        if (nva)
            fprintf(ce, "    va_list ap;\n\n    va_start(ap, a%d);\n", na - 1);
        for (i = 0; i < na; i++) {
            char e[64];

            snprintf(e, sizeof(e), "a%d", i);
            emit_fold(ce, args[i], e, 0);
        }
        /* Anonymous arguments are fetched in order and folded the same way.
         * Each lands in its own named local first, so a composite folds
         * through an lvalue exactly as a fixed one does. */
        for (i = 0; i < nva; i++) {
            char an[64];
            char e[64];

            type_name(an, sizeof(an), va[i]);
            snprintf(e, sizeof(e), "v%d", i);
            fprintf(ce, "    {\n        %s %s = va_arg(ap, %s);\n", an, e, an);
            emit_fold(ce, va[i], e, 0);
            fprintf(ce, "    }\n");
        }
        if (nva)
            fprintf(ce, "    va_end(ap);\n");
        if (ret->kind == K_VOID) {
            /* Nothing comes back, so the checksum leaves through a global
             * the caller reads -- the arguments are still the thing under
             * test. */
            fprintf(ce, "    seen = h;\n}\n");
        } else if (ret->kind == K_STRUCT || ret->kind == K_UNION) {
            counter = 1;
            fprintf(ce, "    T%d r;\n\n", ret->tag);
            emit_build(ce, ret, "r", &counter, 0);
            fprintf(ce, "    seen = h;\n    return r;\n}\n");
        } else {
            fprintf(ce, "    seen = h;\n    return (%s)(h & %lluL);\n}\n", nb,
                    ret_mask(ret->kind));
        }
        fclose(ce);

        snprintf(path, sizeof(path), "%s/caller.c", outdir);
        ca = fopen(path, "w");
        fprintf(ca, "#include \"sig.h\"\n\nlong seen;\n\n");
        fprintf(ca, "int main(void)\n{\n    long h = 0;\n    long want;\n");
        for (i = 0; i < na; i++) {
            char an[64];

            type_name(an, sizeof(an), args[i]);
            fprintf(ca, "    %s a%d;\n", an, i);
        }
        for (i = 0; i < nva; i++) {
            char an[64];

            type_name(an, sizeof(an), va[i]);
            fprintf(ca, "    %s v%d;\n", an, i);
        }
        if (ret->kind != K_VOID) {
            type_name(nb, sizeof(nb), ret);
            fprintf(ca, "    %s rv;\n", nb);
        }
        fprintf(ca, "\n");
        counter = 0;
        for (i = 0; i < na; i++) {
            char e[64];

            snprintf(e, sizeof(e), "a%d", i);
            emit_build(ca, args[i], e, &counter, 0);
        }
        for (i = 0; i < nva; i++) {
            char e[64];

            snprintf(e, sizeof(e), "v%d", i);
            emit_build(ca, va[i], e, &counter, 0);
        }
        fprintf(ca, "\n");
        /* `want` folds the caller's OWN copies, so the comparison isolates
         * argument TRANSPORT from arithmetic: both sides run the same fold. */
        for (i = 0; i < na; i++) {
            char e[64];

            snprintf(e, sizeof(e), "a%d", i);
            emit_fold(ca, args[i], e, 0);
        }
        for (i = 0; i < nva; i++) {
            char e[64];

            snprintf(e, sizeof(e), "v%d", i);
            emit_fold(ca, va[i], e, 0);
        }
        fprintf(ca, "    want = h;\n");
        fprintf(ca, "    ");
        if (ret->kind == K_VOID)
            fprintf(ca, "abi_probe(");
        else
            fprintf(ca, "rv = abi_probe(");
        for (i = 0; i < na; i++)
            fprintf(ca, "%sa%d", i ? ", " : "", i);
        for (i = 0; i < nva; i++)
            fprintf(ca, "%sv%d", (na || i) ? ", " : "", i);
        fprintf(ca, ");\n");
        fprintf(ca, "    if (seen != want) return 1;\n");
        if (ret->kind == K_STRUCT || ret->kind == K_UNION) {
            /* The RETURN path is its own ABI question: fold what came back
             * and compare with the pattern the callee promised to build. */
            long_return_check(ca, ret);
        } else if (ret->kind != K_VOID) {
            fprintf(ca, "    if ((long)rv != (want & %lluL)) return 3;\n",
                    ret_mask(ret->kind));
        }
        fprintf(ca, "    return 0;\n}\n");
        fclose(ca);
    }
    return 0;
}
