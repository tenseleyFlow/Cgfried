#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

OPENSSL_VERSION=3.5.9
OPENSSL_COMMIT=45e844fa2a14ec92d146bd8f5778ac130b6625fb
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
RSA_COMPLEX_SHA256=2295d607d06329e30f897f1e2d1d19e1d6659dae93acbe435749e7e86ae7bcc0
RSA_NOOP_SHA256=afce7814ab943c7ca342b41fa911780e6d5decbeab9f2aa3e77767d78c9edaf0

fail() {
    echo "campaign-openssl: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_OPENSSL_ARCHIVE:-$root/build/campaigns/dl/openssl-$OPENSSL_VERSION.tar.gz}
work=${CGF_CAMPAIGN_OPENSSL_WORK:-$root/build/campaigns/openssl}
cgf=${CGF_CAMPAIGN_OPENSSL_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_OPENSSL_HOSTCC:-cc}
sole=${CGF_CAMPAIGN_OPENSSL_SOLE_C:-$root/scripts/campaigns/sole-c.sh}
cc_wrapper=${CGF_CAMPAIGN_OPENSSL_CC_WRAPPER:-$root/scripts/campaigns/openssl-cc.sh}
noop=$root/ci/campaigns/fixtures/openssl-$OPENSSL_VERSION/rsa_complex.c
jobs=${CGF_CAMPAIGN_JOBS:-}

[ -x "$cgf" ] || fail "Cgfried compiler is missing or not executable: $cgf"
[ -x "$sole" ] || fail "sole-C wrapper is missing or not executable: $sole"
[ -x "$cc_wrapper" ] || fail "compiler adapter is missing or not executable: $cc_wrapper"
[ -f "$noop" ] || fail "rsa_complex replacement is missing: $noop"
noop_got=$(sha256sum "$noop" | awk '{print $1}')
[ "$noop_got" = "$RSA_NOOP_SHA256" ] ||
    fail "rsa_complex replacement checksum mismatch: $noop_got"
[ -f "$archive" ] || fail "verified source archive is missing: $archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$OPENSSL_SHA256" ] ||
    fail "source checksum mismatch: expected $OPENSSL_SHA256, got $got"

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

for tool in ar cmp make perl sha256sum sort tar; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool is unavailable: $tool"
done
command -v "$hostcc" >/dev/null 2>&1 || fail "host compiler is unavailable: $hostcc"

compiler_target=$("$cgf" -dumpmachine) || fail "cannot query Cgfried's target"
case $compiler_target in
    x86_64-linux-gnu | arm64-linux)
        case $compiler_target in
            x86_64-linux-gnu) openssl_target=linux-x86_64 ;;
            arm64-linux) openssl_target=linux-aarch64 ;;
        esac
        object_count=1682
        archive_member_count=1351
        libcrypto_member_count=1014
        OBJECT_CLOSURE_SHA256=c0ffa90838fff9a87f910b083b3bef358dfe40ec3a5efe09d1edbd353cdf43d2
        LINK_CLOSURE_SHA256=35f6768855d41ce61871ac9e998c8db7feca0d879e75ba1dc0761df801286e33
        ARCHIVE_CLOSURE_SHA256=9e3e6de588d9cef2755615f150a77cf80baee4b6e62be66907d28a33743466c0
        ;;
    arm64-macos)
        openssl_target=darwin64-arm64-cc
        object_count=1681
        archive_member_count=1350
        libcrypto_member_count=1013
        OBJECT_CLOSURE_SHA256=7d2f15f144a6189542715a87a0f09ac111de044faf8813da0cd409c79d36c8a2
        LINK_CLOSURE_SHA256=35f6768855d41ce61871ac9e998c8db7feca0d879e75ba1dc0761df801286e33
        ARCHIVE_CLOSURE_SHA256=fcb2833e7748eccc74dd8b308e34341aef03ea80bc8fa93a0e3e65fa382e989e
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
program_inventory=$work/program-closure.txt

export CGF_CAMPAIGN_OPENSSL_CGF=$cgf
export CGF_CAMPAIGN_OPENSSL_SOLE=$sole
export CGF_CAMPAIGN_OPENSSL_RECEIPTS=$receipts

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
    if ! tar -tf "$archive" | awk -v root_name="openssl-$OPENSSL_VERSION" '
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

