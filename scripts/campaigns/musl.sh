#!/bin/sh
set -eu

MUSL_REF=b306b16af15c89a04d8e0c55cac2dadbeb39c083
LIBC_TEST_REF=123433158bf985d7eb3b4072e32121b9e32a1a1a

fail() {
    echo "campaign-musl: $*" >&2
    exit 1
}

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
musl_source=${CGF_CAMPAIGN_MUSL_SOURCE:-$root/.docs/refs/musl}
libc_test_source=${CGF_CAMPAIGN_LIBC_TEST_SOURCE:-$root/.docs/refs/libc-test}
work=${CGF_CAMPAIGN_MUSL_WORK:-$root/build/campaigns/musl}
cgf=${CGF_CAMPAIGN_MUSL_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_MUSL_HOSTCC:-gcc}
checker=${CGF_CAMPAIGN_CHECK:-$root/ci/campaigns/check-expected.sh}
expected=${CGF_CAMPAIGN_MUSL_EXPECTED:-$root/ci/campaigns/musl.expected}
jobs=${CGF_CAMPAIGN_JOBS:-}

[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
[ -x "$checker" ] || fail "expected-results checker is missing: $checker"
command -v "$hostcc" >/dev/null 2>&1 || fail "host compiler is unavailable: $hostcc"

check_ref() {
    checkout=$1
    want=$2
    name=$3
    got=$(git -C "$checkout" rev-parse --verify HEAD 2>/dev/null) ||
        fail "$name checkout is missing or invalid: $checkout"
    [ "$got" = "$want" ] ||
        fail "$name ref mismatch: expected $want, got $got"
    git -C "$checkout" cat-file -e "$want^{commit}" 2>/dev/null ||
        fail "$name pinned commit is unavailable: $want"
}

check_ref "$musl_source" "$MUSL_REF" musl
check_ref "$libc_test_source" "$LIBC_TEST_REF" libc-test

# Recursive cleanup is confined to one direct, non-symlink child of the
# canonical campaign root.  Lexical prefix checks alone admit `..` and
# symlink traversal.
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
[ "$work" != "$musl_source" ] || fail "musl source and work directories must differ"
[ "$work" != "$libc_test_source" ] || fail "libc-test source and work directories must differ"

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

rm -rf "$work"
mkdir -p "$work/logs" "$work/routes"

archive_ref() {
    checkout=$1
    revision=$2
    destination=$3
    mkdir -p "$destination"
    git -C "$checkout" archive "$revision" | tar -x -C "$destination"
}

build_musl() {
    lane=$1
    compiler=$2
    route_dir=$3
    tree=$work/$lane/src
    install=$work/$lane/install

    archive_ref "$musl_source" "$MUSL_REF" "$tree"
    if [ -n "$route_dir" ]; then
        mkdir -p "$route_dir"
        export CGF_MUSL_CGF="$cgf" CGF_MUSL_HOSTCC="$hostcc"
        export CGF_MUSL_ROUTE_DIR="$route_dir"
    else
        unset CGF_MUSL_CGF CGF_MUSL_HOSTCC CGF_MUSL_ROUTE_DIR
    fi
    if ! (
        cd "$tree"
        LC_ALL=C SOURCE_DATE_EPOCH=0 CC="$compiler" \
            ./configure --target=x86_64 --disable-shared --prefix=/usr
    ) >"$work/logs/$lane-configure.log" 2>&1; then
        cat "$work/logs/$lane-configure.log" >&2
        fail "$lane configure failed"
    fi
    cp "$tree/config.mak" "$work/logs/$lane-config.mak"
    if ! LC_ALL=C SOURCE_DATE_EPOCH=0 \
        make -C "$tree" -j"$jobs" AR=ar RANLIB=ranlib \
        >"$work/logs/$lane-build.log" 2>&1; then
        tail -200 "$work/logs/$lane-build.log" >&2
        fail "$lane build failed; see $work/logs/$lane-build.log"
    fi
    if ! make -C "$tree" DESTDIR="$install" install \
        >"$work/logs/$lane-install.log" 2>&1; then
        tail -100 "$work/logs/$lane-install.log" >&2
        fail "$lane install failed"
    fi
    [ -f "$tree/lib/libc.a" ] || fail "$lane produced no libc.a"
    for crt in crt1.o crti.o crtn.o Scrt1.o rcrt1.o; do
        [ -f "$tree/lib/$crt" ] || fail "$lane produced no $crt"
        if ! readelf -SW "$tree/lib/$crt" |
            grep -F '.note.GNU-stack' >/dev/null; then
            fail "$lane $crt omitted .note.GNU-stack"
        fi
    done
}

wrapper=$root/scripts/campaigns/musl-cc.sh
build_musl cgf-a "$wrapper" "$work/routes/cgf-a"
build_musl cgf-b "$wrapper" "$work/routes/cgf-b"
build_musl gcc "$hostcc" ""

collect_routes() {
    route_dir=$1
    output=$2
    find "$route_dir" -type f -name '*.route' -print | LC_ALL=C sort |
        xargs cat | LC_ALL=C sort >"$output"
}
collect_routes "$work/routes/cgf-a" "$work/routes/cgf-a.tsv"
collect_routes "$work/routes/cgf-b" "$work/routes/cgf-b.tsv"
cmp "$work/routes/cgf-a.tsv" "$work/routes/cgf-b.tsv" ||
    fail "compiler provenance differs between clean builds"

cgf_c=$(awk -F '\t' '$1=="cgf" && $2 ~ /\.c$/ {n++} END {print n+0}' \
    "$work/routes/cgf-a.tsv")
host_complex=$(awk -F '\t' '$1=="host" && $2 ~ /src\/complex\/.*\.c$/ {n++} END {print n+0}' \
    "$work/routes/cgf-a.tsv")
host_asm=$(awk -F '\t' '$1=="host" && $2 ~ /\.[sS]$/ {n++} END {print n+0}' \
    "$work/routes/cgf-a.tsv")
total_objects=$(wc -l <"$work/routes/cgf-a.tsv" | tr -d ' ')
[ "$cgf_c" -gt 0 ] || fail "provenance recorded no Cgfried C objects"
[ "$host_complex" -gt 0 ] || fail "provenance recorded no host complex objects"
[ "$host_asm" -gt 0 ] || fail "provenance recorded no host assembler objects"
[ $((cgf_c + host_complex + host_asm)) -eq "$total_objects" ] ||
    fail "provenance contains an unclassified route"

hash_products() {
    tree=$1
    output=$2
    (
        cd "$tree"
        find obj -type f -name '*.o' -print
        find lib -maxdepth 1 -type f \( -name '*.a' -o -name '*.o' \) -print
    ) | LC_ALL=C sort | while IFS= read -r file; do
        (cd "$tree" && sha256sum "$file")
    done >"$output"
}
hash_products "$work/cgf-a/src" "$work/logs/cgf-a-products.sha256"
hash_products "$work/cgf-b/src" "$work/logs/cgf-b-products.sha256"
cmp "$work/logs/cgf-a-products.sha256" "$work/logs/cgf-b-products.sha256" ||
    fail "clean Cgfried musl builds are not byte-identical"

musl_root=$work/cgf-a/install
export CGF_CRT_DIR="$musl_root/usr/lib"
compile_static() {
    input=$1
    output=$2
    "$cgf" --target=x86_64-linux-musl --sysroot="$musl_root" -static -O2 \
        "$input" -o "$output"
}

cat >"$work/hello.c" <<'EOF'
#include <stdio.h>
int main(void) { return puts("cgfried musl campaign") < 0; }
EOF
if ! compile_static "$work/hello.c" "$work/hello" \
    >"$work/logs/hello-build.log" 2>&1; then
    cat "$work/logs/hello-build.log" >&2
    fail "static hello failed to build"
fi
if ! "$work/hello" >"$work/logs/hello-run.log" 2>&1; then
    cat "$work/logs/hello-run.log" >&2
    fail "static hello failed to run"
fi

mkdir -p "$work/bench"
bench_count=0
for source in "$root"/tests/bench/kernels/*.c; do
    name=${source##*/}
    name=${name%.c}
    if ! compile_static "$source" "$work/bench/$name" \
        >"$work/logs/bench-$name-build.log" 2>&1; then
        cat "$work/logs/bench-$name-build.log" >&2
        fail "static benchmark failed to build: $name"
    fi
    if ! "$work/bench/$name" >"$work/logs/bench-$name-run.log" 2>&1; then
        cat "$work/logs/bench-$name-run.log" >&2
        fail "static benchmark failed to run: $name"
    fi
    bench_count=$((bench_count + 1))
