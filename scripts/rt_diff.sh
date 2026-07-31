#!/bin/sh
# Sprint 28 (DoD 4): libcgf_rt correctness, with LIBGCC AS THE ORACLE.
#
# One probe program exercising the 128-bit entry points across boundary
# values is linked TWICE — once against libcgf_rt.a, once against
# libgcc — and the two runs must print identical bytes. That single
# construction proves both halves of the sprint at once: the answers
# are right (libgcc says so) AND the symbol names are compatible (the
# link would not resolve otherwise).
#
# The mixing law is checked the other way too: a gcc-compiled object
# calling __divti3 must link against libcgf_rt.a alone and run.
#
# HARNESS_SKIP (loudly) when gcc or libgcc is missing.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_RT_WORK:-build/rt-diff}
RT=$(ls build*/x86_64-linux-gnu/libcgf_rt.a 2>/dev/null | head -1)

if ! command -v gcc >/dev/null 2>&1; then
    echo "HARNESS_SKIP suite=rtdiff test=all count=1" \
        "reason=\"no gcc on this host\""
    exit 0
fi
if [ -z "$RT" ] || [ ! -f "$RT" ]; then
    echo "HARNESS_SKIP suite=rtdiff test=all count=1" \
        "reason=\"libcgf_rt.a not built (make rt)\""
    exit 0
fi
LIBGCC=$(gcc -print-libgcc-file-name 2>/dev/null)
if [ -z "$LIBGCC" ] || [ ! -f "$LIBGCC" ]; then
    echo "HARNESS_SKIP suite=rtdiff test=all count=1" \
        "reason=\"libgcc not found\""
    exit 0
fi

rm -rf "$WORK"
mkdir -p "$WORK"
SRC="$WORK/probe.c"

# The probe calls the rt entry points DIRECTLY by their libgcc names, so
# neither compiler's inlining can substitute its own arithmetic — what
# runs is the archive under test.
cat > "$SRC" << 'EOF'
typedef unsigned long long u64;
typedef unsigned __int128 u128;
typedef signed __int128 i128;

extern u128 __udivti3(u128, u128);
extern u128 __umodti3(u128, u128);
extern i128 __divti3(i128, i128);
extern i128 __modti3(i128, i128);
extern u128 __multi3(u128, u128);
extern i128 __ashlti3(i128, int);
extern i128 __ashrti3(i128, int);
extern u128 __lshrti3(u128, int);
extern int __popcountdi2(u64);
extern int __clzdi2(u64);
extern int __ctzdi2(u64);

int printf(const char *, ...);

static void show(const char *tag, u128 v)
{
    printf("%s %016llx%016llx\n", tag, (unsigned long long)(v >> 64),
           (unsigned long long)v);
}

int main(void)
{
    /* Boundary set: 1, powers of two either side of the 64-bit seam,
     * the all-ones halves, and values that exercise the shift-subtract
     * path (both operands wide). */
    static const int shifts[] = {0, 1, 63, 64, 65, 127};
    u128 vals[16];
    int nv = 0, i, j, k;

    vals[nv++] = 1;
    vals[nv++] = 2;
    vals[nv++] = 3;
    vals[nv++] = ((u128)1 << 63) - 1;
    vals[nv++] = (u128)1 << 63;
    vals[nv++] = ((u128)1 << 63) + 1;
    vals[nv++] = ~(u64)0;
    vals[nv++] = (u128)1 << 64;
    vals[nv++] = ((u128)1 << 64) + 1;
    vals[nv++] = ((u128)1 << 100) + 12345;
    vals[nv++] = ~(u128)0 >> 1;   /* 2^127 - 1 */
    vals[nv++] = ~(u128)0;        /* all ones */
    vals[nv++] = ((u128)0x0123456789abcdefULL << 64) | 0xfedcba9876543210ULL;
    vals[nv++] = 1000000007;
    vals[nv++] = (u128)1 << 96;
    vals[nv++] = 0;

    for (i = 0; i < nv; i++) {
        for (j = 0; j < nv; j++) {
            if (vals[j] == 0)
                continue; /* divide by zero traps by design */
            show("udiv", __udivti3(vals[i], vals[j]));
            show("umod", __umodti3(vals[i], vals[j]));
            show("sdiv", (u128)__divti3((i128)vals[i], (i128)vals[j]));
            show("smod", (u128)__modti3((i128)vals[i], (i128)vals[j]));
            /* Signed with negated operands: the sign rules (quotient
             * takes the XOR, remainder takes the dividend) are where a
             * hand-rolled division usually gets it wrong. */
            show("sdivn", (u128)__divti3(-(i128)vals[i], (i128)vals[j]));
            show("smodn", (u128)__modti3(-(i128)vals[i], (i128)vals[j]));
            show("sdivnn", (u128)__divti3(-(i128)vals[i], -(i128)vals[j]));
            show("mul", __multi3(vals[i], vals[j]));
        }
        for (k = 0; k < (int)(sizeof shifts / sizeof shifts[0]); k++) {
            show("shl", (u128)__ashlti3((i128)vals[i], shifts[k]));
            show("sar", (u128)__ashrti3((i128)vals[i], shifts[k]));
            show("shr", __lshrti3(vals[i], shifts[k]));
            show("sarn", (u128)__ashrti3(-(i128)vals[i], shifts[k]));
        }
        if (vals[i] != 0) {
            u64 lo = (u64)vals[i];

            if (lo != 0)
                printf("bits %d %d %d\n", __popcountdi2(lo), __clzdi2(lo),
                       __ctzdi2(lo));
        }
    }
    return 0;
}
EOF

