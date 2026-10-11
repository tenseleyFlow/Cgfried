#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

LIBXML2_VERSION=2.15.4
LIBXML2_COMMIT=9fac185ccc0b719eb647db902d5c13c3aaa96263
LIBXML2_SHA256=98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821
RUN_AND_DIFF_SHA256=f40cdbab613908006a554f586c96042663e85f762a07b702cc615339e80b20a3
CATALOG_ADD_DEL_SHA256=1339088a9235f15d9feb52453d6dcd18f726336c910d4ec8d80bae5824d680c7
OBJECT_CLOSURE_SHA256=36546d80975d6f2f0f9751554f61d2c2a29fb3d58c8d8db79eeb008ac0abb43d
LINK_CLOSURE_SHA256=c67634f0d7f49d4b03044429cf8f4acf7965b342b4650a8688da11073ab2ef43
ARCHIVE_CLOSURE_SHA256=49b3a65a2ff56fcb4bcc068056c5edde042b6a7698011317befc3d77eed9b851

fail() {
    echo "campaign-libxml2: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_LIBXML2_ARCHIVE:-$root/build/campaigns/dl/libxml2-$LIBXML2_VERSION.tar.xz}
work=${CGF_CAMPAIGN_LIBXML2_WORK:-$root/build/campaigns/libxml2}
cgf=${CGF_CAMPAIGN_LIBXML2_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_LIBXML2_HOSTCC:-cc}
sole=${CGF_CAMPAIGN_LIBXML2_SOLE_C:-$root/scripts/campaigns/sole-c.sh}
cc_wrapper=${CGF_CAMPAIGN_LIBXML2_CC_WRAPPER:-$root/scripts/campaigns/libxml2-cc.sh}
fixture_dir=$root/ci/campaigns/fixtures/libxml2-$LIBXML2_VERSION
jobs=${CGF_CAMPAIGN_JOBS:-}
cflags='-std=gnu17'

[ -x "$cgf" ] || fail "Cgfried compiler is missing or not executable: $cgf"
[ -x "$sole" ] || fail "sole-C wrapper is missing or not executable: $sole"
[ -x "$cc_wrapper" ] || fail "compiler adapter is missing or not executable: $cc_wrapper"
[ -f "$fixture_dir/run_and_diff.cmake" ] || fail "run-and-diff supplement is missing"
[ -f "$fixture_dir/test_xmlcatalog_add_del.cmake" ] ||
    fail "xmlcatalog supplement is missing"
run_diff_got=$(sha256sum "$fixture_dir/run_and_diff.cmake" | awk '{print $1}')
[ "$run_diff_got" = "$RUN_AND_DIFF_SHA256" ] ||
    fail "run-and-diff supplement checksum mismatch: $run_diff_got"
catalog_got=$(sha256sum "$fixture_dir/test_xmlcatalog_add_del.cmake" | awk '{print $1}')
[ "$catalog_got" = "$CATALOG_ADD_DEL_SHA256" ] ||
    fail "xmlcatalog supplement checksum mismatch: $catalog_got"
[ -f "$archive" ] || fail "verified source archive is missing: $archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$LIBXML2_SHA256" ] ||
    fail "source checksum mismatch: expected $LIBXML2_SHA256, got $got"

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
        destructor_probe=elf-supported
        ;;
    arm64-macos)
        destructor_probe=cgfried-refused-host-supported
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
parity=$work/parity

export CGF_CAMPAIGN_LIBXML2_CGF=$cgf
export CGF_CAMPAIGN_LIBXML2_SOLE=$sole
export CGF_CAMPAIGN_LIBXML2_RECEIPTS=$receipts

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
    if ! tar -tf "$archive" | awk -v root_name="libxml2-$LIBXML2_VERSION" '
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
    if ! tar -tvf "$archive" | awk '
        substr($1, 1, 1) == "-" || substr($1, 1, 1) == "d" { next }
        { bad = 1 }
        END { exit bad }
    '; then
        fail "source archive contains a link or special-file member"
    fi
}

extract_tree() {
    destination=$1
    mkdir -p "$destination"
    tar --no-same-owner --no-same-permissions -xf "$archive" \
        -C "$destination" --strip-components=1
}

