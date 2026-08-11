#!/bin/sh
# Deterministic Sprint 53 kernel measurement and regression gate.
#
# Usage:
#   kernel-static.sh COMPILER TARGET RESULT GOLDEN
#   kernel-static.sh --gate GOLDEN RESULT
#
# TARGET is x86_64-linux-gnu or arm64-linux.  Normal mode compiles every
# tests/bench/kernels/*.c at -O2, measures only the global kernel_run symbol,
# writes RESULT, then compares it with GOLDEN.  CGF_UPDATE_GOLDEN=1 replaces
# GOLDEN with RESULT instead; this is the only update path.
# CGF_KERNEL_MEASURE_ONLY=1 writes without comparing.  Gate-only callers may
# select CGF_KERNEL_GATE_KIND=icount or text; the default checks both.
set -eu

LC_ALL=C
export LC_ALL

prog=kernel_static
gate_kind=${CGF_KERNEL_GATE_KIND:-all}
measure_only=${CGF_KERNEL_MEASURE_ONLY:-0}

die() {
    echo "$prog: $*" >&2
    exit 3
}

gate_metrics() {
    gate_golden=$1
    gate_result=$2

    for gate_file in "$gate_golden" "$gate_result"; do
        [ -r "$gate_file" ] || die "cannot read $gate_file"
    done

    set +e
    awk -v baseline_file="$gate_golden" -v gate_kind="$gate_kind" '
function'" "'schema_fail(message) {
    print "kernel_static: " message > "/dev/stderr"
    schema_failed = 1
}

function'" "'regression(message) {
    print "kernel_static: " message > "/dev/stderr"
    regressed = 1
}

function'" "'trim(value) {
    sub(/^[[:space:]]+/, "", value)
    sub(/[[:space:]]+$/, "", value)
    return value
}

function'" "'read_metric(line, file, line_no,    equals, key, value, name, kind) {
    sub(/[[:space:]]*#.*/, "", line)
    line = trim(line)
    if (line == "")
        return

    equals = index(line, "=")
    if (!equals) {
        schema_fail(file ":" line_no ": expected metric=value")
        return
    }
    key = trim(substr(line, 1, equals - 1))
    value = trim(substr(line, equals + 1))
    if (key !~ /^[A-Za-z0-9_-]+[.](icount|padding|text)$/) {
        schema_fail(file ":" line_no ": invalid kernel metric " key)
        return
    }
    if (value !~ /^[0-9]+$/) {
        schema_fail(file ":" line_no ": metric " key " must be a non-negative integer")
        return
    }

    name = key
    sub(/[.](icount|padding|text)$/, "", name)
    kind = key
    sub(/^.*[.]/, "", kind)

    if (file == baseline_file) {
        if (key in baseline)
            schema_fail(file ":" line_no ": duplicate metric " key)
        baseline[key] = value
        baseline_kernel[name] = 1
        baseline_kind[name SUBSEP kind] = 1
    } else {
        if (key in current)
            schema_fail(file ":" line_no ": duplicate metric " key)
        current[key] = value
        current_kernel[name] = 1
        current_kind[name SUBSEP kind] = 1
    }
}

{
    read_metric($0, FILENAME, FNR)
}

END {
    for (key in baseline) {
        if (!(key in current))
            schema_fail("missing result metric " key)
    }
    for (key in current) {
        if (!(key in baseline))
            schema_fail("unexpected result metric " key)
    }

    for (name in baseline_kernel) {
        kernels++
        for (kind_index = 1; kind_index <= 3; kind_index++) {
            kind = kind_index == 1 ? "icount" : \
                   (kind_index == 2 ? "padding" : "text")
            if (!baseline_kind[name SUBSEP kind])
                schema_fail("baseline kernel " name " lacks ." kind)
            if (!current_kind[name SUBSEP kind])
                schema_fail("result kernel " name " lacks ." kind)
        }

        key = name ".icount"
        before = baseline[key] + 0
        after = current[key] + 0
        if (before <= 0)
            schema_fail("baseline metric " key " must be greater than zero")
        if (after <= 0)
            schema_fail("result metric " key " must be greater than zero")
        text_key = name ".text"
        text_before = baseline[text_key] + 0
        text_after = current[text_key] + 0
        if (text_before <= 0)
            schema_fail("baseline metric " text_key " must be greater than zero")
        if (text_after <= 0)
            schema_fail("result metric " text_key " must be greater than zero")

        delta = after - before
        # "Above golden by more than max(2%, 2 instructions)" means the
        # increase must exceed BOTH thresholds.  Cross multiplication avoids
        # any host awk rounding or ceil/floor convention.
        if (gate_kind == "all" || gate_kind == "icount") {
            if (delta > 2 && delta * 100 > before * 2) {
                regression(key " regressed: baseline=" before " result=" after \
                     " (increase exceeds max(2%,2))")
            } else {
                passed++
            }
        }

        # Text size is a separate Sprint 54 gate: instruction counts do not
        # expose padding, relaxation, or encoding-width growth.  Exact +5%
        # is the boundary and therefore passes.
        text_delta = text_after - text_before
        if (gate_kind == "all" || gate_kind == "text") {
            if (text_delta * 100 > text_before * 5) {
                regression(text_key " regressed: baseline=" text_before \
                     " result=" text_after " (increase exceeds 5%)")
            } else {
                passed++
            }
        }
    }

    for (name in current_kernel) {
        if (!(name in baseline_kernel))
            schema_fail("unexpected result kernel " name)
    }

    if (!kernels)
        schema_fail("baseline contains no kernels")
    if (schema_failed)
        exit 3
    if (regressed)
        exit 1
    print "kernel_static: gate pass (" passed " comparisons)"
}
' "$gate_golden" "$gate_result"
    gate_status=$?
    set -e

    case "$gate_status" in
    0|1|3) return "$gate_status" ;;
    *) die "metric parser failed with status $gate_status" ;;
    esac
}

