#!/bin/sh
# Sprint 51 D7: arm64-linux DWARF, and the CROSS-TARGET agreement that is the
# actual deliverable.
#
# The fixture is tests/debug/dwarf_lines.c -- the same one the x86 lane uses,
# deliberately. A line table is a claim about SOURCE, so the two targets must
# resolve the same markers to the same lines; anything else means one of them
# is describing its own code generation rather than the program. That is the
# comparison D7 asks for, and reusing the fixture is what makes it free.
#
# Everything here is a cross tool: aarch64-linux-gnu-{readelf,addr2line} read
# the object, and nothing needs to run, so this lane works on an x86 host with
# no emulator. Execution-level debugging (gdb stepping, backtraces) stays in
# the native/qemu lanes where a process actually exists.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_A64_DEBUG_WORK:-build/a64-debug-lane}
SRC=tests/debug/dwarf_lines.c
READELF=${CGF_A64_READELF:-aarch64-linux-gnu-readelf}
ADDR2LINE=${CGF_A64_ADDR2LINE:-aarch64-linux-gnu-addr2line}
AS=${CGF_A64_AS:-aarch64-linux-gnu-as}
SYSROOT=${CGF_QEMU_SYSROOT:-/usr/aarch64-linux-gnu}

missing=
for t in "$READELF" "$ADDR2LINE" "$AS"; do
    command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
done
[ -d "$SYSROOT" ] || missing="$missing $SYSROOT"
if [ -n "$missing" ]; then
    echo "HARNESS_SKIP suite=a64-debug test=all count=1 reason=\"missing:$missing\""
    exit 0
fi

case "$WORK" in
'' | / | .)
    echo "a64_debug lane FAIL: unsafe work directory '$WORK'" >&2
    exit 1
    ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

fails=0
checks=0

fail()
{
    echo "a64_debug lane FAIL: $*" >&2
    fails=$((fails + 1))
}

# THE ASSEMBLER ROUTING TRAP, which has bitten four times: cgf defaults to the
# bundled afs-as, and a Rust-free job never builds one. Route explicitly.
compile()
{
    CGF_AS_PATH="$(command -v "$AS")" \
        "$CGF" --target=arm64-linux -isystem "$SYSROOT/include" \
        -I tests/debug "$@"
}

compile -g -c "$SRC" -o "$WORK/dbg.o" 2>"$WORK/dbg.err" ||
    { cat "$WORK/dbg.err" >&2; fail "-g compile"; }
compile -g0 -c "$SRC" -o "$WORK/nodbg.o" 2>/dev/null ||
    fail "-g0 compile"

for sec in debug_line debug_info debug_abbrev eh_frame; do
    checks=$((checks + 1))
    "$READELF" -SW "$WORK/dbg.o" 2>/dev/null |
        grep -Eq "[[:space:]]\\.$sec[[:space:]]" ||
        fail "-g object missing .$sec"
done
# .eh_frame ships WITHOUT -g (unwinders need it); .debug_* must not.
checks=$((checks + 1))
"$READELF" -SW "$WORK/nodbg.o" 2>/dev/null |
    grep -Eq "[[:space:]]\\.eh_frame[[:space:]]" ||
    fail "-g0 object lost .eh_frame"
for sec in debug_line debug_info; do
    checks=$((checks + 1))
    if "$READELF" -SW "$WORK/nodbg.o" 2>/dev/null |
        grep -Eq "[[:space:]]\\.$sec[[:space:]]"; then
        fail "-g0 object still carries .$sec"
    fi
done

# The three CIE fields that differ from x86 and would each corrupt an unwind
# silently if the encoder had been copied rather than rewritten.
"$READELF" --debug-dump=frames "$WORK/dbg.o" >"$WORK/frames.txt" 2>/dev/null
check_cie()
{
    checks=$((checks + 1))
    grep -q "$1" "$WORK/frames.txt" || fail "CIE: expected '$1'"
}
check_cie 'Code alignment factor: 4'
check_cie 'Data alignment factor: -8'
check_cie 'Return address column: 30'
check_cie 'DW_CFA_def_cfa: r31 (sp)'
# Every function's frame must be described, not just the first.
checks=$((checks + 1))
nfde=$(grep -c ' FDE cie=' "$WORK/frames.txt" 2>/dev/null || echo 0)
[ "$nfde" -ge 5 ] || fail "expected an FDE per function, found $nfde"
# The frame pointer has to become the CFA base, or an unwind through a frame
# whose SP has since moved (a VLA, an outgoing-argument area) is wrong.
checks=$((checks + 1))
grep -q 'DW_CFA_def_cfa_register: r29' "$WORK/frames.txt" ||
    fail "no FDE re-bases the CFA onto x29"

