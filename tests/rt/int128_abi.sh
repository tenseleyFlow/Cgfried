#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-int128-abi.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

for cc in gcc clang; do
    if ! command -v "$cc" >/dev/null 2>&1; then
        printf 'HARNESS_SKIP: %s unavailable\n' "$cc"
        continue
    fi
    "$cc" -std=c11 -Wall -Wextra -Werror -O2 -DREFERENCE \
        "$root/tests/rt/int128_abi.c" -o "$tmp/reference-$cc"
    "$cc" -std=c11 -Wall -Wextra -Werror -O2 \
        "$root/tests/rt/int128_abi.c" "$root/src/rt/int128.c" \
        -o "$tmp/candidate-$cc"
    "$tmp/reference-$cc" >"$tmp/reference-$cc.out"
    "$tmp/candidate-$cc" >"$tmp/candidate-$cc.out"
    cmp "$tmp/reference-$cc.out" "$tmp/candidate-$cc.out"
    printf 'int128 ABI/arithmetic: %s PASS (%s)\n' \
        "$cc" "$(cat "$tmp/candidate-$cc.out")"
done

cgf=${CGF_TEST_CC:-$root/build/cgfried}
if [ -x "$cgf" ]; then
    for opt in O0 O2; do
        CGF_AS=0 "$cgf" -std=c11 -Wall -Wextra -Werror -"$opt" \
            -fno-strict-aliasing -c "$root/src/rt/int128.c" \
            -o "$tmp/int128-cgf-$opt.o"
        if command -v gcc >/dev/null 2>&1; then
            gcc -std=c11 -Wall -Wextra -Werror -O2 \
                "$root/tests/rt/int128_abi.c" "$tmp/int128-cgf-$opt.o" \
                -o "$tmp/candidate-cgf-$opt"
            "$tmp/candidate-cgf-$opt" >"$tmp/candidate-cgf-$opt.out"
            cmp "$tmp/reference-gcc.out" "$tmp/candidate-cgf-$opt.out"
        fi
        printf 'int128 strict-C11 self-compile/ABI: %s PASS\n' "$opt"
    done
else
    printf 'HARNESS_SKIP: Cgfried unavailable at %s\n' "$cgf"
fi