case "$gate_kind" in
all|icount|text) ;;
*) die "CGF_KERNEL_GATE_KIND must be all, icount, or text" ;;
esac
case "$measure_only" in
0|1) ;;
*) die "CGF_KERNEL_MEASURE_ONLY must be 0 or 1" ;;
esac

if [ "${1:-}" = "--gate" ]; then
    [ "$#" -eq 3 ] || die "usage: $0 --gate GOLDEN RESULT"
    gate_metrics "$2" "$3"
    exit 0
fi

[ "$#" -eq 4 ] || die "usage: $0 COMPILER TARGET RESULT GOLDEN"

compiler=$1
target=$2
result=$3
golden=$4
repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
kernel_dir=${CGF_KERNEL_DIR:-$repo/tests/bench/kernels}
work=${CGF_KERNEL_WORK:-$repo/build/kernel-static/$target}
update=${CGF_UPDATE_GOLDEN:-0}

case "$update" in
0|1) ;;
*) die "CGF_UPDATE_GOLDEN must be 0 or 1" ;;
esac

[ -x "$compiler" ] || die "compiler is not executable: $compiler"
[ -d "$kernel_dir" ] || die "kernel directory does not exist: $kernel_dir"

case "$target" in
x86_64-linux-gnu)
    objdump=${CGF_KERNEL_OBJDUMP:-objdump}
    readelf=${CGF_KERNEL_READELF:-readelf}
    kernel_as=${CGF_KERNEL_AS:-as}
    CGF_AS_PATH=$(command -v "$kernel_as" 2>/dev/null || true)
    [ -n "$CGF_AS_PATH" ] || die "assembler not found: $kernel_as"
    export CGF_AS_PATH
    ;;
arm64-linux)
    objdump=${CGF_KERNEL_OBJDUMP:-aarch64-linux-gnu-objdump}
    readelf=${CGF_KERNEL_READELF:-aarch64-linux-gnu-readelf}
    kernel_as=${CGF_KERNEL_AS:-aarch64-linux-gnu-as}
    CGF_AS_PATH=$(command -v "$kernel_as" 2>/dev/null || true)
    [ -n "$CGF_AS_PATH" ] || die "assembler not found: $kernel_as"
    export CGF_AS_PATH
    ;;
*)
    die "unsupported target '$target'"
    ;;
esac

command -v "$objdump" >/dev/null 2>&1 || die "objdump not found: $objdump"
command -v "$readelf" >/dev/null 2>&1 || die "readelf not found: $readelf"

mkdir -p "$work" "$(dirname "$result")" ||
    die "cannot create work or result directory"
metrics_tmp=$(mktemp "$work/metrics.XXXXXX") || die "cannot create temporary metrics file"
trap 'rm -f "$metrics_tmp"' EXIT HUP INT TERM

