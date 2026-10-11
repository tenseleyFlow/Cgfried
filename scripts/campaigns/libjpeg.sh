#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

LIBJPEG_VERSION=3.2.0
LIBJPEG_COMMIT=c85e6b905bf237038faa936dab160ebfc5da0344
LIBJPEG_SHA256=6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e
OBJECT_CLOSURE_SHA256=e376f58b8042df322d1b1f543869cdce50ff93c64272dbf179d4ce255684f433
LINK_CLOSURE_SHA256=1fe499e1282e3d575bdad0ad7aab630c586429273d80da7d6a9128128f72189f
ARCHIVE_CLOSURE_SHA256=d780c57620c690e2282c0034cee3c6a809fa95debfb099c50c0ba97a9b70eb9a

fail() {
    echo "campaign-libjpeg: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_LIBJPEG_ARCHIVE:-$root/build/campaigns/dl/libjpeg-turbo-$LIBJPEG_VERSION.tar.gz}
work=${CGF_CAMPAIGN_LIBJPEG_WORK:-$root/build/campaigns/libjpeg}
cgf=${CGF_CAMPAIGN_LIBJPEG_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_LIBJPEG_HOSTCC:-gcc}
sole=${CGF_CAMPAIGN_LIBJPEG_SOLE_C:-$root/scripts/campaigns/sole-c.sh}
cc_wrapper=${CGF_CAMPAIGN_LIBJPEG_CC_WRAPPER:-$root/scripts/campaigns/libjpeg-cc.sh}
jobs=${CGF_CAMPAIGN_JOBS:-}
cgf_cflags=${CGF_CAMPAIGN_LIBJPEG_CGF_CFLAGS:--DSPNG_DISABLE_OPT -ffp-contract=off -Wno-bundled-only-option}
host_cflags=${CGF_CAMPAIGN_LIBJPEG_HOST_CFLAGS:--DSPNG_DISABLE_OPT -ffp-contract=off}

[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
[ -x "$sole" ] || fail "sole-C wrapper is missing or not executable: $sole"
[ -x "$cc_wrapper" ] || fail "compiler adapter is missing or not executable: $cc_wrapper"
[ "$cgf_cflags" = '-DSPNG_DISABLE_OPT -ffp-contract=off -Wno-bundled-only-option' ] ||
    fail "libjpeg-turbo Cgfried flags changed: $cgf_cflags"
[ "$host_cflags" = '-DSPNG_DISABLE_OPT -ffp-contract=off' ] ||
    fail "libjpeg-turbo host flags changed: $host_cflags"
[ -f "$archive" ] || fail "verified source archive is missing: $archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$LIBJPEG_SHA256" ] ||
    fail "source checksum mismatch: expected $LIBJPEG_SHA256, got $got"

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

for tool in cmake ctest ar tar sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool is unavailable: $tool"
done
command -v "$hostcc" >/dev/null 2>&1 || fail "host compiler is unavailable: $hostcc"

compiler_target=$("$cgf" -dumpmachine) || fail "cannot query Cgfried's target"
case $compiler_target in
    x86_64-linux-gnu | arm64-linux)
        system_libraries=-lm
        system_library_label=libm
        ;;
    arm64-macos)
        system_libraries=
        system_library_label=libSystem
        ;;
    *) fail "unsupported native campaign target: $compiler_target" ;;
esac

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

