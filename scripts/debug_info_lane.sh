#!/bin/sh
# Sprint 29: DWARF-v4 line information and always-on .eh_frame CFI.
# System gas is the baseline lane.  Bundled afs-as, afs-ld, gcc, and gdb
# are additional compatibility or differential lanes and skip loudly only
# when the corresponding tool is absent (or ptrace is unavailable for gdb).
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_DEBUG_WORK:-build/debug-info-lane}
SRC=tests/debug/dwarf_lines.c
AFS_AS=${CGF_DEBUG_AFS_AS:-afs-as/target/release/afs-as}
AFS_LD=${CGF_DEBUG_AFS_LD:-afs-ld/target/release/afs-ld}

if ! command -v readelf >/dev/null 2>&1 ||
    ! command -v addr2line >/dev/null 2>&1; then
    echo 'HARNESS_SKIP suite=debug-info test=all count=1 reason="readelf or addr2line not found"'
    exit 0
fi

case "$WORK" in
''|/|.)
    echo "debug_info lane FAIL: unsafe work directory '$WORK'" >&2
    exit 1
    ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

fails=0
checks=0

fail()
{
    echo "debug_info lane FAIL: $*" >&2
    fails=$((fails + 1))
}

has_section()
{
    readelf -SW "$1" 2>/dev/null |
        grep -Eq "[[:space:]]\\.$2[[:space:]]"
}

require_section()
{
    checks=$((checks + 1))
    has_section "$1" "$2" || fail "$(basename "$1") missing .$2"
}

forbid_section()
{
    checks=$((checks + 1))
    if has_section "$1" "$2"; then
        fail "$(basename "$1") unexpectedly contains .$2"
    fi
}

forbid_compressed_section()
{
    obj=$1
    name=$2
    flags=$(readelf -SW "$obj" 2>/dev/null |
        awk -v wanted=".$name" '{
            for (i = 1; i <= NF; i++)
                if ($i == wanted) { print $(i + 6); exit }
        }')
    checks=$((checks + 1))
    case "$flags" in
    *C*) fail "$(basename "$obj") has SHF_COMPRESSED .$name" ;;
    esac
}

check_debug_object()
{
    obj=$1
    require_section "$obj" debug_line
    require_section "$obj" debug_info
    require_section "$obj" debug_abbrev
    require_section "$obj" eh_frame
    forbid_compressed_section "$obj" debug_line
    forbid_compressed_section "$obj" debug_info
    forbid_compressed_section "$obj" debug_abbrev
    if ! readelf --debug-dump=rawline,info,abbrev,frames "$obj" \
        > "$obj.readelf" 2> "$obj.readelf.err"; then
        fail "readelf rejected $(basename "$obj")"
    elif [ -s "$obj.readelf.err" ]; then
        fail "readelf diagnosed $(basename "$obj"): $(tr '\n' ' ' < "$obj.readelf.err")"
    fi
    checks=$((checks + 1))
}

check_nodebug_object()
{
    obj=$1
    forbid_section "$obj" debug_line
    forbid_section "$obj" debug_info
    forbid_section "$obj" debug_abbrev
    require_section "$obj" eh_frame
}

# All accepted -g levels mean line information in v0.1.0.
for level in g g1 g2 g3; do
    obj="$WORK/gas-$level.o"
    if ! CGF_AS=0 "$CGF" "-$level" -c "$SRC" -o "$obj" \
        2> "$obj.cgf.err"; then
        fail "system-gas compile failed for -$level"
        continue
    fi
    check_debug_object "$obj"
done

for mode in none g0; do
    obj="$WORK/gas-$mode.o"
    if [ "$mode" = none ]; then
        CGF_AS=0 "$CGF" -c "$SRC" -o "$obj" 2> "$obj.cgf.err"
    else
        CGF_AS=0 "$CGF" -g0 -c "$SRC" -o "$obj" 2> "$obj.cgf.err"
    fi
    status=$?
    if [ "$status" -ne 0 ]; then
        fail "system-gas compile failed for $mode"
        continue
    fi
    check_nodebug_object "$obj"
done

# Determinism is an object-byte contract, including paths and timestamps.
for n in 1 2; do
    if ! CGF_AS=0 "$CGF" -g -c "$SRC" -o "$WORK/deterministic-$n.o" \
        2> "$WORK/deterministic-$n.err"; then
        fail "determinism build $n failed"
    fi
done
checks=$((checks + 1))
cmp -s "$WORK/deterministic-1.o" "$WORK/deterministic-2.o" ||
    fail "two identical -g builds differ"