printf '# cgfried kernel static metrics v1\n' >"$metrics_tmp" ||
    die "cannot write temporary metrics file"
printf '# target=%s\n' "$target" >>"$metrics_tmp" ||
    die "cannot write temporary metrics file"

kernel_count=0
for source in "$kernel_dir"/*.c; do
    [ -f "$source" ] || continue
    name=${source##*/}
    name=${name%.c}
    case "$name" in
    *[!A-Za-z0-9_-]*) die "invalid kernel filename: ${source##*/}" ;;
    esac

    object=$work/$name.o
    if ! "$compiler" --target="$target" -std=gnu17 -O2 -c \
        -o "$object" "$source" >"$work/$name.cc.out" \
        2>"$work/$name.cc.err"; then
        echo "$prog: $name: compilation failed" >&2
        sed 's/^/  /' "$work/$name.cc.err" >&2
        exit 3
    fi

    symbol=$(
        "$readelf" -sW "$object" |
            awk '$4 == "FUNC" && $7 != "UND" && $8 == "kernel_run" {
                     count++; size = $3
                 }
                 END { print count + 0, size + 0 }'
    ) || die "$name: cannot read symbol table"
    symbol_count=${symbol%% *}
    symbol_size=${symbol#* }
    [ "$symbol_count" -eq 1 ] ||
        die "$name: expected exactly one defined FUNC symbol named kernel_run"
    [ "$symbol_size" -gt 0 ] || die "$name: kernel_run has zero size"

    counts=$(
        "$objdump" -d --no-show-raw-insn --disassemble=kernel_run "$object" |
            awk '
/^[[:space:]]*[[:xdigit:]]+:[[:space:]]+/ {
    decoded++
    instruction = $0
    sub(/^[[:space:]]*[[:xdigit:]]+:[[:space:]]+/, "", instruction)
    fields = split(instruction, word, /[[:space:]]+/)
    is_nop = 0
    for (i = 1; i <= fields; i++) {
        if (word[i] ~ /^nop[bwlq]?$/)
            is_nop = 1
    }
    if (instruction ~ /^xchg[[:space:]]+%ax,%ax$/)
        is_nop = 1
    if (is_nop)
        padding++
    else
        icount++
}
END {
    if (!decoded)
        exit 2
    print icount + 0, padding + 0, decoded + 0
}'
    ) || die "$name: objdump produced no decoded kernel_run instructions"
    icount=${counts%% *}
    counts_tail=${counts#* }
    padding=${counts_tail%% *}
    decoded=${counts_tail#* }
    [ $((icount + padding)) -eq "$decoded" ] ||
        die "$name: internal instruction accounting mismatch"

    text_hex=$(
        "$readelf" -SW "$object" |
            awk '{
                     for (i = 1; i <= NF; i++) {
                         if ($i == ".text") {
                             print $(i + 4)
                             found++
                         }
                     }
                 }
                 END { if (found != 1) exit 2 }'
    ) || die "$name: expected exactly one .text section"
    case "$text_hex" in
    ''|*[!0-9A-Fa-f]*) die "$name: malformed .text size '$text_hex'" ;;
    esac
    text_size=$((0x$text_hex))
    [ "$text_size" -gt 0 ] || die "$name: .text section has zero size"

    {
        printf '%s.icount=%s\n' "$name" "$icount"
        printf '%s.padding=%s\n' "$name" "$padding"
        printf '%s.text=%s\n' "$name" "$text_size"
    } >>"$metrics_tmp" || die "cannot write temporary metrics file"
    kernel_count=$((kernel_count + 1))
done

[ "$kernel_count" -ge 19 ] ||
    die "expected at least 19 kernels, found $kernel_count in $kernel_dir"

mv "$metrics_tmp" "$result" || die "cannot write result $result"
trap - EXIT HUP INT TERM

if [ "$update" -eq 1 ]; then
    mkdir -p "$(dirname "$golden")" ||
        die "cannot create golden directory"
    cp "$result" "$golden" || die "cannot update golden $golden"
    echo "$prog: updated $golden ($kernel_count kernels)"
    exit 0
fi

if [ "$measure_only" -eq 1 ]; then
    echo "$prog: wrote $result ($kernel_count kernels; gate deferred)"
    exit 0
fi

gate_metrics "$golden" "$result"
