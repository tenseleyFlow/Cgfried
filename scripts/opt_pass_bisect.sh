#!/bin/sh
# Diagnose one executable C source.  Exit 0 means an optimizer-level mismatch
# was localized; exit 1 means all levels match O0; exit 2 means O0 could not
# establish a source baseline; exit 3 means the diagnostic setup is invalid.
# Status, stdout, and stderr are all compared. Each compile and execution is
# time-bounded; artifacts are always retained and their directory is printed.
set -u

LC_ALL=C
TZ=UTC
SOURCE_DATE_EPOCH=946684800
export LC_ALL TZ SOURCE_DATE_EPOCH

usage()
{
    echo "usage: opt_pass_bisect.sh <cgfried> <source.c>" >&2
    exit 3
}

[ "$#" -eq 2 ] || usage
CGF=$1
SOURCE=$2
[ -x "$CGF" ] || {
    echo "opt_pass_bisect: compiler is not executable: $CGF" >&2
    exit 3
}
[ -r "$SOURCE" ] || {
    echo "opt_pass_bisect: source is not readable: $SOURCE" >&2
    exit 3
}
TIMEOUT_SECS=${CGF_OPT_BISECT_TIMEOUT:-20}
TIMEOUT_CMD=${CGF_OPT_BISECT_TIMEOUT_CMD:-timeout}
case "$TIMEOUT_SECS" in
    ''|*[!0-9]*|0)
        echo "opt_pass_bisect: CGF_OPT_BISECT_TIMEOUT must be a positive integer" >&2
        exit 3
        ;;
esac
TIMEOUT_PATH=$(command -v "$TIMEOUT_CMD") || {
    echo "opt_pass_bisect: timeout command is unavailable: $TIMEOUT_CMD" >&2
    exit 3
}

if [ -n "${CGF_OPT_BISECT_WORK:-}" ]; then
    WORK=$CGF_OPT_BISECT_WORK
    if [ -e "$WORK" ]; then
        [ -d "$WORK" ] || {
            echo "opt_pass_bisect: work path is not a directory: $WORK" >&2
            exit 3
        }
        [ -z "$(find "$WORK" ! -path "$WORK" -print | sed -n '1p')" ] || {
            echo "opt_pass_bisect: work directory is not empty: $WORK" >&2
            exit 3
        }
    else
        mkdir -p "$WORK" || exit 3
    fi
else
    WORK=$(mktemp -d "${TMPDIR:-/tmp}/cgf-opt-bisect.XXXXXX") || exit 3
fi
printf 'opt_pass_bisect: artifacts=%s\n' "$WORK"

setup_fail()
{
    printf 'opt_pass_bisect: %s\n' "$*" >&2
    exit 3
}

run_case()
{
    case_name=$1
    case_level=$2
    case_toggle=$3
    case_dir="$WORK/$case_name"
    dump_dir="$case_dir/phases"
    toggle_bce=
    toggle_fusion=
    toggle_unswitch=
    toggle_vectorize=
    case "$case_toggle" in
        '') ;;
        BCE) toggle_bce=1 ;;
        FUSION) toggle_fusion=1 ;;
        UNSWITCH) toggle_unswitch=1 ;;
        VECTORIZE) toggle_vectorize=1 ;;
        *) setup_fail "unknown pass toggle: $case_toggle" ;;
    esac
    mkdir -p "$dump_dir" || setup_fail "cannot create phase directory: $dump_dir"

    set +e
    "$TIMEOUT_PATH" "$TIMEOUT_SECS" env \
        CGF_AS=0 CGF_AS_PATH= CGF_LD=0 CGF_LD_PATH= CGF_CRT_DIR= \
        CGF_OPT_DISABLE_BCE="$toggle_bce" \
        CGF_OPT_DISABLE_FUSION="$toggle_fusion" \
        CGF_OPT_DISABLE_UNSWITCH="$toggle_unswitch" \
        CGF_OPT_DISABLE_VECTORIZE="$toggle_vectorize" \
        CGF_VERIFY_AFTER_EACH=1 CGF_DUMP_IR=all \
        CGF_DUMP_IR_DIR="$dump_dir" \
        "$CGF" "-$case_level" "$SOURCE" -o "$case_dir/a.out" \
        >"$case_dir/compile.stdout" 2>"$case_dir/compile.stderr"
    compile_status=$?
    set -e
    printf '%s\n' "$compile_status" >"$case_dir/compile.status"
    if [ "$compile_status" -ne 0 ]; then
        compile_label=$compile_status
        [ "$compile_status" -eq 124 ] && compile_label=timeout
        printf 'opt_pass_bisect: %-22s compile=%s run=- phases=%s\n' \
            "$case_name" "$compile_label" "$dump_dir"
        return 0
    fi

    set +e
    "$TIMEOUT_PATH" "$TIMEOUT_SECS" "$case_dir/a.out" \
        >"$case_dir/run.stdout" 2>"$case_dir/run.stderr"
    run_status=$?
    set -e
    printf '%s\n' "$run_status" >"$case_dir/run.status"
    run_label=$run_status
    [ "$run_status" -eq 124 ] && run_label=timeout
    printf 'opt_pass_bisect: %-22s compile=0 run=%s phases=%s\n' \
        "$case_name" "$run_label" "$dump_dir"
}

set -e
run_case level-O0 O0 ''
if [ "$(sed -n '1p' "$WORK/level-O0/compile.status")" -ne 0 ]; then
    echo "opt_pass_bisect: O0 did not establish an executable source baseline" >&2
    exit 2
fi

first_failure=
for level in O1 O2 Os O3 Ofast; do
    run_case "level-$level" "$level" ''
    case_dir="$WORK/level-$level"
    if [ "$(sed -n '1p' "$case_dir/compile.status")" -ne 0 ]; then
        [ -n "$first_failure" ] || first_failure=$level
    elif ! cmp -s "$WORK/level-O0/run.status" "$case_dir/run.status" ||
         ! cmp -s "$WORK/level-O0/run.stdout" "$case_dir/run.stdout" ||
         ! cmp -s "$WORK/level-O0/run.stderr" "$case_dir/run.stderr"; then
        [ -n "$first_failure" ] || first_failure=$level
    fi
done

target=${first_failure:-O3}
printf 'opt_pass_bisect: toggle_level=-%s first_failure=%s\n' \
    "$target" "${first_failure:-none}"

for toggle in BCE FUSION UNSWITCH VECTORIZE; do
    case "$toggle" in
        BCE) toggle_name=bce ;;
        FUSION) toggle_name=fusion ;;
        UNSWITCH) toggle_name=unswitch ;;
        VECTORIZE) toggle_name=vectorize ;;
    esac
    run_case "disable-$toggle_name" "$target" "$toggle"
done

if [ -z "$first_failure" ]; then
    echo "opt_pass_bisect: no optimizer-level mismatch found; O3 toggles recorded for comparison"
    exit 1
fi

echo "opt_pass_bisect: localized first optimizer-level mismatch at -$first_failure"
exit 0
