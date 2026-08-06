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

# On an arm64 host the toolchain is not cross-prefixed and no emulator is
# needed, which is the whole reason this lane can close Sprint 49's DoD 6:
# qemu-user does not reproduce weak memory ordering and its scheduling is far
# tamer than a real core's, so the four-thread ll/sc hammer only counts on
# hardware. Execution goes through qemu-run.sh either way -- it is already a
# passthrough on a matching arch, so the branch is over TOOL NAMES only.
host=$(uname -m 2>/dev/null || echo unknown)
case "$host" in
aarch64 | arm64)
    AS=${CGF_A64_AS:-as}
    CC=${CGF_A64_GCC:-cc}
    native=1
    ;;
*)
    AS=${CGF_A64_AS:-aarch64-linux-gnu-as}
    CC=${CGF_A64_GCC:-aarch64-linux-gnu-gcc}
    native=0
    ;;
esac
QEMU=${CGF_QEMU:-qemu-aarch64-static}

missing=
command -v "$AS" >/dev/null 2>&1 || missing="$missing $AS"
command -v "$CC" >/dev/null 2>&1 || missing="$missing $CC"
if [ "$native" -eq 0 ]; then
    command -v "$QEMU" >/dev/null 2>&1 || missing="$missing $QEMU"
fi
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
    # -pthread: the atomics fixture hammers the ll/sc loops from four
    # threads, which is the only way a non-atomic sequence shows itself.
    "$CC" -static -O2 -pthread -o "$work/x_$name" "$work/x_$name.o" \
        "$driver" -lm
    out=$(sh scripts/qemu-run.sh "$work/x_$name")
    if [ "$out" != OK ]; then
        echo "a64_exec_lane: $name disagreed with its C reference:" >&2
        echo "$out" >&2
        exit 1
    fi
    ran=$((ran + 1))
done
test "$ran" -ge 4

if [ "$native" -eq 1 ]; then
    how="natively on $host"
else
    how="under $($QEMU --version 2>/dev/null | head -1 | tr -d '\n')"
fi
echo "a64_exec_lane: $emitted modules emitted and assembled, $ran executed $how"