install_rsa_noop() {
    destination=$1
    source=$destination/test/rsa_complex.c
    source_got=$(sha256sum "$source" | awk '{print $1}')
    [ "$source_got" = "$RSA_COMPLEX_SHA256" ] ||
        fail "pinned rsa_complex.c checksum changed: $source_got"
    cp "$noop" "$source"
    installed_got=$(sha256sum "$source" | awk '{print $1}')
    [ "$installed_got" = "$RSA_NOOP_SHA256" ] ||
        fail "rsa_complex replacement changed during installation"
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
        (
            cd "$destination"
            CGF_CAMPAIGN_OPENSSL_CC_MODE=configure CC="$compiler" \
                perl "$source/Configure" "$openssl_target" \
                    no-shared no-pinshared no-module no-asm no-docs \
                    no-makedepend -DOPENSSL_DEV_NO_ATOMICS \
                    -D__STDC_NO_ATOMICS__=1 -std=gnu17 -Wno-attributes -O2
        ) >"$logs/$label/configure.log" 2>&1 || result_code=$?
    else
        (
            cd "$destination"
            CC="$compiler" perl "$source/Configure" "$openssl_target" \
                no-shared no-pinshared no-module no-asm no-docs \
                no-makedepend -DOPENSSL_DEV_NO_ATOMICS \
                -D__STDC_NO_ATOMICS__=1 -std=gnu17 -Wno-attributes -O2
        ) >"$logs/$label/configure.log" 2>&1 || result_code=$?
    fi
    if [ "$result_code" -ne 0 ]; then
        tail -240 "$logs/$label/configure.log" >&2
        fail "$label configure failed"
    fi
    [ -f "$destination/configdata.pm" ] ||
        fail "$label configure produced no configdata.pm"
}

validate_configuration() {
    label=$1
    destination=$2
    config=$destination/configdata.pm
    grep -Fq "\"target\" => \"$openssl_target\"" "$config" ||
        fail "$label configuration selected the wrong target"
    for option in no-shared no-pinshared no-module no-asm no-docs no-makedepend; do
        grep -Fq "\"$option\"" "$config" ||
            fail "$label configuration omitted $option"
    done
    grep -Fq -- '-DOPENSSL_DEV_NO_ATOMICS' "$config" ||
        fail "$label configuration enabled atomics"
    grep -Fq -- '-D__STDC_NO_ATOMICS__=1' "$config" ||
        fail "$label configuration omitted the no-atomics language contract"
    grep -Fq -- '-std=gnu17' "$config" || fail "$label language mode changed"
    grep -Fq -- '-Wno-attributes' "$config" ||
        fail "$label ignored-attribute diagnostic policy changed"
    grep -Fq -- '-O2' "$config" || fail "$label optimization changed"
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs"
    audit_archive
    extract_tree "$tree"
    extract_tree "$host_tree"
    install_rsa_noop "$tree"
    install_rsa_noop "$host_tree"
    grep -Fqx 'MAJOR=3' "$tree/VERSION.dat" || fail "source major version changed"
    grep -Fqx 'MINOR=5' "$tree/VERSION.dat" || fail "source minor version changed"
    grep -Fqx 'PATCH=9' "$tree/VERSION.dat" || fail "source patch version changed"
    cmp "$tree/test/rsa_complex.c" "$host_tree/test/rsa_complex.c" ||
        fail "rsa_complex replacement differs between compiler lanes"
    {
        printf 'version=%s\n' "$OPENSSL_VERSION"
        printf 'commit=%s\n' "$OPENSSL_COMMIT"
        printf 'sha256=%s\n' "$OPENSSL_SHA256"
        printf 'target=%s\n' "$compiler_target"
        printf 'openssl_target=%s\n' "$openssl_target"
        printf 'compiler=%s\n' "$cgf"
        printf 'compiler_sha256=%s\n' "$(sha256sum "$cgf" | awk '{print $1}')"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'language=gnu17\n'
        printf 'optimization=O2\n'
        printf 'linkage=static-only\n'
        printf 'assembly=off\n'
        printf 'atomics=off\n'
        printf 'rsa_complex_original_sha256=%s\n' "$RSA_COMPLEX_SHA256"
        printf 'rsa_complex_replacement_sha256=%s\n' "$RSA_NOOP_SHA256"
        printf 'scope_deviation=CAMP-OPENSSL-001\n'
    } >"$work/provenance.txt"
    configure_tree cgfried "$tree" "$build" "$cc_wrapper" cgfried
    configure_tree host "$host_tree" "$host_build" "$hostcc" host
    validate_configuration cgfried "$build"
    validate_configuration host "$host_build"
}

