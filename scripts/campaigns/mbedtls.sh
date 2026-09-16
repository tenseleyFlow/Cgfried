#!/bin/sh
set -eu

MBEDTLS_VERSION=3.6.7
MBEDTLS_COMMIT=068ff080b369adfac81509f9b57b2afabaf82dc5
MBEDTLS_SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6

fail() {
    echo "campaign-mbedtls: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_MBEDTLS_ARCHIVE:-$root/build/campaigns/dl/mbedtls-$MBEDTLS_VERSION.tar.bz2}
work=${CGF_CAMPAIGN_MBEDTLS_WORK:-$root/build/campaigns/mbedtls}
cgf=${CGF_CAMPAIGN_MBEDTLS_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_MBEDTLS_HOSTCC:-gcc}
sole=${CGF_CAMPAIGN_MBEDTLS_SOLE_C:-$root/scripts/campaigns/sole-c.sh}
cc_wrapper=${CGF_CAMPAIGN_MBEDTLS_CC_WRAPPER:-$root/scripts/campaigns/mbedtls-cc.sh}
jobs=${CGF_CAMPAIGN_JOBS:-}
cflags=${CGF_CAMPAIGN_MBEDTLS_CFLAGS:--O2}
linux_compat=$root/ci/campaigns/compat/arm64-linux-u128-storage.h

[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
[ -x "$sole" ] || fail "sole-C wrapper is missing or not executable: $sole"
[ -x "$cc_wrapper" ] || fail "compiler adapter is missing or not executable: $cc_wrapper"
[ "$cflags" = -O2 ] || fail "Mbed TLS validation requires exactly -O2, got: $cflags"
[ -f "$archive" ] || fail "verified source archive is missing: $archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$MBEDTLS_SHA256" ] ||
    fail "source checksum mismatch: expected $MBEDTLS_SHA256, got $got"

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

command -v "$hostcc" >/dev/null 2>&1 || fail "host compiler is unavailable: $hostcc"
command -v make >/dev/null 2>&1 || fail "GNU make is unavailable"
make --version 2>/dev/null | grep -F 'GNU Make' >/dev/null ||
    fail "Mbed TLS campaign requires GNU make"

compiler_target=$("$cgf" -dumpmachine) || fail "cannot query Cgfried's target"
compat_header=
compat_policy=none
case $compiler_target in
    x86_64-linux-gnu) ;;
    arm64-linux)
        compat_header=$linux_compat
        compat_policy=opaque-u64x2-align16-v1
        ;;
    *) fail "unsupported native campaign target: $compiler_target" ;;
esac
compat_sha256=none
if [ -n "$compat_header" ]; then
    [ -r "$compat_header" ] || fail "hosted-header compatibility file is unreadable"
    compat_sha256=$(sha256sum "$compat_header" | awk '{print $1}')
fi

as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "native assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "native linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

tree=$work/cgfried-src
host_tree=$work/host-gcc-src
logs=$work/logs
receipts=$work/sole-c
manifest=$work/sole-c-closure.tsv
report=$work/sole-c-report.txt
cgf_object=$work/cgfried-selftest.o
cgf_program=$work/cgfried-selftest
host_object=$work/host-gcc-selftest.o
host_program=$work/host-gcc-selftest

