#!/bin/sh
# RESOLVED(audit): TI-M-03 every live c-testsuite debt row cites an obsolete failure cause
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=${1:-$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)}
CGF=${2:-"$ROOT/build/cgfried"}
HOST_CC=${CGF_AUDIT_CC:-cc}
LEDGER="$ROOT/tests/fixtures/ctestsuite-parse-ledger.txt"
CASES="$SCRIPT_DIR/support/ti-m-03"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cgf-ti-m-03.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

[ -x "$CGF" ] || exit 2
[ -r "$LEDGER" ] || exit 2
command -v "$HOST_CC" >/dev/null 2>&1 || exit 2

# The three current rows still claim closed Sprint 28/55 features as causes.
grep -Eq '^00210\.c[[:space:]]+XD-S10-GNUEXT[[:space:]].*attribute.*Sprint 55' \
    "$LEDGER" || exit 1
grep -Eq '^00216\.c[[:space:]]+XD-S10-BUILTIN[[:space:]].*builtin_va_list.*Sprint 28' \
    "$LEDGER" || exit 1
grep -Eq '^00219\.c[[:space:]]+XD-S10-BUILTIN[[:space:]].*builtin_va_list.*Sprint 28' \
    "$LEDGER" || exit 1

for case_name in 00210 00216 00219; do
    source="$CASES/$case_name.txt"
    [ -r "$source" ] || exit 2
    "$HOST_CC" -x c -fsyntax-only -std=c17 -w "$source" \
        >"$WORK/$case_name.host.out" 2>"$WORK/$case_name.host.err" || exit 2
    "$CGF" -x c -fsyntax-only -std=c17 "$source" \
        >"$WORK/$case_name.cgf.out" 2>"$WORK/$case_name.cgf.err"
    status=$?
    [ "$status" -eq 1 ] || { [ "$status" -eq 0 ] && exit 1; exit 2; }
done

grep -Fq 'function returning a function' "$WORK/00210.cgf.err" || exit 1
grep -Fq 'GNU range designators are not supported' "$WORK/00216.cgf.err" || exit 1
grep -Fq "_Generic association for 'const int' duplicates an earlier one" \
    "$WORK/00219.cgf.err" || exit 1

echo 'TI-M-03 reproduced: all three live rows disagree for causes unrelated to their citations'
exit 0
