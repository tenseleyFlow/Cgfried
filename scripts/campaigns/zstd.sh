#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ZSTD_VERSION=1.5.7
ZSTD_COMMIT=f8745da6ff1ad1e7bab384bd1f9d742439278e99
ZSTD_SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
OBJECT_CLOSURE_SHA256=ddba231965267408d1166348687f6f920a55270e95e245c656c29c33ace736fe
LINK_CLOSURE_SHA256=ae3a53e066772312b456c6236759289771aad19a0d638570e6af8489391245b9
ARCHIVE_CLOSURE_SHA256=1618bc40ef98e87063e1afc01622a44decf7d5f7e492da236648a14740b77880

fail() {
    echo "campaign-zstd: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_ZSTD_ARCHIVE:-$root/build/campaigns/dl/zstd-$ZSTD_VERSION.tar.gz}
work=${CGF_CAMPAIGN_ZSTD_WORK:-$root/build/campaigns/zstd}
cgf=${CGF_CAMPAIGN_ZSTD_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_ZSTD_HOSTCC:-gcc}
sole=${CGF_CAMPAIGN_ZSTD_SOLE_C:-$root/scripts/campaigns/sole-c.sh}
cc_wrapper=${CGF_CAMPAIGN_ZSTD_CC_WRAPPER:-$root/scripts/campaigns/zstd-cc.sh}
jobs=${CGF_CAMPAIGN_JOBS:-}
portable_flags='-std=gnu17 -DZSTD_DISABLE_ASM'

[ -x "$cgf" ] || fail "Cgfried compiler is missing or not executable: $cgf"
[ -x "$sole" ] || fail "sole-C wrapper is missing or not executable: $sole"
[ -x "$cc_wrapper" ] || fail "compiler adapter is missing or not executable: $cc_wrapper"
[ -f "$archive" ] || fail "verified source archive is missing: $archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$ZSTD_SHA256" ] ||
    fail "source checksum mismatch: expected $ZSTD_SHA256, got $got"

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

for tool in cmake ctest ar tar sha256sum cmp; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool is unavailable: $tool"
done
command -v "$hostcc" >/dev/null 2>&1 || fail "host compiler is unavailable: $hostcc"

compiler_target=$("$cgf" -dumpmachine) || fail "cannot query Cgfried's target"
case $compiler_target in
    x86_64-linux-gnu | arm64-linux)
        cgf_cflags=$portable_flags
        sdk_selector=not-needed
        ;;
    arm64-macos)
        # TargetConditionals.h documents TARGET_CPU_* as the fallback for a
        # compiler that does not implement Clang's __is_target_* builtins.
        cgf_cflags="$portable_flags -DTARGET_CPU_ARM64=1"
        sdk_selector=TARGET_CPU_ARM64
        ;;
    *) fail "unsupported native campaign target: $compiler_target" ;;
esac
host_cflags=$portable_flags

as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "native assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "native linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

tree=$work/cgfried-src
host_tree=$work/host-src
build=$work/cgfried-build
host_build=$work/host-build
logs=$work/logs
receipts=$work/sole-c
manifest=$work/sole-c-closure.tsv
report=$work/sole-c-report.txt
object_inventory=$work/object-closure.tsv
link_inventory=$work/link-closure.tsv
archive_inventory=$work/archive-closure.tsv
parity=$work/parity

export CGF_CAMPAIGN_ZSTD_CGF=$cgf
export CGF_CAMPAIGN_ZSTD_SOLE=$sole
export CGF_CAMPAIGN_ZSTD_RECEIPTS=$receipts

read_one() {
    key=$1
    receipt=$2
    awk -v key="$key" '
        index($0, key "=") == 1 {
            count++
            value = substr($0, length(key) + 2)
        }
        END {
            if (count != 1) exit 1
            print value
        }
    ' "$receipt"
}

archive_members() {
    ar t "$1" | awk '$0 !~ /^__.SYMDEF( SORTED)?$/ { count++ } END { print count + 0 }'
}

