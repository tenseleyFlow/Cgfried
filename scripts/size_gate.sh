#!/bin/sh
# Deterministic Sprint 54 executable-size measurement and regression gate.
#
# Usage:
#   size_gate.sh COMPILER TARGET RESULT BASELINE
#   size_gate.sh --measure COMPILER TARGET RESULT
#   size_gate.sh --gate BASELINE RESULT
#
# Normal and --measure modes build every CGF_SIZE_KERNEL_DIR/*.c at -O2 and
# -Os, measure the compiler itself, and write a flat metric file.  Only the
# post-strip *.size values gate; *.size_unstripped and section sizes are
# report-only.  Tool paths and flags are overrideable with CGF_SIZE_{SIZE,
# STRIP,SELF_SIZE,SELF_STRIP,AS,LD,CRT_DIR,SYSROOT,CFLAGS,STRIP_FLAGS} for
# cross and fixture lanes. CGF_SIZE_RUNNER may prefix a foreign compiler with
# qemu-user. SELF_* tools process the compiler binary's architecture. Header
# provenance is overrideable with CGF_SIZE_{HOST,HOST_ARCH,HOST_CLASS,
# DATE_UTC,REV,TREE_STATE,COMPILER_ID,CORPUS_ID} for reproducible baselines.
set -eu

LC_ALL=C
export LC_ALL

prog=size_gate
gate_kind=${CGF_SIZE_GATE_KIND:-all}

die()
{
    echo "$prog: $*" >&2
    exit 3
}

case $gate_kind in
all | program | self) ;;
*) die "CGF_SIZE_GATE_KIND must be all, program, or self" ;;
esac