install_harness_supplement() {
    destination=$1
    for item in run_and_diff.cmake test_xmlcatalog_add_del.cmake; do
        [ ! -e "$destination/$item" ] ||
            fail "official archive unexpectedly contains $item"
        cp "$fixture_dir/$item" "$destination/$item"
        cmp "$fixture_dir/$item" "$destination/$item" ||
            fail "test-harness supplement changed during installation: $item"
    done
}

configure_tree() {
    label=$1
    source=$2
    destination=$3
    compiler=$4
    mode=$5
    mkdir -p "$destination" "$logs/$label"
    result_code=0
    if [ "$mode" = cgfried ]; then
        CGF_CAMPAIGN_LIBXML2_CC_MODE=configure \
            cmake -S "$source" -B "$destination" -G 'Unix Makefiles' \
                -DCMAKE_C_COMPILER="$compiler" \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_C_FLAGS="$cflags" \
                -DCMAKE_C_FLAGS_RELEASE=-O2 \
                -DBUILD_SHARED_LIBS=OFF \
                -DLIBXML2_WITH_C14N=ON \
                -DLIBXML2_WITH_CATALOG=ON \
                -DLIBXML2_WITH_DEBUG=ON \
                -DLIBXML2_WITH_DOCS=OFF \
                -DLIBXML2_WITH_HTML=ON \
                -DLIBXML2_WITH_HTTP=OFF \
                -DLIBXML2_WITH_ICONV=OFF \
                -DLIBXML2_WITH_ICU=OFF \
                -DLIBXML2_WITH_ISO8859X=ON \
                -DLIBXML2_WITH_LEGACY=OFF \
                -DLIBXML2_WITH_MODULES=OFF \
                -DLIBXML2_WITH_OUTPUT=ON \
                -DLIBXML2_WITH_PATTERN=ON \
                -DLIBXML2_WITH_PROGRAMS=ON \
                -DLIBXML2_WITH_PUSH=ON \
                -DLIBXML2_WITH_PYTHON=OFF \
                -DLIBXML2_WITH_READER=ON \
                -DLIBXML2_WITH_READLINE=OFF \
                -DLIBXML2_WITH_REGEXPS=ON \
                -DLIBXML2_WITH_RELAXNG=ON \
                -DLIBXML2_WITH_SAX1=ON \
                -DLIBXML2_WITH_SCHEMAS=ON \
                -DLIBXML2_WITH_SCHEMATRON=ON \
                -DLIBXML2_WITH_TESTS=ON \
                -DLIBXML2_WITH_THREADS=ON \
                -DLIBXML2_WITH_THREAD_ALLOC=OFF \
                -DLIBXML2_WITH_TLS=ON \
                -DLIBXML2_WITH_VALID=ON \
                -DLIBXML2_WITH_WRITER=ON \
                -DLIBXML2_WITH_XINCLUDE=ON \
                -DLIBXML2_WITH_XPATH=ON \
                -DLIBXML2_WITH_XPTR=ON \
                -DLIBXML2_WITH_ZLIB=OFF \
                >"$logs/$label/configure.log" 2>&1 || result_code=$?
    else
        cmake -S "$source" -B "$destination" -G 'Unix Makefiles' \
            -DCMAKE_C_COMPILER="$compiler" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_FLAGS="$cflags" \
            -DCMAKE_C_FLAGS_RELEASE=-O2 \
            -DBUILD_SHARED_LIBS=OFF \
            -DLIBXML2_WITH_C14N=ON \
            -DLIBXML2_WITH_CATALOG=ON \
            -DLIBXML2_WITH_DEBUG=ON \
            -DLIBXML2_WITH_DOCS=OFF \
            -DLIBXML2_WITH_HTML=ON \
            -DLIBXML2_WITH_HTTP=OFF \
            -DLIBXML2_WITH_ICONV=OFF \
            -DLIBXML2_WITH_ICU=OFF \
            -DLIBXML2_WITH_ISO8859X=ON \
            -DLIBXML2_WITH_LEGACY=OFF \
            -DLIBXML2_WITH_MODULES=OFF \
            -DLIBXML2_WITH_OUTPUT=ON \
            -DLIBXML2_WITH_PATTERN=ON \
            -DLIBXML2_WITH_PROGRAMS=ON \
            -DLIBXML2_WITH_PUSH=ON \
            -DLIBXML2_WITH_PYTHON=OFF \
            -DLIBXML2_WITH_READER=ON \
            -DLIBXML2_WITH_READLINE=OFF \
            -DLIBXML2_WITH_REGEXPS=ON \
            -DLIBXML2_WITH_RELAXNG=ON \
            -DLIBXML2_WITH_SAX1=ON \
            -DLIBXML2_WITH_SCHEMAS=ON \
            -DLIBXML2_WITH_SCHEMATRON=ON \
            -DLIBXML2_WITH_TESTS=ON \
            -DLIBXML2_WITH_THREADS=ON \
            -DLIBXML2_WITH_THREAD_ALLOC=OFF \
            -DLIBXML2_WITH_TLS=ON \
            -DLIBXML2_WITH_VALID=ON \
            -DLIBXML2_WITH_WRITER=ON \
            -DLIBXML2_WITH_XINCLUDE=ON \
            -DLIBXML2_WITH_XPATH=ON \
            -DLIBXML2_WITH_XPTR=ON \
            -DLIBXML2_WITH_ZLIB=OFF \
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
    cache=$destination/CMakeCache.txt
    for setting in \
        'BUILD_SHARED_LIBS:BOOL=OFF' \
        'LIBXML2_WITH_C14N:BOOL=ON' \
        'LIBXML2_WITH_CATALOG:BOOL=ON' \
        'LIBXML2_WITH_DEBUG:BOOL=ON' \
        'LIBXML2_WITH_DOCS:BOOL=OFF' \
        'LIBXML2_WITH_HTML:BOOL=ON' \
        'LIBXML2_WITH_HTTP:BOOL=OFF' \
        'LIBXML2_WITH_ICONV:BOOL=OFF' \
        'LIBXML2_WITH_ICU:BOOL=OFF' \
        'LIBXML2_WITH_ISO8859X:BOOL=ON' \
        'LIBXML2_WITH_LEGACY:BOOL=OFF' \
        'LIBXML2_WITH_MODULES:BOOL=OFF' \
        'LIBXML2_WITH_OUTPUT:BOOL=ON' \
        'LIBXML2_WITH_PATTERN:BOOL=ON' \
        'LIBXML2_WITH_PROGRAMS:BOOL=ON' \
        'LIBXML2_WITH_PUSH:BOOL=ON' \
        'LIBXML2_WITH_PYTHON:BOOL=OFF' \
        'LIBXML2_WITH_READER:BOOL=ON' \
        'LIBXML2_WITH_READLINE:BOOL=OFF' \
        'LIBXML2_WITH_REGEXPS:BOOL=ON' \
        'LIBXML2_WITH_RELAXNG:BOOL=ON' \
        'LIBXML2_WITH_SAX1:BOOL=ON' \
        'LIBXML2_WITH_SCHEMAS:BOOL=ON' \
        'LIBXML2_WITH_SCHEMATRON:BOOL=ON' \
        'LIBXML2_WITH_TESTS:BOOL=ON' \
        'LIBXML2_WITH_THREADS:BOOL=ON' \
        'LIBXML2_WITH_THREAD_ALLOC:BOOL=OFF' \
        'LIBXML2_WITH_TLS:BOOL=ON' \
        'LIBXML2_WITH_VALID:BOOL=ON' \
        'LIBXML2_WITH_WRITER:BOOL=ON' \
        'LIBXML2_WITH_XINCLUDE:BOOL=ON' \
        'LIBXML2_WITH_XPATH:BOOL=ON' \
        'LIBXML2_WITH_XPTR:BOOL=ON' \
        'LIBXML2_WITH_ZLIB:BOOL=OFF'; do
        grep -Fqx "$setting" "$cache" ||
            fail "$label configuration changed: $setting"
    done
    grep -Fqx "CMAKE_C_FLAGS:STRING=$cflags" "$cache" ||
        fail "$label compiler flags changed"
    grep -Fqx 'CMAKE_C_FLAGS_RELEASE:STRING=-O2' "$cache" ||
        fail "$label release optimization changed"
}