audit_archive() {
    if ! tar -tzf "$archive" | awk -v root_name="zstd-$ZSTD_VERSION" '
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
        fail "source archive contains an unsafe or unexpected path"
    fi
    if ! tar -tvzf "$archive" | awk -v root_name="zstd-$ZSTD_VERSION" '
        substr($1, 1, 1) == "-" || substr($1, 1, 1) == "d" { next }
        substr($1, 1, 1) == "l" {
            if ($(NF - 1) != "->" || $NF != "zstd" ||
                ($(NF - 2) != root_name "/tests/cli-tests/bin/unzstd" &&
                 $(NF - 2) != root_name "/tests/cli-tests/bin/zstdcat"))
                bad = 1
            next
        }
        { bad = 1 }
        END { exit bad }
    '; then
        fail "source archive contains an unexpected link or special-file member"
    fi
}

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
    flags=$5
    mode=$6
    mkdir -p "$destination" "$logs/$label"
    result_code=0
    if [ "$mode" = cgfried ]; then
        CGF_CAMPAIGN_ZSTD_CC_MODE=configure \
            cmake -S "$source/build/cmake" -B "$destination" \
                -G 'Unix Makefiles' \
                -DCMAKE_C_COMPILER="$compiler" \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_C_FLAGS="$flags" \
                -DCMAKE_C_FLAGS_RELEASE=-O2 \
                -DBUILD_SHARED_LIBS=OFF \
                -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_SHARED=OFF \
                -DZSTD_BUILD_COMPRESSION=ON \
                -DZSTD_BUILD_DECOMPRESSION=ON \
                -DZSTD_BUILD_DICTBUILDER=ON \
                -DZSTD_BUILD_DEPRECATED=OFF \
                -DZSTD_BUILD_PROGRAMS=ON -DZSTD_BUILD_TESTS=ON \
                -DZSTD_BUILD_CONTRIB=OFF \
                -DZSTD_PROGRAMS_LINK_SHARED=OFF \
                -DZSTD_MULTITHREAD_SUPPORT=ON \
                -DZSTD_LEGACY_SUPPORT=ON -DZSTD_LEGACY_LEVEL=5 \
                -DZSTD_ZLIB_SUPPORT=OFF -DZSTD_LZMA_SUPPORT=OFF \
                -DZSTD_LZ4_SUPPORT=OFF \
                '-DZSTD_FULLBENCH_FLAGS:STRING=-i1;-P50' \
                '-DZSTD_FUZZER_FLAGS:STRING=-i10000;-s12345' \
                '-DZSTD_ZSTREAM_FLAGS:STRING=-i10000;-s67890' \
                -DZSTD_PLAYTESTS_FLAGS:STRING= \
                >"$logs/$label/configure.log" 2>&1 || result_code=$?
    else
        cmake -S "$source/build/cmake" -B "$destination" \
            -G 'Unix Makefiles' \
            -DCMAKE_C_COMPILER="$compiler" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_FLAGS="$flags" \
            -DCMAKE_C_FLAGS_RELEASE=-O2 \
            -DBUILD_SHARED_LIBS=OFF \
            -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_SHARED=OFF \
            -DZSTD_BUILD_COMPRESSION=ON \
            -DZSTD_BUILD_DECOMPRESSION=ON \
            -DZSTD_BUILD_DICTBUILDER=ON \
            -DZSTD_BUILD_DEPRECATED=OFF \
            -DZSTD_BUILD_PROGRAMS=ON -DZSTD_BUILD_TESTS=ON \
            -DZSTD_BUILD_CONTRIB=OFF \
            -DZSTD_PROGRAMS_LINK_SHARED=OFF \
            -DZSTD_MULTITHREAD_SUPPORT=ON \
            -DZSTD_LEGACY_SUPPORT=ON -DZSTD_LEGACY_LEVEL=5 \
            -DZSTD_ZLIB_SUPPORT=OFF -DZSTD_LZMA_SUPPORT=OFF \
            -DZSTD_LZ4_SUPPORT=OFF \
            '-DZSTD_FULLBENCH_FLAGS:STRING=-i1;-P50' \
            '-DZSTD_FUZZER_FLAGS:STRING=-i10000;-s12345' \
            '-DZSTD_ZSTREAM_FLAGS:STRING=-i10000;-s67890' \
            -DZSTD_PLAYTESTS_FLAGS:STRING= \
            >"$logs/$label/configure.log" 2>&1 || result_code=$?
    fi
    if [ "$result_code" -ne 0 ]; then
        tail -240 "$logs/$label/configure.log" >&2
        fail "$label configure failed"
    fi
    [ -f "$destination/CMakeCache.txt" ] ||
        fail "$label configure produced no CMake cache"
}