export CGF_CAMPAIGN_LIBJPEG_CGF=$cgf
export CGF_CAMPAIGN_LIBJPEG_SOLE=$sole
export CGF_CAMPAIGN_LIBJPEG_RECEIPTS=$receipts

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
    if ! tar -tzf "$archive" | awk -v root_name="libjpeg-turbo-$LIBJPEG_VERSION" '
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
    if ! tar -tvzf "$archive" | awk '
        substr($1, 1, 1) != "-" && substr($1, 1, 1) != "d" { bad = 1 }
        END { exit bad }
    '; then
        fail "source archive contains a link or special-file member"
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
        CGF_CAMPAIGN_LIBJPEG_CC_MODE=configure \
            cmake -S "$source" -B "$destination" -G 'Unix Makefiles' \
                -DCMAKE_C_COMPILER="$compiler" \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_C_FLAGS="$flags" \
                -DCMAKE_C_FLAGS_RELEASE=-O2 \
                -DCMAKE_C_STANDARD_LIBRARIES:STRING="$system_libraries" \
                -DBUILD=20260630 \
                -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
                -DREQUIRE_SIMD=OFF -DWITH_SIMD=OFF \
                -DWITH_ARITH_DEC=ON -DWITH_ARITH_ENC=ON \
                -DWITH_JPEG7=OFF -DWITH_JPEG8=OFF \
                -DWITH_TURBOJPEG=ON -DWITH_TOOLS=ON -DWITH_TESTS=ON \
                -DWITH_FUZZ=OFF -DWITH_PROFILE=OFF \
                -DFLOATTEST8=no-fp-contract \
                -DFLOATTEST12=no-fp-contract \
                >"$logs/$label/configure.log" 2>&1 || result_code=$?
    else
        cmake -S "$source" -B "$destination" -G 'Unix Makefiles' \
            -DCMAKE_C_COMPILER="$compiler" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_FLAGS="$flags" \
            -DCMAKE_C_FLAGS_RELEASE=-O2 \
            -DCMAKE_C_STANDARD_LIBRARIES:STRING="$system_libraries" \
            -DBUILD=20260630 \
            -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
            -DREQUIRE_SIMD=OFF -DWITH_SIMD=OFF \
            -DWITH_ARITH_DEC=ON -DWITH_ARITH_ENC=ON \
            -DWITH_JPEG7=OFF -DWITH_JPEG8=OFF \
            -DWITH_TURBOJPEG=ON -DWITH_TOOLS=ON -DWITH_TESTS=ON \
            -DWITH_FUZZ=OFF -DWITH_PROFILE=OFF \
            -DFLOATTEST8=no-fp-contract \
            -DFLOATTEST12=no-fp-contract \
            >"$logs/$label/configure.log" 2>&1 || result_code=$?
    fi
    if [ "$result_code" -ne 0 ]; then
        tail -240 "$logs/$label/configure.log" >&2
        fail "$label configure failed"
    fi
    [ -f "$destination/CMakeCache.txt" ] ||
        fail "$label configure produced no CMake cache"
}

remove_configure_object() {
    destination=$1
    candidates=$work/configure-objects.txt
    find "$destination/CMakeFiles" -type f \
        -path '*/CompilerIdC/CMakeCCompilerId.o' -print >"$candidates"
    [ "$(wc -l <"$candidates" | tr -d ' ')" -eq 1 ] ||
        fail "Cgfried configure object inventory changed"
    object=$(sed -n '1p' "$candidates")
    rm -f "$object"
}

