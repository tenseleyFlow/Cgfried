#!/bin/sh
# Sprint 47: assemble fixed-register A64 fragments as Mach-O with afs-as and
# as ELF with GNU as, then compare raw instruction bytes (not containers).
set -eu
LC_ALL=C
export LC_ALL

OBJBYTES=${CGF_A64_OBJBYTES:-build/a64_objbytes}
LOGIMM_GEN=${CGF_A64_LOGIMM_GEN:-build/a64_logimm_gen}
AFS=${CGF_A64_AFS_AS:-afs-as/target/release/afs-as}
GNU_AS=${CGF_A64_GNU_AS:-aarch64-linux-gnu-as}
CLANG=${CGF_A64_CLANG:-clang}
WORK=${CGF_A64_DIFF_WORK:-build/a64-asm-diff}

[ -x "$OBJBYTES" ] || {
    echo 'HARNESS_SKIP suite=a64-asm-diff test=extractor count=1 reason="a64_objbytes not built"'
    exit 0
}
[ -x "$LOGIMM_GEN" ] || {
    echo 'HARNESS_SKIP suite=a64-asm-diff test=logical-generator count=1 reason="a64_logimm_gen not built"'
    exit 0
}
[ -x "$AFS" ] || {
    echo 'HARNESS_SKIP suite=a64-asm-diff test=afs-as count=1 reason="afs-as not built (make tools)"'
    exit 0
}

oracle=gnu
if command -v "$GNU_AS" >/dev/null 2>&1; then
    oracle_cmd=$GNU_AS
elif command -v "$CLANG" >/dev/null 2>&1; then
    oracle=clang
    oracle_cmd=$CLANG
    echo 'HARNESS_SKIP suite=a64-asm-diff test=gnu-as count=1 reason="aarch64-linux-gnu-as unavailable; clang integrated assembler fallback ran"'
else
    echo 'HARNESS_SKIP suite=a64-asm-diff test=elf-assembler count=1 reason="neither aarch64-linux-gnu-as nor clang available"'
    exit 0
fi

mkdir -p "$WORK"

assemble_pair()
{
    src=$1
    base=$2
    macho="$WORK/$base.macho.o"
    elf="$WORK/$base.elf.o"
    "$AFS" "$src" -o "$macho" 2>"$WORK/$base.afs.err" || {
        echo "a64-asm-diff FAIL: afs-as rejected $src" >&2
        cat "$WORK/$base.afs.err" >&2
        exit 1
    }
    if [ -s "$WORK/$base.afs.err" ]; then
        echo "a64-asm-diff FAIL: afs-as diagnosed $src" >&2
        cat "$WORK/$base.afs.err" >&2
        exit 1
    fi
    if [ "$oracle" = gnu ]; then
        "$oracle_cmd" "$src" -o "$elf" 2>"$WORK/$base.elf.err"
    else
        "$oracle_cmd" --target=aarch64-linux-gnu -c "$src" -o "$elf" \
            2>"$WORK/$base.elf.err"
    fi || {
        echo "a64-asm-diff FAIL: $oracle assembler rejected $src" >&2
        cat "$WORK/$base.elf.err" >&2
        exit 1
    }
    if [ -s "$WORK/$base.elf.err" ]; then
        echo "a64-asm-diff FAIL: $oracle assembler diagnosed $src" >&2
        cat "$WORK/$base.elf.err" >&2
        exit 1
    fi
    "$OBJBYTES" "$macho" >"$WORK/$base.macho.text"
    "$OBJBYTES" "$elf" >"$WORK/$base.elf.text"
    if ! cmp -s "$WORK/$base.macho.text" "$WORK/$base.elf.text"; then
        echo "a64-asm-diff FAIL: instruction bytes differ for $src" >&2
        diff -u "$WORK/$base.macho.text" "$WORK/$base.elf.text" >&2 || true
        exit 1
    fi
}

if [ "$#" -eq 0 ]; then
    set -- tests/mir/arm64/instruction-fragments.s
fi

n=0
for src in "$@"; do
    [ -f "$src" ] || {
        echo "a64-asm-diff: input not found: $src" >&2
        exit 2
    }
    n=$((n + 1))
    base=$(basename "$src" .s).$n
    assemble_pair "$src" "$base"
done

[ "$n" -gt 0 ] || {
    echo "a64-asm-diff: no fragments supplied" >&2
    exit 1
}

# Exhaustively exercise the 64-bit logical-immediate domain.  The text batch
# proves both assemblers accept and identically encode every constructed
# value.  The second batch contains raw AND-immediate words derived from the
# production encoder's packed N:immr:imms; comparing it with the oracle text
# batch proves those packed fields too, rather than merely testing acceptance.
logical_text="$WORK/logical-immediates.s"
logical_encoded="$WORK/logical-immediates-encoded.s"
"$LOGIMM_GEN" "$logical_text" "$logical_encoded"
assemble_pair "$logical_text" logical-immediates
"$AFS" "$logical_encoded" -o "$WORK/logical-encoded.macho.o" \
    2>"$WORK/logical-encoded.afs.err" || {
    echo "a64-asm-diff FAIL: afs-as rejected production-encoded logical immediates" >&2
    cat "$WORK/logical-encoded.afs.err" >&2
    exit 1
}
if [ -s "$WORK/logical-encoded.afs.err" ]; then
    echo "a64-asm-diff FAIL: afs-as diagnosed production-encoded logical immediates" >&2
    cat "$WORK/logical-encoded.afs.err" >&2
    exit 1
fi
"$OBJBYTES" "$WORK/logical-encoded.macho.o" \
    >"$WORK/logical-encoded.macho.text"
if ! cmp -s "$WORK/logical-encoded.macho.text" \
    "$WORK/logical-immediates.elf.text"; then
    echo "a64-asm-diff FAIL: production N:immr:imms encodings differ from oracle" >&2
    exit 1
fi

echo "a64-asm-diff: $n fixed fragment(s) plus 5334 logical immediates;" \
    "afs-as Mach-O bytes match $oracle ELF bytes and production packed fields"