# DW_AT_comp_dir preserves a validated logical PWD rather than silently
# canonicalizing symlink components. The input and output remain absolute so
# this subshell changes only the cwd spelling under test.
ROOT=$(pwd)
case "$CGF" in
/*) CGF_ABS=$CGF ;;
*) CGF_ABS=$ROOT/$CGF ;;
esac
mkdir -p "$WORK/physical-cwd"
ln -s physical-cwd "$WORK/logical-cwd"
logical_cwd=$ROOT/$WORK/logical-cwd
logical_obj=$ROOT/$WORK/logical-cwd.o
if (cd "$logical_cwd" && PWD=$logical_cwd CGF_AS=0 "$CGF_ABS" -g -c \
    "$ROOT/$SRC" -o "$logical_obj") 2> "$WORK/logical-cwd.err"; then
    readelf --debug-dump=info "$WORK/logical-cwd.o" \
        > "$WORK/logical-cwd.info" 2> "$WORK/logical-cwd.readelf.err"
    checks=$((checks + 1))
    grep -Fq "DW_AT_comp_dir    : $logical_cwd" "$WORK/logical-cwd.info" ||
        fail "DW_AT_comp_dir did not preserve validated logical PWD"
else
    fail "logical-PWD debug compile failed"
fi

# A missing cwd is relevant only when debug metadata actually needs a
# compilation directory. Absolute no-debug/-g0 compilation remains usable;
# -g diagnoses the unavailable directory with the driver's I/O exit code.
for mode in none g0 g; do
    deleted_cwd=$ROOT/$WORK/deleted-cwd-$mode
    mkdir "$deleted_cwd"
    if (cd "$deleted_cwd" && rmdir "$deleted_cwd" &&
        case "$mode" in
        none) "$CGF_ABS" -S "$ROOT/$SRC" \
            -o "$ROOT/$WORK/deleted-$mode.s" ;;
        *) "$CGF_ABS" "-$mode" -S "$ROOT/$SRC" \
            -o "$ROOT/$WORK/deleted-$mode.s" ;;
        esac) > "$WORK/deleted-$mode.out" 2> "$WORK/deleted-$mode.err"; then
        status=0
    else
        status=$?
    fi
    checks=$((checks + 1))
    case "$mode:$status" in
    none:0|g0:0) ;;
    g:3)
        grep -q 'cannot determine current directory' \
            "$WORK/deleted-$mode.err" ||
            fail "-g deleted-cwd failure lacked the cwd diagnostic"
        ;;
    *) fail "-$mode deleted-cwd compile exited $status" ;;
    esac
done

# The upstream afs-as named-section and non-alloc-relocation prerequisite.
if [ -x "$AFS_AS" ]; then
    if CGF_AS=1 "$CGF" -g -c "$SRC" \
        -o "$WORK/afs-g.o" 2> "$WORK/afs-g.err"; then
        check_debug_object "$WORK/afs-g.o"
    else
        fail "bundled afs-as rejected debug assembly"
        cat "$WORK/afs-g.err" >&2
    fi
else
    echo 'HARNESS_SKIP suite=debug-info test=afs-as count=1 reason="afs-as not built (make tools)"'
fi

# System-linked products must run at both currently accepted opt surfaces.
for opt in 0 2; do
    exe="$WORK/cgf-O$opt"
    if ! CGF_AS=0 "$CGF" -g "-O$opt" "$SRC" -o "$exe" \
        2> "$exe.err"; then
        fail "system link failed at -O$opt"
        continue
    fi
    "$exe"
    status=$?
    checks=$((checks + 1))
    [ "$status" -eq 0 ] || fail "-O$opt product exited $status"
    check_debug_object "$exe"
done

# Five-plus source rows must exist and round-trip through addr2line.  Marker
# line numbers are derived from the fixture; only #line's presumed 202 is
# intentionally literal.  This covers the main file, included header, macro
# invocation, and a remapped logical pathname.
DECODED="$WORK/cgf-O0.decoded"
readelf --debug-dump=decodedline "$WORK/cgf-O0" > "$DECODED" \
    2> "$DECODED.err" || fail "readelf decoded-line dump failed"
[ ! -s "$DECODED.err" ] || fail "decoded-line dump produced diagnostics"
grep -q 'tests/debug/dwarf_lines.h:' "$DECODED" ||
    fail "included-header file table entry missing"
grep -q 'virtual/s29_remapped.c:' "$DECODED" ||
    fail "#line-remapped file table entry missing"

marker_line()
{
    awk -v marker="$1" 'index($0, marker) { print NR; exit }' "$2"
}

address_for_line()
{
    awk -v wanted="$1" '$2 == wanted && $3 ~ /^0x/ { print $3; exit }' \
        "$DECODED"
}

check_addr_line()
{
    line=$1
    suffix=$2
    label=$3
    addr=$(address_for_line "$line")
    checks=$((checks + 1))
    if [ -z "$addr" ]; then
        fail "no line-table address for $label ($line)"
        return
    fi
    resolved=$(addr2line -e "$WORK/cgf-O0" "$addr" 2>/dev/null)
    case "$resolved" in
    *"$suffix:$line") ;;
    *) fail "addr2line $label at $addr resolved '$resolved'" ;;
    esac
}

header_line=$(marker_line 'A2L: header-step' tests/debug/dwarf_lines.h)
leaf_line=$(marker_line 'A2L: leaf-body' "$SRC")
macro_line=$(marker_line 'A2L: macro-call' "$SRC")
top_line=$(marker_line 'A2L: top-call' "$SRC")
main_line=$(marker_line 'A2L: main-return' "$SRC")
check_addr_line "$header_line" /tests/debug/dwarf_lines.h header
check_addr_line "$leaf_line" /tests/debug/dwarf_lines.c leaf
check_addr_line "$macro_line" /tests/debug/dwarf_lines.c macro-invocation
check_addr_line "$top_line" /tests/debug/dwarf_lines.c top
check_addr_line "$main_line" /tests/debug/dwarf_lines.c main
check_addr_line 202 /virtual/s29_remapped.c remapped

# afs-ld must accept the exact zR / pcrel|sdata4 CIE/FDE shape and produce a
# runnable static executable.  Keep system gas here so this check is
# independent from the separately-tested bundled assembler.  The driver
# disables GNU as debug-section compression because afs-ld validates raw
# relocation offsets against section sizes.
if [ -x "$AFS_LD" ]; then
    if CGF_AS=0 CGF_LD=1 "$CGF" -g -static "$SRC" \
        -o "$WORK/afsld-static" 2> "$WORK/afsld-static.err"; then
        "$WORK/afsld-static"
        status=$?
        checks=$((checks + 1))
        [ "$status" -eq 0 ] || fail "afs-ld product exited $status"
    else
        fail "afs-ld rejected the -g static product"
        cat "$WORK/afsld-static.err" >&2
    fi
else
    echo 'HARNESS_SKIP suite=debug-info test=afs-ld count=1 reason="afs-ld not built (make tools)"'
fi

# Batch gdb is optional because some containers install gdb but deny ptrace.
# When runnable, enforce exact main/next lines plus four nested frame names at
# both -O0 and -O2.  GCC -O0 is the independent debugger-behavior oracle.
main_first=$(marker_line 'GDB_LINE: main-first' "$SRC")
main_next=$(marker_line 'GDB_LINE: main-next' "$SRC")
main_return=$main_line

run_gdb_checks()
{
    tag=$1
    exe=$2
    opt=$3
    main_log="$WORK/gdb-$tag-O$opt-main.log"
    leaf_log="$WORK/gdb-$tag-O$opt-leaf.log"
    gdb -q -batch -ex 'set debuginfod enabled off' \
        -ex 'set pagination off' -ex 'break main' -ex run -ex next \
        -ex next -ex bt --args "$exe" > "$main_log" 2>&1
    gdb -q -batch -ex 'set debuginfod enabled off' \
        -ex 'set pagination off' -ex 'break s29_leaf' -ex run -ex bt \
        --args "$exe" > "$leaf_log" 2>&1

    checks=$((checks + 1))
    grep -Eq "dwarf_lines.c[:,] line $main_first|dwarf_lines.c:$main_first" \
        "$main_log" || fail "$tag -O$opt did not break main at line $main_first"
    grep -Eq "^[[:space:]]*$main_next[[:space:]]" "$main_log" ||
        fail "$tag -O$opt next missed line $main_next"
    grep -Eq "^[[:space:]]*$main_return[[:space:]]" "$main_log" ||
        fail "$tag -O$opt next missed line $main_return"
    for frame in s29_leaf s29_middle s29_top main; do
        grep -Eq "#[0-9]+[[:space:]].*$frame" "$leaf_log" ||
            fail "$tag -O$opt backtrace missing $frame"
    done
}

if command -v gdb >/dev/null 2>&1; then
    gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break main' -ex run \
        --args "$WORK/cgf-O0" > "$WORK/gdb-probe.log" 2>&1
    if grep -Eqi 'ptrace:|Could not trace|Operation not permitted|During startup program exited' \
        "$WORK/gdb-probe.log"; then
        echo 'HARNESS_SKIP suite=debug-info test=gdb count=1 reason="gdb unavailable or ptrace denied"'
    else
        run_gdb_checks cgf "$WORK/cgf-O0" 0
        run_gdb_checks cgf "$WORK/cgf-O2" 2
        if command -v gcc >/dev/null 2>&1; then
            if gcc -g -O0 "$SRC" -o "$WORK/gcc-O0" \
                2> "$WORK/gcc-O0.err"; then
                run_gdb_checks gcc "$WORK/gcc-O0" 0
            else
                fail "gcc debugger oracle build failed"
            fi
        else
            echo 'HARNESS_SKIP suite=debug-info test=gcc-gdb count=1 reason="gcc not found"'
        fi
    fi
else
    echo 'HARNESS_SKIP suite=debug-info test=gdb count=1 reason="gdb unavailable or ptrace denied"'
fi

[ "$fails" -eq 0 ] || exit 1
echo "debug_info lane: $checks checks; DWARF/CFI, 6 addr2line rows, determinism, links verified"
