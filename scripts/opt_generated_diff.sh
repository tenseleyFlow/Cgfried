#!/bin/sh
# Sprint 60 optimizer validation campaign.  The generator is responsible for
# emitting defined, executable C programs carrying "// OPT_EQ: all".
set -u

LC_ALL=C
TZ=UTC
SOURCE_DATE_EPOCH=946684800
export LC_ALL TZ SOURCE_DATE_EPOCH

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)
CGF=${1:-"$ROOT/build/cgfried"}
OPTGEN=${2:-"$ROOT/build/optgen"}
RUNNER=${CGF_OPT_AUDIT_RUNNER:-"$ROOT/build/cgf-test"}
CASES=${CGF_OPT_AUDIT_CASES:-16}
SEED=${CGF_OPT_AUDIT_SEED:-6001}
GCC=${CGF_DIFF_GCC:-gcc}
CLANG=${CGF_DIFF_CLANG:-clang}
RECEIPT=${CGF_OPT_AUDIT_RECEIPT:-"$ROOT/build/opt-generated.receipt"}
TIMEOUT_SECS=${CGF_OPT_AUDIT_TIMEOUT:-20}
TIMEOUT_CMD=${CGF_OPT_AUDIT_TIMEOUT_CMD:-timeout}
KEEP=1
CALLER_WORK=0

fail()
{
    printf 'opt_generated_diff: FAIL: %s\n' "$*" >&2
    exit 1
}

case "$CASES" in
    ''|*[!0-9]*)
        echo "opt_generated_diff: CGF_OPT_AUDIT_CASES must be a positive integer" >&2
        exit 2
        ;;
    0)
        echo "opt_generated_diff: CGF_OPT_AUDIT_CASES must be greater than zero" >&2
        exit 2
        ;;
esac
case "$SEED" in
    ''|*[!0-9]*)
        echo "opt_generated_diff: CGF_OPT_AUDIT_SEED must be a nonnegative integer" >&2
        exit 2
        ;;
esac
case "$TIMEOUT_SECS" in
    ''|*[!0-9]*|0)
        echo "opt_generated_diff: CGF_OPT_AUDIT_TIMEOUT must be a positive integer" >&2
        exit 2
        ;;
esac
TIMEOUT_PATH=$(command -v "$TIMEOUT_CMD") || {
    echo "opt_generated_diff: timeout command is unavailable: $TIMEOUT_CMD" >&2
    exit 2
}

if [ -n "${CGF_OPT_AUDIT_WORK:-}" ]; then
    WORK=$CGF_OPT_AUDIT_WORK
    CALLER_WORK=1
    if [ -e "$WORK" ]; then
        [ -d "$WORK" ] || {
            echo "opt_generated_diff: work path is not a directory: $WORK" >&2
            exit 2
        }
        [ -z "$(find "$WORK" ! -path "$WORK" -print | sed -n '1p')" ] || {
            echo "opt_generated_diff: work directory is not empty: $WORK" >&2
            exit 2
        }
    else
        mkdir -p "$WORK" || exit 2
    fi
else
    WORK=$(mktemp -d "${TMPDIR:-/tmp}/cgf-opt-generated.XXXXXX") || exit 2
fi

cleanup()
{
    if [ "$KEEP" -eq 0 ] && [ "$CALLER_WORK" -eq 0 ]; then
        rm -rf "$WORK"
    elif [ -d "$WORK" ]; then
        printf 'opt_generated_diff: retained artifacts: %s\n' "$WORK" >&2
    fi
}
trap cleanup EXIT HUP INT TERM

for tool in "$CGF" "$OPTGEN" "$RUNNER"; do
    [ -x "$tool" ] || fail "required executable is unavailable: $tool"
done