gate_metrics()
{
    gate_baseline=$1
    gate_result=$2

    for gate_file in "$gate_baseline" "$gate_result"; do
        [ -f "$gate_file" ] && [ -r "$gate_file" ] ||
            die "cannot read $gate_file"
    done

    gate_status=0
    awk -v gate_kind="$gate_kind" '
function'" "'schema_fail(message) {
    print "size_gate: " message > "/dev/stderr"
    bad_schema = 1
}

function'" "'regression(message) {
    print "size_gate: " message > "/dev/stderr"
    regressed = 1
}

function'" "'trim(value) {
    sub(/^[[:space:]]+/, "", value)
    sub(/[[:space:]]+$/, "", value)
    return value
}

function'" "'read_header(line, file, line_no, is_baseline,
                         content, equals, key, value) {
    content = line
    sub(/^[[:space:]]*#[[:space:]]*/, "", content)
    equals = index(content, "=")
    if (!equals)
        return
    key = trim(substr(content, 1, equals - 1))
    if (key != "target" && key != "corpus" && key != "corpus_count" &&
        key != "protocol" && key != "compiler_flags")
        return
    value = trim(substr(content, equals + 1))
    if (value == "") {
        schema_fail(file ":" line_no ": empty provenance header " key)
        return
    }
    if (key == "corpus_count" && value !~ /^[1-9][0-9]*$/) {
        schema_fail(file ":" line_no ": corpus_count must be a positive integer")
        return
    }
    if (is_baseline) {
        if (key in baseline_header)
            schema_fail(file ":" line_no ": duplicate provenance header " key)
        baseline_header[key] = value
    } else {
        if (key in current_header)
            schema_fail(file ":" line_no ": duplicate provenance header " key)
        current_header[key] = value
    }
}

function'" "'read_metric(line, file, line_no, is_baseline,
                     equals, key, value, stem, kind) {
    if (line ~ /^[[:space:]]*#/) {
        read_header(line, file, line_no, is_baseline)
        return
    }
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
    if (key !~ /^([A-Za-z0-9_-]+[.](O2|Os)|cgf)[.](size|size_unstripped|text|data|rodata)$/) {
        schema_fail(file ":" line_no ": invalid size metric " key)
        return
    }
    if (value !~ /^[0-9]+$/) {
        schema_fail(file ":" line_no ": metric " key \
                    " must be a non-negative integer")
        return
    }

    stem = key
    sub(/[.](size|size_unstripped|text|data|rodata)$/, "", stem)
    kind = key
    sub(/^.*[.]/, "", kind)
    if (is_baseline) {
        if (key in baseline)
            schema_fail(file ":" line_no ": duplicate metric " key)
        baseline[key] = value
        baseline_stem[stem] = 1
        baseline_kind[stem SUBSEP kind] = 1
    } else {
        if (key in current)
            schema_fail(file ":" line_no ": duplicate metric " key)
        current[key] = value
        current_stem[stem] = 1
        current_kind[stem SUBSEP kind] = 1
    }
}

{
    # NR==FNR identifies the first argv input even when callers deliberately
    # compare a file with itself.  Comparing FILENAME strings cannot do that.
    read_metric($0, FILENAME, FNR, NR == FNR)
}

END {
    split("target corpus corpus_count protocol compiler_flags", headers, " ")
    for (header_no = 1; header_no <= 5; header_no++) {
        header = headers[header_no]
        if (!(header in baseline_header))
            schema_fail("baseline lacks provenance header " header)
        if (!(header in current_header))
            schema_fail("result lacks provenance header " header)
        if ((header in baseline_header) && (header in current_header) &&
            baseline_header[header] != current_header[header])
            schema_fail("provenance header " header " differs: baseline=" \
                        baseline_header[header] " result=" current_header[header])
    }

    for (key in baseline) {
        if (!(key in current))
            schema_fail("missing result metric " key)
    }
    for (key in current) {
        if (!(key in baseline))
            schema_fail("unexpected result metric " key)
    }

    for (stem in baseline_stem) {
        stems++
        split("size size_unstripped text data rodata", kinds, " ")
        for (kind_no = 1; kind_no <= 5; kind_no++) {
            kind = kinds[kind_no]
            if (!baseline_kind[stem SUBSEP kind])
                schema_fail("baseline entry " stem " lacks ." kind)
            if (!current_kind[stem SUBSEP kind])
                schema_fail("result entry " stem " lacks ." kind)
        }

        key = stem ".size"
        before = baseline[key] + 0
        after = current[key] + 0
        if (before <= 0)
            schema_fail("baseline metric " key " must be greater than zero")
        if (after <= 0)
            schema_fail("result metric " key " must be greater than zero")
        # Exact +15% passes.  Integer cross multiplication avoids host awk
        # floating-point and rounding differences.
        selected = gate_kind == "all" ||
                   (gate_kind == "self" && stem == "cgf") ||
                   (gate_kind == "program" && stem != "cgf")
        if (selected) {
            if (before > 0 && after > 0 && after * 100 > before * 115)
                regression(key " regressed: baseline=" before " result=" after \
                           " limit=+15%")
            else if (before > 0 && after > 0)
                passed++
        }
    }
    for (stem in current_stem) {
        if (!(stem in baseline_stem))
            schema_fail("unexpected result entry " stem)
    }
    if (!stems)
        schema_fail("baseline contains no size entries")

    if (bad_schema)
        exit 3
    if (regressed)
        exit 1
    print "size_gate: gate pass (" passed " stripped sizes)"
}
' "$gate_baseline" "$gate_result" || gate_status=$?
    case $gate_status in
    0 | 1 | 3) return "$gate_status" ;;
    *) die "cannot process size metric files" ;;
    esac
}

mode=measure_and_gate
case ${1:-} in
--gate)
    [ "$#" -eq 3 ] || die "usage: $0 --gate BASELINE RESULT"
    gate_metrics "$2" "$3"
    exit 0
    ;;
--measure)
    [ "$#" -eq 4 ] || die "usage: $0 --measure COMPILER TARGET RESULT"
    mode=measure
    shift
    ;;
*)
    [ "$#" -eq 4 ] ||
        die "usage: $0 COMPILER TARGET RESULT BASELINE"
    ;;
esac

