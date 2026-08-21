#!/bin/sh
# Sprint 51 D6: the ABI differential harness -- the phase capstone.
#
# For each generated signature, compile the caller with one compiler and the
# callee with the other, link them together, and run. Both directions, every
# time: after the HFA work in this same sprint our CALLEE was correct while
# our CALLER still read the wrong registers, so `ours x theirs` passed and
# `theirs x ours` did not. One direction is two ways of agreeing with
# yourself.
#
# A disagreement is an ABI bug, and it is MINIMIZED before being reported:
# the descriptor is shrunk (drop an argument, replace a composite with one
# member, simplify the return) as long as the failure survives, and the
# 1-minimal reproducer plus its seed is written to tests/abi_differential/
# repro/ for check-in as a permanent fixture.
#
# Targets and their reference compilers:
#   x86_64-linux-gnu   gcc, natively
#   arm64-linux        aarch64-linux-gnu-gcc, run under qemu
#   arm64-macos        clang, ON a Mac -- gcc does not target darwin/arm64
# x86_64-linux-musl and x86_64-freebsd need a musl gcc and a VM respectively;
# neither is wired yet.
set -eu
LC_ALL=C
export LC_ALL

# A disagreeing program often dies on a signal, and qemu-user writes a core
# per crash into the CURRENT directory -- which is the repo root. There is
# nothing to debug in them; the reproducer is the artifact.
ulimit -c 0 2>/dev/null || true

CGF=${1:-build/cgfried}
ABIGEN=${CGF_ABIGEN:-build/abigen}
WORK=${CGF_ABI_DIFF_WORK:-build/abi-differential}
COUNT=${CGF_ABI_DIFF_COUNT:-40}
FIRST=${CGF_ABI_DIFF_SEED:-1}
# Overridable so an anti-vacuity probe (point $CGF at /bin/false and every
# signature must be reported) does not write reproducers into the repo.
REPRO=${CGF_ABI_DIFF_REPRO:-tests/abi_differential/repro}

[ -x "$ABIGEN" ] || {
    echo "abi_differential: $ABIGEN not built" >&2
    exit 1
}

target=${CGF_ABI_DIFF_TARGET:-x86_64-linux-gnu}
# Static where it exists: it takes the dynamic loader out of the picture and
# makes a qemu run self-contained. macOS has no static libSystem and clears it.
LINKFLAGS=-static
case $target in
x86_64-linux-gnu)
    REF=${CGF_ABI_DIFF_REF:-gcc}
    RUN=${CGF_ABI_DIFF_RUN:-}
    ;;
arm64-linux)
    REF=${CGF_ABI_DIFF_REF:-aarch64-linux-gnu-gcc}
    RUN=${CGF_ABI_DIFF_RUN:-"qemu-aarch64-static -L /usr/aarch64-linux-gnu"}
    ;;
arm64-macos)
    # Run this ON a Mac. gcc does not target darwin/arm64, so clang is the
    # reference, and macOS has no static libSystem to link against.
    REF=${CGF_ABI_DIFF_REF:-clang}
    RUN=${CGF_ABI_DIFF_RUN:-}
    LINKFLAGS=
    case $(uname -s):$(uname -m) in
    Darwin:arm64) ;;
    *)
        echo "HARNESS_SKIP suite=abi-differential test=all count=1" \
            "reason=\"arm64-macos needs a Mac\""
        exit 0
        ;;
    esac
    ;;
*)
    echo "abi_differential: no reference compiler wired for $target" >&2
    exit 1
    ;;
esac

command -v "$REF" >/dev/null 2>&1 || {
    echo "HARNESS_SKIP suite=abi-differential test=all count=1 reason=\"$REF not found\""
    exit 0
}

# THE assembler routing, and it is not optional. cgf's default assembler is
# the BUNDLED afs-as; a Rust-free job never builds one, and the driver then
# reports "assembler not found" for every signature -- which this lane would
# faithfully report as 304 ABI disagreements. Sprint 49's native arm64 lane
# lost three rounds to exactly this.
#
# CGF_AS=0 means "system as", which is the HOST's, so it is right only when
# the target is the host. A cross target needs its own by absolute path.
AS_ENV=
if [ ! -x afs-as/target/release/afs-as ]; then
    case $target in
    arm64-linux)
        as_path=$(command -v aarch64-linux-gnu-as 2>/dev/null || true)
        [ -n "$as_path" ] || {
            echo "HARNESS_SKIP suite=abi-differential test=all count=1" \
                "reason=\"no afs-as and no aarch64-linux-gnu-as\""
            exit 0
        }
        AS_ENV="CGF_AS_PATH=$as_path"
        ;;
    *)
        AS_ENV="CGF_AS=0"
        ;;
    esac
