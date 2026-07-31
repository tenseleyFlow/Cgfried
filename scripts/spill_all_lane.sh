#!/bin/sh
# Sprint 22: the CGF_SPILL_ALL=1 lane. Every -emit-mir fixture allocates
# with EVERY interval spilled — the -O0-quality baseline through the SAME
# allocator code path — and must still pass the post-RA MIR verifier (the
# driver ICEs with exit 4 if it does not). MIR_CHECK goldens pin specific
# register patterns, so this lane asserts exit codes + the verifier, not
# text; the behavioral proof is the interpreter differential in
# tests/unit/test_x64_regalloc.c, a quarter of whose seeds run spilled.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgf}
fails=0
n=0
for f in tests/programs/mir/*.c; do
    n=$((n + 1))
    if ! CGF_SPILL_ALL=1 "$CGF" -emit-mir "$f" >/dev/null 2>&1; then
        echo "spill-all lane FAIL: $f" >&2
        fails=$((fails + 1))
    fi
done
if [ "$n" -eq 0 ]; then
    echo "spill-all lane: no -emit-mir fixtures found" >&2
    exit 1
fi
[ "$fails" -eq 0 ] || exit 1
echo "spill-all lane: $n fixtures allocate and verify fully spilled"
