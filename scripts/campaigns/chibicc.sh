#!/bin/sh
set -eu

CHIBICC_REF=90d1f7f199cc55b13c7fdb5839d1409806633fdb

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
source_dir=${CGF_CAMPAIGN_CHIBICC_SOURCE:-$root/.docs/refs/chibicc}
work=${CGF_CAMPAIGN_CHIBICC_WORK:-$root/build/campaigns/chibicc}
cgf=${CGF_CAMPAIGN_CHIBICC_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_CHIBICC_HOSTCC:-gcc}
jobs=${CGF_CAMPAIGN_JOBS:-}

# The repository's bundled tools are optional.  Prefer explicit caller
# routing, then use the standard host binutils paths when present.
if [ -z "${CGF_AS_PATH:-}" ] && [ -x /usr/bin/as ]; then
    CGF_AS_PATH=/usr/bin/as
    export CGF_AS_PATH
fi
if [ -z "${CGF_LD_PATH:-}" ] && [ -x /usr/bin/ld ]; then
    CGF_LD_PATH=/usr/bin/ld
    export CGF_LD_PATH
fi

fail() {
    echo "campaign-chibicc: $*" >&2
    exit 1
}

[ -d "$source_dir" ] || fail "source checkout is missing: $source_dir"
[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
command -v "$hostcc" >/dev/null 2>&1 ||
    fail "host GCC is unavailable: $hostcc"

actual_ref=$(git -C "$source_dir" rev-parse --verify HEAD 2>/dev/null) ||
    fail "source checkout has no valid HEAD: $source_dir"
[ "$actual_ref" = "$CHIBICC_REF" ] ||
    fail "source ref mismatch: expected $CHIBICC_REF, got $actual_ref"
git -C "$source_dir" cat-file -e "$CHIBICC_REF^{commit}" 2>/dev/null ||
    fail "pinned commit is unavailable: $CHIBICC_REF"

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
# must not reuse any configure, object, or test artifact from the Cgfried lane.
git -C "$source_dir" archive "$CHIBICC_REF" |
    tar -x -C "$work/cgfried-src"
git -C "$source_dir" archive "$CHIBICC_REF" |
    tar -x -C "$work/host-gcc-src"
printf '%s\n' "$actual_ref" >"$work/source-revision.txt"

if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')
fi
case $jobs in
    '' | *[!0-9]* | 0) fail "CGF_CAMPAIGN_JOBS must be a positive integer" ;;
esac

{
    printf 'source=%s\n' "$source_dir"
    printf 'revision=%s\n' "$actual_ref"
    printf 'compiler=%s\n' "$cgf"
    printf 'host-gcc=%s\n' "$hostcc"
    printf 'test-linker-driver=%s\n' "$hostcc"
    printf 'assembler=%s\n' "${CGF_AS_PATH:-bundled}"
    printf 'linker=%s\n' "${CGF_LD_PATH:-bundled}"
    printf 'jobs=%s\n' "$jobs"
    printf 'build=make -j%s CC=%s\n' "$jobs" "$cgf"
    printf 'cgfried-test=make test CC=%s\n' "$hostcc"
    printf 'host-build=make -j%s CC=%s\n' "$jobs" "$hostcc"
    printf 'host-test=make test CC=%s\n' "$hostcc"
} >"$work/logs/commands.txt"

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -C "$work/cgfried-src" -j"$jobs" CC="$cgf" \
    >"$work/logs/cgfried/build.log" 2>&1; then
    cat "$work/logs/cgfried/build.log" >&2
    fail "Cgfried build failed; see $work/logs/cgfried/build.log"
fi

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -C "$work/cgfried-src" test CC="$hostcc" \
    >"$work/logs/cgfried/test.log" 2>&1; then
    cat "$work/logs/cgfried/test.log" >&2
    fail "Cgfried-built upstream test suite failed; see $work/logs/cgfried/test.log"
fi

cgf_program_count=$(find "$work/cgfried-src/test" -maxdepth 1 -type f -name '*.exe' |
    LC_ALL=C sort | wc -l | tr -d ' ')
[ "$cgf_program_count" -gt 0 ] ||
    fail "Cgfried-built upstream test suite produced no executables"

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -C "$work/host-gcc-src" -j"$jobs" CC="$hostcc" \
    >"$work/logs/host-gcc/build.log" 2>&1; then
    cat "$work/logs/host-gcc/build.log" >&2
    fail "host-GCC build failed; see $work/logs/host-gcc/build.log"
fi

if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
    make -C "$work/host-gcc-src" test CC="$hostcc" \
    >"$work/logs/host-gcc/test.log" 2>&1; then
    cat "$work/logs/host-gcc/test.log" >&2
    fail "host-GCC upstream test suite failed; see $work/logs/host-gcc/test.log"
fi

host_program_count=$(find "$work/host-gcc-src/test" -maxdepth 1 -type f -name '*.exe' |
    LC_ALL=C sort | wc -l | tr -d ' ')
[ "$host_program_count" -gt 0 ] ||
    fail "host-GCC upstream test suite produced no executables"
[ "$cgf_program_count" -eq "$host_program_count" ] ||
    fail "program-count parity failed: Cgfried=$cgf_program_count host-GCC=$host_program_count"

{
    echo '# cgf-campaign-results-v1'
    printf '# columns=key\toutcome\tdetail\n'
    printf 'baseline.build\tPASS\tcompiler=host-gcc\n'
    printf 'build\tPASS\tcompiler=cgfried\n'
    printf 'parity.cgfried-only-failures\tPASS\tcases=0\n'
    printf 'parity.programs\tPASS\tcgfried=%s,host-gcc=%s\n' \
        "$cgf_program_count" "$host_program_count"
    printf 'source.pin\tPASS\trevision=%s\n' "$actual_ref"
    printf 'test.driver\tPASS\tstatus=OK\n'
    printf 'test.programs\tPASS\tcases=%s\n' "$cgf_program_count"
} >"$work/results.txt"

printf 'campaign-chibicc: PASS programs=%s host-programs=%s revision=%s results=%s\n' \
    "$cgf_program_count" "$host_program_count" "$actual_ref" "$work/results.txt"
