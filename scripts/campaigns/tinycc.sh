#!/bin/sh
set -eu

TINYCC_REF=380597704ee9784442ef3c7ef06e105258a11c5d
TESTS2_EXPECTED=132
TESTSPP_EXPECTED=24

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
source_dir=${CGF_CAMPAIGN_TINYCC_SOURCE:-$root/.docs/refs/tinycc}
work=${CGF_CAMPAIGN_TINYCC_WORK:-$root/build/campaigns/tinycc}
cgf=${CGF_CAMPAIGN_TINYCC_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_TINYCC_HOSTCC:-gcc}
jobs=${CGF_CAMPAIGN_JOBS:-}

fail() {
    echo "campaign-tinycc: $*" >&2
    exit 1
}

[ -d "$source_dir" ] || fail "source checkout is missing: $source_dir"
[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
command -v "$hostcc" >/dev/null 2>&1 ||
    fail "host GCC is unavailable: $hostcc"

actual_ref=$(git -C "$source_dir" rev-parse --verify HEAD 2>/dev/null) ||
    fail "source checkout has no valid HEAD: $source_dir"
[ "$actual_ref" = "$TINYCC_REF" ] ||
    fail "source ref mismatch: expected $TINYCC_REF, got $actual_ref"
git -C "$source_dir" cat-file -e "$TINYCC_REF^{commit}" 2>/dev/null ||
    fail "pinned commit is unavailable: $TINYCC_REF"

# Campaign work directories are intentionally disposable.  Allow exactly one
# direct, non-symlink child of the canonical campaign root before recursive
# cleanup; this rejects `..`, nested paths, and symlink escapes.
case $work in /*) ;; *) work=$root/$work ;; esac
campaign_root=$root/build/campaigns
mkdir -p "$campaign_root"
campaign_root_real=$(CDPATH='' cd "$campaign_root" && pwd -P)
[ "$campaign_root_real" = "$campaign_root" ] ||
    fail "campaign root must not traverse symlinks: $campaign_root"
case $work in
    "$campaign_root"/*)
        work_name=${work#"$campaign_root"/}
        case $work_name in '' | . | .. | */*) fail "unsafe work directory: $work" ;; esac
        ;;
    *) fail "work directory must be a direct child of $campaign_root: $work" ;;
esac
[ ! -L "$work" ] || fail "work directory must not be a symlink: $work"
[ "$work" != "$source_dir" ] || fail "source and work directories must differ"
rm -rf "$work"
mkdir -p "$work/cgfried-src" "$work/host-gcc-src" "$work/logs/cgfried" \
    "$work/logs/host-gcc"

# Export two pristine copies of exactly the pinned tree.  The host baseline
# must not reuse configure, object, or test artifacts from the Cgfried lane,
# and neither lane ever compiles in .docs/refs.
git -C "$source_dir" archive "$TINYCC_REF" |
    tar -x -C "$work/cgfried-src"
git -C "$source_dir" archive "$TINYCC_REF" |
    tar -x -C "$work/host-gcc-src"
printf '%s\n' "$actual_ref" >"$work/source-revision.txt"

if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')
fi
case $jobs in
    '' | *[!0-9]* | 0) fail "CGF_CAMPAIGN_JOBS must be a positive integer" ;;
esac

as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

# TinyCC's build probes and makefiles accept a compiler command, so keep the
# GNU-attribute compatibility flag attached to cgfried in every stage.
cc="$cgf -Wno-attributes"
{
    printf 'source=%s\n' "$source_dir"
    printf 'revision=%s\n' "$actual_ref"
    printf 'compiler=%s\n' "$cc"
    printf 'host-gcc=%s\n' "$hostcc"
    printf 'assembler=%s\n' "$as_path"
    printf 'linker=%s\n' "$ld_path"
    printf 'jobs=%s\n' "$jobs"
    printf 'configure=CC=<compiler> ./configure\n'
    printf 'build=make -j%s\n' "$jobs"
    printf 'tests2=make -s tests2.all\n'
    printf 'testspp=make -s testspp.all\n'
} >"$work/logs/commands.txt"

if ! (
    cd "$work/cgfried-src"
    LC_ALL=C SOURCE_DATE_EPOCH=0 CC="$cc" ./configure
) >"$work/logs/cgfried/configure.log" 2>&1; then
    cat "$work/logs/cgfried/configure.log" >&2
    fail "Cgfried configure failed; see $work/logs/cgfried/configure.log"
fi

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -C "$work/cgfried-src" -j"$jobs" \
    >"$work/logs/cgfried/build.log" 2>&1; then
    cat "$work/logs/cgfried/build.log" >&2
    fail "Cgfried build failed; see $work/logs/cgfried/build.log"
fi
[ -x "$work/cgfried-src/tcc" ] || fail "Cgfried build produced no executable tcc"

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -s -C "$work/cgfried-src" tests2.all \
    >"$work/logs/cgfried/tests2.log" 2>&1; then
    cat "$work/logs/cgfried/tests2.log" >&2
    fail "Cgfried tests2 failed; see $work/logs/cgfried/tests2.log"
