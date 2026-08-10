#!/bin/sh
# CGF_STATS is diagnostic-only: its four deterministic records must not alter
# stdout or the generated artifact, and failures must still report the schema.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:?usage: stats_smoke.sh path/to/cgfried}
ROOT=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
WORK=${CGF_STATS_WORK:-$ROOT/build/stats-smoke}
SRC=$ROOT/tests/corpus/x86_64/int/ackermann_small.c

fail()
{
    echo "stats_smoke: $*" >&2
    exit 1
}

[ -x "$CGF" ] || fail "compiler is not executable: $CGF"
[ -f "$SRC" ] || fail "missing fixture: $SRC"
mkdir -p "$WORK"

SOURCE_DATE_EPOCH=0 CGF_STATS=1 "$CGF" -S "$SRC" -o "$WORK/a.s" \
    >"$WORK/a.out" 2>"$WORK/a.err"
SOURCE_DATE_EPOCH=0 CGF_STATS=1 "$CGF" -S "$SRC" -o "$WORK/b.s" \
    >"$WORK/b.out" 2>"$WORK/b.err"
SOURCE_DATE_EPOCH=0 "$CGF" -S "$SRC" -o "$WORK/control.s" \
    >"$WORK/control.out" 2>"$WORK/control.err"

[ ! -s "$WORK/a.out" ] || fail "CGF_STATS polluted stdout"
[ ! -s "$WORK/b.out" ] || fail "second CGF_STATS run polluted stdout"
[ ! -s "$WORK/control.out" ] || fail "control compile polluted stdout"
[ ! -s "$WORK/control.err" ] || fail "control compile produced diagnostics"
cmp -s "$WORK/a.err" "$WORK/b.err" || fail "statistics are nondeterministic"
cmp -s "$WORK/a.s" "$WORK/b.s" || fail "statistics changed repeat assembly"
cmp -s "$WORK/a.s" "$WORK/control.s" || fail "statistics changed assembly"

[ "$(wc -l < "$WORK/a.err")" -eq 4 ] ||
    fail "expected exactly four statistics records"
grep -Eq '^stat: arena\.ast peak_kb=[0-9]+ blocks=[0-9]+ waste_pct=([0-9]|[1-9][0-9]|100)$' \
    "$WORK/a.err" || fail "bad arena.ast record"
grep -Eq '^stat: arena\.ir peak_kb=[0-9]+ blocks=[0-9]+ waste_pct=([0-9]|[1-9][0-9]|100)$' \
    "$WORK/a.err" || fail "bad arena.ir record"
grep -Eq '^stat: intern lookups=[0-9]+ hits=[0-9]+ hit_pct=([0-9]|[1-9][0-9]|100)$' \
    "$WORK/a.err" || fail "bad intern record"
grep -Eq '^stat: pp includes=[0-9]+ guard_skips=[0-9]+ tokens=[0-9]+$' \
    "$WORK/a.err" || fail "bad pp record"

if CGF_STATS=1 "$CGF" -fsyntax-only "$WORK/does-not-exist.c" \
    >"$WORK/fail.out" 2>"$WORK/fail.err"; then
    fail "missing input unexpectedly compiled"
fi
[ ! -s "$WORK/fail.out" ] || fail "failed compile polluted stdout"
[ "$(grep -c '^stat: ' "$WORK/fail.err")" -eq 4 ] ||
    fail "failed compile did not emit four statistics records"

if CGF_STATS=1 "$CGF" >"$WORK/no-input.out" 2>"$WORK/no-input.err"; then
    fail "no-input invocation unexpectedly succeeded"
fi
[ ! -s "$WORK/no-input.out" ] || fail "no-input invocation polluted stdout"
[ "$(grep -c '^stat: ' "$WORK/no-input.err")" -eq 4 ] ||
    fail "no-input invocation did not emit four statistics records"

echo "stats_smoke: four deterministic records; artifacts unchanged"
