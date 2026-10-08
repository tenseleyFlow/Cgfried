#!/bin/sh
set -eu

LIBPNG_VERSION=1.6.59
LIBPNG_COMMIT=cd952f49f722c8ef3d2097b7fd0078399e2c4b2e
LIBPNG_SHA256=d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389
ZLIB_VERSION=1.3.1
ZLIB_SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23

fail() {
    echo "campaign-libpng: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_LIBPNG_ARCHIVE:-$root/build/campaigns/dl/libpng-$LIBPNG_VERSION.tar.xz}
zlib_archive=${CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE:-$root/build/campaigns/dl/zlib-$ZLIB_VERSION.tar.gz}
work=${CGF_CAMPAIGN_LIBPNG_WORK:-$root/build/campaigns/libpng}
cgf=${CGF_CAMPAIGN_LIBPNG_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_LIBPNG_HOSTCC:-gcc}
sole=${CGF_CAMPAIGN_LIBPNG_SOLE_C:-$root/scripts/campaigns/sole-c.sh}
jobs=${CGF_CAMPAIGN_JOBS:-}
cflags=${CGF_CAMPAIGN_LIBPNG_CFLAGS:--O2}

[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
[ -x "$sole" ] || fail "sole-C wrapper is missing or not executable: $sole"
[ "$cflags" = -O2 ] || fail "libpng validation requires exactly -O2, got: $cflags"
[ -f "$archive" ] || fail "verified libpng archive is missing: $archive"
[ -f "$zlib_archive" ] || fail "verified zlib archive is missing: $zlib_archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$LIBPNG_SHA256" ] ||
    fail "libpng checksum mismatch: expected $LIBPNG_SHA256, got $got"
got=$(sha256sum "$zlib_archive" | awk '{print $1}')
[ "$got" = "$ZLIB_SHA256" ] ||
    fail "zlib checksum mismatch: expected $ZLIB_SHA256, got $got"

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

if [ -n "${CGF_CAMPAIGN_LIBPNG_MAKE:-}" ]; then
    make_cmd=$CGF_CAMPAIGN_LIBPNG_MAKE
elif make --version 2>/dev/null | grep -F 'GNU Make' >/dev/null; then
    make_cmd=make
elif command -v gmake >/dev/null 2>&1; then
    make_cmd=gmake
else
    fail "libpng campaign requires GNU make"
fi
command -v "$make_cmd" >/dev/null 2>&1 || fail "GNU make is unavailable: $make_cmd"
command -v "$hostcc" >/dev/null 2>&1 || fail "host compiler is unavailable: $hostcc"

compiler_target=$("$cgf" -dumpmachine) || fail "cannot query Cgfried's target"
case $compiler_target in
    x86_64-linux-gnu | arm64-linux | arm64-macos) ;;
    *) fail "unsupported native campaign target: $compiler_target" ;;
esac

as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "native assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "native linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

tree=$work/cgfried-libpng-src
host_tree=$work/host-libpng-src
build=$work/cgfried-libpng-build
host_build=$work/host-libpng-build
zlib_tree=$work/cgfried-zlib-src
host_zlib_tree=$work/host-zlib-src
logs=$work/logs
receipts=$work/sole-c
manifest=$work/sole-c-closure.tsv
report=$work/sole-c-report.txt
sole_cc="$sole cc $receipts $cgf"

extract_libpng() {
    destination=$1
    mkdir -p "$destination"
    tar --no-same-owner --no-same-permissions -xJf "$archive" \
        -C "$destination" --strip-components=1
}

extract_zlib() {
    destination=$1
    mkdir -p "$destination"
    tar --no-same-owner --no-same-permissions -xzf "$zlib_archive" \
        -C "$destination" --strip-components=1
}

configure_zlib() {
    label=$1
    destination=$2
    compiler=$3
    mkdir -p "$logs/$label"
    status=0
    (
        cd "$destination"
        LC_ALL=C SOURCE_DATE_EPOCH=0 CC="$compiler" CFLAGS="$cflags" \
            ./configure --static
    ) >"$logs/$label/configure.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -200 "$logs/$label/configure.log" >&2
        fail "$label configure failed"
    fi
    [ -f "$destination/Makefile" ] || fail "$label configure produced no Makefile"
}

