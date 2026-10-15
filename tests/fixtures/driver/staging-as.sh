#!/bin/sh
set -eu

: "${CGF_STAGE_LOG:?CGF_STAGE_LOG is required}"
: "${CGF_STAGE_CASE:?CGF_STAGE_CASE is required}"

input=
output=
want_output=no
for arg in "$@"
do
    if [ "$want_output" = yes ]; then
        output=$arg
        want_output=no
        continue
    fi
    case "$arg" in
        --64|--nocompress-debug-sections|--target=*) ;;
        -o) want_output=yes ;;
        *) input=$arg ;;
    esac
done

[ -n "$input" ] && [ -f "$input" ] || {
    echo "staging-as: missing assembly input" >&2
    exit 90
}
[ -n "$output" ] || {
    echo "staging-as: missing object output" >&2
    exit 91
}

printf '%s|%s|%s\n' "$CGF_STAGE_CASE" "$input" "$output" >>"$CGF_STAGE_LOG"

if [ "${CGF_FAKE_AS_FAIL:-0}" = 1 ]; then
    printf '%s:7: error: intentional staging-test rejection\n' "$input" >&2
    exit 17
fi

printf '%s\n' FAKE-OBJECT >"$output"
