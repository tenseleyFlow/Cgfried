#!/bin/sh
set -eu

PCRE2_VERSION=10.48
PCRE2_COMMIT=7978954dbd2efc6f2196869290553cf1871b4ce6
PCRE2_SHA256=ebcc25aadf2a51fa1fefa9b8bc9e7a79b3dae86870a0f1152a22e42befd46888

fail() {
    echo "campaign-pcre2: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_PCRE2_ARCHIVE:-$root/build/campaigns/dl/pcre2-$PCRE2_VERSION.tar.gz}
work=${CGF_CAMPAIGN_PCRE2_WORK:-$root/build/campaigns/pcre2}
cgf=${CGF_CAMPAIGN_PCRE2_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_PCRE2_HOSTCC:-gcc}
sole=${CGF_CAMPAIGN_PCRE2_SOLE_C:-$root/scripts/campaigns/sole-c.sh}
jobs=${CGF_CAMPAIGN_JOBS:-}
cflags=${CGF_CAMPAIGN_PCRE2_CFLAGS:--O2}
linux_compat=$root/ci/campaigns/compat/arm64-linux-u128-storage.h
macos_compat=$root/ci/campaigns/compat/arm64-macos-u128-storage.h

[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
[ -x "$sole" ] || fail "sole-C wrapper is missing or not executable: $sole"
[ "$cflags" = -O2 ] || fail "PCRE2 validation requires exactly -O2, got: $cflags"
[ -f "$archive" ] || fail "verified source archive is missing: $archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$PCRE2_SHA256" ] ||
    fail "source checksum mismatch: expected $PCRE2_SHA256, got $got"

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

if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')
fi
case $jobs in '' | *[!0-9]* | 0) fail "CGF_CAMPAIGN_JOBS must be positive: $jobs" ;; esac

if [ -n "${CGF_CAMPAIGN_PCRE2_MAKE:-}" ]; then
    make_cmd=$CGF_CAMPAIGN_PCRE2_MAKE
elif make --version 2>/dev/null | grep -F 'GNU Make' >/dev/null; then
    make_cmd=make
elif command -v gmake >/dev/null 2>&1; then
    make_cmd=gmake
else
    fail "PCRE2 campaign requires GNU make"
fi
command -v "$make_cmd" >/dev/null 2>&1 || fail "GNU make is unavailable: $make_cmd"
command -v "$hostcc" >/dev/null 2>&1 || fail "host compiler is unavailable: $hostcc"

compiler_target=$("$cgf" -dumpmachine) || fail "cannot query Cgfried's target"
compat_header=
compat_policy=none
case $compiler_target in
    x86_64-linux-gnu) ;;
    arm64-linux)
        compat_header=$linux_compat
        compat_policy=opaque-u64x2-align16-v1
        ;;
    arm64-macos)
        compat_header=$macos_compat
        compat_policy=opaque-u64x2-align16+cgf-stdarg-v1
        ;;
    *) fail "unsupported native campaign target: $compiler_target" ;;
esac
compat_cppflags=
compat_sha256=none
if [ -n "$compat_header" ]; then
    [ -r "$compat_header" ] || fail "hosted-header compatibility file is unreadable"
    compat_cppflags="-include $compat_header"
    compat_sha256=$(sha256sum "$compat_header" | awk '{print $1}')
fi

as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "native assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "native linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

tree=$work/cgfried-src
host_tree=$work/host-gcc-src
build=$work/cgfried-build
host_build=$work/host-gcc-build
logs=$work/logs
receipts=$work/sole-c
manifest=$work/sole-c-closure.tsv
report=$work/sole-c-report.txt
sole_cc="$sole cc $receipts $cgf"

configure_options='--disable-shared
--enable-static
--enable-pcre2-8
--disable-pcre2-16
--disable-pcre2-32
--disable-jit
--enable-unicode
--disable-rebuild-chartables
--disable-pcre2grep-libz
--disable-pcre2grep-libbz2
--disable-pcre2test-libedit
--disable-pcre2test-libreadline
--disable-fuzz-support
--disable-valgrind
--disable-coverage
--disable-pcre2grep-callout'