fi
cgf_tests2_count=$(grep -c '^Test:' "$work/logs/cgfried/tests2.log" || true)
[ "$cgf_tests2_count" -eq "$TESTS2_EXPECTED" ] ||
    fail "Cgfried tests2 count drift: expected $TESTS2_EXPECTED, got $cgf_tests2_count"

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -s -C "$work/cgfried-src" testspp.all \
    >"$work/logs/cgfried/testspp.log" 2>&1; then
    cat "$work/logs/cgfried/testspp.log" >&2
    fail "Cgfried testspp failed; see $work/logs/cgfried/testspp.log"
fi
cgf_testspp_count=$(grep -c '^PPTest' "$work/logs/cgfried/testspp.log" || true)
[ "$cgf_testspp_count" -eq "$TESTSPP_EXPECTED" ] ||
    fail "Cgfried testspp count drift: expected $TESTSPP_EXPECTED, got $cgf_testspp_count"

if ! (
    cd "$work/host-gcc-src"
    LC_ALL=C SOURCE_DATE_EPOCH=0 CC="$hostcc" ./configure
) >"$work/logs/host-gcc/configure.log" 2>&1; then
    cat "$work/logs/host-gcc/configure.log" >&2
    fail "host-GCC configure failed; see $work/logs/host-gcc/configure.log"
fi

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -C "$work/host-gcc-src" -j"$jobs" \
    >"$work/logs/host-gcc/build.log" 2>&1; then
    cat "$work/logs/host-gcc/build.log" >&2
    fail "host-GCC build failed; see $work/logs/host-gcc/build.log"
fi
[ -x "$work/host-gcc-src/tcc" ] ||
    fail "host-GCC build produced no executable tcc"

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -s -C "$work/host-gcc-src" tests2.all \
    >"$work/logs/host-gcc/tests2.log" 2>&1; then
    cat "$work/logs/host-gcc/tests2.log" >&2
    fail "host-GCC tests2 failed; see $work/logs/host-gcc/tests2.log"
fi
host_tests2_count=$(grep -c '^Test:' "$work/logs/host-gcc/tests2.log" || true)
[ "$host_tests2_count" -eq "$TESTS2_EXPECTED" ] ||
    fail "host-GCC tests2 count drift: expected $TESTS2_EXPECTED, got $host_tests2_count"

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -s -C "$work/host-gcc-src" testspp.all \
    >"$work/logs/host-gcc/testspp.log" 2>&1; then
    cat "$work/logs/host-gcc/testspp.log" >&2
    fail "host-GCC testspp failed; see $work/logs/host-gcc/testspp.log"
fi
host_testspp_count=$(grep -c '^PPTest' "$work/logs/host-gcc/testspp.log" || true)
[ "$host_testspp_count" -eq "$TESTSPP_EXPECTED" ] ||
    fail "host-GCC testspp count drift: expected $TESTSPP_EXPECTED, got $host_testspp_count"

[ "$cgf_tests2_count" -eq "$host_tests2_count" ] ||
    fail "tests2 parity failed: Cgfried=$cgf_tests2_count host-GCC=$host_tests2_count"
[ "$cgf_testspp_count" -eq "$host_testspp_count" ] ||
    fail "testspp parity failed: Cgfried=$cgf_testspp_count host-GCC=$host_testspp_count"

{
    echo '# cgf-campaign-results-v1'
    printf '# columns=key\toutcome\tdetail\n'
    printf 'baseline.build\tPASS\tcompiler=host-gcc\n'
    printf 'baseline.configure\tPASS\tcompiler=host-gcc\n'
    printf 'build\tPASS\tcompiler=cgfried\n'
    printf 'configure\tPASS\tcompiler=cgfried\n'
    printf 'parity.cgfried-only-failures\tPASS\tcases=0\n'
    printf 'parity.tests2\tPASS\tcgfried=%s,host-gcc=%s\n' \
        "$cgf_tests2_count" "$host_tests2_count"
    printf 'parity.testspp\tPASS\tcgfried=%s,host-gcc=%s\n' \
        "$cgf_testspp_count" "$host_testspp_count"
    printf 'source.pin\tPASS\trevision=%s\n' "$actual_ref"
    printf 'test.tests2\tPASS\tcases=%s\n' "$cgf_tests2_count"
    printf 'test.testspp\tPASS\tcases=%s\n' "$cgf_testspp_count"
} >"$work/results.txt"

printf 'campaign-tinycc: PASS tests2=%s testspp=%s host-tests2=%s host-testspp=%s revision=%s results=%s\n' \
    "$cgf_tests2_count" "$cgf_testspp_count" "$host_tests2_count" \
    "$host_testspp_count" "$actual_ref" "$work/results.txt"