# A frame above 4095 bytes takes two SUB instructions. The CFA must move
# after EACH one: a signal arriving between them otherwise unwinds from the
# old SP even though the first 4096 bytes have already been removed. Decode
# the real corpus fixture's FDE and require the intermediate row before the
# final, larger frame-size row.
compile -g -c tests/corpus/x86_64/int/big_frame.c \
    -o "$WORK/big-frame.o" 2>"$WORK/big-frame.err" || {
    cat "$WORK/big-frame.err" >&2
    fail "large-frame -g compile"
}
"$READELF" --debug-dump=frames "$WORK/big-frame.o" \
    >"$WORK/big-frame.frames" 2>/dev/null
checks=$((checks + 1))
awk '
    /DW_CFA_def_cfa_offset:/ {
        offset = $NF + 0
        if (offset == 4096)
            intermediate = 1
        else if (intermediate && offset > 4096)
            final = 1
    }
    END { exit !(intermediate && final) }
' "$WORK/big-frame.frames" ||
    fail "large-frame FDE lacks an intermediate 4096-byte CFA row"

# Determinism: the same input twice, byte for byte.
compile -g -S "$SRC" -o "$WORK/a.s" 2>/dev/null
compile -g -S "$SRC" -o "$WORK/b.s" 2>/dev/null
checks=$((checks + 1))
cmp -s "$WORK/a.s" "$WORK/b.s" || fail "-g assembly is not deterministic"

# --- D7: the same markers resolve to the same source lines on both targets --
marker_line()
{
    grep -n "$1" "$2" | head -1 | cut -d: -f1
}

resolve()
{
    "$3" -e "$1" "$2" 2>/dev/null
}

CGF_AS=0 "$CGF" -g -c "$SRC" -I tests/debug -o "$WORK/x86.o" 2>/dev/null ||
    fail "x86 -g compile (for the cross comparison)"

addr_for_line()
{
    "$READELF" --debug-dump=decodedline "$1" 2>/dev/null |
        awk -v want="$2" '$2 == want && $3 ~ /^0x/ { print $3; exit }'
}

check_cross()
{
    marker=$1
    file=$2
    line=$(marker_line "$marker" "$file")
    checks=$((checks + 1))
    if [ -z "$line" ]; then
        fail "marker '$marker' not found in $file"
        return
    fi
    a=$(addr_for_line "$WORK/dbg.o" "$line")
    if [ -z "$a" ]; then
        fail "arm64 line table has no row for $marker (line $line)"
        return
    fi
    got=$(resolve "$WORK/dbg.o" "$a" "$ADDR2LINE")
    case "$got" in
    *"$(basename "$file"):$line") ;;
    *) fail "arm64 addr2line for $marker resolved '$got', want line $line" ;;
    esac
}

check_cross 'A2L: leaf-body' "$SRC"
check_cross 'A2L: top-call' "$SRC"
check_cross 'A2L: main-return' "$SRC"
check_cross 'A2L: header-step' tests/debug/dwarf_lines.h

# A #line directive remaps the PRESUMED path, and the line table must carry
# the remap rather than the physical location -- the same rule the x86 lane
# pins, checked here so the shared emitter cannot regress on one target only.
checks=$((checks + 1))
"$READELF" --debug-dump=decodedline "$WORK/dbg.o" 2>/dev/null |
    grep -q 's29_remapped.c' ||
    fail "the #line presumed path is missing from the arm64 line table"

if [ "$fails" -ne 0 ]; then
    echo "a64_debug: $fails failure(s) across $checks checks" >&2
    exit 1
fi
echo "a64_debug lane: $checks checks; DWARF/CFI sections, CIE fields," \
    "per-function FDEs, determinism, and cross-target addr2line agreement"
