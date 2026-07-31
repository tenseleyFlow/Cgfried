#!/bin/sh
# Sprint 24: the emission differential. Every backend-complete fixture
# (tests/programs/mir + tests/programs/lower-exec — the Sprint 21-23
# corpus) goes `cgf -S` -> system gas (THE CONTRACT: zero diagnostics)
# and, when afs-as is built, the SAME .s through afs-as with
# cgf-objdiff comparing the objects (strict .text/.rodata/.data,
# normalized symbols/relocs). Lane skew — a fixture passing one
# assembler and failing the other — is a hard failure, not a skip.
#
# Determinism is asserted here too: each fixture emits twice and the
# two .s files must be byte-identical.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgf}
OBJDIFF=${2:-build/cgf-objdiff}
WORK=${CGF_OBJDIFF_WORK:-build/objdiff-work}
AFS=afs-as/target/release/afs-as

mkdir -p "$WORK"
fails=0
n=0

have_afs=0
[ -x "$AFS" ] && have_afs=1

for f in tests/programs/mir/*.c tests/programs/lower-exec/*.c; do
    n=$((n + 1))
    base=$(basename "$f" .c)
    s1="$WORK/$base.s"
    s2="$WORK/$base.2.s"
    if ! "$CGF" -S "$f" -o "$s1" 2>"$WORK/$base.cgf.err"; then
        echo "objdiff FAIL: $f: cgf -S failed" >&2
        cat "$WORK/$base.cgf.err" >&2
        fails=$((fails + 1))
        continue
    fi
    "$CGF" -S "$f" -o "$s2" 2>/dev/null
    if ! cmp -s "$s1" "$s2"; then
        echo "objdiff FAIL: $f: two emissions differ (determinism)" >&2
        fails=$((fails + 1))
        continue
    fi
    # THE contract: assembles under system gas with zero diagnostics.
    if ! as "$s1" -o "$WORK/$base.gas.o" 2>"$WORK/$base.gas.err" ||
        [ -s "$WORK/$base.gas.err" ]; then
        echo "objdiff FAIL: $f: gas rejected or warned on cgf -S output" >&2
        cat "$WORK/$base.gas.err" >&2
        fails=$((fails + 1))
        continue
    fi
    if [ "$have_afs" = 1 ]; then
        if ! "$AFS" --64 "$s1" -o "$WORK/$base.afs.o" \
            2>"$WORK/$base.afs.err"; then
            echo "objdiff FAIL: $f: afs-as rejected cgf -S output" \
                "(lane skew)" >&2
            cat "$WORK/$base.afs.err" >&2
            fails=$((fails + 1))
            continue
        fi
        if ! "$OBJDIFF" "$WORK/$base.afs.o" "$WORK/$base.gas.o"; then
            echo "objdiff FAIL: $f: objects diverge" >&2
            fails=$((fails + 1))
            continue
        fi
    fi
done

if [ "$n" -eq 0 ]; then
    echo "objdiff lane: no fixtures found" >&2
    exit 1
fi
if [ "$have_afs" = 0 ]; then
    echo "HARNESS_SKIP suite=objdiff test=afs-lane count=1" \
        "reason=\"afs-as not built (make tools); gas contract still ran\""
fi
[ "$fails" -eq 0 ] || exit 1
if [ "$have_afs" = 1 ]; then
    echo "objdiff lane: $n fixtures emit, assemble under BOTH, objects agree"
else
    echo "objdiff lane: $n fixtures emit and assemble under gas (contract)"
fi
