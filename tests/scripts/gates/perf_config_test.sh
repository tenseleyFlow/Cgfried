#!/bin/sh
set -eu

script_dir=$(dirname -- "$0")
repo=$(CDPATH='' cd "$script_dir/../../.." && pwd -P)
checker=$repo/scripts/check_perf_configs.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-perf-config-test.XXXXXX") || {
    echo 'perf_config_test: cannot create temporary directory' >&2
    exit 1
}
trap 'rm -rf "$work"' EXIT HUP INT TERM
tests=0

fail()
{
    echo "perf_config_test: $*" >&2
    exit 1
}

write_config()
{
    state=$1
    deferral=${2-}
    mkdir -p "$work/configs"
    {
        printf '%s\n' \
            'name=fixture-gate' \
            "state=$state" \
            'where=fixture' \
            'when=fixture' \
            'threshold=fixture' \
            'rationale=Fixture gate' \
            'owner_sprint=Sprint 54'
        [ -z "$deferral" ] || printf '%s\n' "$deferral"
    } >"$work/configs/fixture-gate.conf"
    printf '%s\n' '<!-- perf-gate:fixture-gate -->' >"$work/perf-gates.md"
}

expect_status()
{
    name=$1
    expected=$2
    shift 2
    tests=$((tests + 1))
    status=0
    "$@" >"$work/$name.out" 2>"$work/$name.err" || status=$?
    [ "$status" -eq "$expected" ] || {
        cat "$work/$name.out" "$work/$name.err" >&2
        fail "$name exited $status, expected $expected"
    }
}

printf '# fixture ratchet\n\n57\n' >"$work/closed-57.txt"

write_config inactive 'defer_until=Sprint 58'
expect_status open-58 0 "$checker" "$work/configs" \
    "$work/perf-gates.md" "$work/closed-57.txt"

# Omitting the third argument remains supported and uses the repository ratchet.
mkdir -p "$work/compat/scripts" "$work/compat/ci"
cp "$checker" "$work/compat/scripts/check_perf_configs.sh"
printf '57\n' >"$work/compat/ci/closed_sprints.txt"
expect_status two-argument-compatibility 0 \
    "$work/compat/scripts/check_perf_configs.sh" "$work/configs" \
    "$work/perf-gates.md"

write_config inactive 'defer_until=Sprint 57'
expect_status closed-57 3 "$checker" "$work/configs" \
    "$work/perf-gates.md" "$work/closed-57.txt"

write_config inactive
expect_status missing-deferral 3 "$checker" "$work/configs" \
    "$work/perf-gates.md" "$work/closed-57.txt"

write_config inactive 'defer_until=58'
expect_status malformed-deferral 3 "$checker" "$work/configs" \
    "$work/perf-gates.md" "$work/closed-57.txt"

write_config blocking 'defer_until=Sprint 58'
expect_status active-with-deferral 3 "$checker" "$work/configs" \
    "$work/perf-gates.md" "$work/closed-57.txt"

write_config inactive 'defer_until=Sprint 58'
printf 'Sprint 57\n' >"$work/malformed-closed.txt"
expect_status malformed-closed-sprints 3 "$checker" "$work/configs" \
    "$work/perf-gates.md" "$work/malformed-closed.txt"

printf '56\n57\n' >"$work/multiple-closed.txt"
expect_status multiple-closed-sprints 3 "$checker" "$work/configs" \
    "$work/perf-gates.md" "$work/multiple-closed.txt"

expect_status missing-closed-sprints 3 "$checker" "$work/configs" \
    "$work/perf-gates.md" "$work/absent-closed.txt"

echo "perf config tests: PASS ($tests cases)"
