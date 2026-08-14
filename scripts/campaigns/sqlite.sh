#!/bin/sh
set -eu

SQLITE_VERSION=3460100
SQLITE_RELEASE=3.46.1
AMALGAMATION_SHA256=77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784
SOURCE_SHA256=def3fc292eb9ecc444f6c1950e5c79d8462ed5e7b3d605fd6152d145e1d5abb4
SPEEDTEST_HASH='111130 1e792c9db61996c477b8ab5ce2d690052e8dae74824a430a'

fail() {
    echo "campaign-sqlite: $*" >&2
    exit 1
}

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
cache=${CGF_CAMPAIGN_SQLITE_CACHE:-$root/build/campaigns/dl}
work=${CGF_CAMPAIGN_SQLITE_WORK:-$root/build/campaigns/sqlite}
cgf=${CGF_CAMPAIGN_SQLITE_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_SQLITE_HOSTCC:-gcc}
timeit=${CGF_CAMPAIGN_SQLITE_TIMEIT:-$root/build/timeit}
smoke=${CGF_CAMPAIGN_SQLITE_SMOKE:-$root/scripts/campaigns/sqlite-smoke.sql}
baseline_config=${CGF_CAMPAIGN_SQLITE_BASELINES:-$root/ci/campaigns/sqlite-baselines.conf}
baseline_check=${CGF_CAMPAIGN_SQLITE_BASELINE_CHECK:-$root/scripts/campaigns/sqlite-baseline-check.sh}
measure=${CGF_CAMPAIGN_SQLITE_MEASURE:-$root/scripts/campaigns/sqlite-measure.sh}
archive_check=${CGF_CAMPAIGN_SQLITE_ARCHIVE_CHECK:-$root/scripts/campaigns/sqlite-archive-check.sh}
policy_check=${CGF_CAMPAIGN_SQLITE_POLICY_CHECK:-$root/scripts/campaigns/sqlite-policy-check.sh}
runs=${CGF_CAMPAIGN_SQLITE_RUNS:-10}
warmup=${CGF_CAMPAIGN_SQLITE_WARMUP:-1}
timeout_seconds=${CGF_CAMPAIGN_SQLITE_TIMEOUT:-720}
compat_header_rel=ci/campaigns/compat/arm64-linux-u128-storage.h
compat_header=$root/$compat_header_rel
compat_policy=opaque-u64x2-align16-v1
amalgamation_zip=$cache/sqlite-amalgamation-$SQLITE_VERSION.zip
source_zip=$cache/sqlite-src-$SQLITE_VERSION.zip
amalgamation_url=https://www.sqlite.org/2024/sqlite-amalgamation-$SQLITE_VERSION.zip
source_url=https://www.sqlite.org/2024/sqlite-src-$SQLITE_VERSION.zip

# Campaign jobs are compiler tests, not bundled-tool tests. Keep the Rust-free
# campaign lane on the runner's native binutils exactly as the other project
# ladders do; an explicit caller override still wins.
as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "native assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "native linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        fail "sha256sum or shasum is required"
    fi
}

verify_archive() {
    file=$1
    expected=$2
    label=$3
    [ -f "$file" ] || fail "$label cache miss: $file (run campaign-sqlite-fetch first)"
    got=$(sha256 "$file")
    [ "$got" = "$expected" ] ||
        fail "$label sha256 mismatch: expected $expected, got $got"
}

fetch_one() {
    url=$1
    output=$2
    expected=$3
    label=$4
    if [ -f "$output" ] && [ "$(sha256 "$output")" = "$expected" ]; then
        printf 'campaign-sqlite: cached %s sha256=%s\n' "$label" "$expected"
        return
    fi
    [ "${CGF_CAMPAIGN_OFFLINE:-0}" != 1 ] ||
        fail "$label archive is absent or invalid in the offline cache: $output"
    command -v curl >/dev/null 2>&1 || fail "curl is required to populate the cache"
    temp=$output.tmp.$$
    trap 'rm -f "$temp"' EXIT HUP INT TERM
    curl -fL --retry 3 --output "$temp" "$url"
    got=$(sha256 "$temp")
    [ "$got" = "$expected" ] ||
        fail "$label download sha256 mismatch: expected $expected, got $got"
    mv "$temp" "$output"
    trap - EXIT HUP INT TERM
    printf 'campaign-sqlite: fetched %s sha256=%s\n' "$label" "$expected"
}

