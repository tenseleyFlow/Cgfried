#!/bin/sh
# Sprint 25 DoD 4: ten designated corpus fixtures compile under BOTH
# cgf and gcc -O0; stdout and exit code must be IDENTICAL (behavioral
# oracle, never an asm compare). Keeps the corpus expectations honest
# continuously — hand-computed EXIT_CODEs rotted unseen before Sprint
# 25 first executed them.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgf}
WORK=${CGF_E2EDIFF_WORK:-build/e2ediff-work}
mkdir -p "$WORK"

if ! command -v gcc >/dev/null 2>&1; then
    echo "HARNESS_SKIP suite=e2ediff test=all count=1" \
        "reason=\"no gcc on this host\""
    exit 0
fi

FIXTURES="int/hello int/fib_rec int/queens8 int/duff int/promo_traps
int/struct_ret int/varargs_sum fp/varargs_mixed fp/u64_double
fp/long_double_basic"

fails=0
n=0
for name in $FIXTURES; do
    f="tests/corpus/x86_64/$name.c"
    base=$(echo "$name" | tr / _)
    n=$((n + 1))
    if ! "$CGF" "$f" -o "$WORK/$base.cgf" 2>"$WORK/$base.cgf.err"; then
        echo "e2ediff FAIL: $name: cgf build failed" >&2
        fails=$((fails + 1))
        continue
    fi
    gcc -O0 -o "$WORK/$base.gcc" "$f" 2>/dev/null
    "$WORK/$base.cgf" >"$WORK/$base.cgf.out" 2>/dev/null
    c=$?
    "$WORK/$base.gcc" >"$WORK/$base.gcc.out" 2>/dev/null
    g=$?
    if [ "$c" != "$g" ]; then
        echo "e2ediff FAIL: $name: exit $c (cgf) vs $g (gcc)" >&2
        fails=$((fails + 1))
        continue
    fi
    if ! cmp -s "$WORK/$base.cgf.out" "$WORK/$base.gcc.out"; then
        echo "e2ediff FAIL: $name: stdout differs" >&2
        fails=$((fails + 1))
        continue
    fi
done
[ "$fails" -eq 0 ] || exit 1
echo "e2ediff: $n/$n fixtures behaviorally identical to gcc -O0"
