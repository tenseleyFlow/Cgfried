#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

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
cgf_test_inventory=$logs/cgfried/generated-test-inputs.tsv
host_test_inventory=$logs/host-gcc/generated-test-inputs.tsv
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

test_support_stems='framework/tests/src/asn1_helpers
framework/tests/src/bignum_codepath_check
framework/tests/src/bignum_helpers
framework/tests/src/drivers/hash
framework/tests/src/drivers/platform_builtin_keys
framework/tests/src/drivers/test_driver_aead
framework/tests/src/drivers/test_driver_asymmetric_encryption
framework/tests/src/drivers/test_driver_cipher
framework/tests/src/drivers/test_driver_key_agreement
framework/tests/src/drivers/test_driver_key_management
framework/tests/src/drivers/test_driver_mac
framework/tests/src/drivers/test_driver_pake
framework/tests/src/drivers/test_driver_signature
framework/tests/src/drivers/xof
framework/tests/src/fake_external_rng_for_test
framework/tests/src/fork_helpers
framework/tests/src/helpers
framework/tests/src/pk_helpers
framework/tests/src/psa_crypto_helpers
framework/tests/src/psa_crypto_stubs
framework/tests/src/psa_exercise_key
framework/tests/src/psa_memory_poisoning_wrappers
framework/tests/src/random
framework/tests/src/test_memory
framework/tests/src/threading_helpers
tests/src/certs
tests/src/psa_test_wrappers
tests/src/test_helpers/ssl_helpers'

# Mbed TLS calls these its normal sample/test programs. Keep the list frozen so
# an upstream Makefile change cannot silently widen or narrow the campaign.
# test/selftest is intentionally absent: it is already built as the campaign's
# separately linked 25-suite self-test above the upstream program tree.
program_targets='aes/crypt_and_hash
cipher/cipher_aead_demo
hash/generic_sum
hash/hello
hash/md_hmac_demo
pkey/dh_client
pkey/dh_genprime
pkey/dh_server
pkey/ecdh_curve25519
pkey/ecdsa
pkey/gen_key
pkey/key_app
pkey/key_app_writer
pkey/mpi_demo
pkey/pk_decrypt
pkey/pk_encrypt
pkey/pk_sign
pkey/pk_verify
pkey/rsa_decrypt
pkey/rsa_encrypt
pkey/rsa_genkey
pkey/rsa_sign
pkey/rsa_sign_pss
pkey/rsa_verify
pkey/rsa_verify_pss
psa/aead_demo
psa/crypto_examples
psa/hmac_demo
psa/key_ladder_demo
psa/psa_constant_names
psa/psa_hash
random/gen_entropy
random/gen_random_ctr_drbg
ssl/dtls_client
ssl/dtls_server
ssl/mini_client
ssl/ssl_client1
ssl/ssl_client2
ssl/ssl_context_info
ssl/ssl_fork_server
ssl/ssl_mail_client
ssl/ssl_server
ssl/ssl_server2
test/benchmark
test/metatest
test/query_compile_time_config
test/query_included_headers
test/udp_proxy
test/zeroize
util/pem2der
util/strerror
x509/cert_app
x509/cert_req
x509/cert_write
x509/crl_app
x509/load_roots
x509/req_app'

program_generated_sources='programs/psa/psa_constant_names_generated.c
programs/test/query_config.c'

program_object_stems='programs/ssl/ssl_test_lib
programs/test/query_config'

# Keep the standalone one-file fuzz drivers frozen independently from the
# normal-program inventory.  With FUZZINGENGINE absent, upstream links each
# fuzz target as an ordinary C executable through onefile.c.
fuzz_targets='fuzz_client
fuzz_dtlsclient
fuzz_dtlsserver
fuzz_pkcs7
fuzz_privkey
fuzz_pubkey
fuzz_server
fuzz_x509crl
fuzz_x509crt
fuzz_x509csr'

fuzz_support_stems='programs/fuzz/common
programs/fuzz/onefile'

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
        printf 'test_scope=generated-suite-normal-and-fuzz-programs\n'
        printf 'compat_policy=%s\n' "$compat_policy"
        printf 'compat_header=%s\n' "${compat_header:-none}"
        printf 'compat_header_sha256=%s\n' "$compat_sha256"
    } >"$work/provenance.txt"
}