compiler=$1
target=$2
result=$3
baseline=${4:-}
repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
kernel_dir=${CGF_SIZE_KERNEL_DIR:-$repo/tests/bench/kernels}
work=${CGF_SIZE_WORK:-$repo/build/size-gate/$target}
minimum=${CGF_SIZE_MIN_KERNELS:-19}
size_flags=${CGF_SIZE_SIZE_FLAGS:--A -d}
strip_flags=${CGF_SIZE_STRIP_FLAGS:---strip-all}
extra_cflags=${CGF_SIZE_CFLAGS:-}
compiler_runner=${CGF_SIZE_RUNNER:-}

case $minimum in
'' | *[!0-9]*) die "CGF_SIZE_MIN_KERNELS must be a positive integer" ;;
0) die "CGF_SIZE_MIN_KERNELS must be greater than zero" ;;
esac
[ -x "$compiler" ] || die "compiler is not executable: $compiler"
[ -d "$kernel_dir" ] || die "kernel directory does not exist: $kernel_dir"

host=$(uname -m 2>/dev/null || echo unknown)
case $target in
x86_64-linux-gnu)
    size_tool=${CGF_SIZE_SIZE:-size}
    strip_tool=${CGF_SIZE_STRIP:-strip}
    as_tool=${CGF_SIZE_AS:-as}
    ld_tool=${CGF_SIZE_LD:-ld}
    sysroot=${CGF_SIZE_SYSROOT:-}
    ;;
arm64-linux)
    case $host in
    aarch64 | arm64)
        size_tool=${CGF_SIZE_SIZE:-size}
        strip_tool=${CGF_SIZE_STRIP:-strip}
        as_tool=${CGF_SIZE_AS:-as}
        ld_tool=${CGF_SIZE_LD:-ld}
        sysroot=${CGF_SIZE_SYSROOT:-}
        ;;
    *)
        size_tool=${CGF_SIZE_SIZE:-aarch64-linux-gnu-size}
        strip_tool=${CGF_SIZE_STRIP:-aarch64-linux-gnu-strip}
        as_tool=${CGF_SIZE_AS:-aarch64-linux-gnu-as}
        ld_tool=${CGF_SIZE_LD:-aarch64-linux-gnu-ld}
        if [ "${CGF_SIZE_SYSROOT+x}" = x ]; then
            sysroot=$CGF_SIZE_SYSROOT
        else
            sysroot=/usr/aarch64-linux-gnu
        fi
        ;;
    esac
    ;;
*) die "unsupported target '$target'" ;;
esac

resolve_tool()
{
    resolved=$(command -v "$1" 2>/dev/null || true)
    [ -n "$resolved" ] || die "required tool not found: $1"
    printf '%s\n' "$resolved"
}

