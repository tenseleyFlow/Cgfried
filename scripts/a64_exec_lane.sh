#!/bin/sh
# Sprint 49: arm64 assembly emission, assembled by the real GNU assembler and
# EXECUTED under qemu-user.
#
# Why execution and not just assembly: the emitter's first draft assembled
# every fixture cleanly and still computed wrong answers, because conditional
# branches carry both edges and only the taken one was being printed. Text
# that an assembler accepts proves nothing about semantics.
#
# The oracle is a C reference compiled by aarch64-linux-gnu-gcc and linked
# against our object: each driver computes both and compares. Hard-coded
# expected values would only prove the emitter agrees with itself.
#
# The driver cannot select arm64 on an x86 host until --target lands in
# Sprint 51, so emission runs through build/a64mir --asm, exactly as Sprints
# 47 and 48 drove MIR through the same tool.

set -eu

tool=${1:-build/a64mir}
work=${CGF_A64_EXEC_WORK:-build/a64-exec-lane}

AS=${CGF_A64_AS:-aarch64-linux-gnu-as}
CC=${CGF_A64_GCC:-aarch64-linux-gnu-gcc}
QEMU=${CGF_QEMU:-qemu-aarch64-static}

missing=
command -v "$AS" >/dev/null 2>&1 || missing="$missing $AS"
command -v "$CC" >/dev/null 2>&1 || missing="$missing $CC"
command -v "$QEMU" >/dev/null 2>&1 || missing="$missing $QEMU"
if [ -n "$missing" ]; then
    echo "a64_exec_lane: skipped (missing:$missing)"
    exit 0
fi

mkdir -p "$work"

# Every MIR fixture must at least EMIT and ASSEMBLE: this is the wide net that
# catches an opcode with no emission rule or a malformed operand spelling.
emitted=0
for input in tests/mir/arm64/*.cgfir; do
    name=${input##*/}
    name=${name%.cgfir}
    "$tool" --asm "$input" >"$work/$name.s"
    "$tool" --asm "$input" >"$work/$name.s2"
    cmp "$work/$name.s" "$work/$name.s2"
    "$AS" -o "$work/$name.o" "$work/$name.s"
    emitted=$((emitted + 1))
done
test "$emitted" -ge 8

# The execution fixtures carry a C driver holding the reference computation.
ran=0
for input in tests/exec/arm64/*.cgfir; do
    name=${input##*/}
    name=${name%.cgfir}
    driver="tests/exec/arm64/$name.c"
    if [ ! -f "$driver" ]; then
        echo "a64_exec_lane: $name has no C driver" >&2
        exit 1
    fi
    "$tool" --asm "$input" >"$work/x_$name.s"
    "$AS" -o "$work/x_$name.o" "$work/x_$name.s"
    "$CC" -static -O2 -o "$work/x_$name" "$work/x_$name.o" "$driver"
    out=$("$QEMU" "$work/x_$name")
    if [ "$out" != OK ]; then
        echo "a64_exec_lane: $name disagreed with its C reference:" >&2
        echo "$out" >&2
        exit 1
    fi
    ran=$((ran + 1))
done
test "$ran" -ge 4

echo "a64_exec_lane: $emitted modules emitted and assembled, \
$ran executed under $($QEMU --version 2>/dev/null | head -1 | tr -d '\n')"