validate_configuration() {
    label=$1
    destination=$2
    expected_flags=$3
    expected_tls=$4
    cache=$destination/CMakeCache.txt
    for setting in \
        'ENABLE_SHARED:BOOL=OFF' \
        'ENABLE_STATIC:BOOL=ON' \
        'WITH_SIMD:BOOL=OFF' \
        'WITH_TURBOJPEG:BOOL=ON' \
        'WITH_TOOLS:BOOL=ON' \
        'WITH_TESTS:BOOL=ON' \
        'WITH_FUZZ:BOOL=OFF' \
        'WITH_PROFILE:BOOL=OFF' \
        'FLOATTEST8:STRING=no-fp-contract' \
        'FLOATTEST12:STRING=no-fp-contract'; do
        grep -Fqx "$setting" "$cache" ||
            fail "$label configuration changed: $setting"
    done
    grep -Fqx "CMAKE_C_FLAGS:STRING=$expected_flags" "$cache" ||
        fail "$label compiler flags changed"
    grep -Fqx 'CMAKE_C_FLAGS_RELEASE:STRING=-O2' "$cache" ||
        fail "$label release optimization changed"
    grep -Fqx "CMAKE_C_STANDARD_LIBRARIES:STRING=$system_libraries" "$cache" ||
        fail "$label standard system libraries changed"
    case $expected_tls in
        yes)
            grep -Fqx 'HAVE_THREAD_LOCAL:INTERNAL=1' "$cache" ||
                fail "$label did not enable required thread-local storage"
            ;;
        no)
            grep -Fqx 'HAVE_THREAD_LOCAL:INTERNAL=' "$cache" ||
                fail "$label unexpectedly changed the published Mach-O TLS boundary"
            ;;
        *) fail "invalid TLS expectation: $expected_tls" ;;
    esac
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs"
    audit_archive
    extract_tree "$tree"
    extract_tree "$host_tree"
    grep -Fqx 'set(VERSION 3.2.0)' "$tree/CMakeLists.txt" ||
        fail "source tree does not identify libjpeg-turbo $LIBJPEG_VERSION"
    grep -F '#define SPNG_DISABLE_OPT' "$tree/src/spng/spng.c" >/dev/null ||
        fail "vendored libspng omitted its supported optimization-disable boundary"
    {
        printf 'version=%s\n' "$LIBJPEG_VERSION"
        printf 'commit=%s\n' "$LIBJPEG_COMMIT"
        printf 'sha256=%s\n' "$LIBJPEG_SHA256"
        printf 'target=%s\n' "$compiler_target"
        printf 'compiler=%s\n' "$cgf"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'build_type=Release\n'
        printf 'optimization=O2\n'
        printf 'linkage=static-only\n'
        printf 'system_libraries=%s\n' "$system_library_label"
        printf 'simd=off\n'
        printf 'vendored_spng_optimization=off\n'
        printf 'float_contract=off\n'
        printf 'float_checks=no-fp-contract\n'
    } >"$work/provenance.txt"
    configure_tree cgfried "$tree" "$build" "$cc_wrapper" "$cgf_cflags" cgfried
    configure_tree host "$host_tree" "$host_build" "$hostcc" "$host_cflags" host
    remove_configure_object "$build"
    case $compiler_target in
        arm64-macos) cgf_tls=no ;;
        *) cgf_tls=yes ;;
    esac
    validate_configuration cgfried "$build" "$cgf_cflags" "$cgf_tls"
    validate_configuration host "$host_build" "$host_cflags" yes
}

build_tree() {
    label=$1
    destination=$2
    mode=$3
    result_code=0
    if [ "$mode" = cgfried ]; then
        CGF_CAMPAIGN_LIBJPEG_CC_MODE=build \
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
    [ -f "$destination/libjpeg.a" ] || fail "$label omitted libjpeg.a"
    [ -f "$destination/libturbojpeg.a" ] || fail "$label omitted libturbojpeg.a"
    [ "$(archive_members "$destination/libjpeg.a")" -eq 101 ] ||
        fail "$label libjpeg.a does not contain exactly 101 members"
    [ "$(archive_members "$destination/libturbojpeg.a")" -eq 129 ] ||
        fail "$label libturbojpeg.a does not contain exactly 129 members"
    for program in bmpsizetest-static cjpeg-static djpeg-static example-static \
        jpegtran-static rdjpgcom strtest test/md5cmp test/md5sum tjbench-static \
        tjunittest-static wrjpgcom; do
        [ -x "$destination/$program" ] || fail "$label omitted executable $program"
    done
    [ ! -e "$destination/libjpeg.so" ] || fail "$label produced a shared library"
    [ ! -e "$destination/libjpeg.dylib" ] || fail "$label produced a shared library"
    [ ! -e "$destination/libturbojpeg.so" ] || fail "$label produced a shared library"
    [ ! -e "$destination/libturbojpeg.dylib" ] || fail "$label produced a shared library"
}