validate_destructor_probe() {
    cgf_cache=$build/CMakeCache.txt
    host_cache=$host_build/CMakeCache.txt
    case $compiler_target in
        x86_64-linux-gnu | arm64-linux)
            grep -Fqx 'HAVE_FUNC_ATTRIBUTE_DESTRUCTOR:INTERNAL=1' "$cgf_cache" ||
                fail "Cgfried lost ELF destructor-attribute support"
            grep -Fqx 'HAVE_FUNC_ATTRIBUTE_DESTRUCTOR:INTERNAL=1' "$host_cache" ||
                fail "host compiler lacks expected ELF destructor-attribute support"
            ;;
        arm64-macos)
            grep -Fqx 'HAVE_FUNC_ATTRIBUTE_DESTRUCTOR:INTERNAL=' "$cgf_cache" ||
                fail "Cgfried's deliberate Mach-O destructor refusal changed"
            grep -Fqx 'HAVE_FUNC_ATTRIBUTE_DESTRUCTOR:INTERNAL=1' "$host_cache" ||
                fail "host compiler lacks expected Mach-O destructor support"
            ;;
    esac
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs"
    audit_archive
    extract_tree "$tree"
    extract_tree "$host_tree"
    install_harness_supplement "$tree"
    install_harness_supplement "$host_tree"
    [ "$(tr -d '\r\n' <"$tree/VERSION")" = "$LIBXML2_VERSION" ] ||
        fail "source tree has the wrong version"
    {
        printf 'version=%s\n' "$LIBXML2_VERSION"
        printf 'commit=%s\n' "$LIBXML2_COMMIT"
        printf 'sha256=%s\n' "$LIBXML2_SHA256"
        printf 'target=%s\n' "$compiler_target"
        printf 'compiler=%s\n' "$cgf"
        printf 'compiler_sha256=%s\n' "$(sha256sum "$cgf" | awk '{print $1}')"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'build_type=Release\n'
        printf 'optimization=O2\n'
        printf 'linkage=static-only\n'
        printf 'external_libraries=off\n'
        printf 'run_and_diff_sha256=%s\n' "$RUN_AND_DIFF_SHA256"
        printf 'test_xmlcatalog_add_del_sha256=%s\n' "$CATALOG_ADD_DEL_SHA256"
        printf 'threads=on\n'
        printf 'tls=on\n'
        printf 'schematron=on\n'
        printf 'destructor_probe=%s\n' "$destructor_probe"
    } >"$work/provenance.txt"
    configure_tree cgfried "$tree" "$build" "$cc_wrapper" cgfried
    configure_tree host "$host_tree" "$host_build" "$hostcc" host
    validate_configuration cgfried "$build"
    validate_configuration host "$host_build"
    validate_destructor_probe
}

