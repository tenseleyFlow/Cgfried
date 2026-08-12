#!/bin/sh
set -eu

gate=
output=
emit=
while [ "$#" -gt 0 ]; do
    case $1 in
        --gate)
            [ "$#" -ge 2 ] || exit 2
            gate=$2
            shift 2
            ;;
        --output)
            [ "$#" -ge 2 ] || exit 2
            output=$2
            shift 2
            ;;
        --emit-passing)
            [ "$#" -ge 2 ] || exit 2
            emit=$2
            shift 2
            ;;
        --)
            shift
            break
            ;;
        -*) exit 2 ;;
        *) break ;;
    esac
done
[ "$#" -gt 0 ] || exit 2

printf 'triage-policy|%s\n' "${CGF_TORTURE_TRIAGE_POLICY:-}" \
    >>"$FAKE_TRIAGE_LOG"

if [ -n "$gate" ]; then
    [ -z "$output$emit" ] || exit 2
    printf 'gate|%s|%s\n' "$gate" "$*" >>"$FAKE_TRIAGE_LOG"
    [ "${FAKE_TRIAGE_FAIL:-}" != gate ] || exit 19
    [ -f "$gate" ] || exit 2
    for results do [ -f "$results" ] || exit 2; done
    exit 0
fi

[ -n "$output$emit" ] || exit 2
printf 'publish|%s|%s|%s\n' "${output:--}" "${emit:--}" "$*" \
    >>"$FAKE_TRIAGE_LOG"
[ "${FAKE_TRIAGE_FAIL:-}" != before-publish ] || exit 19
output_stage=
emit_stage=
trap 'test -z "$output_stage" || rm -f "$output_stage"; test -z "$emit_stage" || rm -f "$emit_stage"' EXIT HUP INT TERM

if [ -n "$output" ]; then
    mkdir -p "$(dirname "$output")"
    output_stage=$(mktemp "$(dirname "$output")/.fake-triage-output.XXXXXX")
    printf '# fixture triage\n\nresults=%s\n' "$*" >"$output_stage"
    [ "${FAKE_TRIAGE_FAIL:-}" != after-output ] || exit 19
fi

if [ -n "$emit" ]; then
    mkdir -p "$(dirname "$emit")"
    emit_stage=$(mktemp "$(dirname "$emit")/.fake-triage-passing.XXXXXX")
    {
        echo '# cgf-torture-passing-v1'
        echo '# Exact PASS set; entries are full matrix-cell keys.'
        echo '# Weekly ritual: re-run the full matrix, fold new passes in, and review the top 3 buckets (30 minutes).'
        for results do sed -n '11,$p' "$results"; done |
            awk -F '\t' '$6 == "PASS" { print $1 }' | LC_ALL=C sort -u
    } >"$emit_stage"
    [ "${FAKE_TRIAGE_FAIL:-}" != after-passing ] || exit 19
fi

if [ -n "$output" ]; then
    mv -f "$output_stage" "$output"
    output_stage=
fi
if [ -n "$emit" ]; then
    mv -f "$emit_stage" "$emit"
    emit_stage=
fi
