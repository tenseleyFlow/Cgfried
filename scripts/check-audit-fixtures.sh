#!/bin/sh
# Sprint 60: keep every confirmed compiler mismatch reproducible until its
# Sprint 61 repair lands. An expected failure is green; an unexpected pass is
# deliberately red until the fixture and finding ledger are advanced together.
set -u
LC_ALL=C
export LC_ALL
ulimit -c 0 2>/dev/null || true

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
CGF=${1:-"$ROOT/build/cgfried"}
FIXTURES="$ROOT/tests/audit-regressions"
MANIFEST="$FIXTURES/manifest.tsv"
HOST_CC=${CGF_AUDIT_CC:-cc}
A64_AS=${CGF_AUDIT_A64_AS:-aarch64-linux-gnu-as}
A64_READELF=${CGF_AUDIT_A64_READELF:-aarch64-linux-gnu-readelf}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cgf-audit-fixtures.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

xfails=0
xpasses=0
fails=0
checks=0

xfail()
{
    xfails=$((xfails + 1))
    printf 'XFAIL %s: %s\n' "$1" "$2"
}

xpass()
{
    xpasses=$((xpasses + 1))
    printf 'XPASS %s: %s\n' "$1" "$2"
}

fail()
{
    fails=$((fails + 1))
    printf 'FAIL %s: %s\n' "$1" "$2" >&2
}

run_cgf()
{
    run_id=$1
    shift
    (exec env CGF_INCLUDE_DIR="$ROOT/include" "$CGF" "$@") \
        >"$WORK/$run_id.stdout" 2>"$WORK/$run_id.stderr"
}

run_cgf_runtime()
{
    run_id=$1
    fusion_disabled=$2
    shift 2
    (exec env CGF_AS=0 CGF_AS_PATH= CGF_LD=0 CGF_LD_PATH= \
        CGF_OPT_DISABLE_FUSION="$fusion_disabled" \
        CGF_INCLUDE_DIR="$ROOT/include" "$CGF" "$@") \
        >"$WORK/$run_id.stdout" 2>"$WORK/$run_id.stderr"
}

run_cgf_verified()
{
    run_id=$1
    shift
    (exec env CGF_INCLUDE_DIR="$ROOT/include" CGF_VERIFY_AFTER_EACH=1 \
        "$CGF" "$@") >"$WORK/$run_id.stdout" 2>"$WORK/$run_id.stderr"
}

run_cgf_full_backtrace()
{
    run_id=$1
    shift
    (exec env CGF_INCLUDE_DIR="$ROOT/include" \
        CGF_DIAG_FULL_BACKTRACE=1 "$CGF" "$@") \
        >"$WORK/$run_id.stdout" 2>"$WORK/$run_id.stderr"
}

if [ ! -x "$CGF" ]; then
    echo "audit fixtures: compiler is not executable: $CGF" >&2
    exit 1
fi
if [ ! -r "$MANIFEST" ]; then
    echo "audit fixtures: missing manifest: $MANIFEST" >&2
    exit 1
fi
if ! command -v "$HOST_CC" >/dev/null 2>&1; then
    echo "audit fixtures: host assembler driver is unavailable: $HOST_CC" >&2
    exit 1
fi

tab=$(printf '\t')
manifest_ids="$WORK/manifest.ids"
manifest_files="$WORK/manifest.files"
awk -F '\t' '!/^#/ && NF { print $1 }' "$MANIFEST" >"$manifest_ids"
awk -F '\t' '!/^#/ && NF { print $2 }' "$MANIFEST" >"$manifest_files"

duplicates=$(sort "$manifest_ids" | uniq -d)
if [ -n "$duplicates" ]; then
    echo "audit fixtures: duplicate finding IDs: $duplicates" >&2
    exit 1
fi
duplicates=$(sort "$manifest_files" | uniq -d)
if [ -n "$duplicates" ]; then
    echo "audit fixtures: duplicate fixture paths: $duplicates" >&2
    exit 1
fi

for source in "$FIXTURES"/*.c "$FIXTURES"/*.cgfir; do
    [ -e "$source" ] || continue
    fixture=${source##*/}
    if ! grep -Fxq "$fixture" "$manifest_files"; then
        echo "audit fixtures: unlisted fixture: $fixture" >&2
        exit 1
    fi
done