fetch() {
    mkdir -p "$cache"
    fetch_one "$amalgamation_url" "$amalgamation_zip" \
        "$AMALGAMATION_SHA256" amalgamation
    fetch_one "$source_url" "$source_zip" "$SOURCE_SHA256" source
}

safe_work_reset() {
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
    rm -rf "$work"
    mkdir -p "$work/src" "$work/bin" "$work/obj" "$work/logs" "$work/receipts"
}

compile_cgf_object() {
    level=$1
    source=$2
    object=$3
    shift 3
    # Match Sprint 52's checked SQLite scale profile.  Warning analysis has
    # its own corpus gates and builds a separate IR module; excluding it here
    # keeps this receipt about front-end/optimizer/codegen scale.  These flags
    # do not affect emitted bytes, which the shell and speedtest bars execute.
    set -- -std=gnu11 "-$level" -Wno-attributes -Wno-mem \
        -Wno-return-type -DSQLITE_THREADSAFE=0 \
        -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_HAVE_READLINE=0 \
        -DSQLITE_DISABLE_INTRINSIC \
        -I "$work/src" "$@" -c "$source" -o "$object"
    case $compat_active:$source in
        yes:"$work/src/shell.c") set -- -include "$compat_header" "$@" ;;
    esac
    "$cgf" "$@"
}

measure_cgf_sqlite() {
    level=$1
    receipt=$work/receipts/sqlite3.$level.txt
    raw=$work/receipts/sqlite3.$level.raw.txt
    log=$work/logs/sqlite3.$level.compile.log
    "$measure" "$timeit" "$runs" "$warmup" "$timeout_seconds" \
        "$raw" "$receipt" "$log" -- \
        "$cgf" -std=gnu11 "-$level" -Wno-attributes -Wno-mem \
        -Wno-return-type -DSQLITE_THREADSAFE=0 \
        -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_HAVE_READLINE=0 \
        -DSQLITE_DISABLE_INTRINSIC \
        -I "$work/src" -c "$work/src/sqlite3.c" \
        -o "$work/obj/sqlite3.$level.o"
}