canonical_file()
{
    canonical_input=$1
    case $canonical_input in
    */*) ;;
    *) canonical_input=$(resolve_tool "$canonical_input") ;;
    esac
    canonical_dir=$(CDPATH='' cd "$(dirname "$canonical_input")" && pwd -P) ||
        die "cannot resolve path: $1"
    printf '%s/%s\n' "$canonical_dir" "$(basename "$canonical_input")"
}

size_tool=$(resolve_tool "$size_tool")
strip_tool=$(resolve_tool "$strip_tool")
self_size_tool=$(resolve_tool "${CGF_SIZE_SELF_SIZE:-size}")
self_strip_tool=$(resolve_tool "${CGF_SIZE_SELF_STRIP:-strip}")
CGF_AS_PATH=$(resolve_tool "$as_tool")
CGF_LD_PATH=$(resolve_tool "$ld_tool")
export CGF_AS_PATH CGF_LD_PATH
if [ -n "${CGF_SIZE_CRT_DIR:-}" ]; then
    CGF_CRT_DIR=$CGF_SIZE_CRT_DIR
    export CGF_CRT_DIR
elif [ -n "$sysroot" ]; then
    CGF_CRT_DIR=$sysroot/lib
    export CGF_CRT_DIR
fi

mkdir -p "$work" "$(dirname "$result")" || die "cannot create output directory"
manifest=$work/kernels.txt
find "$kernel_dir" -maxdepth 1 -type f -name '*.c' -print | sort >"$manifest" ||
    die "cannot enumerate kernels"
kernel_count=$(wc -l <"$manifest" | tr -d ' ')
[ "$kernel_count" -ge "$minimum" ] ||
    die "expected at least $minimum kernels, found $kernel_count in $kernel_dir"
host_name=${CGF_SIZE_HOST:-$(hostname -s 2>/dev/null || uname -n)}
host_arch=${CGF_SIZE_HOST_ARCH:-$host}
host_class=${CGF_SIZE_HOST_CLASS:-local}
date_utc=${CGF_SIZE_DATE_UTC:-$(date -u '+%Y-%m-%dT%H:%M:%SZ')}
if [ -n "${CGF_SIZE_REV:-}" ]; then
    cgf_rev=$CGF_SIZE_REV
    tree_state=${CGF_SIZE_TREE_STATE:-exported-commit}
elif git -C "$repo" rev-parse HEAD >/dev/null 2>&1; then
    cgf_rev=$(git -C "$repo" rev-parse HEAD)
    if [ -n "${CGF_SIZE_TREE_STATE:-}" ]; then
        tree_state=$CGF_SIZE_TREE_STATE
    elif [ -n "$(git -C "$repo" status --porcelain --untracked-files=normal)" ]; then
        tree_state=dirty
    else
        tree_state=clean
    fi
else
    cgf_rev=unknown
    tree_state=${CGF_SIZE_TREE_STATE:-unavailable}
fi
compiler_path=$(canonical_file "$compiler")
if [ -n "${CGF_SIZE_COMPILER_ID:-}" ]; then
    compiler_id=$CGF_SIZE_COMPILER_ID
else
    if [ -n "$compiler_runner" ]; then
        # Deliberate splitting: same documented runner option list used for
        # compilation below.
        # shellcheck disable=SC2086
        set -- $compiler_runner "$compiler" --version
    else
        set -- "$compiler" --version
    fi
    compiler_identity_output=$("$@" 2>&1) ||
        die "cannot identify compiler: $compiler"
    compiler_id=$(printf '%s\n' "$compiler_identity_output" | sed -n '1p')
    [ -n "$compiler_id" ] || die "compiler identity is empty: $compiler"
fi
corpus_id=${CGF_SIZE_CORPUS_ID:-sprint-53-kernels}

metrics_tmp=$(mktemp "$work/metrics.XXXXXX") ||
    die "cannot create temporary metrics file"
trap 'rm -f "$metrics_tmp"' EXIT HUP INT TERM
{
    echo '# cgfried binary size metrics v1'
    echo "# target=$target"
    echo "# host=$host_name"
    echo "# host_arch=$host_arch"
    echo "# host_class=$host_class"
    echo "# date=$date_utc"
    echo "# cgf_rev=$cgf_rev"
    echo "# cgf_tree=$tree_state"
    echo "# compiler_path=$compiler_path"
    echo "# compiler_id=$compiler_id"
    echo "# compiler_runner=${compiler_runner:-none}"
    echo "# size_tool=$size_tool"
    echo "# strip_tool=$strip_tool"
    echo "# self_size_tool=$self_size_tool"
    echo "# self_strip_tool=$self_strip_tool"
    echo "# as_tool=$CGF_AS_PATH"
    echo "# ld_tool=$CGF_LD_PATH"
    echo "# sysroot=${sysroot:-none}"
    echo "# corpus=$corpus_id"
    echo "# corpus_count=$kernel_count"
    echo "# size_flags=$size_flags"
    echo "# strip_flags=$strip_flags"
    echo "# compiler_flags=${extra_cflags:-none}"
    echo '# protocol=opts=O2,Os;whole-file-after-strip-gate=+15%;unstripped-and-sections=report-only'
} >"$metrics_tmp" || die "cannot write temporary metrics file"

file_bytes()
{
    bytes=$(wc -c <"$1" | tr -d ' ') || die "cannot measure $1"
    case $bytes in
    '' | *[!0-9]*) die "invalid byte count for $1" ;;
    esac
    printf '%s\n' "$bytes"
}

section_metrics()
{
    section_file=$1
    section_tool=$2
    # Deliberate splitting: the documented override is a whitespace-separated
    # option list for size(1), not arbitrary shell syntax.
    # shellcheck disable=SC2086
    section_output=$($section_tool $size_flags "$section_file") ||
        die "size tool failed for $section_file"
    printf '%s\n' "$section_output" | awk '
BEGIN { wanted[".text"] = 1; wanted[".data"] = 1; wanted[".rodata"] = 1 }
$1 in wanted {
    if ($2 !~ /^[0-9]+$/ || seen[$1]++)
        bad = 1
    value[$1] = $2
}
END {
    if (bad)
        exit 3
    print (".text" in value ? value[".text"] : 0), \
          (".data" in value ? value[".data"] : 0), \
          (".rodata" in value ? value[".rodata"] : 0)
}' || die "malformed section report for $section_file"
}

record_binary()
{
    record_stem=$1
    record_file=$2
    record_size_tool=$3
    record_strip_tool=$4
    unstripped=$(file_bytes "$record_file")
    sections=$(section_metrics "$record_file" "$record_size_tool")
    text_size=${sections%% *}
    section_tail=${sections#* }
    data_size=${section_tail%% *}
    rodata_size=${section_tail#* }
    stripped=$work/$record_stem.stripped
    cp "$record_file" "$stripped" || die "cannot copy $record_file"
    # Deliberate splitting: the documented override is a whitespace-separated
    # option list for strip(1), not arbitrary shell syntax.
    # shellcheck disable=SC2086
    $record_strip_tool $strip_flags "$stripped" ||
        die "strip failed for $record_file"
    stripped_size=$(file_bytes "$stripped")
    {
        printf '%s.size=%s\n' "$record_stem" "$stripped_size"
        printf '%s.size_unstripped=%s\n' "$record_stem" "$unstripped"
        printf '%s.text=%s\n' "$record_stem" "$text_size"
        printf '%s.data=%s\n' "$record_stem" "$data_size"
        printf '%s.rodata=%s\n' "$record_stem" "$rodata_size"
    } >>"$metrics_tmp" || die "cannot write temporary metrics file"
}

record_binary cgf "$compiler" "$self_size_tool" "$self_strip_tool"

while IFS= read -r source; do
    name=${source##*/}
    name=${name%.c}
    case $name in
    '' | *[!A-Za-z0-9_-]*) die "invalid kernel filename: ${source##*/}" ;;
    esac
    for opt in O2 Os; do
        output=$work/$name.$opt
        compile_log=$work/$name.$opt.err
        set -- "$compiler" "--target=$target" -std=gnu17 "-$opt"
        if [ -n "$sysroot" ]; then
            set -- "$@" -isystem "$sysroot/include"
        fi
        if [ -n "$extra_cflags" ]; then
            # Deliberate splitting: CGF_SIZE_CFLAGS is a documented
            # whitespace-separated compiler option list.
            # shellcheck disable=SC2086
            set -- "$@" $extra_cflags
        fi
        set -- "$@" -o "$output" "$source"
        if [ -n "$compiler_runner" ]; then
            # Deliberate splitting: the runner is a documented executable
            # plus option list, such as qemu-aarch64-static -L SYSROOT.
            # shellcheck disable=SC2086
            set -- $compiler_runner "$@"
        fi
        if ! "$@" >"$work/$name.$opt.out" 2>"$compile_log"; then
            echo "$prog: $name -$opt: compilation failed" >&2
            if [ -r "$compile_log" ]; then
                sed -n '1,20{s/^/  /;p;}' "$compile_log" >&2 || true
            fi
            exit 3
        fi
        [ -f "$output" ] || die "$name -$opt: compiler produced no output"
        record_binary "$name.$opt" "$output" "$size_tool" "$strip_tool"
    done
done <"$manifest"

mv "$metrics_tmp" "$result" || die "cannot install result $result"
trap - EXIT HUP INT TERM

if [ "$mode" = measure ]; then
    echo "$prog: wrote $result ($kernel_count kernels, $((kernel_count * 2 + 1)) binaries)"
else
    gate_metrics "$baseline" "$result"
fi