extract_tree() {
    destination=$1
    mkdir -p "$destination"
    tar --no-same-owner --no-same-permissions -xzf "$archive" \
        -C "$destination" --strip-components=1
}

configure_tree() {
    label=$1
    source=$2
    destination=$3
    compiler=$4
    cppflags=$5
    mkdir -p "$destination" "$logs/$label"
    status=0
    (
        cd "$destination"
        LC_ALL=C SOURCE_DATE_EPOCH=0 CC="$compiler" CFLAGS="$cflags" \
            CPPFLAGS="$cppflags" "$source/configure" $configure_options
    ) >"$logs/$label/configure.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -240 "$logs/$label/configure.log" >&2
        fail "$label configure failed"
    fi
    [ -f "$destination/Makefile" ] || fail "$label configure produced no Makefile"
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs"
    if ! tar -tzf "$archive" | awk -v root="pcre2-$PCRE2_VERSION" '
        {
            name = $0
            original = name
            if (substr(name, 1, 1) == "/" || index(name, "\\") != 0 ||
                (name != root && name != root "/" && index(name, root "/") != 1)) {
                bad = 1
                next
            }
            sub(/\/$/, "", name)
            count = split(name, component, "/")
            for (i = 1; i <= count; i++)
                if (component[i] == "" || component[i] == "." ||
                    component[i] == "..") bad = 1
        }
        END { exit bad }
    '; then
        fail "source archive contains an unsafe or unexpected path"
    fi
    if ! tar -tvzf "$archive" | awk '
        substr($1, 1, 1) != "-" && substr($1, 1, 1) != "d" { bad = 1 }
        END { exit bad }
    '; then
        fail "source archive contains a link or special-file member"
    fi
    extract_tree "$tree"
    extract_tree "$host_tree"
    grep -F 'm4_define(pcre2_major, [10])' "$tree/configure.ac" >/dev/null ||
        fail "source tree does not identify PCRE2 major version 10"
    grep -F 'm4_define(pcre2_minor, [48])' "$tree/configure.ac" >/dev/null ||
        fail "source tree does not identify PCRE2 minor version 48"

    u128_matches=$logs/upstream-uint128-uses.txt
    u128_errors=$logs/upstream-uint128-scan.err
    u128_status=0
    LC_ALL=C grep -r -I -n -F --include='*.c' --include='*.h' -- \
        '__uint128_t' "$tree" >"$u128_matches" 2>"$u128_errors" || u128_status=$?
    case $u128_status in
        0 | 1) ;;
        *)
            cat "$u128_errors" >&2
            fail "cannot audit pinned PCRE2 sources for integer-128 use"
            ;;
    esac
    [ "$(wc -l <"$u128_matches" | tr -d ' ')" -eq 0 ] ||
        fail "pinned PCRE2 sources use unsupported integer-128 semantics"

    {
        printf 'version=%s\n' "$PCRE2_VERSION"
        printf 'commit=%s\n' "$PCRE2_COMMIT"
        printf 'sha256=%s\n' "$PCRE2_SHA256"
        printf 'target=%s\n' "$compiler_target"
        printf 'compiler=%s\n' "$cgf"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'cflags=%s\n' "$cflags"
        printf 'compat_policy=%s\n' "$compat_policy"
        printf 'compat_header=%s\n' "${compat_header:-none}"
        printf 'compat_header_sha256=%s\n' "$compat_sha256"
    } >"$work/provenance.txt"
    configure_tree cgfried "$tree" "$build" "$cgf" "$compat_cppflags"
    configure_tree host-gcc "$host_tree" "$host_build" "$hostcc" ''
}

build_tree() {
    label=$1
    destination=$2
    compiler=${3:-}
    status=0
    if [ -n "$compiler" ]; then
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            "CC=$compiler" >"$logs/$label/build.log" 2>&1 || status=$?
    else
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            >"$logs/$label/build.log" 2>&1 || status=$?
    fi
    if [ "$status" -ne 0 ]; then
        tail -240 "$logs/$label/build.log" >&2
        fail "$label build failed"
    fi
}