configure_libpng() {
    label=$1
    source=$2
    destination=$3
    compiler=$4
    dependency=$5
    mkdir -p "$destination" "$logs/$label"
    status=0
    (
        cd "$destination"
        LC_ALL=C SOURCE_DATE_EPOCH=0 CC="$compiler" CFLAGS="$cflags" \
            CPPFLAGS="-I$dependency" LDFLAGS="-L$dependency" \
            "$source/configure" --disable-maintainer-mode --disable-shared \
                --enable-static --enable-hardware-optimizations=no
    ) >"$logs/$label/configure.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -240 "$logs/$label/configure.log" >&2
        fail "$label configure failed"
    fi
    [ -f "$destination/Makefile" ] || fail "$label configure produced no Makefile"
    awk '
        $1 == "LIBS" && $2 == "=" {
            for (i = 3; i <= NF; i++)
                if ($i == "-lz") found = 1
        }
        END { exit !found }
    ' "$destination/Makefile" ||
        fail "$label configure did not select the pinned zlib interface"
}

audit_archive() {
    format=$1
    input=$2
    root_name=$3
    if [ "$format" = xz ]; then
        names="tar -tJf"
        listing="tar -tvJf"
    else
        names="tar -tzf"
        listing="tar -tvzf"
    fi
    if ! $names "$input" | awk -v root_name="$root_name" '
        {
            name = $0
            if (substr(name, 1, 1) == "/" || index(name, "\\") != 0 ||
                (name != root_name && name != root_name "/" &&
                 index(name, root_name "/") != 1)) {
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
        fail "$root_name archive contains an unsafe or unexpected path"
    fi
    if ! $listing "$input" | awk '
        substr($1, 1, 1) != "-" && substr($1, 1, 1) != "d" { bad = 1 }
        END { exit bad }
    '; then
        fail "$root_name archive contains a link or special-file member"
    fi
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs"
    audit_archive xz "$archive" "libpng-$LIBPNG_VERSION"
    audit_archive gzip "$zlib_archive" "zlib-$ZLIB_VERSION"
    extract_libpng "$tree"
    extract_libpng "$host_tree"
    extract_zlib "$zlib_tree"
    extract_zlib "$host_zlib_tree"
    grep -F 'AC_INIT([libpng],[1.6.59]' "$tree/configure.ac" >/dev/null ||
        fail "source tree does not identify libpng $LIBPNG_VERSION"
    grep -F '#define ZLIB_VERSION "1.3.1"' "$zlib_tree/zlib.h" >/dev/null ||
        fail "dependency tree does not identify zlib $ZLIB_VERSION"
    {
        printf 'version=%s\n' "$LIBPNG_VERSION"
        printf 'commit=%s\n' "$LIBPNG_COMMIT"
        printf 'sha256=%s\n' "$LIBPNG_SHA256"
        printf 'zlib_version=%s\n' "$ZLIB_VERSION"
        printf 'zlib_sha256=%s\n' "$ZLIB_SHA256"
        printf 'target=%s\n' "$compiler_target"
        printf 'compiler=%s\n' "$cgf"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'cflags=%s\n' "$cflags"
        printf 'link_dependency=explicit-pinned-zlib-archive\n'
        printf 'hardware_optimizations=off\n'
        printf 'float16_policy=declaration-layout-only\n'
    } >"$work/provenance.txt"
    configure_zlib cgfried-zlib "$zlib_tree" "$cgf"
    configure_zlib host-zlib "$host_zlib_tree" "$hostcc"
}

build_zlib() {
    label=$1
    destination=$2
    compiler=${3:-}
    status=0
    if [ -n "$compiler" ]; then
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            "CC=$compiler" libz.a >"$logs/$label/build.log" 2>&1 || status=$?
    else
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            libz.a >"$logs/$label/build.log" 2>&1 || status=$?
    fi
    if [ "$status" -ne 0 ]; then
        tail -200 "$logs/$label/build.log" >&2
        fail "$label build failed"
    fi
    [ -f "$destination/libz.a" ] || fail "$label produced no libz.a"
}

build_libpng() {
    label=$1
    destination=$2
    dependency=$3
    compiler=${4:-}
    status=0
    if [ -n "$compiler" ]; then
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            "CC=$compiler" "LIBS=$dependency/libz.a" all \
            >"$logs/$label/build.log" 2>&1 || status=$?
    else
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            "LIBS=$dependency/libz.a" all >"$logs/$label/build.log" 2>&1 || status=$?
    fi
    if [ "$status" -ne 0 ]; then
        tail -240 "$logs/$label/build.log" >&2
        fail "$label build failed"
    fi
}

archive_members() {
    ar t "$1" | awk '$0 !~ /^__.SYMDEF( SORTED)?$/ { count++ } END { print count + 0 }'
}

verify_products() {
    label=$1
    destination=$2
    dependency=$3
    [ -f "$dependency/libz.a" ] || fail "$label omitted libz.a"
    [ "$(archive_members "$dependency/libz.a")" -eq 15 ] ||
        fail "$label libz.a does not contain exactly 15 members"
    [ -f "$destination/.libs/libpng16.a" ] || fail "$label omitted libpng16.a"
    [ "$(archive_members "$destination/.libs/libpng16.a")" -eq 15 ] ||
        fail "$label libpng16.a does not contain exactly 15 members"
    for program in png-fix-itxt pngcp pngfix pnggetset pngimage pngstest \
        pngtest pngunknown pngvalid timepng; do
        [ -x "$destination/$program" ] || fail "$label omitted executable $program"
    done
    [ ! -e "$destination/.libs/libpng16.so" ] || fail "$label produced a shared library"
    [ ! -e "$destination/.libs/libpng16.dylib" ] || fail "$label produced a shared library"
}

build_stage() {
    [ -f "$zlib_tree/Makefile" ] || fail "configure stage has not completed"
    "$sole" init "$receipts" "$cgf"
    build_zlib cgfried-zlib "$zlib_tree" "$sole_cc"
    build_zlib host-zlib "$host_zlib_tree"
    configure_libpng cgfried-libpng "$tree" "$build" "$cgf" "$zlib_tree"
    configure_libpng host-libpng "$host_tree" "$host_build" "$hostcc" "$host_zlib_tree"
    build_libpng cgfried-libpng "$build" "$zlib_tree" "$sole_cc"
    build_libpng host-libpng "$host_build" "$host_zlib_tree"
}

test_libpng() {
    label=$1
    destination=$2
    dependency=$3
    compiler=${4:-}
    status=0
    if [ -n "$compiler" ]; then
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            "CC=$compiler" "LIBS=$dependency/libz.a" check \
            >"$logs/$label/test.log" 2>&1 || status=$?
    else
        LC_ALL=C SOURCE_DATE_EPOCH=0 "$make_cmd" -C "$destination" -j"$jobs" \
            "LIBS=$dependency/libz.a" check >"$logs/$label/test.log" 2>&1 || status=$?
    fi
    if [ "$status" -ne 0 ]; then
        tail -280 "$logs/$label/test.log" >&2
        [ ! -f "$destination/test-suite.log" ] || cat "$destination/test-suite.log" >&2
        fail "$label upstream tests failed"
    fi
    cp "$destination/test-suite.log" "$logs/$label/test-suite.log"
    grep -F '# TOTAL: 36' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite did not run exactly 36 tests"
    grep -F '# PASS:  36' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite did not pass all 36 tests"
    grep -F '# SKIP:  0' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite unexpectedly skipped a test"
    grep -F '# FAIL:  0' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite recorded a failure"
    grep -F '# ERROR: 0' "$destination/test-suite.log" >/dev/null ||
        fail "$label test suite recorded an error"
}

write_manifest() {
    zlib_sources='adler32 crc32 deflate infback inffast inflate inftrees trees
zutil compress uncompr gzclose gzlib gzread gzwrite'
    png_sources='png pngerror pngget pngmem pngpread pngread pngrio pngrtran
pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil'
    programs='png-fix-itxt pngcp pngfix pnggetset pngimage pngstest pngtest
pngunknown pngvalid timepng'
    {
        echo '# cgf-sole-c-closure-v1'
        for name in $zlib_sources; do
            printf 'object\t%s/%s.c\t%s/%s.o\n' "$zlib_tree" "$name" "$zlib_tree" "$name"
        done
        for name in $png_sources; do
            printf 'object\t%s/%s.c\t%s/%s.o\n' "$tree" "$name" "$build" "$name"
        done
        printf 'object\t%s/pngtest.c\t%s/pngtest.o\n' "$tree" "$build"
        for name in pnggetset pngimage pngstest pngunknown pngvalid timepng; do
            printf 'object\t%s/contrib/libtests/%s.c\t%s/contrib/libtests/%s.o\n' \
                "$tree" "$name" "$build" "$name"
        done
        for name in png-fix-itxt pngcp pngfix; do
            printf 'object\t%s/contrib/tools/%s.c\t%s/contrib/tools/%s.o\n' \
                "$tree" "$name" "$build" "$name"
        done

        for name in $zlib_sources; do
            printf 'archive\t%s/libz.a\t%s.o\t%s/%s.o\n' \
                "$zlib_tree" "$name" "$zlib_tree" "$name"
        done
        for name in $png_sources; do
            printf 'archive\t%s/.libs/libpng16.a\t%s.o\t%s/%s.o\n' \
                "$build" "$name" "$build" "$name"
        done

        for program in $programs; do
            case $program in
                pngtest)
                    object=$build/pngtest.o
                    ;;
                png-fix-itxt | pngcp | pngfix)
                    object=$build/contrib/tools/$program.o
                    ;;
                *)
                    object=$build/contrib/libtests/$program.o
                    ;;
            esac
            printf 'link-input\t%s/%s\t%s\n' "$build" "$program" "$object"
            if [ "$program" != png-fix-itxt ]; then
                printf 'link-input\t%s/%s\t%s/.libs/libpng16.a\n' \
                    "$build" "$program" "$build"
            fi
            printf 'link-input\t%s/%s\t%s/libz.a\n' \
                "$build" "$program" "$zlib_tree"
        done
    } >"$manifest"
}