probe()
{
    id=$1
    source=$2
    title=$3
    checks=$((checks + 1))

    case "$id" in
    PP-H-01)
        run_cgf "$id" -E -P \
            -I "$FIXTURES/support/pp-h-01/inc1" \
            -I "$FIXTURES/support/pp-h-01/inc2" "$source"
        status=$?
        if [ "$status" -eq 0 ] &&
           [ "$(grep -c '^PP_H_01_FIRST$' "$WORK/$id.stdout" || true)" -eq 1 ] &&
           [ "$(grep -c '^PP_H_01_SECOND$' "$WORK/$id.stdout" || true)" -eq 1 ]; then
            xpass "$id" "$title"
        elif [ "$status" -ne 0 ] &&
             grep -q 'same.h: No such file or directory' "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected include_next result (status $status)"
        fi
        ;;
    PP-M-02)
        run_cgf "$id" -fsyntax-only "$source"
        status=$?
        if [ "$status" -ne 0 ] &&
           grep -q "in expansion of macro 'DECLARATOR'" "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -ne 0 ] &&
             grep -q "expected a declarator but found '0'" "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected dynamic-builtin diagnostic (status $status)"
        fi
        ;;
    PP-L-03)
        run_cgf_full_backtrace "$id" --dump-tokens "$source"
        status=$?
        notes=$(grep -c "in expansion of macro 'TRACE_" \
            "$WORK/$id.stderr" || true)
        if [ "$status" -ne 0 ] &&
           grep -q "in expansion of macro 'TRACE_000'" "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -ne 0 ] && [ "$notes" -eq 256 ]; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected full-backtrace depth: $notes (status $status)"
        fi
        ;;
    FE-H-01)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source"
        status=$?
        if [ "$status" -eq 0 ]; then
            xfail "$id" "$title"
        elif [ "$status" -eq 1 ]; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected compiler status $status"
        fi
        ;;
    SEMA-C-01)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source" \
            2>>"$WORK/$id.stderr"
        status=$?
        if [ "$status" -ge 128 ]; then
            xfail "$id" "$title"
        elif [ "$status" -eq 1 ] &&
             grep -Eqi 'overflow|constant expression' "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        else
            fail "$id" "expected a clean overflow diagnostic, got status $status"
        fi
        ;;
    SEMA-C-02)
        run_cgf "$id" --target=arm64-linux -fdump-layout -fsyntax-only "$source"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid AAPCS64 layout fixture was rejected"
        elif grep -q 'size=8 align=8' "$WORK/$id.stdout"; then
            xpass "$id" "$title"
        elif grep -q 'size=4 align=4' "$WORK/$id.stdout"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected AAPCS64 layout dump"
        fi
        ;;
    SEMA-H-06)
        run_cgf "$id" -std=gnu17 -w -fdump-layout -fsyntax-only "$source"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid zero-length-array layout fixture was rejected"
        elif grep -q 'struct all_zero_length: size=0 align=4' \
            "$WORK/$id.stdout"; then
            xpass "$id" "$title"
        elif grep -q 'struct all_zero_length: size=4 align=4' \
            "$WORK/$id.stdout"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected zero-length-array layout dump"
        fi
        ;;
    SEMA-H-03|SEMA-H-04|SEMA-H-05)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source"
        status=$?
        if [ "$status" -eq 0 ]; then
            xfail "$id" "$title"
        elif [ "$status" -eq 1 ]; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected compiler status $status"
        fi
        ;;
    IR-C-01)
        asm="$WORK/$id.s"
        run_cgf "$id" -O0 -S "$source" -o "$asm"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid long-double aggregate fixture was rejected"
        # Every one of the three calls sets up a hidden return pointer today.
        # The psABI returns all three in st0, so a correct compiler emits none.
        elif [ "$(grep -c 'movq[[:space:]]*%rax,[[:space:]]*%rdi' "$asm")" \
               -eq 3 ]; then
            xfail "$id" "$title"
        elif ! grep -q '%rdi' "$asm"; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected long-double aggregate return sequence"
        fi
        ;;
    IR-L-02)
        ir="$WORK/$id.stdout"
        run_cgf "$id" -emit-ir "$source"
        status=$?
        if [ "$status" -eq 1 ]; then
            xpass "$id" "$title"
        elif [ "$status" -eq 0 ] && grep -q 'icmp eq i64' "$ir"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected comparison-type validation result"
        fi
        ;;
    IR-C-03)
        ir="$WORK/$id.stdout"
        run_cgf "$id" -std=c17 -O0 -emit-ir "$source"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid atomic-pointer increment was rejected"
        elif grep -Eq 'atomicrmw|cmpxchg|@__atomic' "$ir"; then
            xpass "$id" "$title"
        elif grep -Eq 'load ptr, @cursor,.*seq_cst' "$ir" &&
             grep -Eq 'store ptr .*@cursor,.*seq_cst' "$ir"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected atomic-pointer increment lowering"
        fi
        ;;
    IR-C-04)
        ir="$WORK/$id.stdout"
        run_cgf "$id" -std=c17 -pedantic-errors -O0 -emit-ir "$source"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid backward-goto VLA fixture was rejected"
        elif grep -q 'stackrestore' "$ir"; then
            xpass "$id" "$title"
        elif grep -q 'stacksave' "$ir" && grep -q 'br L.again' "$ir"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected backward-goto VLA lowering"
        fi
        ;;
    IR-H-05)
        ir="$WORK/$id.stdout"
        run_cgf "$id" -std=c17 -pedantic-errors -O0 -emit-ir "$source"
        status=$?
        copies=$(grep -c '^[[:space:]]*memcpy ' "$ir" || true)
        marked=$(grep -c '^[[:space:]]*memcpy .*volatile' "$ir" || true)
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid volatile-aggregate fixture was rejected"
        elif [ "$copies" -eq 2 ] && [ "$marked" -eq 2 ]; then
            xpass "$id" "$title"
        elif [ "$copies" -eq 2 ] && [ "$marked" -eq 0 ]; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected volatile aggregate copy markers"
        fi
        ;;
    IR-H-06)
        ir="$WORK/$id.stdout"
        run_cgf "$id" -std=gnu17 -O0 -emit-ir "$source"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid sigsetjmp fixture was rejected"
        elif grep -Eq '^func i32 @save\(ptr %[0-9]+\) setjmp \{' "$ir"; then
            xpass "$id" "$title"
        elif grep -q 'call i32 @__sigsetjmp' "$ir" &&
             grep -Eq '^func i32 @save\(ptr %[0-9]+\) \{' "$ir"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected sigsetjmp function marker"
        fi
        ;;
    IR-H-07)
        ir="$WORK/$id.stdout"
        run_cgf "$id" -emit-ir "$source"
        status=$?
        if [ "$status" -eq 1 ]; then
            xpass "$id" "$title"
        elif [ "$status" -eq 0 ] &&
             grep -q 'reloc 18446744073709551615' "$ir"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected relocation-boundary validation result"
        fi
        ;;
    OPT-H-01)
        # This reaches the shared service through its optimizer client.
        # `-w` removes the independent default warning-analysis path, so an
        # eventual repair must make the O2 pipeline itself complete.
        run_cgf "$id" -std=c17 -w -O2 -emit-ir "$source"
        status=$?
        if [ "$status" -eq 0 ]; then
            xpass "$id" "$title"
        elif [ "$status" -eq 4 ] &&
             grep -q 'alias: points-to solver did not converge in @advance' \
                 "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected pointer-update analysis result (status $status)"
        fi
        ;;
    OPT-H-02)
        o0="$WORK/$id.o0"
        no_fusion="$WORK/$id.no-fusion"
        o3="$WORK/$id.o3"
        # The fusion-disabled O3 control distinguishes this transform from
        # unrelated optimizer or backend failures in the same pipeline.
        run_cgf_runtime "$id.o0" 0 -std=c17 -O0 "$source" -o "$o0"
        o0_status=$?
        run_cgf_runtime "$id.no-fusion" 1 -std=c17 -O3 "$source" \
            -o "$no_fusion"
        no_fusion_status=$?
        run_cgf_runtime "$id.o3" 0 -std=c17 -O3 "$source" -o "$o3"
        o3_status=$?
        if [ "$o0_status" -ne 0 ]; then
            fail "$id" "valid O0 control fixture was rejected"
        elif [ "$no_fusion_status" -ne 0 ]; then
            fail "$id" "valid fusion-disabled O3 control was rejected"
        elif [ "$o3_status" -ne 0 ]; then
            fail "$id" "valid O3 fixture was rejected"
        else
            "$o0" >"$WORK/$id.o0.run.stdout" 2>"$WORK/$id.o0.run.stderr"
            o0_status=$?
            "$no_fusion" >"$WORK/$id.no-fusion.run.stdout" \
                2>"$WORK/$id.no-fusion.run.stderr"
            no_fusion_status=$?
            "$o3" >"$WORK/$id.o3.run.stdout" \
                2>"$WORK/$id.o3.run.stderr"
            o3_status=$?
            if [ "$o0_status" -ne 0 ]; then
                fail "$id" "O0 control returned status $o0_status"
            elif [ "$no_fusion_status" -ne 0 ]; then
                fail "$id" "fusion-disabled O3 control returned status $no_fusion_status"
            elif [ "$o3_status" -eq 0 ]; then
                xpass "$id" "$title"
            elif [ "$o3_status" -eq 1 ]; then
                xfail "$id" "$title"
            else
                fail "$id" "O3 result had unexpected status $o3_status"
            fi
        fi
        ;;
    OPT-H-03)
        o0="$WORK/$id.o0"
        o2="$WORK/$id.o2"
        # O2 is the pass-absent control: the unroller enters the pipeline at
        # O3, and no standalone unroll-disable switch exists yet.
        run_cgf_runtime "$id.o0" 0 -std=c17 -O0 "$source" -o "$o0"
        o0_status=$?
        run_cgf_runtime "$id.o2" 0 -std=c17 -O2 "$source" -o "$o2"
        o2_status=$?
        run_cgf_verified "$id.o3" -std=c17 -O3 -emit-ir "$source"
        o3_status=$?
        if [ "$o0_status" -ne 0 ]; then
            fail "$id" "valid O0 control fixture was rejected"
        elif [ "$o2_status" -ne 0 ]; then
            fail "$id" "valid O2 control fixture was rejected"
        else
            "$o0" >"$WORK/$id.o0.run.stdout" 2>"$WORK/$id.o0.run.stderr"
            o0_status=$?
            "$o2" >"$WORK/$id.o2.run.stdout" 2>"$WORK/$id.o2.run.stderr"
            o2_status=$?
            if [ "$o0_status" -ne 0 ]; then
                fail "$id" "O0 control returned status $o0_status"
            elif [ "$o2_status" -ne 0 ]; then
                fail "$id" "O2 control returned status $o2_status"
            elif [ "$o3_status" -eq 0 ]; then
                xpass "$id" "$title"
            elif [ "$o3_status" -eq 4 ] &&
                 grep -q "pass 'unroll' produced invalid IR" \
                     "$WORK/$id.o3.stderr" &&
                 grep -q 'operand names value id 0' "$WORK/$id.o3.stderr"; then
                xfail "$id" "$title"
            else
                fail "$id" "unexpected O3 unroll result (status $o3_status)"
            fi
        fi
        ;;
    X64-C-01)
        asm="$WORK/$id.s"
        run_cgf "$id" -O0 -S "$source" -o "$asm"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid floating-atomic fixture was rejected"
        elif grep -Eq 'movsd[[:space:]].*atomic_double' "$asm" &&
             grep -Eq 'fstpt[[:space:]]+atomic_long_double' "$asm" &&
             ! grep -Eq 'lock|xchg|cmpxchg|__atomic' "$asm"; then
            xfail "$id" "$title"
        else
            xpass "$id" "$title"
        fi
        ;;
    X64-C-02)
        asm="$WORK/$id.s"
        run_cgf "$id" -O0 -S "$source" -o "$asm"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid large-frame fixture was rejected"
        elif grep -Eq 'leaq[[:space:]]+\(%rbp\),' "$asm" &&
             ! grep -Eq 'subq[[:space:]].*,[[:space:]]*%rsp' "$asm"; then
            xfail "$id" "$title"
        else
            xpass "$id" "$title"
        fi
        ;;
    X64-M-03)
        asm="$WORK/$id.s"
        run_cgf "$id" -std=gnu17 -O0 -S "$source" -o "$asm"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid extended-assembly fixture was rejected"
        elif "$HOST_CC" -c "$asm" -o "$WORK/$id.o" \
             >"$WORK/$id.as.stdout" 2>"$WORK/$id.as.stderr"; then
            xpass "$id" "$title"
        elif grep -Eqi 'bad register|invalid register' "$WORK/$id.as.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "emitted assembly failed for an unexpected reason"
        fi
        ;;
    A64-H-01)
        asm="$WORK/$id.s"
        run_cgf "$id" --target=arm64-linux -fcommon -O0 -S "$source" \
            -o "$asm"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid TLS fixture was rejected"
        elif grep -Eq '^[[:space:]]*\.comm[[:space:]]+tls_counter,' "$asm"; then
            xfail "$id" "$title"
        elif grep -q '\.tbss' "$asm" && grep -q '@tls_object' "$asm"; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected TLS definition form"
        fi
        ;;
    A64-H-02)
        asm="$WORK/$id.s"
        run_cgf "$id" --target=arm64-linux -O2 -S "$source" -o "$asm"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid TLS-offset fixture was rejected"
        elif grep -Eq 'add[[:space:]]+x[0-9]+, x[0-9]+, #5000' "$asm"; then
            xfail "$id" "$title"
        else
            xpass "$id" "$title"
        fi
        ;;
    A64-H-03)
        asm="$WORK/$id.s"
        run_cgf "$id" --target=arm64-linux -fPIC -O2 -S "$source" -o "$asm"
        status=$?
        if [ "$status" -eq 4 ] &&
           grep -Eqi 'GOT address addend|internal compiler error' \
               "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        elif [ "$status" -eq 0 ]; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected large-GOT-addend result (status $status)"
        fi
        ;;
    A64-M-04)
        asm="$WORK/$id.s"
        run_cgf "$id" --target=arm64-linux -O0 -g -S "$source" -o "$asm"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid unwind fixture was rejected"
        elif ! grep -Eq 'str[[:space:]]+x19,' "$asm"; then
            xpass "$id" "$title"
        elif command -v "$A64_AS" >/dev/null 2>&1 &&
             command -v "$A64_READELF" >/dev/null 2>&1; then
            if ! "$A64_AS" "$asm" -o "$WORK/$id.o" \
                 >"$WORK/$id.as.stdout" 2>"$WORK/$id.as.stderr"; then
                fail "$id" "cross-assembler rejected emitted unwind data"
            else
                "$A64_READELF" --debug-dump=frames "$WORK/$id.o" \
                    >"$WORK/$id.frames" 2>"$WORK/$id.readelf.stderr"
                if grep -q 'DW_CFA_offset: r19' "$WORK/$id.frames" &&
                   grep -q 'DW_CFA_restore: r19' "$WORK/$id.frames" &&
                   grep -q 'DW_CFA_def_cfa_offset: 0' "$WORK/$id.frames"; then
                    xpass "$id" "$title"
                elif ! grep -q 'DW_CFA_offset: r19' "$WORK/$id.frames" &&
                     ! grep -q 'DW_CFA_restore: r19' "$WORK/$id.frames"; then
                    xfail "$id" "$title"
                else
                    fail "$id" "unwind repair is incomplete"
                fi
            fi
        elif ! grep -Eq '\.byte[[:space:]]+147([,[:space:]]|$)|\.cfi_offset[[:space:]]+x?19' "$asm"; then
            xfail "$id" "$title"
        else
            fail "$id" "cross tools absent and unwind state is not classifiable"
        fi
        ;;
    *)
        fail "$id" "no probe is registered"
        ;;
    esac
}