validate_configuration() {
    label=$1
    destination=$2
    expected_flags=$3
    cache=$destination/CMakeCache.txt
    for setting in \
        'BUILD_SHARED_LIBS:UNINITIALIZED=OFF' \
        'ZSTD_BUILD_COMPRESSION:BOOL=ON' \
        'ZSTD_BUILD_CONTRIB:BOOL=OFF' \
        'ZSTD_BUILD_DECOMPRESSION:BOOL=ON' \
        'ZSTD_BUILD_DEPRECATED:BOOL=OFF' \
        'ZSTD_BUILD_DICTBUILDER:BOOL=ON' \
        'ZSTD_BUILD_PROGRAMS:BOOL=ON' \
        'ZSTD_BUILD_SHARED:BOOL=OFF' \
        'ZSTD_BUILD_STATIC:BOOL=ON' \
        'ZSTD_BUILD_TESTS:BOOL=ON' \
        'ZSTD_LEGACY_LEVEL:STRING=5' \
        'ZSTD_LEGACY_SUPPORT:BOOL=ON' \
        'ZSTD_LZ4_SUPPORT:BOOL=OFF' \
        'ZSTD_LZMA_SUPPORT:BOOL=OFF' \
        'ZSTD_MULTITHREAD_SUPPORT:BOOL=ON' \
        'ZSTD_PROGRAMS_LINK_SHARED:BOOL=OFF' \
        'ZSTD_ZLIB_SUPPORT:BOOL=OFF' \
        'ZSTD_FULLBENCH_FLAGS:STRING=-i1;-P50' \
        'ZSTD_FUZZER_FLAGS:STRING=-i10000;-s12345' \
        'ZSTD_ZSTREAM_FLAGS:STRING=-i10000;-s67890'; do
        grep -Fqx "$setting" "$cache" ||
            fail "$label configuration changed: $setting"
    done
    grep -Fqx "CMAKE_C_FLAGS:STRING=$expected_flags" "$cache" ||
        fail "$label compiler flags changed"
    grep -Fqx 'CMAKE_C_FLAGS_RELEASE:STRING=-O2' "$cache" ||
        fail "$label release optimization changed"
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs"
    audit_archive
    extract_tree "$tree"
    extract_tree "$host_tree"
    grep -Fqx '#define ZSTD_VERSION_MAJOR    1' "$tree/lib/zstd.h" ||
        fail "source tree has the wrong zstd major version"
    grep -Fqx '#define ZSTD_VERSION_MINOR    5' "$tree/lib/zstd.h" ||
        fail "source tree has the wrong zstd minor version"
    grep -Fqx '#define ZSTD_VERSION_RELEASE  7' "$tree/lib/zstd.h" ||
        fail "source tree has the wrong zstd release version"
    {
        printf 'version=%s\n' "$ZSTD_VERSION"
        printf 'commit=%s\n' "$ZSTD_COMMIT"
        printf 'sha256=%s\n' "$ZSTD_SHA256"
        printf 'target=%s\n' "$compiler_target"
        printf 'compiler=%s\n' "$cgf"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'build_type=Release\n'
        printf 'optimization=O2\n'
        printf 'linkage=static-only\n'
        printf 'asm=off\n'
        printf 'legacy=5\n'
        printf 'multithread=on\n'
        printf 'sdk_target_selector=%s\n' "$sdk_selector"
        printf 'fuzzer_iterations=10000\n'
        printf 'fuzzer_seed=12345\n'
        printf 'zstream_iterations=10000\n'
        printf 'zstream_seed=67890\n'
    } >"$work/provenance.txt"
    configure_tree cgfried "$tree" "$build" "$cc_wrapper" "$cgf_cflags" cgfried
    configure_tree host "$host_tree" "$host_build" "$hostcc" "$host_cflags" host
    validate_configuration cgfried "$build" "$cgf_cflags"
    validate_configuration host "$host_build" "$host_cflags"
}