fi
if [ -n "$RUN" ]; then
    set -- $RUN
    command -v "$1" >/dev/null 2>&1 || {
        echo "HARNESS_SKIP suite=abi-differential test=all count=1 reason=\"$1 not found\""
        exit 0
    }
fi

rm -rf "$WORK"
mkdir -p "$WORK" "$REPRO"

# Build one direction and run it. Echoes the exit status; 0 is agreement.
# `which` selects who compiles the CALLER; the other side gets the callee.
try_pair() {
    dir=$1
    which=$2
    rm -f "$dir/a.o" "$dir/b.o" "$dir/prog"

    if [ "$which" = cgf-caller ]; then
        env $AS_ENV "$CGF" --target="$target" -I"$dir" -c -o "$dir/a.o" \
            "$dir/caller.c" >"$dir/build.log" 2>&1 || return 90
        "$REF" -std=c11 -I"$dir" -c -o "$dir/b.o" "$dir/callee.c" \
            >>"$dir/build.log" 2>&1 || return 91
    else
        "$REF" -std=c11 -I"$dir" -c -o "$dir/a.o" "$dir/caller.c" \
            >"$dir/build.log" 2>&1 || return 91
        env $AS_ENV "$CGF" --target="$target" -I"$dir" -c -o "$dir/b.o" \
            "$dir/callee.c" >>"$dir/build.log" 2>&1 || return 90
    fi
    # The REFERENCE compiler links: it owns the crt and libc for its target,
    # and the object layout is what is under test, not the link line.
    "$REF" $LINKFLAGS -o "$dir/prog" "$dir/a.o" "$dir/b.o" \
        >>"$dir/build.log" 2>&1 || return 92
    if [ -n "$RUN" ]; then
        $RUN "$dir/prog" >/dev/null 2>&1
    else
        "$dir/prog" >/dev/null 2>&1
    fi
}

# IR-C-01 is outside abigen's repertoire: it has neither long double nor
# bitfields. Pin the zero-width barrier sibling in both mixed-compiler
# directions, alongside the GCC-compatible union control on which GCC and
# Clang disagree. This fixed probe is x86 SysV-specific.
check_ir_c01_fixed() {
    d=$WORK/fixed-ir-c01

    rm -rf "$d"
    mkdir -p "$d"
    cat >"$d/abi.h" <<'EOF'
struct barrier_value { int :0; long double value; };
union union_value { int :0; long double value; };
struct barrier_value make_barrier(long double value);
long double read_barrier(struct barrier_value value);
union union_value make_union(long double value);
long double read_union(union union_value value);
EOF
    cat >"$d/caller.c" <<'EOF'
#include "abi.h"
int main(void)
{
    struct barrier_value s = make_barrier(2.5L);
    union union_value u = make_union(3.5L);
    if (read_barrier(s) != 2.5L)
        return 1;
    if (read_union(u) != 3.5L)
        return 2;
    return 0;
}
EOF
    cat >"$d/callee.c" <<'EOF'
#include "abi.h"
struct barrier_value make_barrier(long double value)
{
    struct barrier_value out = {value};
    return out;
}
long double read_barrier(struct barrier_value value) { return value.value; }
union union_value make_union(long double value)
{
    union union_value out = {value};
    return out;
}
long double read_union(union union_value value) { return value.value; }
EOF
    try_pair "$d" cgf-caller && try_pair "$d" cgf-callee
}

