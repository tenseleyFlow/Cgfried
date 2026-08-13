#!/bin/sh
set -eu

ZLIB_REF=1.3.1
ZLIB_SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23

fail() {
    echo "campaign-zlib: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_ZLIB_ARCHIVE:-$root/build/campaigns/dl/zlib-$ZLIB_REF.tar.gz}
work=${CGF_CAMPAIGN_ZLIB_WORK:-$root/build/campaigns/zlib}
cgf=${CGF_CAMPAIGN_ZLIB_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_ZLIB_HOSTCC:-gcc}
timeit=${CGF_CAMPAIGN_ZLIB_TIMEIT:-$root/build/timeit}
target=${CGF_CAMPAIGN_ZLIB_TARGET:-native}
sysroot=${CGF_CAMPAIGN_ZLIB_SYSROOT:-}
run_prefix=${CGF_CAMPAIGN_ZLIB_RUNNER_PREFIX:-}
jobs=${CGF_CAMPAIGN_JOBS:-}
cflags=${CGF_CAMPAIGN_ZLIB_CFLAGS:--O2}

[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
[ "$cflags" = -O2 ] ||
    fail "Sprint 59 zlib validation requires exactly -O2, got: $cflags"
[ -f "$archive" ] || fail "verified source archive is missing: $archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$ZLIB_SHA256" ] ||
    fail "source checksum mismatch: expected $ZLIB_SHA256, got $got"

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

host_machine=$(uname -m)
case $host_machine in
    x86_64 | amd64) host_target=x86_64-linux-gnu; host_arch=x86_64 ;;
    aarch64 | arm64) host_target=arm64-linux; host_arch=aarch64 ;;
    *) fail "unsupported campaign host architecture: $host_machine" ;;
esac
case $target in
    native) target=$host_target; target_arch=$host_arch; target_flags= ;;
    x86_64-linux-gnu)
        target_arch=x86_64
        target_flags="--target=x86_64-linux-gnu"
        ;;
    arm64-linux | aarch64-linux-gnu)
        target=arm64-linux
        target_arch=aarch64
        target_flags="--target=arm64-linux"
        ;;
    x86_64-linux-musl)
        target_arch=x86_64
        target_flags="--target=x86_64-linux-musl -static"
        [ -n "$sysroot" ] ||
            fail "x86_64-linux-musl requires CGF_CAMPAIGN_ZLIB_SYSROOT"
        ;;
    *) fail "unsupported CGF_CAMPAIGN_ZLIB_TARGET: $target" ;;
esac

