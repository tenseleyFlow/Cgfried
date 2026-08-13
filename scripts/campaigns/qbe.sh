#!/bin/sh
set -eu

QBE_REF=d62b154d05de438e12e8b5e980d43ef65ea1bb6c

fail() {
    echo "qbe-campaign: $*" >&2
    exit 1
}

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repo=$(CDPATH='' cd -- "$script_dir/../.." && pwd -P)
source=${CGF_CAMPAIGN_QBE_SOURCE:-$repo/.docs/refs/qbe}
work=${CGF_CAMPAIGN_QBE_WORK:-$repo/build/campaigns/qbe}
checker=${CGF_CAMPAIGN_CHECK:-$repo/ci/campaigns/check-expected.sh}
cgf=${CGF:-$repo/build/cgfried}
hostcc=${CGF_CAMPAIGN_QBE_HOSTCC:-gcc}

case $source in /*) ;; *) source=$repo/$source ;; esac
case $work in /*) ;; *) work=$repo/$work ;; esac
case $checker in /*) ;; *) checker=$repo/$checker ;; esac
case $cgf in /*) ;; *) cgf=$repo/$cgf ;; esac

machine=$(uname -m)
arm64_exclusions=
arm64_exclusion_detail=
case $machine in
    x86_64 | amd64)
        arch=x86_64
        deftgt=T_amd64_sysv
        expected_default=$repo/ci/campaigns/qbe.expected
        excluded=0
        ;;
    aarch64 | arm64)
        arch=arm64
        deftgt=T_arm64
        expected_default=$repo/ci/campaigns/qbe-arm64.expected
        # The pinned QBE ARM backend has four target-specific upstream holes.
        # dark.ssa declares its own skip.  The other three were proven on a
        # real ARM runner in both pristine compiler lanes: non-entry alloc8 is
        # not selected, and the two vararg cases violate the architectural
        # 16-byte stack-alignment rule.  Exclude the same immutable manifest
        # from both lanes; the remaining suite must stay clean and identical.
        arm64_exclusions='dark.ssa dynalloc.ssa vararg1.ssa vararg2.ssa'
        arm64_exclusion_detail='cases=dark.ssa:upstream-declared-skip,dynalloc.ssa:upstream-non-entry-alloc,vararg1.ssa:upstream-sp-alignment,vararg2.ssa:upstream-sp-alignment'
        excluded=4
        ;;
    *) fail "unsupported native architecture: $machine" ;;
esac
expected=${CGF_CAMPAIGN_QBE_EXPECTED:-$expected_default}
case $expected in /*) ;; *) expected=$repo/$expected ;; esac

[ -x "$cgf" ] || fail "cgfried is not executable: $cgf"
[ -x "$checker" ] || fail "expected-results checker is not executable: $checker"
command -v "$hostcc" >/dev/null 2>&1 ||
    fail "host GCC is unavailable: $hostcc"

# Campaign jobs build only the compiler, not the optional bundled Rust tools.
# Route Cgfried to native binutils explicitly on both supported hosts.
as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

got=$(git -C "$source" rev-parse --verify HEAD 2>/dev/null) ||
    fail "qbe ref checkout missing or invalid: $source"
[ "$got" = "$QBE_REF" ] ||
    fail "qbe ref mismatch: expected $QBE_REF, got $got"

# `work` is disposable.  Resolve the repository root, allow one direct child
# of its campaign directory, and reject symlinks before the recursive cleanup.
# This prevents `..`, nested overrides, and symlink escapes from widening rm.
campaign_root=$repo/build/campaigns
mkdir -p "$campaign_root"
campaign_root_real=$(CDPATH='' cd -- "$campaign_root" && pwd -P)
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
[ "$work" != "$source" ] || fail "source and work directories must differ"

rm -rf "$work"
mkdir -p "$work/cgfried-src" "$work/host-gcc-src" "$work/logs/cgfried" \
    "$work/logs/host-gcc"

# Two independent archive exports make the host-GCC lane a true baseline: it
# shares no generated config, object, or test artifact with Cgfried's lane.
git -C "$source" archive "$QBE_REF" | tar -x -C "$work/cgfried-src"
git -C "$source" archive "$QBE_REF" | tar -x -C "$work/host-gcc-src"
printf '%s\n' "$QBE_REF" >"$work/source-ref.txt"

write_config() {
    tree=$1
    {
        echo '#define Defasm Gaself'
        printf '#define Deftgt %s\n' "$deftgt"
    } >"$tree/config.h"
}

prepare_tree() {
    tree=$1
    write_config "$tree"
    if [ "$arch" = arm64 ]; then
        for case_name in $arm64_exclusions; do
            [ -f "$tree/test/$case_name" ] ||
                fail "missing pinned ARM exclusion: $case_name"
            mv "$tree/test/$case_name" "$tree/test/_$case_name"
        done
    fi
}

prepare_tree "$work/cgfried-src"
prepare_tree "$work/host-gcc-src"
{
    printf 'architecture=%s\n' "$arch"
    printf 'config=#define Defasm Gaself; #define Deftgt %s\n' "$deftgt"
    echo 'config-source=campaign-generated from uname -m'
    echo "reason=pinned Makefile contains an invalid literal \$define on aarch64"
    if [ "$arch" = arm64 ]; then
        echo 'native-arm64-exclude=dark.ssa (upstream # skip arm64)'
        echo 'native-arm64-exclude=dynalloc.ssa (upstream backend leaves non-entry alloc8 unselected)'
        echo 'native-arm64-exclude=vararg1.ssa (upstream backend violates 16-byte SP alignment)'
        echo 'native-arm64-exclude=vararg2.ssa (upstream backend violates 16-byte SP alignment)'
    fi
} >"$work/campaign-fixes.log"

jobs=${CGF_CAMPAIGN_JOBS:-}
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')
fi
case $jobs in *[!0-9]* | 0) fail "CGF_CAMPAIGN_JOBS must be positive: $jobs" ;; esac

build_lane() {
    label=$1
    tree=$2
    compiler=$3
    log=$work/logs/$label/build.log

    if LC_ALL=C SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0} \
        make -C "$tree" -j"$jobs" V= CC="$compiler" >"$log" 2>&1; then
        return
    fi
    status=$?
    sed "s/^/qbe-$label-build: /" "$log" >&2
    fail "$label build failed with status $status (log: $log)"
}

run_test_lane() {
    label=$1
    tree=$2
    log=$work/logs/$label/test.log
    summary=$work/logs/$label/test-summary.txt
    failures=$work/logs/$label/failures.txt
    raw_failures=$failures.raw

    status=0
    if LC_ALL=C make -C "$tree" check >"$log" 2>&1; then
        :
    else
        status=$?
    fi
    cases=$(find "$tree/test" -maxdepth 1 -type f -name '[!_]*.ssa' -print |
        wc -l | tr -d ' ')
    oks=$(grep -c '\[ok\]$' "$log" || true)
    [ "$cases" -gt 0 ] || fail "$label suite contains no native cases"

    # QBE prints a test name before invoking the generated program, then emits
    # the final marker either on that line or after the program's output.  Keep
    # a tiny state machine so signal deaths and multi-line diffs are attributed
    # to the right case instead of disappearing into an aggregate exit code.
    awk '
        match($0, /^[^[:space:]]+\.ssa\.\.\./) {
            if (current != "") {
                if (result == "fail")
                    print current
                else if (result == "") {
                    print "qbe-campaign: test has no outcome marker: " current > "/dev/stderr"
                    bad = 1
                }
            }
            current = substr($0, 1, RLENGTH - 3)
            result = ""
        }
        current != "" && index($0, "[ok]") != 0 {
            result = "ok"
        }
        current != "" && index($0, " fail]") != 0 {
            result = "fail"
        }
        END {
            if (current != "") {
                if (result == "fail")
                    print current
                else if (result == "") {
                    print "qbe-campaign: test has no outcome marker: " current > "/dev/stderr"
                    bad = 1
                }
            }
            exit bad
        }
    ' "$log" >"$raw_failures" ||
        fail "$label native test log could not be classified: $log"
    LC_ALL=C sort -u "$raw_failures" >"$failures"
    rm -f "$raw_failures"
    failed=$(wc -l <"$failures" | tr -d ' ')
    [ $((oks + failed)) -eq "$cases" ] ||
        fail "$label accounting mismatch: cases=$cases ok=$oks failed=$failed"

    if [ "$status" -eq 0 ]; then
        [ "$failed" -eq 0 ] ||
            fail "$label returned success with $failed classified failures"
        grep -q '^All is fine!$' "$log" ||
            fail "$label suite did not emit its success sentinel"
    else
        [ "$failed" -gt 0 ] ||
            fail "$label failed with status $status but named no failing case"
        grep -Eq '^[0-9]+ test\(s\) failed!$' "$log" ||
            fail "$label suite did not emit its failure sentinel"
    fi
    {
        printf 'status=%s\n' "$status"
        printf 'cases=%s\n' "$cases"
        printf 'passed=%s\n' "$oks"
        printf 'failed=%s\n' "$failed"
    } >"$summary"
}

build_lane cgfried "$work/cgfried-src" "$cgf"
build_lane host-gcc "$work/host-gcc-src" "$hostcc"
run_test_lane cgfried "$work/cgfried-src"
run_test_lane host-gcc "$work/host-gcc-src"
cgf_cases=$(sed -n 's/^cases=//p' "$work/logs/cgfried/test-summary.txt")
gcc_cases=$(sed -n 's/^cases=//p' "$work/logs/host-gcc/test-summary.txt")
cgf_passed=$(sed -n 's/^passed=//p' "$work/logs/cgfried/test-summary.txt")
gcc_passed=$(sed -n 's/^passed=//p' "$work/logs/host-gcc/test-summary.txt")
[ "$cgf_cases" -eq "$gcc_cases" ] ||
    fail "native case parity failed: Cgfried=$cgf_cases host-GCC=$gcc_cases"
[ "$cgf_cases" -eq $((32 - excluded)) ] ||
    fail "unexpected $arch case count: expected $((32 - excluded)), got $cgf_cases"

cgf_failures=$work/logs/cgfried/failures.txt
gcc_failures=$work/logs/host-gcc/failures.txt
cgf_only=$work/cgfried-only-failures.txt
gcc_only=$work/host-gcc-only-failures.txt
LC_ALL=C comm -23 "$cgf_failures" "$gcc_failures" >"$cgf_only"
LC_ALL=C comm -13 "$cgf_failures" "$gcc_failures" >"$gcc_only"
cgf_failed=$(wc -l <"$cgf_failures" | tr -d ' ')
gcc_failed=$(wc -l <"$gcc_failures" | tr -d ' ')
cgf_only_count=$(wc -l <"$cgf_only" | tr -d ' ')

failure_detail() {
    passed=$1
    cases=$2
    failures=$3
    if [ -s "$failures" ]; then
        names=$(paste -sd, "$failures")
        printf 'cases=%s/%s,failures=%s' "$passed" "$cases" "$names"
    else
        printf 'cases=%s' "$cases"
    fi
}

parity_outcome=PASS
parity_detail='cases=0'
if [ "$cgf_only_count" -ne 0 ]; then
    parity_outcome=FAIL
    parity_names=$(paste -sd, "$cgf_only")
    parity_detail="cases=$cgf_only_count,tests=$parity_names"
fi
cgf_outcome=PASS
gcc_outcome=PASS
[ "$cgf_failed" -eq 0 ] || cgf_outcome=FAIL
[ "$gcc_failed" -eq 0 ] || gcc_outcome=FAIL
cgf_detail=$(failure_detail "$cgf_passed" "$cgf_cases" "$cgf_failures")
gcc_detail=$(failure_detail "$gcc_passed" "$gcc_cases" "$gcc_failures")

tab=$(printf '\t')
{
    echo '# cgf-campaign-results-v1'
    printf '# columns=key%coutcome%cdetail\n' "$tab" "$tab"
    printf 'baseline.build%cPASS%ccompiler=host-gcc\n' "$tab" "$tab"
    printf 'build%cPASS%ccompiler=cgfried\n' "$tab" "$tab"
    printf 'host.arch%cPASS%carchitecture=%s\n' "$tab" "$tab" "$arch"
    printf 'parity.cgfried-only-failures%c%s%c%s\n' "$tab" \
        "$parity_outcome" "$tab" "$parity_detail"
    printf 'source.pin%cPASS%crevision=%s\n' "$tab" "$tab" "$QBE_REF"
    printf 'test.cgfried-native%c%s%c%s\n' "$tab" "$cgf_outcome" "$tab" \
        "$cgf_detail"
    if [ "$arch" = arm64 ]; then
        printf 'test.excluded%cPASS%c%s\n' "$tab" "$tab" \
            "$arm64_exclusion_detail"
    fi
    printf 'test.host-gcc-native%c%s%c%s\n' "$tab" "$gcc_outcome" "$tab" \
        "$gcc_detail"
} >"$work/results.txt"

"$checker" "$expected" "$work/results.txt"
printf 'qbe-campaign: PASS revision=%s architecture=%s native-cases=%s artifacts=%s\n' \
    "$QBE_REF" "$arch" "$cgf_cases" "$work"