# IR-C-09 is outside abigen's repertoire because its composites never carry
# an explicit 16-byte alignment. Exercise fixed and variadic placement in
# both mixed-compiler directions: Linux C.10 must skip x1, while va_arg must
# find the same value at the rounded register-save-area offset.
check_ir_c09_fixed() {
    d=$WORK/fixed-ir-c09

    rm -rf "$d"
    mkdir -p "$d"
    cat >"$d/abi.h" <<'EOF'
struct pair16 { _Alignas(16) long first; long second; };
long fixed_value(long tag, struct pair16 value);
long variadic_value(long tag, ...);
EOF
    cat >"$d/caller.c" <<'EOF'
#include "abi.h"
int main(void)
{
    struct pair16 value = {11, 13};
    if (fixed_value(7, value) != 31)
        return 1;
    if (variadic_value(5, value) != 29)
        return 2;
    return 0;
}
EOF
    cat >"$d/callee.c" <<'EOF'
#include "abi.h"
#include <stdarg.h>
long fixed_value(long tag, struct pair16 value)
{
    return tag + value.first + value.second;
}
long variadic_value(long tag, ...)
{
    va_list ap;
    struct pair16 value;

    va_start(ap, tag);
    value = va_arg(ap, struct pair16);
    va_end(ap);
    return tag + value.first + value.second;
}
EOF
    try_pair "$d" cgf-caller && try_pair "$d" cgf-callee
}

# IR-C-10 is also outside abigen's repertoire: the source alignment is the
# contract, but descriptors only encode member shapes. A preceding stacked
# scalar leaves NSAA at 8; both fixed arguments and variadic overflow must
# round the following composite to 16 in both mixed-compiler directions.
check_ir_c10_fixed() {
    d=$WORK/fixed-ir-c10

    rm -rf "$d"
    mkdir -p "$d"
    cat >"$d/abi.h" <<'EOF'
struct pair16 { _Alignas(16) long first; long second; };
long fixed_value(long, long, long, long, long, long, long, long, long,
                 struct pair16);
long variadic_value(long, long, long, long, long, long, long, long, ...);
EOF
    cat >"$d/caller.c" <<'EOF'
#include "abi.h"
int main(void)
{
    struct pair16 value = {11, 13};
    if (fixed_value(0, 1, 2, 3, 4, 5, 6, 7, 9, value) != 61)
        return 1;
    if (variadic_value(0, 1, 2, 3, 4, 5, 6, 7, 9L, value) != 61)
        return 2;
    return 0;
}
EOF
    cat >"$d/callee.c" <<'EOF'
#include "abi.h"
#include <stdarg.h>
long fixed_value(long a0, long a1, long a2, long a3, long a4, long a5,
                 long a6, long a7, long stacked, struct pair16 value)
{
    return a0+a1+a2+a3+a4+a5+a6+a7+stacked+value.first+value.second;
}
long variadic_value(long a0, long a1, long a2, long a3, long a4, long a5,
                    long a6, long a7, ...)
{
    va_list ap;
    long stacked;
    struct pair16 value;

    va_start(ap, a7);
    stacked = va_arg(ap, long);
    value = va_arg(ap, struct pair16);
    va_end(ap);
    return a0+a1+a2+a3+a4+a5+a6+a7+stacked+value.first+value.second;
}
EOF
    try_pair "$d" cgf-caller && try_pair "$d" cgf-callee
}

# Regenerate sources from a descriptor and test both directions.
# Returns 0 when both agree.
check_desc() {
    d=$1
    "$ABIGEN" --emit "$d/desc.txt" --out "$d" >/dev/null 2>&1 || return 0
    try_pair "$d" cgf-caller || return 1
    try_pair "$d" cgf-callee || return 1
    return 0
}

# Shrink while the failure survives. Two reductions, applied to a fixpoint:
# drop one argument, and replace one composite with its first member (which
# `abigen --simplify` performs, since it owns the descriptor grammar). Plain
# text surgery -- the reason the generator emits a descriptor at all is that
# shrinking never has to re-derive its random state.
minimize() {
    d=$1
    work=$d/min
    rm -rf "$work"
    mkdir -p "$work/t"
    cp "$d/desc.txt" "$work/desc.txt"
    changed=1
    while [ "$changed" = 1 ]; do
        changed=0

        # Drop arguments, fixed and anonymous alike. Dropping only `A` lines
        # left a "1-minimal" reproducer carrying three untouched variadic
        # arguments, which is not minimal and hides which one matters.
        # abigen rejects a variadic descriptor with no fixed argument, so
        # such a reduction fails to emit and is rejected on its own.
        for kind in A V; do
            n=$(grep -c "^$kind " "$work/desc.txt" 2>/dev/null || true)
            [ -n "$n" ] || n=0
            i=1
            while [ "$i" -le "$n" ]; do
                awk -v skip="$i" -v k="$kind" \
                    '$1 == k {seen++; if (seen == skip) next} {print}' \
                    "$work/desc.txt" >"$work/t/desc.txt"
                if check_desc "$work/t"; then
                    i=$((i + 1))
                else
                    cp "$work/t/desc.txt" "$work/desc.txt"
                    changed=1
                    n=$((n - 1))
                fi
            done
        done

        # Composite simplification: --simplify K unwraps the K'th composite
        # by one level, and exits nonzero when K is out of range.
        k=0
        while "$ABIGEN" --simplify "$k" --emit "$work/desc.txt" \
            >"$work/t/desc.txt" 2>/dev/null; do
            if check_desc "$work/t"; then
                k=$((k + 1))
            else
                cp "$work/t/desc.txt" "$work/desc.txt"
                changed=1
            fi
        done
    done
    cp "$work/desc.txt" "$d/minimal.txt"
}

