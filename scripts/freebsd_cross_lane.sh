#!/bin/sh
# Sprint 51 D5: x86_64-freebsd bring-up.
#
# The sprint calls this the easy port and it is right: ABI = SysV, same as
# linux, so there is ZERO backend work. char is signed, long double is x87-80,
# aggregate classification is identical. Everything that differs is driver and
# link time, and the FreeBSD sysroot proves it without a VM.
#
# What a VM adds is one question this cannot answer -- does the product
# actually EXEC? -- and it has been answered on FreeBSD 15 by hand:
# both a system-as and an afs-as build print and exit 0. See the ELF BRANDING
# note below for why that was in doubt.
#
# Freestanding by construction: FreeBSD's sys/_types.h uses
# __attribute__((__aligned__)) UNCONDITIONALLY, so hosted compilation waits on
# Sprint 55 exactly as it does on macOS. __aligned__ is semantically
# load-bearing, so it cannot be skipped the way a format attribute could.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_FREEBSD_WORK:-build/freebsd-cross}
SYSROOT=${CGF_FREEBSD_SYSROOT:-}

if [ -z "$SYSROOT" ] || [ ! -f "$SYSROOT/usr/lib/crt1.o" ]; then
    echo "HARNESS_SKIP suite=freebsd-cross test=all count=1 reason=\"no FreeBSD sysroot (set CGF_FREEBSD_SYSROOT)\""
    exit 0
fi

mkdir -p "$WORK"
cat >"$WORK/probe.c" <<'SRC'
/* No headers -- see the Sprint 55 note above. These are the raw libc entry
   points, which is enough to prove the link and the exec. */
long write(int, const void *, unsigned long);
int main(void)
{
    write(1, "freebsd ok\n", 11);
    return 0;
}
SRC

pass=0
for as in afs system; do
    out=$WORK/probe.$as
    if [ "$as" = system ]; then
        CGF_AS=0 "$CGF" --target=x86_64-freebsd --sysroot="$SYSROOT" \
            -static -o "$out" "$WORK/probe.c"
    else
        "$CGF" --target=x86_64-freebsd --sysroot="$SYSROOT" \
            -static -o "$out" "$WORK/probe.c"
    fi

    # ELF BRANDING. FreeBSD's kernel decides a binary's ABI from, in order,
    # the FreeBSD .note.tag and then the EI_OSABI byte. crt1.o carries the
    # note and we inherit it, which is what makes these run -- verified on
    # FreeBSD 15. The OSABI byte itself reads "UNIX - GNU" because GNU ld
    # writes it and afs-as emits System V; that is cosmetic HERE but would
    # matter to a linker that trusted it, so the note is checked explicitly
    # rather than assumed.
    if ! readelf -n "$out" 2>/dev/null | grep -q "FreeBSD"; then
        echo "freebsd_cross: $as build carries no FreeBSD ABI note;" >&2
        echo "  the kernel would have nothing to brand it by" >&2
        exit 1
    fi
    if ! file "$out" | grep -q "statically linked"; then
        echo "freebsd_cross: $as build is not static" >&2
        exit 1
    fi
    pass=$((pass + 1))
done

echo "freebsd_cross: $pass static FreeBSD binaries cross-linked and branded" \
    "(afs-as + system as)"