done
[ "$bench_count" -eq 21 ] || fail "expected 21 benchmark cases, got $bench_count"

make_gcc_wrapper() {
    output=$1
    musl_install=$2
    lane=$3
    specs=$work/logs/libc-test-$lane-musl-gcc.specs
    include_dir=$musl_install/usr/include
    lib_dir=$musl_install/usr/lib
    ldso=$musl_install/lib/ld-musl-x86_64.so.1

    sh "$musl_source/tools/musl-gcc.specs.sh" \
        "$include_dir" "$lib_dir" "$ldso" >"$specs"
    cat >"$output" <<EOF
#!/bin/sh
exec "$hostcc" -static -specs="$specs" "\$@"
EOF
    chmod +x "$output"
}

verify_musl_wrapper() {
    lane=$1
    musl_install=$2
    cc_wrapper=$3
    source=$work/logs/libc-test-$lane-link-probe.c
    binary=$work/logs/libc-test-$lane-link-probe
    map=$work/logs/libc-test-$lane-link.map
    expected_crt=$musl_install/usr/lib/Scrt1.o
    expected_libc=$musl_install/usr/lib/libc.a

    printf 'int main(void) { return 0; }\n' >"$source"
    if ! "$cc_wrapper" "$source" "-Wl,-Map,$map" -o "$binary" \
        >"$work/logs/libc-test-$lane-link.log" 2>&1; then
        cat "$work/logs/libc-test-$lane-link.log" >&2
        fail "libc-test $lane wrapper failed its staged-musl link probe"
    fi
    [ -x "$binary" ] || fail "libc-test $lane wrapper produced no link probe"
    if ! awk -v expected_crt="$expected_crt" -v expected_libc="$expected_libc" '
        {
            for (i = 1; i <= NF; i++) {
                path = $i
                sub(/\(.*/, "", path)
                if (path ~ /\/Scrt1[.]o$/) {
                    seen_crt = 1
                    if (path != expected_crt)
                        bad_crt = 1
                }
                if (path ~ /\/libc[.]a$/) {
                    seen_libc = 1
                    if (path != expected_libc)
                        bad_libc = 1
                }
            }
        }
        END {
            exit !(seen_crt && !bad_crt && seen_libc && !bad_libc)
        }
    ' "$map"; then
        fail "libc-test $lane wrapper selected non-staged CRT or libc"
    fi
    if ! "$binary"; then
        fail "libc-test $lane staged-musl link probe failed to run"
    fi
}

run_libc_test() {
    lane=$1
    musl_install=$2
    tree=$work/libc-test-$lane/src
    build=$work/libc-test-$lane/build
    cc_wrapper=$work/libc-test-$lane/musl-gcc

    archive_ref "$libc_test_source" "$LIBC_TEST_REF" "$tree"
    "$root/scripts/campaigns/libc-test-static-prepare.sh" "$tree"
    # libc-test computes its object and directory lists before config.mak is
    # included, so B must be a command-line variable.  options.h also writes
    # into common/ without depending on that directory target.
    mkdir -p "$build/common"
    make_gcc_wrapper "$cc_wrapper" "$musl_install" "$lane"
    verify_musl_wrapper "$lane" "$musl_install" "$cc_wrapper"
    cat >"$tree/config.mak" <<EOF
B := $build
CC := $cc_wrapper
AR := ar
RANLIB := ranlib
CFLAGS := -I$build/common -I$tree/src/common -pipe -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wno-unused-function -Wno-missing-braces -Wno-unused -Wno-overflow -Wno-unknown-pragmas -fno-builtin -frounding-math -Werror=implicit-function-declaration -Werror=implicit-int -Werror=pointer-sign -Werror=pointer-arith -g
LDFLAGS := -g -static
LDLIBS := $build/common/libtest.a -lpthread -lm -lrt -lcrypt -ldl -lresolv -lutil
EOF
    expected_static=$work/logs/libc-test-$lane-expected-static.txt
    actual_static=$work/logs/libc-test-$lane-actual-static.txt
    LC_ALL=C "$root/scripts/campaigns/libc-test-static-make.sh" \
        "$tree" "$build" -s cgfried-static-manifest |
        LC_ALL=C sort >"$expected_static"
    # A clean parallel `all` may schedule common/REPORT before any common
    # object has produced an .err file.  Build the shared runner first so the
    # report rule has real inputs and the remainder can still run in parallel.
    if ! (
        LC_ALL=C "$root/scripts/campaigns/libc-test-static-make.sh" \
            "$tree" "$build" -j"$jobs" "$build/common/runtest.exe" &&
            test -x "$build/common/runtest.exe" &&
            LC_ALL=C "$root/scripts/campaigns/libc-test-static-make.sh" \
                "$tree" "$build" -j"$jobs"
    ) >"$work/logs/libc-test-$lane.log" 2>&1; then
        tail -200 "$work/logs/libc-test-$lane.log" >&2
        fail "libc-test $lane harness failed"
    fi
    find "$build" -type f -name '*.err' -size +0c -printf '%P\n' |
        LC_ALL=C sort >"$work/libc-test-$lane/failures.txt"
    cp "$build/REPORT" "$work/libc-test-$lane/REPORT"
    # Prove exact coverage against libc-test's own expanded BINS manifest.
    # libc-test also has four explicit, unpaired `.exe` prerequisites for
    # dlopen/TLS coverage; the wrapper still links those statically, so prove
    # their ELF linkage instead of inferring it from exceptional filenames.
    find "$build/functional" "$build/regression" "$build/math" \
        "$build/musl" -maxdepth 1 -type f -name '*-static.exe' -print |
        LC_ALL=C sort >"$actual_static"
    cmp "$expected_static" "$actual_static" ||
        fail "libc-test $lane static binary manifest drifted"
    for suite in functional regression math musl; do
        suite_dir=$build/$suite
        unsuffixed=$work/logs/libc-test-$lane-$suite-unsuffixed.txt
        [ -d "$suite_dir" ] || fail "libc-test $lane omitted suite: $suite"
        find "$suite_dir" -maxdepth 1 -type f \
            -name '*.exe' ! -name '*-static.exe' -print >"$unsuffixed"
        while IFS= read -r binary; do
            paired=${binary%.exe}-static.exe
            headers=$binary.readelf-headers
            dynamic=$binary.readelf-dynamic
            [ ! -e "$paired" ] ||
                fail "libc-test $lane duplicated a suite binary: $binary and $paired"
            readelf -l "$binary" >"$headers" ||
                fail "libc-test $lane could not inspect suite binary: $binary"
            readelf -d "$binary" >"$dynamic" ||
                fail "libc-test $lane could not inspect suite binary: $binary"
            if grep -q 'INTERP' "$headers" || grep -q 'NEEDED' "$dynamic"; then
                fail "libc-test $lane produced a dynamically linked suite binary: $binary"
            fi
        done <"$unsuffixed"
    done
}

run_libc_test cgf "$work/cgf-a/install"
run_libc_test gcc "$work/gcc/install"
comm -23 "$work/libc-test-cgf/failures.txt" \
    "$work/libc-test-gcc/failures.txt" >"$work/libc-test-cgf-only.txt"
cgf_only_failures=$(wc -l <"$work/libc-test-cgf-only.txt" | tr -d ' ')
[ "$cgf_only_failures" -eq 0 ] || {
    sed 's/^/campaign-musl: cgf-only libc-test failure: /' \
        "$work/libc-test-cgf-only.txt" >&2
    fail "libc-test parity failed"
}

tab=$(printf '\t')
{
    echo '# cgf-campaign-results-v1'
    printf '# columns=key%coutcome%cdetail\n' "$tab" "$tab"
    printf 'build.crt%cPASS%cobjects=5\n' "$tab" "$tab"
    printf 'build.libc-a%cPASS%carchive=libc.a\n' "$tab" "$tab"
    printf 'compile.assembler%cPASS%chost=%s\n' "$tab" "$tab" "$host_asm"
    printf 'compile.complex%cSKIP%cCAMP-MUSL-001;host=%s\n' "$tab" "$tab" "$host_complex"
    printf 'compile.non-complex%cPASS%ccgfried=%s,total-c=%s\n' "$tab" "$tab" \
        "$cgf_c" "$((cgf_c + host_complex))"
    printf 'determinism.objects%cPASS%cobjects=%s\n' "$tab" "$tab" "$total_objects"
    printf 'libc-test.linkage%cPASS%clanes=2,mode=staged-musl-static\n' "$tab" "$tab"
    printf 'libc-test.parity%cPASS%ccgf-only-failures=%s\n' "$tab" "$tab" \
        "$cgf_only_failures"
    printf 'run.benchmarks%cPASS%ccases=%s\n' "$tab" "$tab" "$bench_count"
    printf 'run.static-hello%cPASS%cexit=0\n' "$tab" "$tab"
    printf 'shared%cSKIP%cCAMP-MUSL-002\n' "$tab" "$tab"
    printf 'source.libc-test-pin%cPASS%crevision=%s\n' "$tab" "$tab" "$LIBC_TEST_REF"
    printf 'source.musl-pin%cPASS%crevision=%s\n' "$tab" "$tab" "$MUSL_REF"
} >"$work/results.txt"

"$checker" "$expected" "$work/results.txt"
printf 'campaign-musl: PASS cgf-c=%s complex-host=%s asm-host=%s objects=%s benches=%s artifacts=%s\n' \
    "$cgf_c" "$host_complex" "$host_asm" "$total_objects" "$bench_count" "$work"
