#!/bin/sh
# Sprint 50 D5: the Mach-O object differential.
#
# Every macOS fixture is emitted once, then assembled BOTH by the bundled
# afs-as and by Apple's assembler, and the two objects must agree on section
# bytes, on the relocation list, and on the symbol table. That triple is the
# acceptance test, and each leg earns its place: identical __text alone would
# not catch a relocation naming the wrong symbol, and neither a relocation
# nor a byte would catch a symbol emitted with the wrong scope.
#
# The ELF twin is scripts/a64_objdiff_lane.sh. This lane found the first
# thing it looked at: we spelled anonymous globals `Lstr.0`, an assembler
# TEMPORARY, where clang writes `l_.str`, a private extern. Apple's
# assembler resolves the former section-relative and afs-as does not, so the
# bug was invisible to every test that only ran the binary.
#
# Runs on arm64 Darwin only. There is no CI runner yet (D6); until there is,
# run it on nomad-1 after any emitter change.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_MACHO_OBJDIFF_WORK:-build/macho-objdiff}
AFS=${CGF_AFS_AS:-afs-as/target/release/afs-as}
SYSAS=${CGF_MACHO_AS:-clang}

# The translation units CGFRIED compiles. tests/macos also holds the clang
# side of each pair, and those are deliberately absent: they include system
# headers, which need the Sprint 55 attribute work. Naming ours explicitly
# beats inferring it -- a partner TU that quietly started compiling would
# otherwise join the differential without anyone deciding it should.
OURS="gotlink rev23 rev_callee rows23 rows47 vaboth vacall
      gotlink_defs rows23_defs vadefs"

# Fixtures blocked on afs-as INSTRUCTION coverage rather than on its Mach-O
# writer. The pin is exact in BOTH directions: a newly-broken fixture fails
# the lane, and a fixture that starts assembling fails it too, so the list
# can only shrink deliberately.
# EMPTY -- afs-as encodes every instruction the Mach-O emitter produces.
UNENCODABLE=""

case $(uname -s):$(uname -m) in
Darwin:arm64) ;;
*)
    echo "HARNESS_SKIP suite=macho-objdiff test=all count=1 reason=\"not arm64 Darwin\""
    exit 0
    ;;
esac

missing=
[ -x "$AFS" ] || missing="$missing $AFS"
command -v "$SYSAS" >/dev/null 2>&1 || missing="$missing $SYSAS"
command -v otool >/dev/null 2>&1 || missing="$missing otool"
command -v nm >/dev/null 2>&1 || missing="$missing nm"
if [ -n "$missing" ]; then
    echo "macho_objdiff: skipped (missing:$missing)"
    exit 0
fi

rm -rf "$WORK"
mkdir -p "$WORK"

# Every (segment, section) pair either object carries. Enumerating the UNION
# rather than a fixed list is what makes "afs-as omitted a section entirely"
# a failure instead of a silent pass.
sections() {
    otool -l "$1" | awk '
        /^  sectname / { s = $2 }
        /^   segname / { print $2 " " s }
    '
}

pinned() {
    for p in $UNENCODABLE; do
        [ "$p" = "$1" ] && return 0
    done
    return 1
}

compared=0
pins_seen=0
for name in $OURS; do
    src=tests/macos/$name.c
    s=$WORK/$name.s

    if [ ! -f "$src" ]; then
        echo "macho_objdiff: $src is listed but missing" >&2
        exit 1
    fi
    afso=$WORK/$name.afs.o
    syso=$WORK/$name.sys.o

    "$CGF" -S -o "$s" "$src" || {
        echo "macho_objdiff: $name failed to compile" >&2
        exit 1
    }
    "$SYSAS" -c -o "$syso" "$s" || {
        echo "macho_objdiff: Apple's assembler rejected our own output for $name" >&2
        exit 1
    }

    if pinned "$name"; then
        if "$AFS" "$s" -o "$afso" 2>/dev/null; then
            echo "macho_objdiff: $name is pinned UNENCODABLE but assembled;" \
                "remove it from the pin list" >&2
            exit 1
        fi
        pins_seen=$((pins_seen + 1))
        continue
    fi
    if ! "$AFS" "$s" -o "$afso" 2>"$WORK/$name.err"; then
        echo "macho_objdiff: afs-as rejected $name:" >&2
        cat "$WORK/$name.err" >&2
        exit 1
    fi

    # Relocations. Line 1 names the file, so it is dropped on both sides.
    otool -r "$afso" | tail -n +2 >"$WORK/$name.afs.rel"
    otool -r "$syso" | tail -n +2 >"$WORK/$name.sys.rel"
    if ! diff -u "$WORK/$name.sys.rel" "$WORK/$name.afs.rel" \
        >"$WORK/$name.rel.diff"; then
        echo "macho_objdiff: $name relocations differ (system vs afs-as):" >&2
        cat "$WORK/$name.rel.diff" >&2
        exit 1
    fi

    # Symbol table.
    nm -ap "$afso" >"$WORK/$name.afs.nm"
    nm -ap "$syso" >"$WORK/$name.sys.nm"
    if ! diff -u "$WORK/$name.sys.nm" "$WORK/$name.afs.nm" \
        >"$WORK/$name.nm.diff"; then
        echo "macho_objdiff: $name symbol tables differ:" >&2
        cat "$WORK/$name.nm.diff" >&2
        exit 1
    fi

    # Section bytes, over the union of both objects' sections.
    sections "$afso" >"$WORK/$name.afs.secs"
    sections "$syso" >"$WORK/$name.sys.secs"
    sort -u "$WORK/$name.afs.secs" "$WORK/$name.sys.secs" >"$WORK/$name.secs"
    while read -r seg sect; do
        [ -n "$seg" ] || continue
        otool -s "$seg" "$sect" "$afso" | tail -n +3 >"$WORK/$name.a.bytes"
        otool -s "$seg" "$sect" "$syso" | tail -n +3 >"$WORK/$name.s.bytes"
        if ! cmp -s "$WORK/$name.s.bytes" "$WORK/$name.a.bytes"; then
            echo "macho_objdiff: $name $seg,$sect bytes differ:" >&2
            diff -u "$WORK/$name.s.bytes" "$WORK/$name.a.bytes" >&2
            exit 1
        fi
    done <"$WORK/$name.secs"

    compared=$((compared + 1))
done

if [ "$compared" -lt 8 ]; then
    echo "macho_objdiff: only $compared objects compared; the fixture set shrank" >&2
    exit 1
fi

echo "macho_objdiff: $compared objects identical to Apple's assembler" \
    "(sections, relocations, symbols), $pins_seen pinned unencodable"
