#!/bin/sh
# Enforce the x86-64 SSE2 backend ceiling on one compiler-produced object.
# Labels and comments are deliberately outside the contract: only GNU
# objdump instruction records are parsed.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <source.c> <compiler-object.o>" >&2
    exit 2
fi

source_path=$1
object_path=$2
objdump_tool=${OBJDUMP:-objdump}
script_dir=$(dirname "$0")
awk_program=$script_dir/../tests/isa/check_isa.awk

[ -f "$source_path" ] || {
    echo "check_isa: missing source: $source_path" >&2
    exit 2
}
[ -f "$object_path" ] || {
    echo "check_isa: missing object: $object_path" >&2
    exit 2
}
command -v "$objdump_tool" >/dev/null 2>&1 || {
    echo "check_isa: GNU objdump is required" >&2
    exit 2
}

work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-check-isa.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

if ! "$objdump_tool" --version >"$work/objdump-version" 2>/dev/null ||
    ! grep -q 'GNU objdump' "$work/objdump-version"; then
    echo "check_isa: GNU objdump is required" >&2
    exit 2
fi
[ -f "$awk_program" ] || {
    echo "check_isa: missing decoder policy: $awk_program" >&2
    exit 2
}

if ! "$objdump_tool" -d --no-show-raw-insn "$object_path" \
    >"$work/disassembly" 2>"$work/objdump.err"; then
    echo "check_isa: objdump failed for $object_path" >&2
    sed 's/^/  /' "$work/objdump.err" >&2
    exit 2
fi

# This is intentionally a source-text license. It may accept a comment and
# does not recognize typedef- or macro-created long double, as documented by
# Sprint 36.
if awk '
    { text = text " " $0 }
    END { exit(text ~ /long[[:space:]]+double/ ? 0 : 1) }
' "$source_path"; then
    x87_licensed=1
else
    x87_licensed=0
fi

awk -v source="$source_path" -v object="$object_path" \
    -v x87_licensed="$x87_licensed" -f "$awk_program" \
    "$work/disassembly"