crypto_members='aes.o aesni.o aesce.o aria.o asn1parse.o asn1write.o
base64.o bignum.o bignum_core.o bignum_mod.o bignum_mod_raw.o
block_cipher.o camellia.o ccm.o chacha20.o chachapoly.o cipher.o
cipher_wrap.o cmac.o constant_time.o ctr_drbg.o des.o dhm.o ecdh.o
ecdsa.o ecjpake.o ecp.o ecp_curves.o entropy.o entropy_poll.o error.o
gcm.o hkdf.o hmac_drbg.o lmots.o lms.o md.o md5.o memory_buffer_alloc.o
nist_kw.o oid.o padlock.o pem.o pk.o pk_ecc.o pk_wrap.o pkcs12.o pkcs5.o
pkparse.o pkwrite.o platform.o platform_util.o poly1305.o psa_crypto.o
psa_crypto_aead.o psa_crypto_cipher.o psa_crypto_client.o
psa_crypto_driver_wrappers_no_static.o psa_crypto_ecp.o psa_crypto_ffdh.o
psa_crypto_hash.o psa_crypto_mac.o psa_crypto_pake.o psa_crypto_random.o
psa_crypto_rsa.o psa_crypto_se.o psa_crypto_slot_management.o
psa_crypto_storage.o psa_its_file.o psa_util.o ripemd160.o rsa.o
rsa_alt_helpers.o sha1.o sha256.o sha512.o sha3.o threading.o timing.o
version.o version_features.o everest.o x25519.o Hacl_Curve25519_joined.o
p256-m_driver_entrypoints.o p256-m.o'
x509_members='x509.o x509_create.o x509_crl.o x509_crt.o x509_csr.o
x509write.o x509write_crt.o x509write_csr.o pkcs7.o'
tls_members='debug.o mps_reader.o mps_trace.o net_sockets.o ssl_cache.o
ssl_ciphersuites.o ssl_client.o ssl_cookie.o ssl_debug_helpers_generated.o
ssl_msg.o ssl_ticket.o ssl_tls.o ssl_tls12_client.o ssl_tls12_server.o
ssl_tls13_keys.o ssl_tls13_client.o ssl_tls13_server.o ssl_tls13_generic.o'

extract_tree() {
    destination=$1
    mkdir -p "$destination"
    tar --no-same-owner --no-same-permissions -xjf "$archive" \
        -C "$destination" --strip-components=1
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs"
    if ! tar -tjf "$archive" | awk -v root="mbedtls-$MBEDTLS_VERSION" '
        {
            name = $0
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
    if ! tar -tvjf "$archive" | awk '
        substr($1, 1, 1) != "-" && substr($1, 1, 1) != "d" { bad = 1 }
        END { exit bad }
    '; then
        fail "source archive contains a link or special-file member"
    fi
    extract_tree "$tree"
    extract_tree "$host_tree"
    grep -F '#define MBEDTLS_VERSION_MAJOR  3' "$tree/include/mbedtls/build_info.h" >/dev/null ||
        fail "source tree does not identify Mbed TLS major version 3"
    grep -F '#define MBEDTLS_VERSION_MINOR  6' "$tree/include/mbedtls/build_info.h" >/dev/null ||
        fail "source tree does not identify Mbed TLS minor version 6"
    grep -F '#define MBEDTLS_VERSION_PATCH  7' "$tree/include/mbedtls/build_info.h" >/dev/null ||
        fail "source tree does not identify Mbed TLS patch version 7"
    config=$tree/configs/config-symmetric-only.h
    grep -Fqx '#define MBEDTLS_SELF_TEST' "$config" ||
        fail "symmetric-only configuration omitted self tests"
    grep -Fqx '#define MBEDTLS_HAVE_ASM' "$config" &&
        fail "symmetric-only configuration unexpectedly enables assembly"
    grep -Fqx '#define MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED' "$config" &&
        fail "symmetric-only configuration unexpectedly enables Everest arithmetic"
    grep -Fqx '#define MBEDTLS_PSA_P256M_DRIVER_ENABLED' "$config" &&
        fail "symmetric-only configuration unexpectedly enables P-256 arithmetic"

    {
        printf 'version=%s\n' "$MBEDTLS_VERSION"
        printf 'commit=%s\n' "$MBEDTLS_COMMIT"
        printf 'sha256=%s\n' "$MBEDTLS_SHA256"
        printf 'target=%s\n' "$compiler_target"
        printf 'compiler=%s\n' "$cgf"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'cflags=%s\n' "$cflags"
        printf 'configuration=config-symmetric-only.h\n'
        printf 'compat_policy=%s\n' "$compat_policy"
        printf 'compat_header=%s\n' "${compat_header:-none}"
        printf 'compat_header_sha256=%s\n' "$compat_sha256"
    } >"$work/provenance.txt"
}

set_wrapper_environment() {
    mode=$1
    source=$2
    export CGF_CAMPAIGN_MBEDTLS_CC_MODE=$mode
    export CGF_CAMPAIGN_MBEDTLS_CONFIG=$source/configs/config-symmetric-only.h
    export CGF_CAMPAIGN_MBEDTLS_SOLE=$sole
    export CGF_CAMPAIGN_MBEDTLS_RECEIPTS=$receipts
    export CGF_CAMPAIGN_MBEDTLS_CGF=$cgf
    export CGF_CAMPAIGN_MBEDTLS_HOSTCC=$hostcc
    export CGF_CAMPAIGN_MBEDTLS_COMPAT=$compat_header
    export CC=$cc_wrapper HOSTCC=$cc_wrapper
}

expected_members() {
    archive=$1
    case $archive in
        libmbedcrypto.a) printf '%s\n' $crypto_members ;;
        libmbedx509.a) printf '%s\n' $x509_members ;;
        libmbedtls.a) printf '%s\n' $tls_members ;;
        *) fail "unknown archive inventory: $archive" ;;
    esac
}