build_stage() {
    [ -f "$build/CMakeCache.txt" ] || fail "configure stage has not completed"
    [ ! -e "$receipts" ] || fail "receipt root already exists: $receipts"
    "$sole" init "$receipts" "$cgf"
    build_tree cgfried "$build" cgfried
    build_tree host "$host_build" host
    verify_products cgfried "$build"
    verify_products host "$host_build"
    [ "$(find "$build" -type f -name '*.o' | wc -l | tr -d ' ')" -eq 297 ] ||
        fail "Cgfried retained object inventory is not exactly 297"
}

test_tree() {
    label=$1
    destination=$2
    result_code=0
    ctest --test-dir "$destination" --output-on-failure -j "$jobs" \
        >"$logs/$label/test.log" 2>&1 || result_code=$?
    if [ "$result_code" -ne 0 ]; then
        tail -320 "$logs/$label/test.log" >&2
        fail "$label upstream tests failed"
    fi
    grep -E '^100% tests passed(, 0 tests failed)? out of 332$' \
        "$logs/$label/test.log" >/dev/null ||
        fail "$label test suite did not pass exactly 332 tests"
}

write_closure_inventories() {
    : >"$object_inventory"
    : >"$link_inventory"
    for receipt in "$receipts"/invocations/*/receipt.txt; do
        [ -f "$receipt" ] || fail "no compiler receipts were recorded"
        kind=$(read_one kind "$receipt") || fail "receipt has no unique kind: $receipt"
        output=$(read_one output "$receipt") || fail "receipt has no unique output: $receipt"
        case $output in "$build"/*) ;; *) fail "receipt output escaped build tree: $output" ;; esac
        output_relative=${output#"$build"/}
        case $kind in
            object)
                source=$(read_one source "$receipt") ||
                    fail "receipt has no unique source: $receipt"
                case $source in "$tree"/*) ;; *) fail "object source escaped pinned tree: $source" ;; esac
                printf '%s\t%s\n' "${source#"$tree"/}" "$output_relative" \
                    >>"$object_inventory"
                ;;
            link)
                awk -v product="$output_relative" -v prefix="$build/" '
                    index($0, "input=") == 1 {
                        input = substr($0, 7)
                        if (index(input, prefix) != 1) exit 2
                        print product "\t" substr(input, length(prefix) + 1)
                    }
                ' "$receipt" >>"$link_inventory" ||
                    fail "link receipt names input outside build tree: $receipt"
                ;;
            *) fail "unexpected retained compiler invocation kind: $kind" ;;
        esac
    done
    sort -o "$object_inventory" "$object_inventory"
    sort -o "$link_inventory" "$link_inventory"
    [ "$(wc -l <"$object_inventory" | tr -d ' ')" -eq 297 ] ||
        fail "sole-C object closure is not exactly 297 translations"
    [ "$(sort -u "$object_inventory" | wc -l | tr -d ' ')" -eq 297 ] ||
        fail "sole-C object closure contains a duplicate identity"
    got=$(sha256sum "$object_inventory" | awk '{print $1}')
    [ "$got" = "$OBJECT_CLOSURE_SHA256" ] ||
        fail "sole-C object closure changed: expected $OBJECT_CLOSURE_SHA256, got $got"
    [ "$(wc -l <"$link_inventory" | tr -d ' ')" -eq 104 ] ||
        fail "sole-C link-input closure is not exactly 104 entries"
    got=$(sha256sum "$link_inventory" | awk '{print $1}')
    [ "$got" = "$LINK_CLOSURE_SHA256" ] ||
        fail "sole-C link closure changed: expected $LINK_CLOSURE_SHA256, got $got"
    products=$(awk -F '\t' '!seen[$1]++ { print $1 }' "$link_inventory")
    expected_products='bmpsizetest-static
cjpeg-static
djpeg-static
example-static
jpegtran-static
rdjpgcom
strtest
test/md5cmp
test/md5sum
tjbench-static
tjunittest-static
wrjpgcom'
    [ "$products" = "$expected_products" ] || fail "linked-product inventory changed"
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
        if [ -z "$object" ]; then
            object=$(find "$build" -type f -name "$member" -print)
        fi
        count=$(printf '%s\n' "$object" | awk 'NF { n++ } END { print n + 0 }')
        [ "$count" -eq 1 ] ||
            fail "cannot uniquely map $archive_relative($member) to an object"
        printf '%s\t%s\t%s\n' "$archive_relative" "$member" \
            "${object#"$build"/}" >>"$archive_inventory"
    done <"$member_list"
}

write_manifest() {
    write_closure_inventories
    : >"$archive_inventory"
    append_archive_inventory libjpeg.a CMakeFiles/jpeg-static.dir
    append_archive_inventory libturbojpeg.a CMakeFiles/turbojpeg-static.dir
    [ "$(wc -l <"$archive_inventory" | tr -d ' ')" -eq 230 ] ||
        fail "sole-C archive closure is not exactly 230 members"
    got=$(sha256sum "$archive_inventory" | awk '{print $1}')
    [ "$got" = "$ARCHIVE_CLOSURE_SHA256" ] ||
        fail "sole-C archive closure changed: expected $ARCHIVE_CLOSURE_SHA256, got $got"
    {
        echo '# cgf-sole-c-closure-v1'
        while IFS="$(printf '\t')" read -r source output; do
            printf 'object\t%s/%s\t%s/%s\n' "$tree" "$source" "$build" "$output"
        done <"$object_inventory"
        while IFS="$(printf '\t')" read -r archive_relative member object; do
            printf 'archive\t%s/%s\t%s\t%s/%s\n' \
                "$build" "$archive_relative" "$member" "$build" "$object"
        done <"$archive_inventory"
        while IFS="$(printf '\t')" read -r product input; do
            printf 'link-input\t%s/%s\t%s/%s\n' "$build" "$product" "$build" "$input"
        done <"$link_inventory"
    } >"$manifest"
}

validate_stage() {
    verify_products cgfried "$build"
    verify_products host "$host_build"
    test_tree cgfried "$build"
    test_tree host "$host_build"
    for artifact in testout-static_3x2_float_prog.jpg \
        testout-static_3x2_float.ppm testout12-static_3x2_float_prog.jpg \
        testout12-static_3x2_float.png; do
        [ -f "$build/$artifact" ] || fail "Cgfried test omitted parity artifact $artifact"
        [ -f "$host_build/$artifact" ] || fail "host test omitted parity artifact $artifact"
        cmp "$host_build/$artifact" "$build/$artifact" ||
            fail "host/Cgfried output differs: $artifact"
    done
    write_manifest
    "$sole" verify "$receipts" "$cgf" "$manifest" "$report"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-cc,opt=O2\n'
        printf 'baseline.test.upstream\tPASS\tcases=332\n'
        printf 'build\tPASS\tlibraries=2,objects=297\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=297,archive-members=230,linked-products=12\n'
        printf 'configure\tPASS\tfloat-checks=no-fp-contract,mode=static,simd=off,spng-opt=off\n'
        printf 'linkage\tPASS\tbinaries=12,libraries=2,static=yes\n'
        printf 'parity.outputs\tPASS\tartifacts=4,float-idct=yes\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$LIBJPEG_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' \
            "$LIBJPEG_COMMIT" "$LIBJPEG_VERSION"
        printf 'test.upstream\tPASS\tcases=332,opt=O2\n'
    } >"$work/results.txt"
    printf 'campaign-libjpeg: PASS target=%s results=%s artifacts=%s\n' \
        "$compiler_target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