verify_products() {
    label=$1
    destination=$2
    [ -f "$destination/.libs/libpcre2-8.a" ] || fail "$label omitted libpcre2-8.a"
    [ -f "$destination/.libs/libpcre2-posix.a" ] || fail "$label omitted libpcre2-posix.a"
    members8=$(ar t "$destination/.libs/libpcre2-8.a" |
        awk '$0 !~ /^__.SYMDEF( SORTED)?$/ { count++ } END { print count + 0 }')
    members_posix=$(ar t "$destination/.libs/libpcre2-posix.a" |
        awk '$0 !~ /^__.SYMDEF( SORTED)?$/ { count++ } END { print count + 0 }')
    [ "$members8" -eq 31 ] || fail "$label libpcre2-8.a has $members8 members, expected 31"
    [ "$members_posix" -eq 1 ] ||
        fail "$label libpcre2-posix.a has $members_posix members, expected 1"
    for program in pcre2grep pcre2test pcre2posix_test; do
        [ -x "$destination/$program" ] || fail "$label omitted executable $program"
    done
    [ ! -e "$destination/.libs/libpcre2-8.so" ] || fail "$label produced a shared library"
    [ ! -e "$destination/.libs/libpcre2-16.a" ] || fail "$label unexpectedly built 16-bit PCRE2"
    [ ! -e "$destination/.libs/libpcre2-32.a" ] || fail "$label unexpectedly built 32-bit PCRE2"
}

build_stage() {
    [ -f "$build/Makefile" ] || fail "configure stage has not completed"
    "$sole" init "$receipts" "$cgf"
    build_tree cgfried "$build" "$sole_cc"
    build_tree host-gcc "$host_build"
    verify_products cgfried "$build"
    verify_products host-gcc "$host_build"
}

test_tree() {
    label=$1
    destination=$2
    compiler=${3:-}
    status=0
    if [ -n "$compiler" ]; then
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            "CC=$compiler" check >"$logs/$label/test.log" 2>&1 || status=$?
    else
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            check >"$logs/$label/test.log" 2>&1 || status=$?
    fi
    if [ "$status" -ne 0 ]; then
        tail -280 "$logs/$label/test.log" >&2
        [ ! -f "$destination/test-suite.log" ] || cat "$destination/test-suite.log" >&2
        fail "$label upstream tests failed"
    fi
    cp "$destination/test-suite.log" "$logs/$label/test-suite.log"
    grep -F '# TOTAL: 3' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite did not run exactly three tests"
    grep -F '# PASS:  3' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite did not pass all three tests"
    grep -F '# SKIP:  0' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite unexpectedly skipped a test"
    grep -F '# FAIL:  0' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite recorded a failure"
}

