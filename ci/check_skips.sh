#!/bin/sh
# Asserts the EXACT set of HARNESS_SKIP lines in a test log against
# ci/expected_skips_<profile>.txt. Any difference — extra skip, missing
# skip, changed count — fails. The why: an unexpectedly-skipped suite is how
# a broken test silently stops protecting you; asserting the exact set makes
# skips a reviewed, diffable artifact.
#
# Usage: ci/check_skips.sh <profile> <logfile>
set -eu
LC_ALL=C
export LC_ALL

profile=${1:?usage: check_skips.sh <profile> <logfile>}
log=${2:?usage: check_skips.sh <profile> <logfile>}
expected="ci/expected_skips_${profile}.txt"

[ -f "$expected" ] || {
    echo "check_skips: missing expected-skip file: $expected" >&2
    exit 1
}
[ -f "$log" ] || {
    echo "check_skips: missing log file: $log" >&2
    exit 1
}

got=$(grep '^HARNESS_SKIP ' "$log" | sort || true)
exp=$(sort "$expected")

if [ "$got" != "$exp" ]; then
    echo "check_skips: HARNESS_SKIP set differs from $expected" >&2
    echo "--- expected ---" >&2
    printf '%s\n' "$exp" >&2
    echo "--- got ---" >&2
    printf '%s\n' "$got" >&2
    exit 1
fi
echo "check_skips: skip set matches $expected"
