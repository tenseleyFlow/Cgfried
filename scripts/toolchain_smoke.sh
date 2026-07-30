#!/bin/sh
# afs-as vs system-as differential smoke: the permanent pattern Sprint 24
# reuses over every codegen fixture. Compares lifted SECTION BYTES, not whole
# objects — symbol-table order, string-table layout, and section-header
# ordering legitimately differ between gas and afs-as; encoding equality is
# the invariant, container layout is not.
set -eu
LC_ALL=C
export LC_ALL

AS_BUNDLED=afs-as/target/release/afs-as
FIX=tests/toolchain
WORK=build/toolchain-smoke

if [ ! -x "$AS_BUNDLED" ]; then
    # Never silent: the skip line is asserted against the profile's
    # expected-skip file (toolchain-notools locally; the CI toolchain
    # profile expects NO skips, so CI can never silently lose this test).
    echo 'HARNESS_SKIP suite=toolchain test=smoke-differential count=1 reason="afs-as not built (run make tools)"'
    exit 0
fi
for tool in as objcopy cmp; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "toolchain_smoke: required tool '$tool' not found" >&2
        exit 1
    }
done

mkdir -p "$WORK"

"$AS_BUNDLED" --64 "$FIX/smoke_x86_64.s" -o "$WORK/afs.o"
as --64 -o "$WORK/gas.o" "$FIX/smoke_x86_64.s"

for sec in .text .data .rodata; do
    objcopy -O binary --only-section=$sec "$WORK/afs.o" "$WORK/afs$sec.bin"
    objcopy -O binary --only-section=$sec "$WORK/gas.o" "$WORK/gas$sec.bin"
    cmp "$WORK/afs$sec.bin" "$WORK/gas$sec.bin" || {
        echo "toolchain_smoke: section $sec bytes differ (afs-as vs gas)" >&2
        exit 1
    }
done

# Failure parity on a broken fixture: both must reject (diagnostics parity
# is NOT asserted — only that neither silently accepts).
if "$AS_BUNDLED" --64 "$FIX/broken.s" -o "$WORK/broken-afs.o" 2>/dev/null; then
    echo "toolchain_smoke: afs-as accepted broken.s" >&2
    exit 1
fi
if as --64 -o "$WORK/broken-gas.o" "$FIX/broken.s" 2>/dev/null; then
    echo "toolchain_smoke: gas accepted broken.s" >&2
    exit 1
fi

echo "toolchain_smoke: .text/.data/.rodata identical; broken fixture fails both"
