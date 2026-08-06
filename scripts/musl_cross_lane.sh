#!/bin/sh
# Sprint 51 D4: x86_64-linux-musl bring-up, proved by CROSS-COMPILING into a
# musl sysroot from a glibc host and running the result.
#
# Static-first, which is musl's sweet spot and the sprint's flagship story:
# cgf -> afs-as -> afs-ld, no system toolchain in the chain at all. The
# system-ld leg runs too, so a failure says which half broke.
#
# There are NO TargetSpec deltas against x86_64-linux-gnu for codegen -- same
# SysV ABI, signed char, x87 long double -- and saying so is part of the
# deliverable. Everything that differs is driver and link time.
#
# The sysroot comes from an Alpine image via podman/docker. Without a
# container runtime the lane skips loudly rather than testing nothing.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_MUSL_CROSS_WORK:-build/musl-cross}
IMAGE=${CGF_MUSL_IMAGE:-alpine:3.20}

RT=
for c in podman docker; do
    command -v "$c" >/dev/null 2>&1 && { RT=$c; break; }
done
if [ -z "$RT" ]; then
    echo "HARNESS_SKIP suite=musl-cross test=all count=1 reason=\"no podman or docker\""
    exit 0
fi

root=$WORK/sysroot
if [ ! -f "$root/usr/lib/crt1.o" ]; then
    rm -rf "$root"
    mkdir -p "$root"
    if ! "$RT" run --rm -v "$(cd "$(dirname "$root")" && pwd)/$(basename "$root")":/out:Z \
        "$IMAGE" sh -c 'apk add --no-cache musl-dev >/dev/null 2>&1 &&
            mkdir -p /out/usr && cp -a /usr/include /out/usr/ &&
            cp -a /usr/lib /out/usr/' >/dev/null 2>&1; then
        echo "HARNESS_SKIP suite=musl-cross test=all count=1 reason=\"$IMAGE unavailable\""
        exit 0
    fi
fi
[ -f "$root/usr/lib/crt1.o" ] || {
    echo "musl_cross: sysroot has no crt1.o" >&2
    exit 1
}

# afs-ld cannot yet read DEBUG sections in third-party inputs (LD-ELF-006):
# Alpine's crt objects carry SHF_COMPRESSED .debug_info, whose relocation
# offsets name the UNCOMPRESSED image. A separate debug-stripped copy keeps
# the afs-ld leg honest about what it IS able to link.
nodbg=$WORK/sysroot-nodbg
if [ ! -f "$nodbg/usr/lib/crt1.o" ]; then
    rm -rf "$nodbg"
    cp -a "$root" "$nodbg"
    for o in crt1.o Scrt1.o crti.o crtn.o libc.a; do
        [ -f "$nodbg/usr/lib/$o" ] && strip --strip-debug "$nodbg/usr/lib/$o"
    done
fi

mkdir -p "$WORK"
cat >"$WORK/probe.c" <<'SRC'
#include <stdio.h>
#include <string.h>
int main(void)
{
    char buf[32];

    snprintf(buf, sizeof buf, "%d/%s", 42, "musl");
    printf("%s len=%d ld=%d\n", buf, (int)strlen(buf), (int)sizeof(long double));
    return 0;
}
SRC

want="42/musl len=7 ld=16"
pass=0

# system ld, then afs-ld. The assembler is the bundled afs-as in both:
# musl's static posture needs no PIC, so nothing here trips the @GOTPCREL
# gap that blocks PIC through afs-as (.docs/audits/afs-as-pic-debt.md).
for ld in system afs; do
    out=$WORK/probe.$ld
    if [ "$ld" = afs ]; then
        CGF_LD=1 "$CGF" --target=x86_64-linux-musl --sysroot="$nodbg" \
            -static -o "$out" "$WORK/probe.c"
    else
        "$CGF" --target=x86_64-linux-musl --sysroot="$root" \
            -static -o "$out" "$WORK/probe.c"
    fi
    got=$("$out")
    if [ "$got" != "$want" ]; then
        echo "musl_cross: $ld ld produced '$got', want '$want'" >&2
        exit 1
    fi
    # Static is the claim, so check it rather than assume it.
    if ! file "$out" | grep -q "statically linked"; then
        echo "musl_cross: $ld ld did not produce a static binary" >&2
        exit 1
    fi
    pass=$((pass + 1))
done

echo "musl_cross: $pass static musl binaries cross-compiled from" \
    "$(uname -m) glibc and executed (system ld + afs-ld)"