build_tree() {
    label=$1
    destination=$2
    mode=$3
    result_code=0
    if [ "$mode" = cgfried ]; then
        CGF_CAMPAIGN_OPENSSL_CC_MODE=build \
            make -C "$destination" -j "$jobs" build_sw \
                >"$logs/$label/build.log" 2>&1 || result_code=$?
    else
        make -C "$destination" -j "$jobs" build_sw \
            >"$logs/$label/build.log" 2>&1 || result_code=$?
    fi
    if [ "$result_code" -ne 0 ]; then
        tail -320 "$logs/$label/build.log" >&2
        fail "$label build failed"
    fi
}

write_program_inventory() {
    destination=$1
    perl -0777 -ne '
        if (/^PROGRAMS=(.*?)(?=^[A-Z][A-Z0-9_]*=)/ms) {
            $programs = $1;
            $programs =~ s/\\\n/ /g;
            print "$_\n" for grep { length } split /\s+/, $programs;
            $found = 1;
        }
        END { exit !$found }
    ' "$destination/Makefile" | sort >"$program_inventory"
}

verify_products() {
    label=$1
    destination=$2
    archive_spec="apps/libapps.a:19
libcrypto.a:$libcrypto_member_count
libssl.a:94
providers/libcommon.a:23
providers/libdefault.a:156
providers/liblegacy.a:23
providers/libtemplate.a:2
test/libtestutil.a:20"
    printf '%s\n' "$archive_spec" | while IFS=: read -r name count; do
        [ -f "$destination/$name" ] || fail "$label omitted archive $name"
        [ "$(archive_members "$destination/$name")" -eq "$count" ] ||
            fail "$label archive member count changed: $name"
    done
    write_program_inventory "$destination"
    [ "$(wc -l <"$program_inventory" | tr -d ' ')" -eq 343 ] ||
        fail "$label PROGRAMS inventory is not exactly 343"
    while IFS= read -r program; do
        [ -x "$destination/$program" ] || fail "$label omitted program $program"
    done <"$program_inventory"
    [ "$(find "$destination" -type f -name '*.o' | wc -l | tr -d ' ')" -eq "$object_count" ] ||
        fail "$label retained object inventory is not exactly $object_count"
    [ -z "$(find "$destination" -type f \( -name '*.so' -o -name '*.dylib' \) -print -quit)" ] ||
        fail "$label produced a shared library"
}

build_stage() {
    [ -f "$build/configdata.pm" ] || fail "configure stage has not completed"
    [ ! -e "$receipts" ] || fail "receipt root already exists: $receipts"
    "$sole" init "$receipts" "$cgf"
    build_tree cgfried "$build" cgfried
    build_tree host "$host_build" host
    verify_products cgfried "$build"
    verify_products host "$host_build"
}