build_tree() {
    label=$1
    destination=$2
    mode=$3
    result_code=0
    if [ "$mode" = cgfried ]; then
        CGF_CAMPAIGN_ZSTD_CC_MODE=build \
            cmake --build "$destination" --parallel "$jobs" \
                >"$logs/$label/build.log" 2>&1 || result_code=$?
    else
        cmake --build "$destination" --parallel "$jobs" \
            >"$logs/$label/build.log" 2>&1 || result_code=$?
    fi
    if [ "$result_code" -ne 0 ]; then
        tail -280 "$logs/$label/build.log" >&2
        fail "$label build failed"
    fi
}

verify_products() {
    label=$1
    destination=$2
    [ -f "$destination/lib/libzstd.a" ] || fail "$label omitted libzstd.a"
    [ "$(archive_members "$destination/lib/libzstd.a")" -eq 37 ] ||
        fail "$label libzstd.a does not contain exactly 37 members"
    for program in programs/zstd programs/zstd-frugal tests/datagen \
        tests/fullbench tests/fuzzer tests/paramgrill tests/zstreamtest; do
        [ -x "$destination/$program" ] || fail "$label omitted executable $program"
    done
    [ ! -e "$destination/lib/libzstd.so" ] || fail "$label produced a shared library"
    [ ! -e "$destination/lib/libzstd.dylib" ] || fail "$label produced a shared library"
}

build_stage() {
    [ -f "$build/CMakeCache.txt" ] || fail "configure stage has not completed"
    [ ! -e "$receipts" ] || fail "receipt root already exists: $receipts"
    "$sole" init "$receipts" "$cgf"
    build_tree cgfried "$build" cgfried
    build_tree host "$host_build" host
    verify_products cgfried "$build"
    verify_products host "$host_build"
    [ "$(find "$build" -type f -name '*.o' | wc -l | tr -d ' ')" -eq 81 ] ||
        fail "Cgfried retained object inventory is not exactly 81"
}

test_tree() {
    label=$1
    destination=$2
    result_code=0
    ctest --test-dir "$destination" --output-on-failure -j "$jobs" \
        >"$logs/$label/test.log" 2>&1 || result_code=$?
    if [ "$result_code" -ne 0 ]; then
        tail -360 "$logs/$label/test.log" >&2
        fail "$label upstream tests failed"
    fi
    grep -E '^100% tests passed(, 0 tests failed)? out of 4$' \
        "$logs/$label/test.log" >/dev/null ||
        fail "$label test suite did not pass exactly four tests"
}

