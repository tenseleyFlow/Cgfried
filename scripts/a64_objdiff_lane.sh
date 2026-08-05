#!/bin/sh
# Sprint 49 DoD 1: the arm64 object differential.
#
# Every arm64 fixture is emitted, then assembled BOTH by the bundled afs-as
# (`--target=aarch64-elf`) and by `aarch64-linux-gnu-as`, and the two objects
# must agree on section bytes, on the relocation list, and on the symbol
# table. That triple is the acceptance test for the upstream ELF work:
# identical .text alone would not catch a relocation naming the wrong symbol
# or carrying the wrong addend, and neither shows up in a disassembly.
#
# Fixtures afs-as cannot yet ENCODE are a separate matter from the ELF path,
# so they are pinned by name below. The pin is exact in both directions: a
# newly-broken fixture fails the lane, and a fixture that starts assembling
# fails it too, so the list can only shrink deliberately.
set -u
LC_ALL=C
export LC_ALL

TOOL=${1:-build/a64mir}
WORK=${CGF_A64_OBJDIFF_WORK:-build/a64-objdiff}
AFS=${CGF_AFS_AS:-afs-as/target/release/afs-as}

AS=${CGF_A64_AS:-aarch64-linux-gnu-as}
READELF=${CGF_A64_READELF:-aarch64-linux-gnu-readelf}
OBJCOPY=${CGF_A64_OBJCOPY:-aarch64-linux-gnu-objcopy}

# Fixtures blocked on arm64 INSTRUCTION coverage in afs-as, not on its ELF
# writer. Each name is followed by the mnemonics it needs; when a row is
# fixed upstream, delete it here and the lane starts checking that fixture.
#   atomics       ldar ldaxr stlxr clrex
#   neon          vector register operands, ext, dup, umov
#   review-idioms mneg smull
#   scalar-fp     ucvtf fcvtzu
UNENCODABLE="atomics neon review-idioms scalar-fp"

missing=
command -v "$AS" >/dev/null 2>&1 || missing="$missing $AS"
command -v "$READELF" >/dev/null 2>&1 || missing="$missing $READELF"
command -v "$OBJCOPY" >/dev/null 2>&1 || missing="$missing $OBJCOPY"
[ -x "$AFS" ] || missing="$missing $AFS"
if [ -n "$missing" ]; then
    echo "a64_objdiff: skipped (missing:$missing)"
    exit 0
fi

mkdir -p "$WORK"
fails=0
same=0
pinned=0

# POSIX sh has no locals, so this deliberately does not reuse `name`:
# the caller's loop variable would be clobbered on every call.
is_pinned() {
    for pin_candidate in $UNENCODABLE; do
        [ "$pin_candidate" = "$1" ] && return 0
    done
    return 1
}

for input in tests/mir/arm64/*.cgfir tests/exec/arm64/*.cgfir; do
    name=${input##*/}
    name=${name%.cgfir}
    s="$WORK/$name.s"

    if ! "$TOOL" --asm "$input" >"$s" 2>"$WORK/$name.emit.err"; then
        echo "a64_objdiff FAIL: $name: emission failed" >&2
        cat "$WORK/$name.emit.err" >&2
        fails=$((fails + 1))
        continue
    fi

    # THE contract on the system side: gas accepts our text outright.
    if ! "$AS" -o "$WORK/$name.gas.o" "$s" 2>"$WORK/$name.gas.err"; then
        echo "a64_objdiff FAIL: $name: system as rejected our assembly" >&2
        cat "$WORK/$name.gas.err" >&2
        fails=$((fails + 1))
        continue
    fi

    if "$AFS" --target=aarch64-elf -o "$WORK/$name.afs.o" "$s" \
        2>"$WORK/$name.afs.err"; then
        if is_pinned "$name"; then
            echo "a64_objdiff FAIL: $name is pinned unencodable but assembled;" \
                "remove it from UNENCODABLE" >&2
            fails=$((fails + 1))
            continue
        fi
    else
        if is_pinned "$name"; then
            pinned=$((pinned + 1))
            continue
        fi
        echo "a64_objdiff FAIL: $name: afs-as rejected our assembly" >&2
        head -3 "$WORK/$name.afs.err" >&2
        fails=$((fails + 1))
        continue
    fi

    bad=0
    for sec in .text .rodata .data; do
        "$OBJCOPY" -O binary --only-section="$sec" \
            "$WORK/$name.afs.o" "$WORK/afs.bin" 2>/dev/null
        "$OBJCOPY" -O binary --only-section="$sec" \
            "$WORK/$name.gas.o" "$WORK/gas.bin" 2>/dev/null
        if ! cmp -s "$WORK/afs.bin" "$WORK/gas.bin"; then
            echo "a64_objdiff FAIL: $name: $sec bytes differ" >&2
            bad=1
        fi
    done

    # Relocations: type, offset and addend all matter, and so does WHICH
    # symbol is named -- gas points a local reference at its section.
    for who in afs gas; do
        "$READELF" -rW "$WORK/$name.$who.o" |
            grep R_AARCH64 | sort >"$WORK/$name.$who.rel"
    done
    if ! cmp -s "$WORK/$name.afs.rel" "$WORK/$name.gas.rel"; then
        echo "a64_objdiff FAIL: $name: relocations differ" >&2
        diff "$WORK/$name.gas.rel" "$WORK/$name.afs.rel" | head -8 >&2
        bad=1
    fi

    # Symbols compared on value/size/type/binding/visibility/name. The
    # index column is skipped: it is a position, not a fact about the symbol.
    for who in afs gas; do
        "$READELF" -sW "$WORK/$name.$who.o" |
            awk 'NR>3 {print $2, $3, $4, $5, $6, $8}' | sort >"$WORK/$name.$who.sym"
    done
    if ! cmp -s "$WORK/$name.afs.sym" "$WORK/$name.gas.sym"; then
        echo "a64_objdiff FAIL: $name: symbol tables differ" >&2
        diff "$WORK/$name.gas.sym" "$WORK/$name.afs.sym" | head -8 >&2
        bad=1
    fi

    if [ "$bad" -eq 0 ]; then
        same=$((same + 1))
    else
        fails=$((fails + 1))
    fi
done

# A pin that names a fixture which no longer exists is silent rot.
for name in $UNENCODABLE; do
    if [ ! -f "tests/mir/arm64/$name.cgfir" ] && [ ! -f "tests/exec/arm64/$name.cgfir" ]; then
        echo "a64_objdiff FAIL: pinned fixture '$name' does not exist" >&2
        fails=$((fails + 1))
    fi
done

if [ "$fails" -ne 0 ]; then
    echo "a64_objdiff: $fails failure(s)" >&2
    exit 1
fi
test "$same" -ge 12
echo "a64_objdiff: $same objects identical to $AS, $pinned pinned unencodable"