while IFS="$tab" read -r id fixture title extra; do
    case "$id" in
    ''|'#'*) continue ;;
    esac
    if [ -n "${extra:-}" ]; then
        echo "audit fixtures: too many manifest fields for $id" >&2
        exit 1
    fi
    case "$id" in
    *-[CHML]-[0-9][0-9]) ;;
    *)
        echo "audit fixtures: malformed finding ID: $id" >&2
        exit 1
        ;;
    esac
    case "$fixture" in
    */*|'')
        echo "audit fixtures: unsafe fixture path for $id: $fixture" >&2
        exit 1
        ;;
    *.c|*.cgfir) ;;
    *)
        echo "audit fixtures: unsupported fixture type for $id: $fixture" >&2
        exit 1
        ;;
    esac
    source="$FIXTURES/$fixture"
    if [ ! -r "$source" ]; then
        echo "audit fixtures: manifest entry has no file: $fixture" >&2
        exit 1
    fi
    IFS= read -r header <"$source"
    expected="// XFAIL(audit): $id $title"
    if [ "$header" != "$expected" ]; then
        echo "audit fixtures: header mismatch in $fixture" >&2
        echo "  expected: $expected" >&2
        echo "  actual:   $header" >&2
        exit 1
    fi
    probe "$id" "$source" "$title"
done <"$MANIFEST"

printf 'audit fixtures: %d checks; %d XFAIL, %d XPASS, %d FAIL\n' \
    "$checks" "$xfails" "$xpasses" "$fails"
if [ "$xpasses" -ne 0 ] || [ "$fails" -ne 0 ]; then
    exit 1
fi