build_tree() {
    label=$1
    destination=$2
    mode=$3
    result_code=0
    if [ "$mode" = cgfried ]; then
        CGF_CAMPAIGN_LIBXML2_CC_MODE=build \
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
    [ -f "$destination/libxml2.a" ] || fail "$label omitted libxml2.a"
    [ "$(archive_members "$destination/libxml2.a")" -eq 37 ] ||
        fail "$label libxml2.a does not contain exactly 37 members"
    for program in runxmlconf runsuite runtest testModule testapi testcatalog \
        testchar testdict testlimits testparser testrecurse xmlcatalog xmllint; do
        [ -x "$destination/$program" ] || fail "$label omitted executable $program"
    done
    [ ! -e "$destination/libxml2.so" ] || fail "$label produced a shared library"
    [ ! -e "$destination/libxml2.dylib" ] || fail "$label produced a shared library"
}

build_stage() {
    [ -f "$build/CMakeCache.txt" ] || fail "configure stage has not completed"
    [ ! -e "$receipts" ] || fail "receipt root already exists: $receipts"
    "$sole" init "$receipts" "$cgf"
    build_tree cgfried "$build" cgfried
    build_tree host "$host_build" host
    verify_products cgfried "$build"
    verify_products host "$host_build"
    [ "$(find "$build" -type f -name '*.o' | wc -l | tr -d ' ')" -eq 52 ] ||
        fail "Cgfried retained object inventory is not exactly 52"
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
    grep -E '^100% tests passed(, 0 tests failed)? out of 22$' \
        "$logs/$label/test.log" >/dev/null ||
        fail "$label test suite did not pass exactly 22 tests"
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
    [ "$(wc -l <"$object_inventory" | tr -d ' ')" -eq 52 ] ||
        fail "sole-C object closure is not exactly 52 translations"
    [ "$(sort -u "$object_inventory" | wc -l | tr -d ' ')" -eq 52 ] ||
        fail "sole-C object closure contains a duplicate identity"
    [ "$(wc -l <"$link_inventory" | tr -d ' ')" -eq 28 ] ||
        fail "sole-C link-input closure is not exactly 28 entries"
    [ "$(wc -l <"$archive_inventory" | tr -d ' ')" -eq 37 ] ||
        fail "sole-C archive closure is not exactly 37 members"
    products=$(awk -F '\t' '!seen[$1]++ { print $1 }' "$link_inventory")
    expected_products='runsuite
runtest
runxmlconf
testModule
testapi
testcatalog
testchar
testdict
testlimits
testparser
testrecurse
xmlcatalog
xmllint'
    [ "$products" = "$expected_products" ] || fail "linked-product inventory changed"
    if [ "$object_got" != "$OBJECT_CLOSURE_SHA256" ] ||
       [ "$link_got" != "$LINK_CLOSURE_SHA256" ] ||
       [ "$archive_got" != "$ARCHIVE_CLOSURE_SHA256" ]; then
        printf 'campaign-libxml2: measured object closure %s (%s entries)\n' \
            "$object_got" "$(wc -l <"$object_inventory" | tr -d ' ')" >&2
        printf 'campaign-libxml2: measured link closure %s (%s entries)\n' \
            "$link_got" "$(wc -l <"$link_inventory" | tr -d ' ')" >&2
        printf 'campaign-libxml2: measured archive closure %s (%s entries)\n' \
            "$archive_got" "$(wc -l <"$archive_inventory" | tr -d ' ')" >&2
        fail "sole-C closure digests changed"
    fi
}