fails=0
# Same source, same compiler, DIFFERENT runtime archive.
gcc -std=c11 -w -c "$SRC" -o "$WORK/probe.o" || {
    echo "rt_diff: probe failed to compile" >&2
    exit 1
}
gcc "$WORK/probe.o" "$RT" -o "$WORK/ours" || {
    echo "rt_diff: link against libcgf_rt.a failed (symbol names?)" >&2
    exit 1
}
gcc "$WORK/probe.o" "$LIBGCC" -o "$WORK/theirs" || {
    echo "rt_diff: link against libgcc failed" >&2
    exit 1
}
"$WORK/ours" > "$WORK/ours.out" 2>&1
ours_rc=$?
"$WORK/theirs" > "$WORK/theirs.out" 2>&1
theirs_rc=$?
if [ "$ours_rc" -ne "$theirs_rc" ]; then
    echo "rt_diff: exit codes differ (ours=$ours_rc libgcc=$theirs_rc)" >&2
    fails=$((fails + 1))
fi
if ! cmp -s "$WORK/ours.out" "$WORK/theirs.out"; then
    echo "rt_diff: results differ from libgcc:" >&2
    diff -u "$WORK/theirs.out" "$WORK/ours.out" 2>&1 | head -20 >&2
    fails=$((fails + 1))
fi
cases=$(grep -c '' "$WORK/ours.out")

# Mixing law, the other direction: a gcc-compiled object that uses
# 128-bit division NATURALLY (no explicit __divti3 call) must link
# against libcgf_rt.a alone and produce the right answer.
cat > "$WORK/mix.c" << 'EOF'
typedef unsigned __int128 u128;
int printf(const char *, ...);
int main(void)
{
    u128 a = ((u128)0x0123456789abcdefULL << 64) | 0xfedcba9876543210ULL;
    u128 b = 1000000007;
    u128 q = a / b, r = a % b;

    printf("%016llx%016llx %016llx%016llx\n", (unsigned long long)(q >> 64),
           (unsigned long long)q, (unsigned long long)(r >> 64),
           (unsigned long long)r);
    return 0;
}
EOF
gcc -std=c11 -w -c "$WORK/mix.c" -o "$WORK/mix.o"
if gcc -nodefaultlibs "$WORK/mix.o" "$RT" -lc -o "$WORK/mix_ours" \
    2> "$WORK/mix.err"; then
    gcc "$WORK/mix.o" -o "$WORK/mix_theirs"
    if ! cmp -s <("$WORK/mix_ours") <("$WORK/mix_theirs"); then
        echo "rt_diff: mixing law: gcc object + libcgf_rt.a gives a" \
            "different answer than gcc's own runtime" >&2
        fails=$((fails + 1))
    fi
else
    echo "rt_diff: mixing law: gcc object failed to link against" \
        "libcgf_rt.a alone" >&2
    cat "$WORK/mix.err" >&2
    fails=$((fails + 1))
fi

# The fp128 stubs must ABORT, never return a plausible wrong number.
cat > "$WORK/tf.c" << 'EOF'
typedef struct { unsigned long long w[2]; } cgf_tf;
extern cgf_tf __addtf3(cgf_tf, cgf_tf);
int main(void)
{
    cgf_tf a = {{1, 0}}, b = {{2, 0}};
    cgf_tf c = __addtf3(a, b);
    return (int)c.w[0];
}
EOF
gcc -std=c11 -w "$WORK/tf.c" "$RT" -o "$WORK/tf" 2>/dev/null
# The abort is the POINT of this check. The "Aborted (core dumped)"
# line comes from the shell REPORTING the signal, not from the program,
# so it has to be silenced in the reporting shell — an inner sh whose
# own stderr is redirected.
sh -c '"$1" > "$2" 2>&1' _ "$WORK/tf" "$WORK/tf.out" 2>/dev/null
tf_rc=$?
if [ "$tf_rc" -eq 0 ]; then
    echo "rt_diff: an fp128 stub RETURNED instead of aborting — a silent" \
        "wrong answer is exactly what Sprint 28 forbids" >&2
    fails=$((fails + 1))
fi
grep -q "Sprint 49" "$WORK/tf.out" || {
    echo "rt_diff: fp128 stub did not name Sprint 49" >&2
    cat "$WORK/tf.out" >&2
    fails=$((fails + 1))
}

# DoD 7: the archive is reproducible.
cp "$RT" "$WORK/first.a"
rm -f "$RT"
# BUILD must be threaded through: `make test` may be running in a
# different tree (build-san), and rebuilding into build/ would compare
# an archive that is not the one under test.
make -s BUILD="${BUILD:-build}" rt > /dev/null 2>&1
if ! cmp -s "$WORK/first.a" "$RT"; then
    echo "rt_diff: libcgf_rt.a is not byte-reproducible across builds" >&2
    fails=$((fails + 1))
fi

[ "$fails" -eq 0 ] || exit 1
echo "rt_diff: $cases result lines identical to libgcc; mixing law both" \
    "directions; fp128 stubs abort naming Sprint 49; archive reproducible"
