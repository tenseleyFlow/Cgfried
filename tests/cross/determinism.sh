#!/bin/sh
# Sprint 51 D1: cross-compilation determinism.
#
# For every target in the closed set, `cgf --target=T` must produce the SAME
# bytes no matter what host it runs on. A difference means some code path
# asked the HOST what the target looks like -- char signedness, long-double
# format, object dialect -- and that produces a compiler which is correct
# only when host == target, with no diagnostic when it is not.
#
# Two modes:
#   sh tests/cross/determinism.sh <cgf>              print the manifest
#   sh tests/cross/determinism.sh <cgf> <reference>  diff against one
#
# CI runs it on an x86_64 host and an arm64 host and compares the two
# manifests; locally, `make test` prints one, which at least proves every
# target still compiles from wherever you are.
#
# scripts/check_target_seam.sh is the static half of the same law. This is
# the empirical half: the grep cannot see a host fact that arrived through a
# library call rather than a macro.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
REF=${2:-}
WORK=${CGF_CROSS_WORK:-build/cross-determinism}

# One TU per property that is known to differ by target: char signedness,
# long-double format and calling convention, aggregate classification,
# varargs shape, and the object dialect itself.
mkdir -p "$WORK"
cat >"$WORK/probe.c" <<'PROBE'
/* char signedness: signed on x86 and arm64-macos, unsigned on arm64-linux */
int char_is_signed(void) { return (char)-1 < 0; }

/* long double: x87-80 on x86, binary128 on arm64-linux, double on macOS */
long double ld_add(long double a, long double b) { return a + b; }
int ld_size(void) { return (int)sizeof(long double); }

/* aggregate classification cliff edges */
struct Small { int a, b; };
struct Big { double a, b, c; };
struct Hfa { float x, y, z, w; };
struct Small pass_small(struct Small s) { return s; }
/* Returning an HFA is a distinct AAPCS64 shape and is not lowered yet
   (.docs/audits/abi-debt.md, ABI-001); passing one by value is. */
void take_big(struct Big s) { (void)s; }
void take_hfa(struct Hfa s) { (void)s; }
double sum_hfa(struct Hfa s) { return s.x + s.y + s.z + s.w; }

/* varargs: register save area vs all-stack */
int printf(const char *, ...);
int variadic(int n) { return printf("%d %f %s\n", n, 1.5, "x"); }

/* string literals and statics exercise the object dialect */
static int counter;
const char *tag(void) { counter++; return "cgfried"; }
PROBE

# The closed set, in the enum's own order. Kept literal rather than scraped
# so that adding a target forces this file to be revisited too.
for t in x86_64-linux-gnu arm64-linux arm64-macos x86_64-linux-musl \
    x86_64-freebsd; do
    # SOURCE_DATE_EPOCH: __DATE__/__TIME__ are not in the probe, but pinning
    # it keeps the manifest honest if one ever is.
    if ! SOURCE_DATE_EPOCH=0 "$CGF" --target="$t" -S -o "$WORK/$t.s" \
        "$WORK/probe.c" 2>"$WORK/$t.err"; then
        echo "cross_determinism: --target=$t failed to compile" >&2
        cat "$WORK/$t.err" >&2
        exit 1
    fi
    printf '%s %s\n' "$t" "$(cksum <"$WORK/$t.s" | cut -d' ' -f1,2)"
done >"$WORK/manifest"

# --sysroot is the other half of cross-compiling: choosing the target is
# useless if the headers still come from the host. Proved by construction --
# a header that exists ONLY under the fake root, so finding it cannot be an
# accident of the host's /usr/include.
root=$WORK/sysroot
rm -rf "$root"
mkdir -p "$root/usr/include"
echo '#define CGF_SYSROOT_MARKER 4242' >"$root/usr/include/cgf_probe.h"
cat >"$WORK/sysroot_user.c" <<'SRC'
#include <cgf_probe.h>
int marker(void) { return CGF_SYSROOT_MARKER; }
SRC
if "$CGF" -fsyntax-only "$WORK/sysroot_user.c" 2>/dev/null; then
    echo "cross_determinism: the sysroot header resolved WITHOUT --sysroot;" >&2
    echo "  the probe is vacuous" >&2
    exit 1
fi
if ! "$CGF" --sysroot="$root" -fsyntax-only "$WORK/sysroot_user.c"; then
    echo "cross_determinism: --sysroot did not reach the header search" >&2
    exit 1
fi
# ...and the crt probe, which is what a cross LINK needs. A header-free TU
# on purpose: with a bogus root the include search would fail first and the
# test would pass for the wrong reason.
echo 'int main(void) { return 0; }' >"$WORK/bare.c"
if "$CGF" --sysroot=/nonexistent/root -o "$WORK/never" \
    "$WORK/bare.c" 2>"$WORK/crt.err"; then
    echo "cross_determinism: linking against a bogus sysroot succeeded" >&2
    exit 1
fi
if ! grep -q '/nonexistent/root/usr/lib/crt1.o' "$WORK/crt.err"; then
    echo "cross_determinism: the crt probe ignored --sysroot; it searched:" >&2
    cat "$WORK/crt.err" >&2
    exit 1
fi

if [ -n "$REF" ]; then
    if ! diff -u "$REF" "$WORK/manifest"; then
        echo "cross_determinism: output DEPENDS ON THE HOST -- some path" >&2
        echo "  asked cgf_target_host() where it meant" >&2
        echo "  cgf_target_selected(); see scripts/check_target_seam.sh" >&2
        exit 1
    fi
    echo "cross_determinism: 5 targets byte-identical to the reference host;" \
        "--sysroot reaches headers and crt"
    exit 0
fi

cat "$WORK/manifest"
echo "cross_determinism: 5 targets compiled and --sysroot verified;" \
    "manifest above" >&2
