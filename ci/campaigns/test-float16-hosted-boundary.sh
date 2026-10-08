#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
cgf=${1:-$root/build/cgfried}
declarations=$root/tests/campaigns/float16_hosted_declarations.c
value=$root/tests/campaigns/float16_value_refused.c
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-float16-hosted.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail() {
    echo "campaign-float16-hosted-meta: $*" >&2
    exit 1
}

[ -x "$cgf" ] || fail "compiler is not executable: $cgf"

for target in x86_64-linux-gnu arm64-linux arm64-macos; do
    for level in O0 O2; do
        "$cgf" --target="$target" -std=gnu17 "-$level" -S \
            "$declarations" -o /dev/null
    done
done

if "$cgf" -std=gnu17 -fsyntax-only "$value" >"$tmp/value.out" \
    2>"$tmp/value.err"; then
    fail "an _Float16 object definition crossed the declaration-only boundary"
fi
grep -F "defining an object whose type contains '_Float16' is not yet supported" \
    "$tmp/value.err" >/dev/null ||
    fail "the _Float16 value rejection did not identify the compiler boundary"

compiler_target=$("$cgf" -dumpmachine) || fail "cannot query Cgfried's target"
if [ "$compiler_target" = arm64-macos ]; then
    command -v xcrun >/dev/null 2>&1 ||
        fail "xcrun is required for the native Apple SDK boundary"
    sdk_root=$(xcrun --sdk macosx --show-sdk-path) ||
        fail "cannot locate the macOS SDK"
    for level in O0 O2; do
        "$cgf" --target=arm64-macos --sysroot="$sdk_root" -std=gnu17 \
            "-$level" -DCGF_APPLE_SDK=1 -S "$declarations" -o /dev/null
    done
fi

printf 'campaign-float16-hosted-meta: PASS policy=declaration-layout-only\n'