write_manifest() {
    library_sources='pcre2_auto_possess pcre2_chkdint pcre2_compile
pcre2_compile_cgroup pcre2_compile_class pcre2_config pcre2_context
pcre2_convert pcre2_dfa_match pcre2_error pcre2_extuni pcre2_find_bracket
pcre2_jit_compile pcre2_maketables pcre2_match pcre2_match_data
pcre2_match_next pcre2_newline pcre2_ord2utf pcre2_pattern_info
pcre2_script_run pcre2_serialize pcre2_string_utils pcre2_study
pcre2_substitute pcre2_substring pcre2_tables pcre2_ucd pcre2_valid_utf
pcre2_xclass'
    {
        echo '# cgf-sole-c-closure-v1'
        for name in $library_sources; do
            printf 'object\t%s/src/%s.c\t%s/src/libpcre2_8_la-%s.o\n' \
                "$tree" "$name" "$build" "$name"
        done
        printf 'object\t%s/src/pcre2_chartables.c\t%s/src/libpcre2_8_la-pcre2_chartables.o\n' \
            "$build" "$build"
        printf 'object\t%s/src/pcre2posix.c\t%s/src/libpcre2_posix_la-pcre2posix.o\n' \
            "$tree" "$build"
        printf 'object\t%s/src/pcre2grep.c\t%s/src/pcre2grep-pcre2grep.o\n' "$tree" "$build"
        printf 'object\t%s/src/pcre2test.c\t%s/src/pcre2test-pcre2test.o\n' "$tree" "$build"
        printf 'object\t%s/src/pcre2posix_test.c\t%s/src/pcre2posix_test-pcre2posix_test.o\n' \
            "$tree" "$build"

        for name in $library_sources; do
            printf 'archive\t%s/.libs/libpcre2-8.a\tlibpcre2_8_la-%s.o\t%s/src/libpcre2_8_la-%s.o\n' \
                "$build" "$name" "$build" "$name"
        done
        printf 'archive\t%s/.libs/libpcre2-8.a\tlibpcre2_8_la-pcre2_chartables.o\t%s/src/libpcre2_8_la-pcre2_chartables.o\n' \
            "$build" "$build"
        printf 'archive\t%s/.libs/libpcre2-posix.a\tlibpcre2_posix_la-pcre2posix.o\t%s/src/libpcre2_posix_la-pcre2posix.o\n' \
            "$build" "$build"

        printf 'link-input\t%s/pcre2grep\t%s/src/pcre2grep-pcre2grep.o\n' "$build" "$build"
        printf 'link-input\t%s/pcre2grep\t%s/.libs/libpcre2-8.a\n' "$build" "$build"
        for program in pcre2test pcre2posix_test; do
            printf 'link-input\t%s/%s\t%s/src/%s-%s.o\n' \
                "$build" "$program" "$build" "$program" "$program"
            printf 'link-input\t%s/%s\t%s/.libs/libpcre2-posix.a\n' \
                "$build" "$program" "$build"
            printf 'link-input\t%s/%s\t%s/.libs/libpcre2-8.a\n' \
                "$build" "$program" "$build"
            printf 'link-input\t%s/%s\t%s/.libs/libpcre2-8.a\n' \
                "$build" "$program" "$build"
        done
    } >"$manifest"
}

validate_stage() {
    verify_products cgfried "$build"
    verify_products host-gcc "$host_build"
    test_tree cgfried "$build" "$sole_cc"
    test_tree host-gcc "$host_build"

    "$build/pcre2grep" -V >"$logs/cgfried/pcre2grep-version.out" 2>&1
    "$host_build/pcre2grep" -V >"$logs/host-gcc/pcre2grep-version.out" 2>&1
    cmp "$logs/host-gcc/pcre2grep-version.out" "$logs/cgfried/pcre2grep-version.out" ||
        fail "pcre2grep version output differs from host GCC"
    "$build/pcre2test" -C >"$logs/cgfried/pcre2test-config.out" 2>&1
    "$host_build/pcre2test" -C >"$logs/host-gcc/pcre2test-config.out" 2>&1
    cmp "$logs/host-gcc/pcre2test-config.out" "$logs/cgfried/pcre2test-config.out" ||
        fail "pcre2test configuration output differs from host GCC"

    write_manifest
    "$sole" verify "$receipts" "$cgf" "$manifest" "$report"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-gcc,opt=O2\n'
        printf 'baseline.test.upstream\tPASS\tcases=3\n'
        printf 'build\tPASS\tlibraries=2,objects=35\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=35,archive-members=32,linked-products=3\n'
        printf 'configure\tPASS\tmode=static-8bit-unicode,jit=off\n'
        printf 'linkage\tPASS\tbinaries=3,libraries=2,static=yes\n'
        printf 'parity.outputs\tPASS\tcommands=pcre2grep-V+pcre2test-C\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$PCRE2_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' "$PCRE2_COMMIT" "$PCRE2_VERSION"
        printf 'test.upstream\tPASS\tcases=3,opt=O2\n'
    } >"$work/results.txt"
    printf 'campaign-pcre2: PASS target=%s results=%s artifacts=%s\n' \
        "$compiler_target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
