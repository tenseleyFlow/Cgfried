#!/bin/sh
set -eu

cc=${1:-build/cgfried}
oracle=${CGF_DIFF_GCC:-gcc}
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-strict-alias.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
src=tests/programs/opt/s32_strict_alias.c

compile()
{
    compiler=$1
    shift
    "$compiler" "$@" "$src" -o "$work/a.out"
}

run_status()
{
    set +e
    "$work/a.out"
    status=$?
    set -e
    printf '%s\n' "$status"
}

compile "$cc" -O0
cgf_o0=$(run_status)
compile "$cc" -O2 -fno-strict-aliasing
cgf_nostrict=$(run_status)
compile "$cc" -O2
cgf_strict=$(run_status)

compile "$oracle" -O0
gcc_o0=$(run_status)
compile "$oracle" -O2 -fno-strict-aliasing
gcc_nostrict=$(run_status)
compile "$oracle" -O2 -fstrict-aliasing
gcc_strict=$(run_status)

test "$cgf_o0" = "$cgf_nostrict"
test "$cgf_o0" = "$gcc_o0"
test "$cgf_nostrict" = "$gcc_nostrict"
test "$cgf_strict" = "$gcc_strict"
test "$cgf_strict" != "$cgf_nostrict"

echo "strict_alias_diff: O0/no-strict=$cgf_o0 strict=$cgf_strict (gcc agrees)"