if [ -n "$sysroot" ]; then
    case $sysroot in /*) ;; *) sysroot=$root/$sysroot ;; esac
    [ -d "$sysroot" ] || fail "sysroot is not a directory: $sysroot"
    target_flags="$target_flags --sysroot=$sysroot"
    # Debian's cross root is rooted at /usr/aarch64-linux-gnu and keeps
    # headers directly under include/, while a conventional staged sysroot
    # keeps them under usr/include/.  The driver handles the latter itself.
    if [ "$target" = arm64-linux ] && [ -d "$sysroot/include" ]; then
        target_flags="$target_flags -isystem $sysroot/include"
    fi
fi

case $host_arch:$target_arch in
    x86_64:x86_64 | aarch64:aarch64)
        target_is_native=1
        ;;
    *) target_is_native=0 ;;
esac
command -v "$hostcc" >/dev/null 2>&1 ||
    fail "host compiler is unavailable: $hostcc"
[ -x "$timeit" ] || fail "timer is unavailable: $timeit"
case $target in
    arm64-linux)
        as_default=$(command -v aarch64-linux-gnu-as 2>/dev/null || true)
        ld_default=$(command -v aarch64-linux-gnu-ld 2>/dev/null || true)
        expected_machine='AArch64'
        ;;
    *)
        as_default=$(command -v as 2>/dev/null || true)
        ld_default=$(command -v ld 2>/dev/null || true)
        expected_machine='Advanced Micro Devices X86-64'
        ;;
esac
as_path=${CGF_AS_PATH:-$as_default}
ld_path=${CGF_LD_PATH:-$ld_default}
[ -n "$as_path" ] || fail "assembler is unavailable for $target"
[ -n "$ld_path" ] || fail "linker is unavailable for $target"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"
if [ -n "${CGF_CAMPAIGN_ZLIB_CRT_DIR:-}" ]; then
    CGF_CRT_DIR=$CGF_CAMPAIGN_ZLIB_CRT_DIR
    export CGF_CRT_DIR
elif [ "$target" = x86_64-linux-musl ]; then
    CGF_CRT_DIR=$sysroot/usr/lib
    export CGF_CRT_DIR
fi

tree=$work/cgfried-src
host_tree=$work/host-gcc-src
logs=$work/logs
cc="$cgf${target_flags:+ $target_flags}"

extract_tree() {
    destination=$1
    mkdir -p "$destination"
    tar --no-same-owner --no-same-permissions -xzf "$archive" \
        -C "$destination" --strip-components=1
}

configure_tree() {
    label=$1
    destination=$2
    compiler=$3
    mkdir -p "$logs/$label"
    if ! (
        cd "$destination"
        LC_ALL=C SOURCE_DATE_EPOCH=0 CC="$compiler" CFLAGS="$cflags" \
            ./configure --static
    ) >"$logs/$label/configure.stdout" 2>&1; then
        tail -200 "$logs/$label/configure.stdout" >&2
        [ ! -f "$destination/configure.log" ] ||
            cp "$destination/configure.log" "$logs/$label/configure.log"
        fail "$label configure failed"
    fi
    cp "$destination/configure.log" "$logs/$label/configure.log"
    [ -f "$destination/Makefile" ] ||
        fail "$label configure produced no Makefile"
}

build_tree() {
    label=$1
    destination=$2
    [ -f "$destination/Makefile" ] || fail "$label configure has not completed"
    if ! LC_ALL=C SOURCE_DATE_EPOCH=0 make -C "$destination" -j"$jobs" static \
        >"$logs/$label/build.log" 2>&1; then
        tail -200 "$logs/$label/build.log" >&2
        fail "$label build failed"
    fi
    [ -f "$destination/libz.a" ] || fail "$label produced no libz.a"
    objects=$(ar t "$destination/libz.a" | wc -l | tr -d ' ')
    [ "$objects" -eq 15 ] ||
        fail "$label libz.a has $objects objects, expected 15"
}

test_tree() {
    label=$1
    destination=$2
    prefix=$3
    if ! LC_ALL=C SOURCE_DATE_EPOCH=0 make -C "$destination" -j"$jobs" \
        QEMU_RUN="$prefix" test >"$logs/$label/test.log" 2>&1; then
        tail -240 "$logs/$label/test.log" >&2
        fail "$label make test failed"
    fi
    grep -F '*** zlib test OK ***' "$logs/$label/test.log" >/dev/null ||
        fail "$label omitted the static-suite success sentinel"
    grep -F '*** zlib 64-bit test OK ***' "$logs/$label/test.log" >/dev/null ||
        fail "$label omitted the 64-bit-suite success sentinel"
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs"
    if ! tar -tzf "$archive" | awk '
        {
            name = $0
            if (substr(name, 1, 1) == "/" || index(name, "\\") != 0 ||
                index(name, "zlib-1.3.1/") != 1) {
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
    {
        printf 'version=%s\n' "$ZLIB_REF"
        printf 'sha256=%s\n' "$ZLIB_SHA256"
        printf 'target=%s\n' "$target"
        printf 'host=%s\n' "$host_machine"
        printf 'compiler=%s\n' "$cc"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'cflags=%s\n' "$cflags"
        printf 'sysroot=%s\n' "${sysroot:-none}"
        printf 'runner=%s\n' "${run_prefix:-native}"
    } >"$work/provenance.txt"
    configure_tree cgfried "$tree" "$cc"
    configure_tree host-gcc "$host_tree" "$hostcc"
}

build_stage() {
    build_tree cgfried "$tree"
    build_tree host-gcc "$host_tree"
}

write_benchmark() {
    output=$1
    cat >"$output" <<'EOF'
#include "zlib.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    static unsigned char data[1 << 20];
    uLong adler = adler32(0L, Z_NULL, 0);
    uLong crc = crc32(0L, Z_NULL, 0);
    unsigned i, round;

    for (i = 0; i < sizeof(data); i++)
        data[i] = (unsigned char)((i * 17u + i / 251u) & 255u);
    for (round = 0; round < 128; round++) {
        adler = adler32(adler, data, sizeof(data));
        crc = crc32(crc, data, sizeof(data));
    }
    (void)argv;
    if (argc == 1)
        printf("%08lx %08lx\n", (unsigned long)adler, (unsigned long)crc);
    return 0;
}
EOF
}

native_differential() {
    bench=$work/zlib-crc-adler-bench.c
    write_benchmark "$bench"
    set -- "$cgf"
    case $target in
        x86_64-linux-gnu)
            [ "$target" = "$host_target" ] ||
                set -- "$@" --target=x86_64-linux-gnu
            ;;
        arm64-linux)
            [ "$target" = "$host_target" ] ||
                set -- "$@" --target=arm64-linux
            ;;
        x86_64-linux-musl)
            set -- "$@" --target=x86_64-linux-musl -static
            ;;
    esac
    if [ -n "$sysroot" ]; then
        set -- "$@" "--sysroot=$sysroot"
        if [ "$target" = arm64-linux ] && [ -d "$sysroot/include" ]; then
            set -- "$@" -isystem "$sysroot/include"
        fi
    fi
    "$@" "$cflags" -I "$tree" "$bench" "$tree/libz.a" \
        -o "$work/zlib-bench-cgfried"
    "$hostcc" "$cflags" -I "$host_tree" "$bench" "$host_tree/libz.a" \
        -o "$work/zlib-bench-host-gcc"
    if [ -n "$run_prefix" ]; then
        {
            echo '#!/bin/sh'
            printf 'exec %s "$@"\n' "$run_prefix"
        } >"$work/run-target"
        chmod +x "$work/run-target"
        target_runner=$work/run-target
    else
        target_runner=
    fi
    "$work/zlib-bench-host-gcc" >"$logs/host-gcc/benchmark.out"
    if [ -n "$target_runner" ]; then
        "$target_runner" "$work/zlib-bench-cgfried" >"$logs/cgfried/benchmark.out"
    else
        "$work/zlib-bench-cgfried" >"$logs/cgfried/benchmark.out"
    fi
    cmp "$logs/host-gcc/benchmark.out" "$logs/cgfried/benchmark.out" ||
        fail "CRC/adler benchmark output differs from host GCC"
    for binary in example example64; do
        # zlib's example writes TESTFILE (foo.gz) in its current directory.
        # Execute each binary inside its extracted campaign tree so validation
        # cannot leak upstream scratch files into the caller's worktree.
        (cd "$host_tree" && "./$binary") >"$logs/host-gcc/$binary.out"
        if [ -n "$target_runner" ]; then
            (cd "$tree" && "$target_runner" "./$binary") \
                >"$logs/cgfried/$binary.out"
        else
            (cd "$tree" && "./$binary") >"$logs/cgfried/$binary.out"
        fi
        cmp "$logs/host-gcc/$binary.out" "$logs/cgfried/$binary.out" ||
            fail "$binary output differs from host GCC"
    done
    if [ -n "$target_runner" ]; then
        "$timeit" -n 5 -w 1 -t 60 -o "$logs/cgfried/benchmark.raw" -- \
            "$target_runner" "$work/zlib-bench-cgfried" --quiet \
            >"$logs/cgfried/benchmark.metrics"
    else
        "$timeit" -n 5 -w 1 -t 60 -o "$logs/cgfried/benchmark.raw" -- \
            "$work/zlib-bench-cgfried" --quiet \
            >"$logs/cgfried/benchmark.metrics"
    fi
    "$timeit" -n 5 -w 1 -t 60 -o "$logs/host-gcc/benchmark.raw" -- \
        "$work/zlib-bench-host-gcc" --quiet \
        >"$logs/host-gcc/benchmark.metrics"
    {
        echo 'protocol=sprint-53-report-only-v1'
        echo 'benchmark=crc32+adler32,bytes=134217728'
        sed 's/^/cgfried./' "$logs/cgfried/benchmark.metrics"
        sed 's/^/host_gcc./' "$logs/host-gcc/benchmark.metrics"
    } >"$work/performance-report.txt"
}

validate_stage() {
    [ -f "$tree/libz.a" ] || fail "build stage has not completed"
    if [ "$target_is_native" -eq 0 ] && [ -z "$run_prefix" ]; then
        fail "cross-target validation requires CGF_CAMPAIGN_ZLIB_RUNNER_PREFIX"
    fi
    test_tree cgfried "$tree" "$run_prefix"
    test_tree host-gcc "$host_tree" ''
    native_differential
    for binary in example minigzip example64 minigzip64; do
        [ -x "$tree/$binary" ] || fail "test binary is missing: $binary"
        readelf -h "$tree/$binary" | grep -F "Machine:                           $expected_machine" >/dev/null ||
            fail "$binary has the wrong ELF machine for $target"
        if [ "$target" = x86_64-linux-musl ]; then
            readelf -l "$tree/$binary" | grep -F 'INTERP' >/dev/null &&
                fail "$binary is dynamically linked in the musl-static lane"
        fi
    done
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-gcc,opt=O2\n'
        printf 'baseline.test.upstream\tPASS\tcases=4\n'
        printf 'build\tPASS\tarchive=libz.a,objects=15\n'
        printf 'configure\tPASS\tmode=static\n'
        printf 'linkage\tPASS\tbinaries=4,library=static\n'
        printf 'parity.outputs\tPASS\texamples=2,benchmark=crc32+adler32\n'
        printf 'performance.report\tPASS\tprotocol=sprint-53-report-only-v1,artifact=performance-report.txt\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$ZLIB_SHA256"
        printf 'source.pin\tPASS\tversion=%s\n' "$ZLIB_REF"
        printf 'test.upstream\tPASS\tcases=4,opt=O2\n'
    } >"$work/results.txt"
    printf 'campaign-zlib: PASS target=%s results=%s artifacts=%s\n' \
        "$target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