verify_archive() {
    label=$1
    source=$2
    archive=$3
    expected_count=$4
    path=$source/library/$archive
    [ -f "$path" ] || fail "$label omitted $archive"
    actual=$logs/$label/$archive.members
    expected=$logs/$label/$archive.expected
    ar t "$path" | awk '$0 !~ /^__.SYMDEF( SORTED)?$/' >"$actual"
    expected_members "$archive" >"$expected"
    cmp "$expected" "$actual" >/dev/null || fail "$label $archive member inventory changed"
    count=$(wc -l <"$actual" | tr -d ' ')
    [ "$count" -eq "$expected_count" ] ||
        fail "$label $archive has $count members, expected $expected_count"
}

verify_products() {
    label=$1
    source=$2
    program=$3
    verify_archive "$label" "$source" libmbedcrypto.a 86
    verify_archive "$label" "$source" libmbedx509.a 9
    verify_archive "$label" "$source" libmbedtls.a 18
    [ -x "$program" ] || fail "$label omitted the self-test executable"
    [ ! -e "$source/library/libmbedcrypto.so" ] || fail "$label produced a shared library"
    [ ! -e "$source/library/libmbedx509.so" ] || fail "$label produced a shared library"
    [ ! -e "$source/library/libmbedtls.so" ] || fail "$label produced a shared library"
}

build_tree() {
    label=$1
    source=$2
    object=$3
    program=$4
    mode=$5
    mkdir -p "$logs/$label"
    set_wrapper_environment "$mode" "$source"
    status=0
    LC_ALL=C SOURCE_DATE_EPOCH=0 make -C "$source" -j"$jobs" \
        CC="$cc_wrapper" HOSTCC="$cc_wrapper" CFLAGS= lib \
        >"$logs/$label/build.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -260 "$logs/$label/build.log" >&2
        fail "$label library build failed"
    fi
    (
        cd "$source"
        "$cc_wrapper" -Iinclude -Ilibrary -c programs/test/selftest.c -o "$object"
        "$cc_wrapper" "$object" library/libmbedtls.a library/libmbedx509.a \
            library/libmbedcrypto.a -o "$program"
    ) >"$logs/$label/selftest-build.log" 2>&1 || {
        tail -200 "$logs/$label/selftest-build.log" >&2
        fail "$label self-test build failed"
    }
    verify_products "$label" "$source" "$program"
}

build_stage() {
    [ -f "$tree/Makefile" ] || fail "configure stage has not completed"
    "$sole" init "$receipts" "$cgf"
    build_tree cgfried "$tree" "$cgf_object" "$cgf_program" cgfried
    build_tree host-gcc "$host_tree" "$host_object" "$host_program" host
}