normalize_build_path() {
    path=$1
    case $path in "$build"/*) ;; *) fail "build artifact escaped build tree: $path" ;; esac
    relative=${path#"$build"/}
    encoded_tree=${tree#/}/
    printf '%s\n' "$relative" | awk -v needle="$encoded_tree" '
        {
            at = index($0, needle)
            if (at)
                print substr($0, 1, at - 1) "@src/" \
                    substr($0, at + length(needle))
            else
                print
        }
    '
}

materialize_build_path() {
    relative=$1
    case $relative in
        *'@src/'*)
            prefix=${relative%%@src/*}
            suffix=${relative#*@src/}
            printf '%s/%s%s/%s\n' "$build" "$prefix" "${tree#/}" "$suffix"
            ;;
        *) printf '%s/%s\n' "$build" "$relative" ;;
    esac
}

write_closure_inventories() {
    : >"$object_inventory"
    : >"$link_inventory"
    for receipt in "$receipts"/invocations/*/receipt.txt; do
        [ -f "$receipt" ] || fail "no compiler receipts were recorded"
        kind=$(read_one kind "$receipt") || fail "receipt has no unique kind: $receipt"
        output=$(read_one output "$receipt") || fail "receipt has no unique output: $receipt"
        output_relative=$(normalize_build_path "$output")
        case $kind in
            object)
                source=$(read_one source "$receipt") ||
                    fail "receipt has no unique source: $receipt"
                case $source in "$tree"/*) ;; *) fail "object source escaped pinned tree: $source" ;; esac
                printf '%s\t%s\n' "${source#"$tree"/}" "$output_relative" \
                    >>"$object_inventory"
                ;;
            link)
                awk 'index($0, "input=") == 1 { print substr($0, 7) }' \
                    "$receipt" | while IFS= read -r input; do
                        input_relative=$(normalize_build_path "$input")
                        printf '%s\t%s\n' "$output_relative" "$input_relative"
                    done >>"$link_inventory"
                ;;
            *) fail "unexpected retained compiler invocation kind: $kind" ;;
        esac
    done
    sort -o "$object_inventory" "$object_inventory"
    sort -o "$link_inventory" "$link_inventory"
}

append_archive_inventory() {
    archive_relative=$1
    object_directory_relative=$2
    member_list=$work/archive-members.txt
    ar t "$build/$archive_relative" >"$member_list"
    while IFS= read -r member; do
        case $member in __.SYMDEF*) continue ;; esac
        object=$(find "$build/$object_directory_relative" -type f \
            -name "$member" -print)
        count=$(printf '%s\n' "$object" | awk 'NF { n++ } END { print n + 0 }')
        [ "$count" -eq 1 ] ||
            fail "cannot uniquely map $archive_relative($member) to an object"
        object_relative=$(normalize_build_path "$object")
        printf '%s\t%s\t%s\n' "$archive_relative" "$member" \
            "$object_relative" >>"$archive_inventory"
    done <"$member_list"
}

verify_inventory_contract() {
    object_got=$(sha256sum "$object_inventory" | awk '{print $1}')
    link_got=$(sha256sum "$link_inventory" | awk '{print $1}')
    archive_got=$(sha256sum "$archive_inventory" | awk '{print $1}')
    [ "$(wc -l <"$object_inventory" | tr -d ' ')" -eq 81 ] ||
        fail "sole-C object closure is not exactly 81 translations"
    [ "$(sort -u "$object_inventory" | wc -l | tr -d ' ')" -eq 81 ] ||
        fail "sole-C object closure contains a duplicate identity"
    [ "$(wc -l <"$link_inventory" | tr -d ' ')" -eq 51 ] ||
        fail "sole-C link-input closure is not exactly 51 entries"
    [ "$(wc -l <"$archive_inventory" | tr -d ' ')" -eq 37 ] ||
        fail "sole-C archive closure is not exactly 37 members"
    products=$(awk -F '\t' '!seen[$1]++ { print $1 }' "$link_inventory")
    expected_products='programs/zstd
programs/zstd-frugal
tests/datagen
tests/fullbench
tests/fuzzer
tests/paramgrill
tests/zstreamtest'
    [ "$products" = "$expected_products" ] || fail "linked-product inventory changed"
    if [ "$object_got" != "$OBJECT_CLOSURE_SHA256" ] ||
       [ "$link_got" != "$LINK_CLOSURE_SHA256" ] ||
       [ "$archive_got" != "$ARCHIVE_CLOSURE_SHA256" ]; then
        printf 'campaign-zstd: measured object closure %s\n' "$object_got" >&2
        printf 'campaign-zstd: measured link closure %s\n' "$link_got" >&2
        printf 'campaign-zstd: measured archive closure %s\n' "$archive_got" >&2
        fail "sole-C closure digests changed"
    fi
}

write_manifest() {
    write_closure_inventories
    : >"$archive_inventory"
    append_archive_inventory lib/libzstd.a lib/CMakeFiles/libzstd_static.dir
    verify_inventory_contract
    {
        echo '# cgf-sole-c-closure-v1'
        while IFS="$(printf '\t')" read -r source output_relative; do
            output=$(materialize_build_path "$output_relative")
            printf 'object\t%s/%s\t%s\n' "$tree" "$source" "$output"
        done <"$object_inventory"
        while IFS="$(printf '\t')" read -r archive_relative member object_relative; do
            object=$(materialize_build_path "$object_relative")
            printf 'archive\t%s/%s\t%s\t%s\n' \
                "$build" "$archive_relative" "$member" "$object"
        done <"$archive_inventory"
        while IFS="$(printf '\t')" read -r product_relative input_relative; do
            product=$(materialize_build_path "$product_relative")
            input=$(materialize_build_path "$input_relative")
            printf 'link-input\t%s\t%s\n' "$product" "$input"
        done <"$link_inventory"
    } >"$manifest"
}

validate_parity() {
    mkdir -p "$parity"
    "$build/tests/datagen" -g1M -s314159 -P55 >"$parity/cgfried.input"
    "$host_build/tests/datagen" -g1M -s314159 -P55 >"$parity/host.input"
    cmp "$parity/host.input" "$parity/cgfried.input" ||
        fail "host/Cgfried datagen output differs"
    "$build/programs/zstd" -q -f --single-thread --no-asyncio -3 \
        "$parity/cgfried.input" -o "$parity/cgfried.zst"
    "$host_build/programs/zstd" -q -f --single-thread --no-asyncio -3 \
        "$parity/host.input" -o "$parity/host.zst"
    cmp "$parity/host.zst" "$parity/cgfried.zst" ||
        fail "host/Cgfried compressed output differs"
    "$build/programs/zstd" -q -f -d "$parity/host.zst" \
        -o "$parity/cgfried.restored"
    "$host_build/programs/zstd" -q -f -d "$parity/cgfried.zst" \
        -o "$parity/host.restored"
    cmp "$parity/cgfried.input" "$parity/cgfried.restored" ||
        fail "Cgfried-built CLI did not restore the oracle archive"
    cmp "$parity/host.input" "$parity/host.restored" ||
        fail "host-built CLI did not restore the Cgfried archive"
}

validate_stage() {
    verify_products cgfried "$build"
    verify_products host "$host_build"
    test_tree cgfried "$build"
    test_tree host "$host_build"
    validate_parity
    write_manifest
    "$sole" verify "$receipts" "$cgf" "$manifest" "$report"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-cc,opt=O2\n'
        printf 'baseline.test.upstream\tPASS\tcases=4,fuzzer=10000,zstream=10000\n'
        printf 'build\tPASS\tlibraries=1,objects=81\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=81,archive-members=37,linked-products=7\n'
        printf 'configure\tPASS\tasm=off,legacy=5,mode=static,multithread=on\n'
        printf 'linkage\tPASS\tbinaries=7,libraries=1,static=yes\n'
        printf 'parity.outputs\tPASS\tartifacts=3,cli=single-thread\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$ZSTD_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' \
            "$ZSTD_COMMIT" "$ZSTD_VERSION"
        printf 'test.upstream\tPASS\tcases=4,fuzzer=10000,zstream=10000\n'
    } >"$work/results.txt"
    printf 'campaign-zstd: PASS target=%s results=%s artifacts=%s\n' \
        "$compiler_target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
