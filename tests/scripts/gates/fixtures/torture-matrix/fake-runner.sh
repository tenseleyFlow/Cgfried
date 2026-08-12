#!/bin/sh
set -eu

cc=
suite=
level=
target=
manifest=
output=
work=
while [ "$#" -gt 0 ]; do
    key=$1
    shift
    [ "$#" -gt 0 ] || exit 2
    value=$1
    shift
    case $key in
        --cc) cc=$value ;;
        --suite) suite=$value ;;
        --level) level=$value ;;
        --target) target=$value ;;
        --manifest) manifest=$value ;;
        --output) output=$value ;;
        --work) work=$value ;;
        *) exit 2 ;;
    esac
done

[ -n "$cc" ] && [ -n "$suite" ] && [ -n "$level" ] &&
    [ -n "$target" ] && [ -n "$manifest" ] && [ -n "$output" ] &&
    [ -n "$work" ] || exit 2

printf '%s|%s|%s|%s|%s|%s\n' "$suite" "$level" "$target" "$manifest" \
    "$output" "$work" >>"$FAKE_TORTURE_LOG"

if [ "${FAKE_MUTATE_CELL:-}" = "$suite/$level" ]; then
    [ -n "${FAKE_MUTATE_FILE:-}" ] || exit 2
    if [ "${FAKE_MUTATE_REPLACE:-0}" = 1 ]; then
        printf 'b\n' >"$FAKE_MUTATE_FILE"
    else
        printf '\n# changed during matrix\n' >>"$FAKE_MUTATE_FILE"
    fi
fi

if [ "${FAKE_FAIL_CELL:-}" = "$suite/$level" ]; then
    exit 17
fi

mkdir -p "$(dirname "$output")" "$work"
{
    echo '# cgf-torture-results-v1'
    printf '# columns=key\tsuite\tfile\tlevel\ttarget\toutcome\tsignal\tfingerprint\tphase\tdetail\n'
    if [ "${FAKE_OMIT_CELL:-}" != "$suite/$level" ] &&
        [ "${FAKE_NO_ROWS:-0}" != 1 ]; then
        row_suite=$suite
        row_level=$level
        row_target=$target
        row_outcome=PASS
        row_signal=-
        row_fingerprint=-
        row_phase=run
        row_detail=-
        if [ "$suite" = torture-execute ]; then
            row_outcome=SKIP
            row_phase=policy
            row_detail='fixture non-PASS row'
        fi
        if [ "${FAKE_INVALID_SCHEMA_CELL:-}" = "$suite/$level" ]; then
            case ${FAKE_INVALID_SCHEMA_KIND:-} in
                outcome) row_outcome=BOGUS ;;
                signal) row_signal=9 ;;
                phase) row_phase=frontend ;;
                *) exit 2 ;;
            esac
        fi
        if [ "${FAKE_MISLABEL_CELL:-}" = "$suite/$level" ]; then
            case ${FAKE_MISLABEL_KIND:-level} in
                suite) row_suite=ctestsuite ;;
                level) row_level=O3 ;;
                target) row_target=arm64-linux ;;
                *) exit 2 ;;
            esac
        fi
        printf '%s/file.c@%s@%s\t%s\tfile.c\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$row_suite" "$row_level" "$row_target" "$row_suite" \
            "$row_level" "$row_target" "$row_outcome" "$row_signal" \
            "$row_fingerprint" "$row_phase" "$row_detail"
        if [ "${FAKE_DUPLICATE:-0}" = 1 ]; then
            printf '%s/file.c@%s@%s\t%s\tfile.c\t%s\t%s\tPASS\t-\t-\trun\tduplicate\n' \
                "$row_suite" "$row_level" "$row_target" "$row_suite" \
                "$row_level" "$row_target"
        fi
        if [ "${FAKE_UNEXPECTED_CELL:-}" = "$suite/$level" ]; then
            printf '%s/extra.c@%s@%s\t%s\textra.c\t%s\t%s\tSKIP\t-\t-\tpolicy\textra\n' \
                "$suite" "$level" "$target" "$suite" "$level" "$target"
        fi
    fi
} >"$output"
