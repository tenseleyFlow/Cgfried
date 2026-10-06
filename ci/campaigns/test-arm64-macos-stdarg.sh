#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
cgf=${1:-$root/build/cgfried}
fixture=$root/tests/campaigns/arm64_macos_stdarg_native_ti.c
partial_fixture=$root/tests/campaigns/arm64_macos_stdarg_partial.c
pack_fixture=$root/tests/campaigns/arm64_macos_pragma_pack_sdk.c
pack_overlay=$root/tests/bench/compat/arm64-macos-self-overlay/mach/port.h
va_overlay=$root/tests/bench/compat/arm64-macos-self-overlay/sys/_types/_va_list.h
compat_header=$root/ci/campaigns/compat/arm64-macos-stdarg.h
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-arm64-macos-stdarg.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail() {
    echo "campaign-arm64-macos-stdarg-meta: $*" >&2
    exit 1
}

[ -x "$cgf" ] || fail "compiler is not executable: $cgf"
[ -r "$pack_fixture" ] || fail "pragma-pack SDK fixture is unreadable"
[ -r "$partial_fixture" ] || fail "partial stdarg fixture is unreadable"
[ ! -e "$compat_header" ] || fail "retired stdarg compatibility header was restored"
[ ! -e "$va_overlay" ] || fail "retired va_list SDK overlay was restored"
[ ! -e "$pack_overlay" ] ||
    fail "retired mach/port.h assertion-bypass overlay was restored"

for level in O0 O2; do
    "$cgf" --target=arm64-macos -std=c17 "-$level" -S "$fixture" -o /dev/null
    "$cgf" --target=arm64-macos -std=c17 "-$level" -S "$partial_fixture" \
        -o /dev/null
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
            "-$level" -include stdarg.h -include sys/_types/_va_list.h \
            -include mach/arm/_structs.h \
            -S "$fixture" -o /dev/null
        # PCRE2 reaches <stdio.h> before <stdarg.h>. In strict ISO mode the
        # SDK then owns va_list through its documented void-pointer fallback;
        # Cgfried must retain that typedef and accept it as the Apple cursor.
        "$cgf" --target=arm64-macos --sysroot="$sdk_root" -std=c17 \
            "-$level" -DCGF_APPLE_SDK_VA_LIST_FIRST=1 \
            -S "$fixture" -o /dev/null
    done
    # This source reaches the SDK directly, without the benchmark overlay.
    # Its public XNU assertions are the regression oracle for pragma-pack
    # state and the exact trailer layouts that originally exposed the gap.
    "$cgf" --target=arm64-macos --sysroot="$sdk_root" -std=c17 \
        -fsyntax-only "$pack_fixture"
fi

if "$cgf" --target=x86_64-linux-gnu -std=c17 -fsyntax-only "$fixture" \
    >"$tmp/wrong-target.out" \
    2>"$tmp/wrong-target.err"; then
    fail "Apple's va_list ownership guard leaked into a Linux target"
fi
grep -F "Apple stdarg must publish the SDK va_list guard" \
    "$tmp/wrong-target.err" >/dev/null ||
    fail "wrong-target rejection did not identify the target boundary"

printf 'campaign-arm64-macos-stdarg-meta: PASS policy=native-apple-stdarg-pack-v3\n'
