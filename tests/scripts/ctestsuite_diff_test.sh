#!/bin/sh
# Focused contract test for scripts/ctestsuite_diff.sh. This file also acts as
# its fake compilers through symlinks so the test remains self-contained.
set -eu
LC_ALL=C
export LC_ALL

case ${0##*/} in
fake-cgf)
    source=
    for arg in "$@"; do source=$arg; done
    case ${source##*/}:${CGF_CTEST_META_MODE:-known} in
    00001.c:known)
        echo 'fixture.c:1:1: error: expected diagnostic fingerprint' >&2
        exit 1
        ;;
    00001.c:drift)
        echo 'fixture.c:1:1: error: unrelated replacement failure' >&2
        exit 1
        ;;
    esac
    exit 0
    ;;
fake-gcc)
    exit 0
    ;;
esac

repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-ctestsuite-diff-test.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/corpus"
: >"$work/corpus/00001.c"
: >"$work/corpus/00002.c"
ln -s "$repo/tests/scripts/ctestsuite_diff_test.sh" "$work/fake-cgf"
ln -s "$repo/tests/scripts/ctestsuite_diff_test.sh" "$work/fake-gcc"

ledger=$work/ledger.tsv
printf '%s\t%s\t%s\t%s\n' \
    00001.c XD-TI-KNOWN 'expected diagnostic fingerprint' \
    'fixture known rejection' >"$ledger"

run_diff() {
    CGF_DIFF_GCC="$work/fake-gcc" CGF_CTEST_META_MODE=${1:-known} \
        sh "$repo/scripts/ctestsuite_diff.sh" "$work/fake-cgf" \
        "$work/corpus" "$ledger"
}

out=$(run_diff known 2>&1) || {
    echo "ctestsuite_diff_test: valid known debt failed: $out" >&2
    exit 1
}
case $out in
*'2 files, 1 agree, 1 known-deferred, 0 new, 0 xpass'*) ;;
*) echo "ctestsuite_diff_test: unexpected success summary: $out" >&2; exit 1 ;;
esac

if out=$(run_diff drift 2>&1); then
    echo 'ctestsuite_diff_test: diagnostic drift was accepted' >&2
    exit 1
fi
case $out in
*'DIAGNOSTIC DRIFT 00001.c (XD-TI-KNOWN no longer matches: expected diagnostic fingerprint)'*) ;;
*) echo "ctestsuite_diff_test: drift diagnostic missing: $out" >&2; exit 1 ;;
esac

if out=$(run_diff xpass 2>&1); then
    echo 'ctestsuite_diff_test: XPASS was accepted' >&2
    exit 1
fi
case $out in
*'XPASS 00001.c now agrees with gcc'*) ;;
*) echo "ctestsuite_diff_test: XPASS diagnostic missing: $out" >&2; exit 1 ;;
esac

printf '%s\t%s\t%s\t%s\n' \
    00001.c XD-TI-KNOWN 'expected diagnostic fingerprint' \
    'fixture known rejection' >>"$ledger"
if out=$(run_diff known 2>&1); then
    echo 'ctestsuite_diff_test: duplicate ledger row was accepted' >&2
    exit 1
fi
case $out in
*'malformed, unsorted, or duplicate ledger'*) ;;
*) echo "ctestsuite_diff_test: ledger diagnostic missing: $out" >&2; exit 1 ;;
esac

: >"$ledger"
out=$(run_diff xpass 2>&1) || {
    echo "ctestsuite_diff_test: empty debt ledger failed: $out" >&2
    exit 1
}
case $out in
*'2 files, 2 agree, 0 known-deferred, 0 new, 0 xpass'*) ;;
*) echo "ctestsuite_diff_test: unexpected empty-ledger summary: $out" >&2; exit 1 ;;
esac

echo 'ctestsuite_diff_test: all contract checks passed'