run() {
    verify_archive "$amalgamation_zip" "$AMALGAMATION_SHA256" amalgamation
    verify_archive "$source_zip" "$SOURCE_SHA256" source
    [ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
    [ -x "$timeit" ] || fail "Sprint 52 timer is missing or not executable: $timeit"
    [ -r "$smoke" ] || fail "smoke script is missing: $smoke"
    [ -r "$baseline_config" ] || fail "baseline config is missing: $baseline_config"
    [ -x "$baseline_check" ] || fail "baseline checker is not executable: $baseline_check"
    [ -x "$measure" ] || fail "measurement helper is not executable: $measure"
    [ -x "$archive_check" ] || fail "archive checker is not executable: $archive_check"
    [ -x "$policy_check" ] || fail "policy checker is not executable: $policy_check"
    command -v "$hostcc" >/dev/null 2>&1 || fail "host GCC is unavailable: $hostcc"
    command -v unzip >/dev/null 2>&1 || fail "unzip is required"
    [ -r "$compat_header" ] ||
        fail "ARM64 hosted-header compatibility file is unreadable: $compat_header"

    compiler_target=$("$cgf" -dumpmachine) ||
        fail "cannot query Cgfried's target"
    compat_active=no
    case $compiler_target in
        x86_64-linux-gnu) ;;
        arm64-linux) compat_active=yes ;;
        *) fail "unsupported native campaign target: $compiler_target" ;;
    esac
    compat_header_sha256=$(sha256 "$compat_header")

    statement_count=$(awk '/;[[:space:]]*$/ { n++ } END { print n + 0 }' "$smoke")
    [ "$statement_count" -ge 20 ] || fail "smoke script has fewer than 20 statements"
    safe_work_reset
    "$archive_check" "$amalgamation_zip" "sqlite-amalgamation-$SQLITE_VERSION" \
        >"$work/logs/archive-amalgamation-check.log"
    "$archive_check" "$source_zip" "sqlite-src-$SQLITE_VERSION" \
        >"$work/logs/archive-source-check.log"
    unzip -q "$amalgamation_zip" -d "$work/unpack-amalgamation"
    unzip -q "$source_zip" "sqlite-src-$SQLITE_VERSION/test/speedtest1.c" \
        -d "$work/unpack-source"
    cp "$work/unpack-amalgamation/sqlite-amalgamation-$SQLITE_VERSION/"* "$work/src/"
    cp "$work/unpack-source/sqlite-src-$SQLITE_VERSION/test/speedtest1.c" "$work/src/"
    u128_matches=$work/logs/upstream-uint128-uses.txt
    u128_scan_errors=$work/logs/upstream-uint128-scan.err
    u128_scan_status=0
    LC_ALL=C grep -r -I -n -F --include='*.c' --include='*.h' -- \
        '__uint128_t' "$work/src" >"$u128_matches" \
        2>"$u128_scan_errors" || u128_scan_status=$?
    case $u128_scan_status in
        0 | 1) ;;
        *)
            cat "$u128_scan_errors" >&2
            fail "cannot audit pinned SQLite sources for integer-128 use"
            ;;
    esac
    upstream_u128_count=$(wc -l <"$u128_matches" | tr -d ' ')
    [ "$upstream_u128_count" -eq 0 ] ||
        fail "pinned SQLite sources use unsupported integer-128 semantics"

    actual_version=$(awk '/^#define SQLITE_VERSION / {gsub(/"/, "", $3); print $3; exit}' \
        "$work/src/sqlite3.h")
    [ "$actual_version" = "$SQLITE_RELEASE" ] ||
        fail "amalgamation version mismatch: expected $SQLITE_RELEASE, got $actual_version"

    levels='O0 O1 O2 O3 Os'
    for level in $levels; do
        case $level in
            O0 | O2) measure_cgf_sqlite "$level" ;;
            *) compile_cgf_object "$level" "$work/src/sqlite3.c" \
                "$work/obj/sqlite3.$level.o" ;;
        esac
        compile_cgf_object "$level" "$work/src/shell.c" "$work/obj/shell.$level.o"
        "$cgf" "$work/obj/sqlite3.$level.o" "$work/obj/shell.$level.o" \
            -lm -o "$work/bin/sqlite3-cgf-$level"
    done
    campaign_host=$(hostname -s 2>/dev/null || uname -n)
    policy_detail=$("$policy_check" "$baseline_config")
    case $policy_detail in
    *,state=numeric)
        policy_outcome=PASS
        policy_result_detail=$("$policy_check" --result-detail "$baseline_config")
        ;;
    *,state=unmeasured)
        policy_outcome=FAIL
        policy_result_detail=$policy_detail
        ;;
    *) fail "policy checker returned an unknown state: $policy_detail" ;;
    esac
    "$baseline_check" "$baseline_config" "$campaign_host" \
        "$work/receipts/sqlite3.O0.txt" "$work/receipts/sqlite3.O2.txt" \
        >"$work/logs/baseline-check.log"

    for level in O0 O2; do
        "$hostcc" -std=gnu11 "-$level" -DSQLITE_THREADSAFE=0 \
            -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_HAVE_READLINE=0 \
            -DSQLITE_DISABLE_INTRINSIC \
            "$work/src/sqlite3.c" "$work/src/shell.c" -lm \
            -o "$work/bin/sqlite3-gcc-$level"
        "$work/bin/sqlite3-cgf-$level" :memory: <"$smoke" \
            >"$work/logs/smoke-cgf-$level.out" 2>"$work/logs/smoke-cgf-$level.err"
        "$work/bin/sqlite3-gcc-$level" :memory: <"$smoke" \
            >"$work/logs/smoke-gcc-$level.out" 2>"$work/logs/smoke-gcc-$level.err"
        cmp "$work/logs/smoke-gcc-$level.out" "$work/logs/smoke-cgf-$level.out" ||
            fail "$level smoke output differs from host GCC"
        [ ! -s "$work/logs/smoke-cgf-$level.err" ] ||
            fail "$level Cgfried shell wrote unexpected stderr"
        [ ! -s "$work/logs/smoke-gcc-$level.err" ] ||
            fail "$level host-GCC shell wrote unexpected stderr"
    done
    cmp "$work/logs/smoke-cgf-O0.out" "$work/logs/smoke-cgf-O2.out" ||
        fail "Cgfried shell output differs between O0 and O2"

    compile_cgf_object O2 "$work/src/speedtest1.c" "$work/obj/speedtest1.O2.o"
    "$cgf" "$work/obj/sqlite3.O2.o" "$work/obj/speedtest1.O2.o" -lm \
        -o "$work/bin/speedtest1-cgf-O2"
    "$work/bin/speedtest1-cgf-O2" --size 1 --testset main --verify :memory: \
        >"$work/logs/speedtest1.out" 2>"$work/logs/speedtest1.err"
    [ ! -s "$work/logs/speedtest1.err" ] || fail "speedtest1 wrote unexpected stderr"
    grep -Fq '       TOTAL' "$work/logs/speedtest1.out" ||
        fail "speedtest1 did not emit its completion sentinel"
    actual_hash=$(sed -n 's/^Verification Hash: //p' "$work/logs/speedtest1.out")
    [ "$actual_hash" = "$SPEEDTEST_HASH" ] ||
        fail "speedtest1 verification mismatch: expected $SPEEDTEST_HASH, got $actual_hash"

    {
        printf 'sqlite.release=%s\n' "$SQLITE_RELEASE"
        printf 'sqlite.amalgamation_sha256=%s\n' "$AMALGAMATION_SHA256"
        printf 'sqlite.source_sha256=%s\n' "$SOURCE_SHA256"
        printf 'host=%s\n' "$campaign_host"
        printf 'architecture=%s\n' "$(uname -m)"
        printf 'compiler=%s\n' "$cgf"
        printf 'compiler_target=%s\n' "$compiler_target"
        printf 'runs=%s\n' "$runs"
        printf 'warmup=%s\n' "$warmup"
        printf 'timeout_seconds=%s\n' "$timeout_seconds"
        printf 'timeit_protocol=sprint-52-compile-median-mad-v1\n'
        printf 'compile_profile=sprint-52-sqlite-scale-v1\n'
        printf 'compile_flags=-Wno-attributes,-Wno-mem,-Wno-return-type\n'
        printf 'hosted_header_compatibility=%s\n' "$compat_active"
        printf 'hosted_header_policy=%s\n' "$compat_policy"
        printf 'hosted_header_path=%s\n' "$compat_header_rel"
        printf 'hosted_header_sha256=%s\n' "$compat_header_sha256"
        printf 'upstream_uint128_occurrences=%s\n' "$upstream_u128_count"
        printf 'baseline_policy=%s\n' "$policy_detail"
        for level in O0 O2; do
            sed "s/^/$level./" "$work/receipts/sqlite3.$level.txt"
        done
    } >"$work/compile-receipts.txt"

    tab=$(printf '\t')
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key%coutcome%cdetail\n' "$tab" "$tab"
        printf 'baseline.build%cPASS%ccompiler=host-gcc,levels=O0,O2\n' "$tab" "$tab"
        printf 'build.levels%cPASS%clevels=O0,O1,O2,O3,Os\n' "$tab" "$tab"
        printf 'compile.baseline-policy%c%s%c%s\n' "$tab" "$policy_outcome" "$tab" "$policy_result_detail"
        printf 'compile.receipts%cPASS%clevels=O0,O2,profile=sprint-52-sqlite-scale-v1,protocol=sprint-52-timeit-v1,runs=%s,warmup=%s\n' "$tab" "$tab" "$runs" "$warmup"
        printf 'hosted-header.compat%cPASS%cCAMP-ALL-004;header-sha256=%s,policy=%s,scope=arm64-linux-system-headers,upstream-uses=%s\n' \
            "$tab" "$tab" "$compat_header_sha256" "$compat_policy" "$upstream_u128_count"
        printf 'parity.cgfried-only-failures%cPASS%ccases=0\n' "$tab" "$tab"
        printf 'source.amalgamation%cPASS%crelease=%s,sha256=%s\n' "$tab" "$tab" \
            "$SQLITE_RELEASE" "$AMALGAMATION_SHA256"
        printf 'source.speedtest1%cPASS%crelease=%s,sha256=%s\n' "$tab" "$tab" \
            "$SQLITE_RELEASE" "$SOURCE_SHA256"
        printf 'test.smoke.o0%cPASS%cstatements=%s,oracle=host-gcc\n' "$tab" "$tab" "$statement_count"
        printf 'test.smoke.o2%cPASS%cstatements=%s,oracle=host-gcc\n' "$tab" "$tab" "$statement_count"
        printf 'test.speedtest1%cPASS%ctestset=main,size=1,verification=%s\n' "$tab" "$tab" \
            "$(printf '%s' "$SPEEDTEST_HASH" | tr ' ' ':')"
    } >"$work/results.txt"
    printf 'campaign-sqlite: PASS release=%s statements=%s results=%s receipts=%s\n' \
        "$SQLITE_RELEASE" "$statement_count" "$work/results.txt" "$work/compile-receipts.txt"
}

case ${1:-run} in
    fetch) fetch ;;
    run) run ;;
    *) fail "usage: $0 [fetch|run]" ;;
esac
