#!/bin/sh
# RESOLVED(audit): DET-M-02 stage1 timing derives MAD from a single sample
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT_INPUT=${1:-$SCRIPT_DIR/../..}
ROOT=$(CDPATH= cd -- "$ROOT_INPUT" && pwd) || exit 2
BOOTSTRAP=$ROOT/scripts/bootstrap.sh

[ -r "$BOOTSTRAP" ] || exit 2

if grep -Eq '"\$stage0/timeit" -n 1 -w 0 ' "$BOOTSTRAP"; then
    echo 'DET-M-02 reproduced: stage1 timing still requests one sample'
    exit 0
fi
grep -Eq '"\$stage0/timeit" -n 3 -w 0 ' "$BOOTSTRAP" || {
    echo 'DET-M-02 reproduced: stage1 timing does not request three samples'
    exit 0
}
grep -Eq '"\$make_cmd" -B -s --no-print-directory ' "$BOOTSTRAP" || {
    echo 'DET-M-02 reproduced: repeated timing does not force real rebuilds'
    exit 0
}
grep -Fq "echo 'samples=3'" "$BOOTSTRAP" || {
    echo 'DET-M-02 reproduced: timing receipt does not record three samples'
    exit 0
}

exit 1