member_source_and_object() {
    source=$1
    member=$2
    case $member in
        everest.o | x25519.o | Hacl_Curve25519_joined.o)
            relative=3rdparty/everest/library/${member%.o}.c
            object=3rdparty/everest/library/$member
            ;;
        p256-m_driver_entrypoints.o)
            relative=3rdparty/p256-m/p256-m_driver_entrypoints.c
            object=3rdparty/p256-m/$member
            ;;
        p256-m.o)
            relative=3rdparty/p256-m/p256-m/p256-m.c
            object=3rdparty/p256-m/p256-m/$member
            ;;
        *)
            relative=library/${member%.o}.c
            object=library/$member
            ;;
    esac
    printf '%s\t%s\n' "$source/$relative" "$source/$object"
}

write_archive_manifest() {
    archive=$1
    members=$2
    for member in $members; do
        mapping=$(member_source_and_object "$tree" "$member")
        source=${mapping%%"$(printf '\t')"*}
        object=${mapping#*"$(printf '\t')"}
        printf 'object\t%s\t%s\n' "$source" "$object"
    done
    for member in $members; do
        mapping=$(member_source_and_object "$tree" "$member")
        object=${mapping#*"$(printf '\t')"}
        printf 'archive\t%s/library/%s\t%s\t%s\n' \
            "$tree" "$archive" "$member" "$object"
    done
}

write_manifest() {
    {
        echo '# cgf-sole-c-closure-v1'
        write_archive_manifest libmbedcrypto.a "$crypto_members"
        write_archive_manifest libmbedx509.a "$x509_members"
        write_archive_manifest libmbedtls.a "$tls_members"
        printf 'object\t%s/programs/test/selftest.c\t%s\n' "$tree" "$cgf_object"
        printf 'link-input\t%s\t%s\n' "$cgf_program" "$cgf_object"
        printf 'link-input\t%s\t%s/library/libmbedtls.a\n' "$cgf_program" "$tree"
        printf 'link-input\t%s\t%s/library/libmbedx509.a\n' "$cgf_program" "$tree"
        printf 'link-input\t%s\t%s/library/libmbedcrypto.a\n' "$cgf_program" "$tree"
    } >"$manifest"
}

test_tree() {
    label=$1
    program=$2
    runtime=$work/runtime-$label
    rm -rf "$runtime"
    mkdir -p "$runtime"
    status=0
    (cd "$runtime" && LC_ALL=C "$program") >"$logs/$label/selftest.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -240 "$logs/$label/selftest.log" >&2
        fail "$label self-test failed"
    fi
    grep -F 'Executed 25 test suites' "$logs/$label/selftest.log" >/dev/null ||
        fail "$label self-test did not execute exactly 25 suites"
    grep -F '[ All tests PASS ]' "$logs/$label/selftest.log" >/dev/null ||
        fail "$label self-test omitted its success sentinel"
}

validate_stage() {
    verify_products cgfried "$tree" "$cgf_program"
    verify_products host-gcc "$host_tree" "$host_program"
    test_tree cgfried "$cgf_program"
    test_tree host-gcc "$host_program"
    cmp "$logs/host-gcc/selftest.log" "$logs/cgfried/selftest.log" >/dev/null ||
        fail "self-test output differs from host GCC"

    write_manifest
    "$sole" verify "$receipts" "$cgf" "$manifest" "$report"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-gcc,opt=O2\n'
        printf 'baseline.test.selftest\tPASS\tsuites=25\n'
        printf 'build\tPASS\tlibraries=3,objects=114\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=114,archive-members=113,linked-products=1\n'
        printf 'configure\tPASS\tmode=symmetric-only,asm=off\n'
        printf 'linkage\tPASS\tbinaries=1,libraries=3,static=yes\n'
        printf 'parity.outputs\tPASS\tcommand=selftest\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$MBEDTLS_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' "$MBEDTLS_COMMIT" "$MBEDTLS_VERSION"
        printf 'test.selftest\tPASS\tsuites=25,opt=O2\n'
    } >"$work/results.txt"
    printf 'campaign-mbedtls: PASS target=%s results=%s artifacts=%s\n' \
        "$compiler_target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
