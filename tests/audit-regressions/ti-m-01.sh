#!/bin/sh
# RESOLVED(audit): TI-M-01 the POSIX-shell expected-skip profile is never enforced
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=${1:-$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cgf-ti-m-01.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

producer="$ROOT/scripts/check_posix_sh.sh"
checker="$ROOT/ci/check_skips.sh"
expected="$ROOT/ci/expected_skips_posixsh-nodash.txt"
makefile="$ROOT/Makefile"

for path in "$producer" "$checker" "$expected" "$makefile"; do
    [ -r "$path" ] || exit 2
done

mkdir "$WORK/empty-path" || exit 2
PATH="$WORK/empty-path" /bin/sh "$producer" >"$WORK/producer.log" || exit 2

cmp -s "$expected" "$WORK/producer.log" || exit 1
(CDPATH= cd -- "$ROOT" && sh ci/check_skips.sh posixsh-nodash \
    "$WORK/producer.log") \
    >"$WORK/checker.out" 2>"$WORK/checker.err" || exit 2

# The defect is the disconnected ratchet: the producer is in `make test`, but
# no recipe or workflow sends its log through the checked posixsh profile.
grep -Fq 'sh scripts/check_posix_sh.sh' "$makefile" || exit 2
if grep -Eq 'check_skips\.sh[[:space:]]+([^[:space:]]+[[:space:]]+)*posixsh' \
    "$makefile" "$ROOT/.github/workflows/ci.yml" 2>/dev/null; then
    exit 1
fi

echo 'TI-M-01 reproduced: posixsh emits an accepted skip, but no gate enforces its profile'
exit 0
