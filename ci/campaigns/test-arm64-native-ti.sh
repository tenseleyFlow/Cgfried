#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
cgf=${1:-$root/build/cgfried}
layout=$root/tests/campaigns/arm64_u128_storage_layout.c
arithmetic=$root/tests/campaigns/arm64_native_uint128.c
linux_overlay=compat/arm64-linux-u128-storage.h

fail() {
    echo "campaign-arm64-native-ti-meta: $*" >&2
    exit 1
}

[ -x "$cgf" ] || fail "compiler is not executable: $cgf"

for target in arm64-linux x86_64-linux-gnu; do
    "$cgf" --target="$target" -std=c17 -fsyntax-only "$layout"
    "$cgf" --target="$target" -std=c17 -fsyntax-only "$arithmetic"
    for level in O0 O2; do
        "$cgf" --target="$target" -std=c17 "-$level" -S "$arithmetic" \
            -o /dev/null
    done
done

for runner in pcre2.sh mbedtls.sh mbedtls-cc.sh curl.sh sqlite.sh libpng.sh \
    libjpeg.sh libjpeg-cc.sh; do
    if grep -F "$linux_overlay" "$root/scripts/campaigns/$runner" >/dev/null; then
        fail "$runner still injects the retired Linux integer-128 overlay"
    fi
done

printf 'campaign-arm64-native-ti-meta: PASS policy=native-ti-no-overlay-v1\n'