write_manifest() {
    write_closure_inventories
    : >"$archive_inventory"
    append_archive_inventory libxml2.a CMakeFiles/LibXml2.dir
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
    printf '%s\n' \
        '<?xml version="1.0" encoding="UTF-8"?>' \
        '<catalog><item id="b"><name>Beta &amp; Co</name></item><item id="a"><name>Alpha</name></item></catalog>' \
        >"$parity/input.xml"
    XMLLINT_INDENT='  ' "$build/xmllint" --format "$parity/input.xml" \
        >"$parity/cgfried.format.xml"
    XMLLINT_INDENT='  ' "$host_build/xmllint" --format "$parity/input.xml" \
        >"$parity/host.format.xml"
    cmp "$parity/host.format.xml" "$parity/cgfried.format.xml" ||
        fail "host/Cgfried formatted XML differs"
    "$build/xmllint" --c14n "$parity/input.xml" >"$parity/cgfried.c14n.xml"
    "$host_build/xmllint" --c14n "$parity/input.xml" >"$parity/host.c14n.xml"
    cmp "$parity/host.c14n.xml" "$parity/cgfried.c14n.xml" ||
        fail "host/Cgfried canonical XML differs"
    "$build/xmllint" --xpath 'count(/catalog/item)' "$parity/input.xml" \
        >"$parity/cgfried.xpath.txt"
    "$host_build/xmllint" --xpath 'count(/catalog/item)' "$parity/input.xml" \
        >"$parity/host.xpath.txt"
    cmp "$parity/host.xpath.txt" "$parity/cgfried.xpath.txt" ||
        fail "host/Cgfried XPath output differs"
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
        printf 'baseline.test.upstream\tPASS\tcases=22\n'
        printf 'build\tPASS\tlibraries=1,objects=52\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=52,archive-members=37,linked-products=13\n'
        printf 'configure\tPASS\texternal-libs=off,mode=static,schematron=on,threads=on,tls=on\n'
        printf 'linkage\tPASS\tbinaries=13,libraries=1,static=yes\n'
        printf 'parity.outputs\tPASS\tartifacts=3,tool=xmllint\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$LIBXML2_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' \
            "$LIBXML2_COMMIT" "$LIBXML2_VERSION"
        printf 'test.upstream\tPASS\tcases=22\n'
    } >"$work/results.txt"
    printf 'campaign-libxml2: PASS target=%s results=%s artifacts=%s\n' \
        "$compiler_target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
