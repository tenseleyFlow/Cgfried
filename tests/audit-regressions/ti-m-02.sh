#!/bin/sh
# XFAIL(audit): TI-M-02 the primary XFAIL ledger reports retired float debt as open
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=${1:-$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)}
LEDGER="$ROOT/.docs/audits/xfail-debt.md"
BANS="$ROOT/scripts/check_bans.sh"
RUNNER="$ROOT/tests/runner/main.c"

for path in "$LEDGER" "$BANS" "$RUNNER"; do
    [ -r "$path" ] || exit 2
done

# Establish that the implementation-side debt really is retired.
grep -Fq 'XD-S08-FPHOST) was retired in Sprint 15' "$BANS" || exit 2
(CDPATH= cd -- "$ROOT" && sh scripts/check_bans.sh) >/dev/null 2>&1 || exit 2

# Then reproduce both ledger-integrity defects: the row remains open and uses
# an ID namespace the runner's ledger loader never indexes.
grep -Eq '^\| XD-S08-FPHOST \| Sprint 15 \|.*\| open \|$' "$LEDGER" || exit 1
grep -Fq 'memcmp(src + pos + 2, "XF-", 3)' "$RUNNER" || exit 2
if grep -Eq '^\| XF-[0-9]{4} \|' "$LEDGER"; then
    exit 1
fi

echo 'TI-M-02 reproduced: retired float debt remains open under an unenforceable XD id'
exit 0