write_test_inventory() {
    label=$1
    source=$2
    inventory=$logs/$label/generated-test-inputs.tsv
    : >"$inventory"
    runner_count=0
    for generated in "$source"/tests/test_suite_*.c; do
        [ -f "$generated" ] || fail "$label omitted generated test runners"
        relative=tests/${generated##*/}
        digest=$(sha256sum "$generated" | awk '{print $1}')
        printf 'runner\t%s\t%s\n' "$relative" "$digest" >>"$inventory"
        runner_count=$((runner_count + 1))
    done
    [ "$runner_count" -eq 140 ] ||
        fail "$label generated $runner_count test runners, expected 140"

    data_count=0
    for data in "$source"/tests/suites/test_suite_*.data; do
        [ -f "$data" ] || fail "$label omitted generated test data"
        relative=tests/suites/${data##*/}
        digest=$(sha256sum "$data" | awk '{print $1}')
        printf 'data\t%s\t%s\n' "$relative" "$digest" >>"$inventory"
        data_count=$((data_count + 1))
    done
    [ "$data_count" -eq 140 ] ||
        fail "$label generated $data_count test data files, expected 140"

    support_count=0
    for stem in $test_support_stems; do
        relative=$stem.c
        [ -f "$source/$relative" ] || fail "$label omitted test support source: $relative"
        digest=$(sha256sum "$source/$relative" | awk '{print $1}')
        printf 'support\t%s\t%s\n' "$relative" "$digest" >>"$inventory"
        support_count=$((support_count + 1))
    done
    [ "$support_count" -eq 28 ] ||
        fail "$label has $support_count test support sources, expected 28"

    program_generated_count=0
    for relative in $program_generated_sources; do
        [ -f "$source/$relative" ] ||
            fail "$label omitted generated program source: $relative"
        digest=$(sha256sum "$source/$relative" | awk '{print $1}')
        printf 'program-generated\t%s\t%s\n' "$relative" "$digest" >>"$inventory"
        program_generated_count=$((program_generated_count + 1))
    done
    [ "$program_generated_count" -eq 2 ] ||
        fail "$label generated $program_generated_count program sources, expected 2"
}

verify_program_inventory() {
    label=$1
    source=$2
    expected=$logs/$label/program-targets.expected
    actual=$logs/$label/program-targets.actual
    printf '%s\n' $program_targets >"$expected"
    : >"$actual"
    set -- $(make -s -C "$source/programs" list)
    [ "$#" -eq 58 ] ||
        fail "$label upstream normal-program inventory has $# entries, expected 58"
    selftests=0
    for target in "$@"; do
        if [ "$target" = test/selftest ]; then
            selftests=$((selftests + 1))
        else
            printf '%s\n' "$target" >>"$actual"
        fi
    done
    [ "$selftests" -eq 1 ] ||
        fail "$label upstream program inventory omitted its single self-test"
    cmp "$expected" "$actual" >/dev/null ||
        fail "$label normal-program inventory changed"
}

verify_fuzz_inventory() {
    label=$1
    source=$2
    expected=$logs/$label/fuzz-targets.expected
    actual=$logs/$label/fuzz-targets.actual
    printf '%s\n' $fuzz_targets >"$expected"
    : >"$actual"
    count=0
    for fuzz_source in "$source"/programs/fuzz/fuzz_*.c; do
        [ -f "$fuzz_source" ] || fail "$label omitted upstream fuzz sources"
        target=${fuzz_source##*/}
        target=${target%.c}
        printf '%s\n' "$target" >>"$actual"
        count=$((count + 1))
    done
    [ "$count" -eq 10 ] ||
        fail "$label upstream fuzz-program inventory has $count entries, expected 10"
    cmp "$expected" "$actual" >/dev/null ||
        fail "$label fuzz-program inventory changed"
    for stem in $fuzz_support_stems; do
        [ -f "$source/$stem.c" ] ||
            fail "$label omitted fuzz support source: $stem.c"
    done
}

generate_test_sources() {
    label=$1
    source=$2
    mkdir -p "$logs/$label"
    set_wrapper_environment host "$source"
    status=0
    SOURCE_DATE_EPOCH=0 make -C "$source/tests" -j"$jobs" \
        CC="$cc_wrapper" HOSTCC="$cc_wrapper" CFLAGS= generated_files c \
        >"$logs/$label/generate-tests.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -260 "$logs/$label/generate-tests.log" >&2
        fail "$label generated-test source preparation failed"
    fi
    status=0
    SOURCE_DATE_EPOCH=0 make -C "$source/programs" -j"$jobs" \
        CC="$cc_wrapper" HOSTCC="$cc_wrapper" CFLAGS= generated_files \
        >"$logs/$label/generate-programs.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -200 "$logs/$label/generate-programs.log" >&2
        fail "$label generated-program source preparation failed"
    fi
    verify_program_inventory "$label" "$source"
    verify_fuzz_inventory "$label" "$source"
    write_test_inventory "$label" "$source"
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

verify_test_products() {
    label=$1
    source=$2
    inventory=$logs/$label/generated-test-inputs.tsv
    [ -f "$inventory" ] || fail "$label generated-test-input inventory is missing"
    count=0
    while IFS="$(printf '\t')" read -r kind relative digest; do
        [ "$kind" = runner ] || continue
        product=$source/${relative%.c}
        [ -x "$product" ] || fail "$label omitted generated test product: $relative"
        count=$((count + 1))
    done <"$inventory"
    [ "$count" -eq 140 ] || fail "$label retained $count test products, expected 140"

    actual=0
    for product in "$source"/tests/test_suite_*; do
        [ -f "$product" ] && [ -x "$product" ] || continue
        actual=$((actual + 1))
    done
    [ "$actual" -eq 140 ] ||
        fail "$label produced $actual executable test runners, expected 140"
}

build_generated_tests() {
    label=$1
    source=$2
    mode=$3
    set_wrapper_environment "$mode" "$source"
    status=0
    SOURCE_DATE_EPOCH=0 make -C "$source" -j"$jobs" \
        CC="$cc_wrapper" HOSTCC="$cc_wrapper" CFLAGS= GEN_FILES= \
        LDFLAGS='../library/libmbedtls.a ../library/libmbedx509.a ../library/libmbedcrypto.a' \
        tests >"$logs/$label/build-tests.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -260 "$logs/$label/build-tests.log" >&2
        fail "$label generated-test build failed"
    fi
    verify_test_products "$label" "$source"
}

verify_program_products() {
    label=$1
    source=$2
    count=0
    for target in $program_targets; do
        [ -x "$source/programs/$target" ] ||
            fail "$label omitted normal program: $target"
        count=$((count + 1))
    done
    [ "$count" -eq 57 ] ||
        fail "$label retained $count normal programs, expected 57"
    for stem in $program_object_stems; do
        [ -f "$source/$stem.o" ] ||
            fail "$label omitted normal-program object: $stem.o"
    done
}

build_programs() {
    label=$1
    source=$2
    mode=$3
    set_wrapper_environment "$mode" "$source"
    status=0
    SOURCE_DATE_EPOCH=0 make -C "$source/programs" -j"$jobs" \
        CC="$cc_wrapper" HOSTCC="$cc_wrapper" CFLAGS= GEN_FILES= \
        LDFLAGS='../library/libmbedtls.a ../library/libmbedx509.a ../library/libmbedcrypto.a' \
        $program_targets >"$logs/$label/build-programs.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -260 "$logs/$label/build-programs.log" >&2
        fail "$label normal-program build failed"
    fi
    verify_program_products "$label" "$source"
}

verify_fuzz_products() {
    label=$1
    source=$2
    count=0
    for target in $fuzz_targets; do
        [ -x "$source/programs/fuzz/$target" ] ||
            fail "$label omitted fuzz program: $target"
        [ -f "$source/programs/fuzz/$target.o" ] ||
            fail "$label omitted fuzz-program object: $target.o"
        count=$((count + 1))
    done
    [ "$count" -eq 10 ] ||
        fail "$label retained $count fuzz programs, expected 10"
    for stem in $fuzz_support_stems; do
        [ -f "$source/$stem.o" ] ||
            fail "$label omitted fuzz support object: $stem.o"
    done
}

build_fuzz_programs() {
    label=$1
    source=$2
    mode=$3
    set_wrapper_environment "$mode" "$source"
    status=0
    (
        unset FUZZINGENGINE
        SOURCE_DATE_EPOCH=0 make -C "$source/programs/fuzz" -j"$jobs" \
            CC="$cc_wrapper" HOSTCC="$cc_wrapper" CFLAGS= GEN_FILES= \
            LDFLAGS='../../library/libmbedtls.a ../../library/libmbedx509.a ../../library/libmbedcrypto.a' \
            all
    ) >"$logs/$label/build-fuzz-programs.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -260 "$logs/$label/build-fuzz-programs.log" >&2
        fail "$label fuzz-program build failed"
    fi
    verify_fuzz_products "$label" "$source"
}

build_stage() {
    [ -f "$tree/Makefile" ] || fail "configure stage has not completed"
    generate_test_sources cgfried "$tree"
    generate_test_sources host-gcc "$host_tree"
    cmp "$cgf_test_inventory" "$host_test_inventory" >/dev/null ||
        fail "generated test-input closure differs between pristine trees"
    "$sole" init "$receipts" "$cgf"
    build_tree cgfried "$tree" "$cgf_object" "$cgf_program" cgfried
    build_generated_tests cgfried "$tree" cgfried
    build_programs cgfried "$tree" cgfried
    build_fuzz_programs cgfried "$tree" cgfried
    build_tree host-gcc "$host_tree" "$host_object" "$host_program" host
    build_generated_tests host-gcc "$host_tree" host
    build_programs host-gcc "$host_tree" host
    build_fuzz_programs host-gcc "$host_tree" host
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

program_source_relative() {
    target=$1
    case $target in
        test/metatest | test/query_compile_time_config | test/query_included_headers | test/zeroize)
            printf 'framework/tests/programs/%s.c\n' "${target#test/}"
            ;;
        *) printf 'programs/%s.c\n' "$target" ;;
    esac
}

write_program_link_inputs() {
    target=$1
    product=$tree/programs/$target
    case $target in
        ssl/ssl_client2 | ssl/ssl_server2)
            printf 'link-input\t%s\t%s/programs/test/query_config.o\n' "$product" "$tree"
            printf 'link-input\t%s\t%s/programs/ssl/ssl_test_lib.o\n' "$product" "$tree"
            ;;
        ssl/ssl_context_info | test/query_compile_time_config)
            printf 'link-input\t%s\t%s/programs/test/query_config.o\n' "$product" "$tree"
            ;;
    esac
    for stem in $test_support_stems; do
        printf 'link-input\t%s\t%s/%s.o\n' "$product" "$tree" "$stem"
    done
    printf 'link-input\t%s\t%s/library/libmbedtls.a\n' "$product" "$tree"
    printf 'link-input\t%s\t%s/library/libmbedx509.a\n' "$product" "$tree"
    printf 'link-input\t%s\t%s/library/libmbedcrypto.a\n' "$product" "$tree"
}

write_fuzz_link_inputs() {
    target=$1
    product=$tree/programs/fuzz/$target
    printf 'link-input\t%s\t%s/programs/fuzz/%s.o\n' "$product" "$tree" "$target"
    for stem in $fuzz_support_stems; do
        printf 'link-input\t%s\t%s/%s.o\n' "$product" "$tree" "$stem"
    done
    for stem in $test_support_stems; do
        printf 'link-input\t%s\t%s/%s.o\n' "$product" "$tree" "$stem"
    done
    printf 'link-input\t%s\t%s/library/libmbedtls.a\n' "$product" "$tree"
    printf 'link-input\t%s\t%s/library/libmbedx509.a\n' "$product" "$tree"
    printf 'link-input\t%s\t%s/library/libmbedcrypto.a\n' "$product" "$tree"
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
        for stem in $test_support_stems; do
            printf 'object\t%s/%s.c\t%s/%s.o\n' "$tree" "$stem" "$tree" "$stem"
        done
        for stem in $program_object_stems; do
            printf 'object\t%s/%s.c\t%s/%s.o\n' "$tree" "$stem" "$tree" "$stem"
        done
        while IFS="$(printf '\t')" read -r kind relative digest; do
            [ "$kind" = runner ] || continue
            source=$tree/$relative
            product=$tree/${relative%.c}
            printf 'compile-link\t%s\t%s\n' "$source" "$product"
            printf 'link-input\t%s\t%s\n' "$product" "$source"
            for stem in $test_support_stems; do
                printf 'link-input\t%s\t%s/%s.o\n' "$product" "$tree" "$stem"
            done
            printf 'link-input\t%s\t%s/library/libmbedtls.a\n' "$product" "$tree"
            printf 'link-input\t%s\t%s/library/libmbedx509.a\n' "$product" "$tree"
            printf 'link-input\t%s\t%s/library/libmbedcrypto.a\n' "$product" "$tree"
        done <"$cgf_test_inventory"
        for target in $program_targets; do
            relative=$(program_source_relative "$target")
            source=$tree/$relative
            product=$tree/programs/$target
            printf 'compile-link\t%s\t%s\n' "$source" "$product"
            printf 'link-input\t%s\t%s\n' "$product" "$source"
            write_program_link_inputs "$target"
        done
        for stem in $fuzz_support_stems; do
            printf 'object\t%s/%s.c\t%s/%s.o\n' "$tree" "$stem" "$tree" "$stem"
        done
        for target in $fuzz_targets; do
            printf 'object\t%s/programs/fuzz/%s.c\t%s/programs/fuzz/%s.o\n' \
                "$tree" "$target" "$tree" "$target"
            write_fuzz_link_inputs "$target"
        done
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

test_generated_tree() {
    label=$1
    source=$2
    status=0
    (cd "$source/tests" && perl scripts/run-test-suites.pl) \
        >"$logs/$label/generated-tests.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -260 "$logs/$label/generated-tests.log" >&2
        fail "$label generated-test execution failed"
    fi
    grep -Fqx 'PASSED (140 suites, 13266 tests run)' \
        "$logs/$label/generated-tests.log" ||
        fail "$label generated-test summary changed"
    passed=$(grep -c ' PASS$' "$logs/$label/generated-tests.log" || true)
    [ "$passed" -eq 140 ] ||
        fail "$label generated-test log has $passed passing suites, expected 140"
}

test_program_tree() {
    label=$1
    source=$2
    runtime=$work/runtime-$label
    log=$logs/$label/program-smokes.log
    printf 'Cgfried Mbed TLS program smoke\n' >"$runtime/program-input.txt"
    status=0
    (
        cd "$runtime"
        printf 'probe=hash/hello\n'
        "$source/programs/hash/hello"
        printf 'probe=hash/generic_sum\n'
        "$source/programs/hash/generic_sum" SHA256 program-input.txt
        printf 'probe=test/query_compile_time_config\n'
        "$source/programs/test/query_compile_time_config" MBEDTLS_AES_C
        printf 'probe=test/query_included_headers\n'
        "$source/programs/test/query_included_headers"
        printf 'probe=psa/psa_constant_names\n'
        "$source/programs/psa/psa_constant_names" alg 0x02000009
    ) >"$log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -200 "$log" >&2
        fail "$label normal-program smoke failed"
    fi
    probes=$(grep -c '^probe=' "$log" || true)
    [ "$probes" -eq 5 ] ||
        fail "$label normal-program log has $probes probes, expected 5"
    grep -Fqx "  MD5('Hello, world!') = 6cd3556deb0da54bca060b4c39479839" "$log" ||
        fail "$label hash/hello result changed"
    grep -Fqx '4cb113e2b9b7b7ea08646330b5d328b83376d5bb18d954a21dda97f726568823  program-input.txt' "$log" ||
        fail "$label hash/generic_sum result changed"
    grep -Fqx 'PSA_CRYPTO_PLATFORM_H' "$log" ||
        fail "$label included-header platform result changed"
    grep -Fqx 'PSA_CRYPTO_STRUCT_H' "$log" ||
        fail "$label included-header struct result changed"
    grep -Fqx 'PSA_ALG_SHA_256' "$log" ||
        fail "$label PSA constant-name result changed"
}

test_fuzz_tree() {
    label=$1
    source=$2
    runtime=$work/runtime-$label
    log=$logs/$label/fuzz-program-smokes.log
    printf 'Cgfried-fuzz-v1\n' >"$runtime/fuzz-input.bin"
    status=0
    (
        cd "$runtime"
        for target in $fuzz_targets; do
            printf 'probe=%s\n' "$target"
            if "$source/programs/fuzz/$target" fuzz-input.bin; then
                printf 'status=0\n'
            else
                result=$?
                printf 'status=%s\n' "$result"
                exit "$result"
            fi
        done
    ) >"$log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        tail -220 "$log" >&2
        fail "$label fuzz-program smoke failed"
    fi
    probes=$(grep -c '^probe=' "$log" || true)
    [ "$probes" -eq 10 ] ||
        fail "$label fuzz-program log has $probes probes, expected 10"
    successes=$(grep -c '^status=0$' "$log" || true)
    [ "$successes" -eq 10 ] ||
        fail "$label fuzz-program log has $successes successes, expected 10"
}

validate_stage() {
    verify_products cgfried "$tree" "$cgf_program"
    verify_products host-gcc "$host_tree" "$host_program"
    verify_test_products cgfried "$tree"
    verify_test_products host-gcc "$host_tree"
    verify_program_products cgfried "$tree"
    verify_program_products host-gcc "$host_tree"
    verify_fuzz_products cgfried "$tree"
    verify_fuzz_products host-gcc "$host_tree"
    test_tree cgfried "$cgf_program"
    test_tree host-gcc "$host_program"
    cmp "$logs/host-gcc/selftest.log" "$logs/cgfried/selftest.log" >/dev/null ||
        fail "self-test output differs from host GCC"
    test_generated_tree cgfried "$tree"
    test_generated_tree host-gcc "$host_tree"
    cmp "$logs/host-gcc/generated-tests.log" \
        "$logs/cgfried/generated-tests.log" >/dev/null ||
        fail "generated-test output differs from host GCC"
    test_program_tree cgfried "$tree"
    test_program_tree host-gcc "$host_tree"
    cmp "$logs/host-gcc/program-smokes.log" \
        "$logs/cgfried/program-smokes.log" >/dev/null ||
        fail "normal-program smoke output differs from host GCC"
    test_fuzz_tree cgfried "$tree"
    test_fuzz_tree host-gcc "$host_tree"
    cmp "$logs/host-gcc/fuzz-program-smokes.log" \
        "$logs/cgfried/fuzz-program-smokes.log" >/dev/null ||
        fail "fuzz-program smoke output differs from host GCC"

    write_manifest
    "$sole" verify "$receipts" "$cgf" "$manifest" "$report"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-gcc,opt=O2\n'
        printf 'baseline.test.fuzz-programs\tPASS\tproducts=10,probes=10\n'
        printf 'baseline.test.generated\tPASS\tsuites=140,tests=13266\n'
        printf 'baseline.test.programs\tPASS\tproducts=57,probes=5\n'
        printf 'baseline.test.selftest\tPASS\tsuites=25\n'
        printf 'build\tPASS\tlibraries=3,translations=353\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=156,archive-members=113,linked-products=208\n'
        printf 'configure\tPASS\tmode=symmetric-only,asm=off\n'
        printf 'generated.inputs\tPASS\trunners=140,data=140,support=28,programs=2,trees=byte-identical\n'
        printf 'linkage\tPASS\tbinaries=208,libraries=3,static=yes\n'
        printf 'parity.outputs\tPASS\tcommands=selftest,generated-tests,program-smokes,fuzz-program-smokes\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$MBEDTLS_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' "$MBEDTLS_COMMIT" "$MBEDTLS_VERSION"
        printf 'test.fuzz-programs\tPASS\tproducts=10,probes=10,opt=O2\n'
        printf 'test.generated\tPASS\tsuites=140,tests=13266,opt=O2\n'
        printf 'test.programs\tPASS\tproducts=57,probes=5,opt=O2\n'
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
