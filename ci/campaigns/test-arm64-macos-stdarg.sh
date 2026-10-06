#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
cgf=${1:-$root/build/cgfried}
header=$root/ci/campaigns/compat/arm64-macos-stdarg.h
fixture=$root/tests/campaigns/arm64_macos_stdarg_native_ti.c
pack_fixture=$root/tests/campaigns/arm64_macos_pragma_pack_sdk.c
pack_overlay=$root/tests/bench/compat/arm64-macos-self-overlay/mach/port.h
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-arm64-macos-stdarg.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail() {
    echo "campaign-arm64-macos-stdarg-meta: $*" >&2
    exit 1
}

[ -x "$cgf" ] || fail "compiler is not executable: $cgf"
[ -r "$header" ] || fail "compatibility header is unreadable: $header"
[ -r "$pack_fixture" ] || fail "pragma-pack SDK fixture is unreadable"
[ ! -e "$pack_overlay" ] ||
    fail "retired mach/port.h assertion-bypass overlay was restored"

if grep -E '^#[[:space:]]*define[[:space:]]+__uint128_t|__cgf_campaign_macos_u128_storage|_Alignas' \
    "$header" >/dev/null; then
    fail "stdarg compatibility header still replaces native integer-128"
fi

for level in O0 O2; do
    "$cgf" --target=arm64-macos -std=c17 "-$level" -include "$header" \
        -S "$fixture" -o /dev/null
done

compiler_target=$("$cgf" -dumpmachine) ||
    fail "cannot query Cgfried's target"
if [ "$compiler_target" = arm64-macos ]; then
    command -v xcrun >/dev/null 2>&1 ||
        fail "xcrun is required for the native Apple SDK boundary"
    sdk_root=$(xcrun --sdk macosx --show-sdk-path) ||
        fail "cannot locate the macOS SDK"
    for level in O0 O2; do
        "$cgf" --target=arm64-macos --sysroot="$sdk_root" -std=c17 \
            "-$level" -include "$header" \
            -include sys/_types/_va_list.h -include mach/arm/_structs.h \
            -S "$fixture" -o /dev/null
    done
    # This source reaches the SDK directly, without the benchmark overlay.
    # Its public XNU assertions are the regression oracle for pragma-pack
    # state and the exact trailer layouts that originally exposed the gap.
    "$cgf" --target=arm64-macos --sysroot="$sdk_root" -std=c17 \
        -fsyntax-only "$pack_fixture"
fi

if "$cgf" --target=x86_64-linux-gnu -std=c17 -include "$header" \
    -fsyntax-only "$fixture" >"$tmp/wrong-target.out" \
    2>"$tmp/wrong-target.err"; then
    fail "ARM64 macOS compatibility header accepted a Linux target"
fi
grep -F 'only for Cgfried ARM64 macOS campaigns' \
    "$tmp/wrong-target.err" >/dev/null ||
    fail "wrong-target rejection did not identify the compatibility boundary"

printf 'campaign-arm64-macos-stdarg-meta: PASS policy=cgf-stdarg-va-list-pack-v2\n'