checked=0
failed=0

if [ "$target" = x86_64-linux-gnu ]; then
    if check_ir_c01_fixed; then
        checked=$((checked + 1))
    else
        echo "abi_differential: REGRESSION on fixed IR-C-01 zero-width shapes" \
            >&2
        failed=$((failed + 1))
    fi
fi

if [ "$target" = arm64-linux ]; then
    if check_ir_c09_fixed; then
        checked=$((checked + 1))
    else
        echo "abi_differential: REGRESSION on fixed IR-C-09 aligned composite" \
            >&2
        failed=$((failed + 1))
    fi
fi

case $target in
arm64-linux|arm64-macos)
    if check_ir_c10_fixed; then
        checked=$((checked + 1))
    else
        echo "abi_differential: REGRESSION on fixed IR-C-10 stacked composite" \
            >&2
        failed=$((failed + 1))
    fi
    ;;
esac

# The permanent fixtures FIRST: every descriptor ever minimized out of a real
# disagreement, replayed on every run. A generated seed only covers a shape
# until the generator changes; these are the shapes that actually broke.
for fixture in tests/abi_differential/repro/*.txt; do
    [ -f "$fixture" ] || continue
    name=$(basename "$fixture" .txt)
    d=$WORK/repro-$name
    mkdir -p "$d"
    cp "$fixture" "$d/desc.txt"
    # check_desc treats an unparseable descriptor as agreement, which is
    # right while shrinking (never accept a broken reduction) and wrong here:
    # a fixture that does not parse would report as a silent pass. The first
    # run counted the README as one.
    if ! "$ABIGEN" --emit "$d/desc.txt" --out "$d" >/dev/null 2>&1; then
        echo "abi_differential: fixture $name is not a valid descriptor" >&2
        failed=$((failed + 1))
        continue
    fi
    if check_desc "$d"; then
        checked=$((checked + 1))
    else
        echo "abi_differential: REGRESSION on fixture $name ($target)" >&2
        cat "$fixture" >&2
        failed=$((failed + 1))
    fi
done

seed=$FIRST
end=$((FIRST + COUNT))
while [ "$seed" -lt "$end" ]; do
    d=$WORK/s$seed
    mkdir -p "$d"
    "$ABIGEN" --seed "$seed" >"$d/desc.txt"
    if check_desc "$d"; then
        checked=$((checked + 1))
        seed=$((seed + 1))
        continue
    fi
    echo "abi_differential: DISAGREEMENT at seed $seed ($target)" >&2
    echo "--- signature:" >&2
    cat "$d/desc.txt" >&2
    minimize "$d"
    echo "--- 1-minimal:" >&2
    cat "$d/minimal.txt" >&2
    out=$REPRO/${target}-seed$seed
    mkdir -p "$out"
    cp "$d/minimal.txt" "$out/desc.txt"
    "$ABIGEN" --emit "$out/desc.txt" --out "$out" >/dev/null 2>&1 || true
    echo "abi_differential: reproducer written to $out" >&2
    failed=$((failed + 1))
    seed=$((seed + 1))
done

if [ "$failed" -ne 0 ]; then
    echo "abi_differential: $failed of $((checked + failed)) signatures disagree" >&2
    exit 1
fi
echo "abi_differential: $checked signatures agree with $REF on $target," \
    "both directions"
