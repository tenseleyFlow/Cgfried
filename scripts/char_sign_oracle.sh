#!/bin/sh
# Sprint 49 DoD 4: the char-sign fixtures diverge BY DESIGN, so each one
# carries an expectation per architecture. The x86 half is asserted every
# `make test` run by the corpus lane; this script asserts the OTHER half
# while the arm64 native runner does not exist yet, by compiling each
# fixture with aarch64-linux-gnu-gcc and running it under qemu.
#
# Without it the arm64 expectations would be unverified guesses that only
# get checked once a native runner lands. They are not: writing them by hand
# already produced one wrong sum, which this caught (127+128+255+1 is 511,
# not 639). The repo's rule since Sprint 25 is that every corpus expectation
# is oracle-verified before it is pinned.
#
# Note the oracle is gcc's answer for the LANGUAGE, not for our codegen —
# it proves the expectation, not the compiler. Our arm64 output is proven
# separately by scripts/a64_exec_lane.sh.

set -eu

dir=${1:-tests/corpus/char_sign}
CC=${CGF_A64_GCC:-aarch64-linux-gnu-gcc}
QEMU=${CGF_QEMU:-qemu-aarch64-static}
work=${CGF_CHAR_SIGN_WORK:-build/char-sign-oracle}

missing=
command -v "$CC" >/dev/null 2>&1 || missing="$missing $CC"
command -v "$QEMU" >/dev/null 2>&1 || missing="$missing $QEMU"
if [ -n "$missing" ]; then
    echo "char_sign_oracle: skipped (missing:$missing)"
    exit 0
fi

mkdir -p "$work"
checked=0
bad=0
for src in "$dir"/*.c; do
    name=${src##*/}
    name=${name%.c}
    want=$(sed -n 's|^// CHECK(arm64-linux): ||p' "$src")
    if [ -z "$want" ]; then
        echo "char_sign_oracle: $name has no arm64-linux expectation" >&2
        bad=$((bad + 1))
        continue
    fi
    if ! "$CC" -std=c17 -static -O0 -o "$work/$name" "$src" 2>"$work/$name.err"; then
        echo "char_sign_oracle: $name did not compile for arm64:" >&2
        cat "$work/$name.err" >&2
        bad=$((bad + 1))
        continue
    fi
    got=$("$QEMU" "$work/$name")
    if [ "$got" != "$want" ]; then
        echo "char_sign_oracle: $name expects '$want' but gcc/qemu print '$got'" >&2
        bad=$((bad + 1))
        continue
    fi
    checked=$((checked + 1))
done

if [ "$bad" -ne 0 ]; then
    echo "char_sign_oracle: $bad unverified expectation(s)" >&2
    exit 1
fi
test "$checked" -ge 15
echo "char_sign_oracle: $checked arm64-linux expectations confirmed by $($CC -dumpversion)"