# Resolve symlink chains rather than treating two PATH spellings as independent
# oracles. The inode tuple also rejects hard-linked copies of one executable.
canonical_path()
{
    path=$1
    case "$path" in
    /*) ;;
    *)
        path="$(CDPATH='' cd -- "$(dirname -- "$path")" && pwd -P)/$(basename -- "$path")" ||
            return 1
        ;;
    esac
    while [ -L "$path" ]; do
        link=$(readlink "$path") || return 1
        case "$link" in
        /*) path=$link ;;
        *) path="$(CDPATH='' cd -- "$(dirname -- "$path")" && pwd -P)/$link" ;;
        esac
    done
    directory=$(CDPATH='' cd -- "$(dirname -- "$path")" && pwd -P) || return 1
    printf '%s/%s\n' "$directory" "$(basename -- "$path")"
}

file_identity()
{
    if identity=$(stat -Lc '%d:%i' "$1" 2>/dev/null); then
        printf '%s\n' "$identity"
    elif identity=$(stat -f '%d:%i' "$1" 2>/dev/null); then
        printf '%s\n' "$identity"
    else
        return 1
    fi
}

GCC_PATH=$(command -v "$GCC") || fail "host oracle is unavailable: $GCC"
CLANG_PATH=$(command -v "$CLANG") ||
    fail "host oracle is unavailable: $CLANG"
[ -x "$GCC_PATH" ] || fail "host oracle is not executable: $GCC_PATH"
[ -x "$CLANG_PATH" ] || fail "host oracle is not executable: $CLANG_PATH"
GCC_PATH=$(canonical_path "$GCC_PATH") ||
    fail "cannot canonicalize host oracle: $GCC"
CLANG_PATH=$(canonical_path "$CLANG_PATH") ||
    fail "cannot canonicalize host oracle: $CLANG"
GCC_ID=$(file_identity "$GCC_PATH") ||
    fail "cannot identify host oracle: $GCC_PATH"
CLANG_ID=$(file_identity "$CLANG_PATH") ||
    fail "cannot identify host oracle: $CLANG_PATH"
if [ "$GCC_PATH" = "$CLANG_PATH" ] || [ "$GCC_ID" = "$CLANG_ID" ]; then
    fail "host oracles resolve to one executable: $GCC_PATH ($GCC_ID)"
fi
GCC_VERSION=$("$GCC_PATH" --version 2>&1 | sed -n '1p')
CLANG_VERSION=$("$CLANG_PATH" --version 2>&1 | sed -n '1p')
RECEIPT_DIR=$(dirname -- "$RECEIPT")
[ -d "$RECEIPT_DIR" ] || fail "receipt directory is unavailable: $RECEIPT_DIR"

write_receipt()
{
    receipt_tmp="$RECEIPT.tmp.$$"

    {
        printf 'campaign=opt-generated\n'
        printf 'status=PASS\n'
        printf 'cases=%s\n' "$CASES"
        printf 'seed=%s\n' "$SEED"
        printf 'generator_digest=%s\n' "$generator_digest"
        printf 'timeout_seconds=%s\n' "$TIMEOUT_SECS"
        printf 'gcc.path=%s\n' "$GCC_PATH"
        printf 'gcc.identity=%s\n' "$GCC_ID"
        printf 'gcc.version=%s\n' "$GCC_VERSION"
        printf 'clang.path=%s\n' "$CLANG_PATH"
        printf 'clang.identity=%s\n' "$CLANG_ID"
        printf 'clang.version=%s\n' "$CLANG_VERSION"
    } >"$receipt_tmp" || fail "cannot write receipt: $receipt_tmp"
    mv -f "$receipt_tmp" "$RECEIPT" || fail "cannot publish receipt: $RECEIPT"
}

SOURCES="$WORK/sources"
ORACLE="$WORK/oracle"
mkdir "$SOURCES" "$ORACLE" || fail "cannot create isolated campaign directories"

run_timed()
{
    status_file=$1
    shift
    if "$TIMEOUT_PATH" "$TIMEOUT_SECS" "$@"; then
        TIMED_STATUS=0
    else
        TIMED_STATUS=$?
    fi
    printf '%s\n' "$TIMED_STATUS" >"$status_file" ||
        fail "cannot record command status: $status_file"
}

run_timed "$WORK/generator.status" "$OPTGEN" --out "$SOURCES" \
    --count "$CASES" --seed "$SEED" --hash \
    >"$WORK/generator.stdout" 2>"$WORK/generator.stderr"
case "$TIMED_STATUS" in
0) ;;
124) fail "generator timed out after ${TIMEOUT_SECS}s (see generator.stderr)" ;;
*) fail "generator failed with exit $TIMED_STATUS (see generator.stderr)" ;;
esac

generator_digest=$(awk '$1 == "optgen" && $2 == "hash" { print $3 }' \
    "$WORK/generator.stdout")
if [ "$CASES" -eq 16 ] && [ "$SEED" -eq 6001 ]; then
    digest_file=${CGF_OPT_AUDIT_DIGEST:-"$ROOT/ci/optgen_sequence_digest.txt"}
    [ -r "$digest_file" ] || fail "missing canonical generator digest: $digest_file"
    expected_digest=$(sed -n '1p' "$digest_file")
    if [ "$generator_digest" != "$expected_digest" ]; then
        fail "generator digest changed: got $generator_digest want $expected_digest"
    fi
    printf 'opt_generated_diff: generator digest %s matches\n' \
        "$generator_digest"
fi

find "$SOURCES" -type f -name '*.c' -print | LC_ALL=C sort >"$WORK/sources.list"
actual=$(awk 'END { print NR + 0 }' "$WORK/sources.list")
[ "$actual" -eq "$CASES" ] ||
    fail "generator emitted $actual C sources; expected $CASES"

run_program()
{
    run_exe=$1
    run_out=$2
    run_status=$3
    run_timed "$run_status" "$run_exe" >"$run_out" 2>"$run_out.stderr"
    [ "$TIMED_STATUS" -ne 124 ] ||
        fail "$run_exe timed out after ${TIMEOUT_SECS}s"
}

set -e
index=0
while IFS= read -r source; do
    index=$((index + 1))
    tag=$(printf '%04d' "$index")
    grep -Fqx '// OPT_EQ: all' "$source" ||
        fail "$source does not carry the required // OPT_EQ: all directive"

    for oracle_name in gcc clang; do
        case "$oracle_name" in
            gcc) oracle_cc=$GCC_PATH ;;
            clang) oracle_cc=$CLANG_PATH ;;
        esac
        for level in O0 O3; do
            stem="$ORACLE/$tag.$oracle_name.$level"
            run_timed "$stem.compile.status" "$oracle_cc" \
                -std=c17 -pedantic-errors -Wall -Wextra -Werror \
                "-$level" "$source" -o "$stem.exe" \
                >"$stem.compile.stdout" 2>"$stem.compile.stderr"
            case "$TIMED_STATUS" in
            0) ;;
            124) fail "$source timed out in $oracle_name -$level" ;;
            *) fail "$source is rejected by $oracle_name -$level" ;;
            esac
            run_program "$stem.exe" "$stem.stdout" "$stem.status"
        done
    done

    baseline="$ORACLE/$tag.gcc.O0"
    for stem in \
        "$ORACLE/$tag.gcc.O3" \
        "$ORACLE/$tag.clang.O0" \
        "$ORACLE/$tag.clang.O3"
    do
        cmp -s "$baseline.status" "$stem.status" ||
            fail "$source has host-oracle exit-status divergence ($baseline vs $stem)"
        cmp -s "$baseline.stdout" "$stem.stdout" ||
            fail "$source has host-oracle stdout divergence ($baseline vs $stem)"
        cmp -s "$baseline.stdout.stderr" "$stem.stdout.stderr" ||
            fail "$source has host-oracle stderr divergence ($baseline vs $stem)"
    done

    cgf_stem="$ORACLE/$tag.cgf.O0"
    run_timed "$cgf_stem.compile.status" env \
        CGF_AS=0 CGF_AS_PATH= CGF_LD=0 CGF_LD_PATH= CGF_CRT_DIR= \
        CGF_OPT_DISABLE_BCE= CGF_OPT_DISABLE_FUSION= \
        CGF_OPT_DISABLE_UNSWITCH= CGF_OPT_DISABLE_VECTORIZE= \
        CGF_VERIFY_AFTER_EACH= CGF_DUMP_IR= CGF_DUMP_IR_DIR= \
        "$CGF" -O0 "$source" -o "$cgf_stem.exe" \
        >"$cgf_stem.compile.stdout" 2>"$cgf_stem.compile.stderr"
    case "$TIMED_STATUS" in
    0) ;;
    124) fail "$source timed out in Cgfried -O0" ;;
    *) fail "$source is rejected by Cgfried -O0" ;;
    esac
    run_program "$cgf_stem.exe" "$cgf_stem.stdout" "$cgf_stem.status"
    cmp -s "$baseline.status" "$cgf_stem.status" ||
        fail "$source differs from the host oracle at Cgfried -O0 (status)"
    cmp -s "$baseline.stdout" "$cgf_stem.stdout" ||
        fail "$source differs from the host oracle at Cgfried -O0 (stdout)"
    cmp -s "$baseline.stdout.stderr" "$cgf_stem.stdout.stderr" ||
        fail "$source differs from the host oracle at Cgfried -O0 (stderr)"
done <"$WORK/sources.list"

run_timed "$WORK/cgf-test.status" env \
    CGF_AS=0 CGF_AS_PATH= CGF_LD=0 CGF_LD_PATH= CGF_CRT_DIR= \
    CGF_OPT_DISABLE_BCE= CGF_OPT_DISABLE_FUSION= \
    CGF_OPT_DISABLE_UNSWITCH= CGF_OPT_DISABLE_VECTORIZE= \
    CGF_DUMP_IR= CGF_DUMP_IR_DIR= CGF_VERIFY_AFTER_EACH=1 \
    CGF_TEST_CC="$CGF" "$RUNNER" --profile linux-x86_64 "$SOURCES" \
    >"$WORK/cgf-test.log" 2>&1
case "$TIMED_STATUS" in
0) ;;
124) fail "Cgfried OPT_EQ campaign timed out after ${TIMEOUT_SECS}s (see cgf-test.log)" ;;
*) fail "Cgfried OPT_EQ campaign failed with exit $TIMED_STATUS (see cgf-test.log)" ;;
esac

tail -1 "$WORK/cgf-test.log"
grep -Eq "^cgf-test: profile=linux-x86_64 total=$CASES pass=$CASES fail=0 xfail=0 xpass=0 skip=0 config=0$" \
    "$WORK/cgf-test.log" || fail "runner summary did not match the generated case count"

write_receipt
printf 'opt_generated_diff: PASS cases=%s seed=%s; gcc/clang O0/O3 and Cgfried O0/all levels agree\n' \
    "$CASES" "$SEED"
printf 'opt_generated_diff: receipt=%s\n' "$RECEIPT"
KEEP=0