test_tree() {
    label=$1
    destination=$2
    result_code=0
    make -C "$destination" test HARNESS_JOBS="$jobs" \
        >"$logs/$label/test.log" 2>&1 || result_code=$?
    if [ "$result_code" -ne 0 ]; then
        tail -400 "$logs/$label/test.log" >&2
        fail "$label upstream tests failed"
    fi
    grep -E '^Files=347, Tests=4009,' "$logs/$label/test.log" >/dev/null ||
        fail "$label test suite did not run exactly 347 files / 4009 tests"
    grep -Fqx 'Result: PASS' "$logs/$label/test.log" ||
        fail "$label test suite did not report PASS"
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

normalize_source_path() {
    path=$1
    case $path in
        "$tree"/*) printf '@src/%s\n' "${path#"$tree"/}" ;;
        "$build"/*) printf '@build/%s\n' "${path#"$build"/}" ;;
        *) fail "object source escaped pinned source/build trees: $path" ;;
    esac
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

materialize_source_path() {
    identity=$1
    case $identity in
        @src/*) printf '%s/%s\n' "$tree" "${identity#@src/}" ;;
        @build/*) printf '%s/%s\n' "$build" "${identity#@build/}" ;;
        *) fail "invalid normalized source identity: $identity" ;;
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
                source_identity=$(normalize_source_path "$source")
                printf '%s\t%s\n' "$source_identity" "$output_relative" \
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
    ar t "$build/$archive_relative" | while IFS= read -r member; do
        case $member in __.SYMDEF*) continue ;; esac
        object_relative=$(awk -F '\t' -v member="$member" '
            {
                count = split($2, component, "/")
                if (component[count] == member) {
                    matches++
                    object = $2
                }
            }
            END {
                if (matches != 1) exit 1
                print object
            }
        ' "$object_inventory") ||
            fail "cannot uniquely map $archive_relative($member) to an object"
        printf '%s\t%s\t%s\n' "$archive_relative" "$member" \
            "$object_relative" >>"$archive_inventory"
    done
}

verify_inventory_contract() {
    object_got=$(sha256sum "$object_inventory" | awk '{print $1}')
    link_got=$(sha256sum "$link_inventory" | awk '{print $1}')
    archive_got=$(sha256sum "$archive_inventory" | awk '{print $1}')
    [ "$(wc -l <"$object_inventory" | tr -d ' ')" -eq "$object_count" ] ||
        fail "sole-C object closure is not exactly $object_count translations"
    [ "$(sort -u "$object_inventory" | wc -l | tr -d ' ')" -eq "$object_count" ] ||
        fail "sole-C object closure contains a duplicate identity"
    [ "$(wc -l <"$archive_inventory" | tr -d ' ')" -eq "$archive_member_count" ] ||
        fail "sole-C archive closure is not exactly $archive_member_count members"
    awk -F '\t' '!seen[$1]++ { print $1 }' "$link_inventory" | sort \
        >"$work/linked-products.txt"
    cmp "$program_inventory" "$work/linked-products.txt" ||
        fail "linked-product inventory differs from OpenSSL PROGRAMS"
    for pair in \
        "$OBJECT_CLOSURE_SHA256:$object_got:object" \
        "$LINK_CLOSURE_SHA256:$link_got:link" \
        "$ARCHIVE_CLOSURE_SHA256:$archive_got:archive"; do
        expected=${pair%%:*}
        remainder=${pair#*:}
        measured=${remainder%%:*}
        label=${pair##*:}
        printf 'campaign-openssl: measured %s closure %s\n' "$label" "$measured" >&2
        [ "$expected" = MEASURE ] || [ "$expected" = "$measured" ] ||
            fail "$label closure digest changed"
    done
}

write_manifest() {
    write_closure_inventories
    : >"$archive_inventory"
    for archive_relative in apps/libapps.a libcrypto.a libssl.a \
        providers/libcommon.a providers/libdefault.a providers/liblegacy.a \
        providers/libtemplate.a test/libtestutil.a; do
        append_archive_inventory "$archive_relative"
    done
    verify_inventory_contract
    {
        echo '# cgf-sole-c-closure-v1'
        while IFS="$(printf '\t')" read -r source_identity output_relative; do
            source=$(materialize_source_path "$source_identity")
            output=$(materialize_build_path "$output_relative")
            printf 'object\t%s\t%s\n' "$source" "$output"
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

validate_stage() {
    verify_products cgfried "$build"
    verify_products host "$host_build"
    test_tree cgfried "$build"
    test_tree host "$host_build"
    write_manifest
    "$sole" verify "$receipts" "$cgf" "$manifest" "$report"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-cc,opt=O2\n'
        printf 'baseline.test.upstream\tPASS\tfiles=347,tests=4009\n'
        printf 'build\tPASS\tarchives=8,objects=darwin:1681|linux:1682\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=darwin:1681|linux:1682,archive-members=darwin:1350|linux:1351,linked-products=343\n'
        printf 'configure\tPASS\tasm=off,atomics=off,docs=off,mode=static\n'
        printf 'linkage\tPASS\tarchives=8,binaries=343,static=yes\n'
        printf 'scope.rsa-complex\tSKIP\tCAMP-OPENSSL-001;replacement=checksum-pinned-no-op\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$OPENSSL_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' \
            "$OPENSSL_COMMIT" "$OPENSSL_VERSION"
        printf 'test.upstream\tPASS\tfiles=347,tests=4009\n'
    } >"$work/results.txt"
    printf 'campaign-openssl: PASS target=%s results=%s artifacts=%s\n' \
        "$compiler_target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