validate_stage() {
    verify_products cgfried "$build" "$zlib_tree"
    verify_products host "$host_build" "$host_zlib_tree"
    test_libpng cgfried-libpng "$build" "$zlib_tree" "$sole_cc"
    test_libpng host-libpng "$host_build" "$host_zlib_tree"
    verify_products cgfried "$build" "$zlib_tree"
    verify_products host "$host_build" "$host_zlib_tree"

    mkdir -p "$work/parity/cgfried" "$work/parity/host"
    (
        cd "$work/parity/cgfried"
        "$build/pngtest" --strict "$tree/pngtest.png"
    ) >"$logs/cgfried-libpng/pngtest-strict.out" 2>&1
    (
        cd "$work/parity/host"
        "$host_build/pngtest" --strict "$tree/pngtest.png"
    ) >"$logs/host-libpng/pngtest-strict.out" 2>&1
    cmp "$logs/host-libpng/pngtest-strict.out" \
        "$logs/cgfried-libpng/pngtest-strict.out" ||
        fail "pngtest strict output differs from the host compiler"
    cmp "$work/parity/host/pngout.png" "$work/parity/cgfried/pngout.png" ||
        fail "pngtest output image differs from the host compiler"

    write_manifest
    "$sole" verify "$receipts" "$cgf" "$manifest" "$report"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-cc,opt=O2\n'
        printf 'baseline.test.upstream\tPASS\tcases=36\n'
        printf 'build\tPASS\tlibraries=2,objects=40\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=40,archive-members=30,linked-products=10\n'
        printf 'configure\tPASS\thardware-optimizations=off,mode=static,zlib=pinned\n'
        printf 'dependency.zlib\tPASS\tarchive-members=15,sole-c=yes,version=%s\n' "$ZLIB_VERSION"
        printf 'hosted-header.float16\tPASS\tpolicy=declaration-layout-only,value-codegen=refused\n'
        printf 'linkage\tPASS\tbinaries=10,libraries=2,static=yes\n'
        printf 'parity.outputs\tPASS\tartifacts=pngout.png,commands=pngtest-strict\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$LIBPNG_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' "$LIBPNG_COMMIT" "$LIBPNG_VERSION"
        printf 'test.upstream\tPASS\tcases=36,opt=O2\n'
    } >"$work/results.txt"
    printf 'campaign-libpng: PASS target=%s results=%s artifacts=%s\n' \
        "$compiler_target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
