#!/bin/sh
# Sprint 60/61: keep every confirmed compiler mismatch reproducible throughout
# remediation. OPEN defects are XFAIL and unexpected repairs are XPASS; PASS
# repairs stay green and any regression is FAIL.
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
passes=0
fails=0
checks=0

xfail()
{
    if [ "$fixture_state" = OPEN ]; then
        xfails=$((xfails + 1))
        printf 'XFAIL %s: %s\n' "$1" "$2"
    else
        fails=$((fails + 1))
        printf 'FAIL %s: resolved fixture regressed: %s\n' "$1" "$2" >&2
    fi
}

xpass()
{
    if [ "$fixture_state" = PASS ]; then
        passes=$((passes + 1))
        printf 'PASS %s: %s\n' "$1" "$2"
    else
        xpasses=$((xpasses + 1))
        printf 'XPASS %s: %s\n' "$1" "$2"
    fi
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

run_cgf_memcap()
{
    run_id=$1
    shift
    (ulimit -v 262144 2>/dev/null || exit 125
     exec env CGF_INCLUDE_DIR="$ROOT/include" "$CGF" "$@") \
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

run_cgf_verified_runtime()
{
    run_id=$1
    shift
    (exec env CGF_AS=0 CGF_AS_PATH= CGF_LD=0 CGF_LD_PATH= \
        CGF_OPT_DISABLE_BCE= CGF_OPT_DISABLE_FUSION= \
        CGF_OPT_DISABLE_VECTORIZE= CGF_OPT_DISABLE_UNSWITCH= \
        CGF_VERIFY_AFTER_EACH=1 CGF_INCLUDE_DIR="$ROOT/include" "$CGF" "$@") \
        >"$WORK/$run_id.stdout" 2>"$WORK/$run_id.stderr"
}

run_cgf_verified_phase_runtime()
{
    run_id=$1
    dump_dir=$2
    shift 2
    (exec env CGF_AS=0 CGF_AS_PATH= CGF_LD=0 CGF_LD_PATH= \
        CGF_OPT_DISABLE_BCE= CGF_OPT_DISABLE_FUSION= \
        CGF_OPT_DISABLE_VECTORIZE= CGF_OPT_DISABLE_UNSWITCH= \
        CGF_VERIFY_AFTER_EACH=1 CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$dump_dir" \
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

manifest_error=$(awk -F '\t' '
    !/^#/ && NF && NF != 4 {
        printf "audit fixtures: manifest row %d must have exactly 4 fields", NR
        exit
    }
    !/^#/ && NF && $3 == "" {
        printf "audit fixtures: missing state for %s", $1
        exit
    }
    !/^#/ && NF && $3 != "OPEN" && $3 != "PASS" {
        printf "audit fixtures: malformed state for %s: %s", $1, $3
        exit
    }
' "$MANIFEST")
if [ -n "$manifest_error" ]; then
    echo "$manifest_error" >&2
    exit 1
fi

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

for source in "$FIXTURES"/*.c "$FIXTURES"/*.cgfir "$FIXTURES"/*.sh; do
    [ -e "$source" ] || continue
    fixture=${source##*/}
    if ! grep -Fxq "$fixture" "$manifest_files"; then
        echo "audit fixtures: unlisted fixture: $fixture" >&2
        exit 1
    fi
done

# Validate the complete lifecycle inventory before running any potentially
# expensive reproducer. OPEN rows retain the audit XFAIL marker; PASS rows
# carry an explicit resolved marker so removing XFAIL cannot hide stale state.
while IFS="$tab" read -r id fixture fixture_state title extra; do
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
    *.c|*.cgfir|*.sh) ;;
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
    case "$fixture" in
    *.sh)
        header=$(sed -n '2p' "$source")
        prefix='#'
        ;;
    *)
        IFS= read -r header <"$source"
        prefix='//'
        ;;
    esac
    case "$fixture_state" in
    OPEN) expected="$prefix XFAIL(audit): $id $title" ;;
    PASS) expected="$prefix RESOLVED(audit): $id $title" ;;
    esac
    if [ "$header" != "$expected" ]; then
        echo "audit fixtures: header/state mismatch in $fixture ($fixture_state)" >&2
        echo "  expected: $expected" >&2
        echo "  actual:   $header" >&2
        exit 1
    fi
    if [ "$fixture_state" = PASS ] && grep -Fq 'XFAIL(audit)' "$source"; then
        echo "audit fixtures: PASS fixture retains XFAIL marker: $fixture" >&2
        exit 1
    fi
done <"$MANIFEST"

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
    PP-M-04)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source"
        status=$?
        inner=$(grep -c "in expansion of macro 'ARG_BAD'" \
            "$WORK/$id.stderr" || true)
        outer=$(grep -c "in expansion of macro 'PASS'" \
            "$WORK/$id.stderr" || true)
        if [ "$status" -eq 1 ] && [ "$inner" -eq 1 ] &&
           [ "$outer" -eq 1 ] &&
           grep -q "invalid digit '9' in octal constant" \
               "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -eq 1 ] && [ "$inner" -eq 0 ] &&
             [ "$outer" -eq 1 ] &&
             grep -q "invalid digit '9' in octal constant" \
                 "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected pre-expanded-argument backtrace ($inner inner, $outer outer, status $status)"
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
    FE-H-02)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source"
        status=$?
        errors=$(grep -c ': error:' "$WORK/$id.stderr" || true)
        if [ "$status" -eq 0 ]; then
            xfail "$id" "$title"
        elif [ "$status" -eq 1 ] && [ "$errors" -eq 1 ] &&
             grep -q "parameter names (without types) are only allowed" \
                 "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected nested K&R result ($errors errors, status $status)"
        fi
        ;;
    FE-M-03)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source"
        status=$?
        errors=$(grep -c ': error:' "$WORK/$id.stderr" || true)
        if [ "$status" -eq 1 ] && [ "$errors" -eq 1 ] &&
           grep -q "expected a parameter declaration but found ','" \
               "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -eq 1 ] && [ "$errors" -eq 6 ] &&
             grep -q "expected a parameter declaration but found ','" \
                 "$WORK/$id.stderr" &&
             grep -q "expected ')' after parameter list" \
                 "$WORK/$id.stderr" &&
             grep -q "expected a declaration but found ')'" \
                 "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected parameter-list diagnostic cascade ($errors errors, status $status)"
        fi
        ;;
    FE-M-04)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source"
        status=$?
        errors=$(grep -c ': error:' "$WORK/$id.stderr" || true)
        if [ "$status" -eq 1 ] && [ "$errors" -eq 1 ] &&
           grep -q "expected an expression but found 'struct'" \
               "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -eq 1 ] && [ "$errors" -eq 3 ] &&
             grep -q "expected an expression but found 'struct'" \
                 "$WORK/$id.stderr" &&
             grep -q "expected a declaration but found '2'" \
                 "$WORK/$id.stderr" &&
             grep -q "expected a declaration but found '}'" \
                 "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected initializer diagnostic cascade ($errors errors, status $status)"
        fi
        ;;
    FE-M-05)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source"
        status=$?
        errors=$(grep -c ': error:' "$WORK/$id.stderr" || true)
        if [ "$status" -eq 1 ] && [ "$errors" -eq 1 ] &&
           grep -q "expected a type name but found ':'" \
               "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -eq 1 ] && [ "$errors" -eq 2 ] &&
             grep -q "expected a type name but found ':'" \
                 "$WORK/$id.stderr" &&
             grep -q "_Generic association for 'int' duplicates an earlier one" \
                 "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected _Generic diagnostics ($errors errors, status $status)"
        fi
        ;;
    SEMA-C-01)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source" \
            2>>"$WORK/$id.stderr"
        status=$?
        if [ "$status" -ge 128 ]; then
            xfail "$id" "$title"
        elif [ "$status" -eq 1 ] &&
             grep -Fq 'runtime error: division of -9223372036854775808 by -1 cannot be represented in type '\''long int'\''' \
                 "$WORK/$id.stderr"; then
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
    SEMA-H-07)
        run_cgf "$id" -std=c17 -pedantic-errors -fsyntax-only "$source"
        status=$?
        errors=$(grep -c ': error:' "$WORK/$id.stderr" || true)
        if [ "$status" -eq 0 ]; then
            xpass "$id" "$title"
        elif [ "$status" -eq 1 ] && [ "$errors" -eq 1 ] &&
             grep -q 'this is not a constant expression' \
                 "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected _Generic ICE result ($errors errors, status $status)"
        fi
        ;;
    SEMA-C-08)
        run_cgf "$id" --target=arm64-linux -fdump-layout -fsyntax-only "$source"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid AAPCS64 unnamed-bitfield fixture was rejected"
        elif grep -q 'struct anon_long_only: size=8 align=8' \
            "$WORK/$id.stdout" &&
             grep -q 'struct mixed_anon: size=16 align=8' \
                 "$WORK/$id.stdout" &&
             grep -q 'union anon_long_union: size=8 align=8' \
                 "$WORK/$id.stdout"; then
            xpass "$id" "$title"
        elif grep -q 'struct anon_long_only: size=3 align=1' \
            "$WORK/$id.stdout" &&
             grep -q 'struct mixed_anon: size=12 align=4' \
                 "$WORK/$id.stdout" &&
             grep -q 'union anon_long_union: size=1 align=1' \
                 "$WORK/$id.stdout"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected AAPCS64 unnamed-bitfield layout dump"
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
        ir="$WORK/$id.stdout"
        run_cgf "$id" -O0 -emit-ir "$source"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid long-double aggregate fixture was rejected"
        elif grep -q 'func f80 @bare(f80' "$ir" &&
             grep -q 'call f80 @ret_struct()' "$ir" &&
             grep -q 'call f80 @ret_union()' "$ir" &&
             grep -q 'call f80 @ret_array()' "$ir"; then
            xpass "$id" "$title"
        elif grep -q 'func f80 @bare(f80' "$ir" &&
             grep -q 'call void @ret_struct(ptr .*abi(sret)' "$ir" &&
             grep -q 'call void @ret_union(ptr .*abi(sret)' "$ir" &&
             grep -q 'call void @ret_array(ptr .*abi(sret)' "$ir"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected long-double aggregate IR contract"
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
    IR-H-08)
        host="$WORK/$id.host"
        if "$HOST_CC" -std=c17 -pedantic-errors -Wall -Wextra -Werror -O1 \
            "$source" -o "$host" >"$WORK/$id.host.stdout" \
            2>"$WORK/$id.host.stderr"; then
            host_compile_status=0
        else
            host_compile_status=$?
        fi
        run_cgf "$id.o0" -std=c17 -pedantic-errors -O0 -emit-ir "$source"
        o0_status=$?
        run_cgf_verified "$id.o1" -std=c17 -pedantic-errors -O1 -emit-ir \
            "$source"
        o1_status=$?
        if [ "$host_compile_status" -ne 0 ]; then
            fail "$id" "host C17 control fixture was rejected"
        elif ! "$host"; then
            fail "$id" "host C17 control returned a wrong result"
        elif [ "$o0_status" -ne 0 ]; then
            fail "$id" "valid O0 IR control fixture was rejected"
        elif [ "$o1_status" -eq 0 ]; then
            xpass "$id" "$title"
        elif [ "$o1_status" -eq 4 ] &&
             grep -q -- '-emit-ir round-trip broke: parse(print(M)) != M' \
                 "$WORK/$id.o1.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected optimized IR round-trip result (status $o1_status)"
        fi
        ;;
    IR-H-09)
        run_cgf_memcap "$id" -emit-ir "$source"
        status=$?
        if [ "$status" -eq 1 ] &&
           grep -q 'initializer has' "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -eq 4 ] &&
             grep -q 'out of memory' "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        elif [ "$status" -eq 125 ]; then
            fail "$id" "could not establish compiler memory cap"
        else
            fail "$id" "unexpected oversized-initializer result (status $status)"
        fi
        ;;
    IR-C-09)
        host="$WORK/$id.host"
        linux_asm="$WORK/$id.linux.s"
        apple_asm="$WORK/$id.apple.s"
        linux_ir="$WORK/$id.linux.ir"
        if "$HOST_CC" -std=c17 -pedantic-errors -Wall -Wextra -Werror -O0 \
            "$source" -o "$host" >"$WORK/$id.host.stdout" \
            2>"$WORK/$id.host.stderr"; then
            host_compile_status=0
        else
            host_compile_status=$?
        fi
        run_cgf "$id.linux-asm" --target=arm64-linux -std=c17 \
            -pedantic-errors -O0 -S "$source" -o "$linux_asm"
        linux_asm_status=$?
        run_cgf "$id.apple-asm" --target=arm64-macos -std=c17 \
            -pedantic-errors -O0 -S "$source" -o "$apple_asm"
        apple_asm_status=$?
        run_cgf "$id.linux-ir" --target=arm64-linux -std=c17 \
            -pedantic-errors -O0 -emit-ir "$source"
        linux_ir_status=$?
        cp "$WORK/$id.linux-ir.stdout" "$linux_ir"
        awk '/^_?fixed_probe:$/ { in_fn = 1 }
             in_fn { print }
             in_fn && /bl[[:space:]]+_?fixed_sink$/ { exit }' \
            "$linux_asm" >"$WORK/$id.linux.fixed"
        awk '/^_?fixed_probe:$/ { in_fn = 1 }
             in_fn { print }
             in_fn && /bl[[:space:]]+_?fixed_sink$/ { exit }' \
            "$apple_asm" >"$WORK/$id.apple.fixed"
        # Only the final marshal window determines the call ABI. Earlier
        # x1/x2 moves feed make_pair and are not evidence about fixed_sink.
        tail -n 8 "$WORK/$id.linux.fixed" >"$WORK/$id.linux.call"
        tail -n 8 "$WORK/$id.apple.fixed" >"$WORK/$id.apple.call"
        awk '/^func .*@variadic_sink\(/ { in_fn = 1 }
             in_fn { print }
             in_fn && /^}/ { exit }' "$linux_ir" \
            >"$WORK/$id.linux.va"
        linux_wrong_regs=0
        linux_fixed_regs=0
        apple_control_regs=0
        linux_aligned_va=0
        if grep -Eq 'mov[[:space:]]+x1, x[0-9]+$' \
               "$WORK/$id.linux.call" &&
           grep -Eq 'mov[[:space:]]+x2, x[0-9]+$' \
               "$WORK/$id.linux.call"; then
            linux_wrong_regs=1
        fi
        if { grep -Eq 'mov[[:space:]]+x2, x[0-9]+$' \
                 "$WORK/$id.linux.call" &&
             grep -Eq 'mov[[:space:]]+x3, x[0-9]+$' \
                 "$WORK/$id.linux.call"; } ||
           grep -Eq 'ldp[[:space:]]+x2, x3,' "$WORK/$id.linux.call"; then
            linux_fixed_regs=1
        fi
        if { grep -Eq 'mov[[:space:]]+x1, x[0-9]+$' \
                 "$WORK/$id.apple.call" &&
             grep -Eq 'mov[[:space:]]+x2, x[0-9]+$' \
                 "$WORK/$id.apple.call"; } ||
           grep -Eq 'ldp[[:space:]]+x1, x2,' "$WORK/$id.apple.call"; then
            apple_control_regs=1
        fi
        if grep -Eq 'iadd i32 .* 15$' "$WORK/$id.linux.va" &&
           grep -Eq 'and i32 .* -16$' "$WORK/$id.linux.va"; then
            linux_aligned_va=1
        fi
        if [ "$host_compile_status" -ne 0 ]; then
            fail "$id" "host strict-C17 control fixture was rejected"
        elif ! "$host"; then
            fail "$id" "host strict-C17 control returned a wrong result"
        elif [ "$linux_asm_status" -ne 0 ] ||
             [ "$apple_asm_status" -ne 0 ] ||
             [ "$linux_ir_status" -ne 0 ]; then
            fail "$id" "valid arm64 ABI fixture was rejected"
        elif [ "$linux_wrong_regs" -eq 1 ] &&
             [ "$linux_aligned_va" -eq 0 ] &&
             [ "$apple_control_regs" -eq 1 ] &&
             grep -Eq 'iadd i32 .* 16$' "$WORK/$id.linux.va"; then
            xfail "$id" "$title"
        elif [ "$linux_wrong_regs" -eq 0 ] &&
             [ "$linux_fixed_regs" -eq 1 ] &&
             [ "$linux_aligned_va" -eq 1 ] &&
             [ "$apple_control_regs" -eq 1 ]; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected Linux register/va_arg or Apple control placement"
        fi
        ;;
    IR-C-10)
        host="$WORK/$id.host"
        linux_asm="$WORK/$id.linux.s"
        apple_asm="$WORK/$id.apple.s"
        if "$HOST_CC" -std=c17 -pedantic-errors -Wall -Wextra -Werror -O0 \
            "$source" -o "$host" >"$WORK/$id.host.stdout" \
            2>"$WORK/$id.host.stderr"; then
            host_compile_status=0
        else
            host_compile_status=$?
        fi
        run_cgf "$id.linux" --target=arm64-linux -std=c17 \
            -pedantic-errors -O0 -S "$source" -o "$linux_asm"
        linux_status=$?
        run_cgf "$id.apple" --target=arm64-macos -std=c17 \
            -pedantic-errors -O0 -S "$source" -o "$apple_asm"
        apple_status=$?
        awk '/^_?stacked_probe:$/ { in_fn = 1 }
             in_fn { print }
             in_fn && /bl[[:space:]]+_?stacked_sink$/ { exit }' \
            "$linux_asm" >"$WORK/$id.linux.caller"
        awk '/^_?stacked_sink:$/ { in_fn = 1 }
             in_fn { print }
             in_fn && /bl[[:space:]]+_?pair_sum$/ { exit }' \
            "$linux_asm" >"$WORK/$id.linux.callee"
        awk '/^_?stacked_probe:$/ { in_fn = 1 }
             in_fn { print }
             in_fn && /bl[[:space:]]+_?stacked_sink$/ { exit }' \
            "$apple_asm" >"$WORK/$id.apple.caller"
        awk '/^_?stacked_sink:$/ { in_fn = 1 }
             in_fn { print }
             in_fn && /bl[[:space:]]+_?pair_sum$/ { exit }' \
            "$apple_asm" >"$WORK/$id.apple.callee"
        linux_wrong=0
        apple_wrong=0
        linux_fixed=0
        apple_fixed=0
        if grep -Eq 'stp[[:space:]]+x[0-9]+, x[0-9]+, \[sp\]$' \
               "$WORK/$id.linux.caller" &&
           grep -Eq 'str[[:space:]]+x[0-9]+, \[sp, #16\]$' \
               "$WORK/$id.linux.caller" &&
           grep -Eq 'ldp[[:space:]]+x[0-9]+, x[0-9]+, \[x29, #144\]$' \
               "$WORK/$id.linux.callee" &&
           grep -Eq 'ldr[[:space:]]+x[0-9]+, \[x29, #160\]$' \
               "$WORK/$id.linux.callee"; then
            linux_wrong=1
        fi
        if grep -Eq 'stp[[:space:]]+x[0-9]+, x[0-9]+, \[sp\]$' \
               "$WORK/$id.apple.caller" &&
           grep -Eq 'str[[:space:]]+x[0-9]+, \[sp, #16\]$' \
               "$WORK/$id.apple.caller" &&
           grep -Eq 'ldp[[:space:]]+x[0-9]+, x[0-9]+, \[x29, #144\]$' \
               "$WORK/$id.apple.callee" &&
           grep -Eq 'ldr[[:space:]]+x[0-9]+, \[x29, #160\]$' \
               "$WORK/$id.apple.callee"; then
            apple_wrong=1
        fi
        if grep -Eq 'str[[:space:]]+x[0-9]+, \[sp\]$' \
               "$WORK/$id.linux.caller" &&
           grep -Eq 'stp[[:space:]]+x[0-9]+, x[0-9]+, \[sp, #16\]$' \
               "$WORK/$id.linux.caller" &&
           grep -Eq 'ldr[[:space:]]+x[0-9]+, \[x29, #144\]$' \
               "$WORK/$id.linux.callee" &&
           grep -Eq 'ldp[[:space:]]+x[0-9]+, x[0-9]+, \[x29, #160\]$' \
               "$WORK/$id.linux.callee"; then
            linux_fixed=1
        fi
        if grep -Eq 'str[[:space:]]+x[0-9]+, \[sp\]$' \
               "$WORK/$id.apple.caller" &&
           grep -Eq 'stp[[:space:]]+x[0-9]+, x[0-9]+, \[sp, #16\]$' \
               "$WORK/$id.apple.caller" &&
           grep -Eq 'ldr[[:space:]]+x[0-9]+, \[x29, #144\]$' \
               "$WORK/$id.apple.callee" &&
           grep -Eq 'ldp[[:space:]]+x[0-9]+, x[0-9]+, \[x29, #160\]$' \
               "$WORK/$id.apple.callee"; then
            apple_fixed=1
        fi
        if [ "$host_compile_status" -ne 0 ]; then
            fail "$id" "host strict-C17 control fixture was rejected"
        elif ! "$host"; then
            fail "$id" "host strict-C17 control returned a wrong result"
        elif [ "$linux_status" -ne 0 ] || [ "$apple_status" -ne 0 ]; then
            fail "$id" "valid stacked arm64 fixture was rejected"
        elif [ "$linux_wrong" -eq 1 ] && [ "$apple_wrong" -eq 1 ]; then
            xfail "$id" "$title"
        elif [ "$linux_fixed" -eq 1 ] && [ "$apple_fixed" -eq 1 ]; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected Linux or Apple stack aggregate placement"
        fi
        ;;
    IR-C-11)
        run_cgf "$id.x86" --target=x86_64-linux-gnu -emit-ir "$source"
        x86_status=$?
        run_cgf "$id.arm" --target=arm64-linux -emit-ir "$source"
        arm_status=$?
        if [ "$x86_status" -eq 0 ] && [ "$arm_status" -eq 0 ]; then
            xfail "$id" "$title"
        elif [ "$x86_status" -eq 1 ] && [ "$arm_status" -eq 1 ] &&
             grep -q "'load' of f32.*alignment 1" \
                 "$WORK/$id.x86.stderr" &&
             grep -q "'store' of f64.*alignment 1" \
                 "$WORK/$id.x86.stderr" &&
             grep -q "'load' of i64.*alignment 1" \
                 "$WORK/$id.arm.stderr" &&
             grep -q "'store' of i32.*alignment 2" \
                 "$WORK/$id.arm.stderr"; then
            xpass "$id" "$title"
        else
            fail "$id" "unexpected atomic-alignment verifier result"
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
    OPT-H-04)
        host="$WORK/$id.host"
        o0="$WORK/$id.o0"
        o1="$WORK/$id.o1"
        o2="$WORK/$id.o2"
        phase_dir="$WORK/$id.phases"
        mkdir "$phase_dir"
        if "$HOST_CC" -std=c17 -pedantic-errors -Wall -Wextra -Werror -O3 \
            "$source" -o "$host" >"$WORK/$id.host.stdout" \
            2>"$WORK/$id.host.stderr"; then
            host_compile_status=0
        else
            host_compile_status=$?
        fi
        run_cgf_verified_runtime "$id.o0" -std=c17 -O0 "$source" -o "$o0"
        o0_compile_status=$?
        run_cgf_verified_runtime "$id.o1" -std=c17 -O1 "$source" -o "$o1"
        o1_compile_status=$?
        run_cgf_verified_phase_runtime "$id.o2" "$phase_dir" \
            -std=c17 -O2 "$source" -o "$o2"
        o2_compile_status=$?
        if [ "$host_compile_status" -ne 0 ]; then
            fail "$id" "host C17 control fixture was rejected"
        elif [ "$o0_compile_status" -ne 0 ]; then
            fail "$id" "valid O0 control fixture was rejected"
        elif [ "$o1_compile_status" -ne 0 ]; then
            fail "$id" "valid O1 control fixture was rejected"
        elif [ "$o2_compile_status" -ne 0 ]; then
            fail "$id" "valid O2 fixture was rejected"
        else
            "$host" >"$WORK/$id.host.zero.stdout" \
                2>"$WORK/$id.host.zero.stderr"
            host_zero_status=$?
            "$host" x >"$WORK/$id.host.one.stdout" \
                2>"$WORK/$id.host.one.stderr"
            host_one_status=$?
            "$o0" >"$WORK/$id.o0.zero.stdout" \
                2>"$WORK/$id.o0.zero.stderr"
            o0_zero_status=$?
            "$o0" x >"$WORK/$id.o0.one.stdout" \
                2>"$WORK/$id.o0.one.stderr"
            o0_one_status=$?
            "$o1" >"$WORK/$id.o1.zero.stdout" \
                2>"$WORK/$id.o1.zero.stderr"
            o1_zero_status=$?
            "$o1" x >"$WORK/$id.o1.one.stdout" \
                2>"$WORK/$id.o1.one.stderr"
            o1_one_status=$?
            "$o2" >"$WORK/$id.o2.zero.stdout" \
                2>"$WORK/$id.o2.zero.stderr"
            o2_zero_status=$?
            "$o2" x >"$WORK/$id.o2.one.stdout" \
                2>"$WORK/$id.o2.one.stderr"
            o2_one_status=$?
            if [ "$host_zero_status" -ne 0 ] || [ "$host_one_status" -ne 0 ]; then
                fail "$id" "host C17 control returned $host_zero_status/$host_one_status"
            elif [ "$o0_zero_status" -ne 0 ] || [ "$o0_one_status" -ne 0 ]; then
                fail "$id" "O0 control returned $o0_zero_status/$o0_one_status"
            elif [ "$o1_zero_status" -ne 0 ] || [ "$o1_one_status" -ne 0 ]; then
                fail "$id" "O1 control returned $o1_zero_status/$o1_one_status"
            elif [ "$o2_zero_status" -eq 0 ] && [ "$o2_one_status" -eq 0 ]; then
                xpass "$id" "$title"
            elif [ "$o2_zero_status" -eq 1 ] && [ "$o2_one_status" -eq 1 ] && \
                 grep -q '^    %10 = load i32, %5, align 4, etype i32$' \
                     "$phase_dir/400006-ir-fp01-i01-p05-simplify_cfg.cgfir" && \
                 grep -q '^    ret i32 22$' \
                     "$phase_dir/400007-ir-fp01-i01-p06-gvn.cgfir"; then
                xfail "$id" "$title"
            else
                fail "$id" "unexpected O2 alias-selection result $o2_zero_status/$o2_one_status"
            fi
        fi
        ;;
    DRV-M-01)
        signal_as="$FIXTURES/support/drv-m-01/as-signal.sh"
        if [ ! -x "$signal_as" ]; then
            fail "$id" "signal-terminating assembler helper is not executable"
        else
            (exec env CGF_INCLUDE_DIR="$ROOT/include" \
                CGF_AS_PATH="$signal_as" "$CGF" -c "$source" \
                -o "$WORK/$id.o") >"$WORK/$id.stdout" \
                2>"$WORK/$id.stderr"
            status=$?
            rejected=$(grep -c 'assembler rejected' "$WORK/$id.stderr" || true)
            if [ "$status" -eq 1 ] && grep -Eq 'signal 15|signal SIGTERM' \
                   "$WORK/$id.stderr"; then
                xpass "$id" "$title"
            elif [ "$status" -eq 1 ] && [ "$rejected" -eq 1 ] &&
                 ! grep -Eq 'signal 15|signal SIGTERM' "$WORK/$id.stderr"; then
                xfail "$id" "$title"
            else
                fail "$id" "unexpected assembler-signal diagnostic (status $status, rejected $rejected)"
            fi
        fi
        ;;
    TI-M-01|TI-M-02)
        "$source" "$ROOT" >"$WORK/$id.stdout" 2>"$WORK/$id.stderr"
        status=$?
        case "$status" in
        0) xfail "$id" "$title" ;;
        1) xpass "$id" "$title" ;;
        *) fail "$id" "integrity reproducer failed with status $status" ;;
        esac
        ;;
    TI-M-03)
        "$source" "$ROOT" "$CGF" >"$WORK/$id.stdout" \
            2>"$WORK/$id.stderr"
        status=$?
        case "$status" in
        0) xfail "$id" "$title" ;;
        1) xpass "$id" "$title" ;;
        *) fail "$id" "integrity reproducer failed with status $status" ;;
        esac
        ;;
    DET-M-01|DET-M-02|DET-M-03)
        "$source" "$ROOT" >"$WORK/$id.stdout" 2>"$WORK/$id.stderr"
        status=$?
        case "$status" in
        0) xfail "$id" "$title" ;;
        1) xpass "$id" "$title" ;;
        *) fail "$id" "determinism evidence reproducer failed with status $status" ;;
        esac
        ;;
    MS-C-01)
        run_cgf "$id.control" -fsafe -fsyntax-only "$source"
        control_status=$?
        run_cgf "$id" -fsafe -Wno-mem-uninit-read -fsyntax-only "$source"
        status=$?
        if [ "$control_status" -ne 1 ] ||
           ! grep -q 'read of uninitialized heap memory' \
               "$WORK/$id.control.stderr"; then
            fail "$id" "safe-mode control did not diagnose the uninitialized read"
        elif [ "$status" -eq 1 ] &&
             grep -q 'read of uninitialized heap memory' \
                 "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -eq 0 ] && [ ! -s "$WORK/$id.stderr" ]; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected safe-mode warning-override result (status $status)"
        fi
        ;;
    MS-C-04)
        run_cgf "$id" -fsafe -fsyntax-only "$source"
        status=$?
        if [ "$status" -eq 1 ] && grep -qi 'null' "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -eq 0 ] && [ ! -s "$WORK/$id.stderr" ]; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected proven-null safe-mode result (status $status)"
        fi
        ;;
    MS-C-05)
        executable="$WORK/$id.exe"
        run_cgf_runtime "$id" 0 -fsafe "$source" -o "$executable"
        status=$?
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid safe-mode fixture failed to compile (status $status)"
        else
            "$executable" >"$WORK/$id.run.stdout" 2>"$WORK/$id.run.stderr"
            run_status=$?
            if [ "$run_status" -ne 0 ] &&
               grep -q 'cgf-safe: out-of-bounds' "$WORK/$id.run.stderr"; then
                xpass "$id" "$title"
            elif [ "$run_status" -eq 0 ]; then
                xfail "$id" "$title"
            else
                fail "$id" "unexpected far-out-of-bounds runtime result (status $run_status)"
            fi
        fi
        ;;
    MS-C-06)
        run_cgf "$id" -fsafe -fsyntax-only "$source"
        status=$?
        if [ "$status" -eq 1 ] &&
           grep -q 'asprintf/vasprintf.*not registered' "$WORK/$id.stderr"; then
            xpass "$id" "$title"
        elif [ "$status" -eq 0 ] && [ ! -s "$WORK/$id.stderr" ]; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected unregistered-allocator result (status $status)"
        fi
        ;;
    MS-M-02|MS-M-03)
        run_cgf "$id" -Wmem -fsyntax-only "$source"
        status=$?
        leaks=$(grep -c '\[-Wmem-leak\]' "$WORK/$id.stderr" || true)
        if [ "$status" -ne 0 ]; then
            fail "$id" "valid memory-analysis fixture was rejected"
        elif [ "$leaks" -eq 0 ] && [ ! -s "$WORK/$id.stderr" ]; then
            xpass "$id" "$title"
        elif [ "$leaks" -eq 1 ]; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected memory diagnostic output ($leaks leak diagnostics)"
        fi
        ;;
    RT-H-01)
        run_cgf "$id" --target=arm64-macos -std=c17 -pedantic-errors \
            -fsyntax-only "$source"
        status=$?
        if [ "$status" -eq 0 ]; then
            xpass "$id" "$title"
        elif [ "$status" -eq 1 ] &&
             grep -q 'the predefined size macro must describe the target type' \
                 "$WORK/$id.stderr" &&
             ! grep -q 'arm64-macos long double must follow the 8-byte Apple ABI' \
                 "$WORK/$id.stderr"; then
            xfail "$id" "$title"
        else
            fail "$id" "unexpected arm64-macos long-double macro result (status $status)"
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

while IFS="$tab" read -r id fixture fixture_state title extra; do
    case "$id" in
    ''|'#'*) continue ;;
    esac
    source="$FIXTURES/$fixture"
    probe "$id" "$source" "$title"
done <"$MANIFEST"

printf 'audit fixtures: %d checks; %d PASS, %d XFAIL, %d XPASS, %d FAIL\n' \
    "$checks" "$passes" "$xfails" "$xpasses" "$fails"
if [ "$xpasses" -ne 0 ] || [ "$fails" -ne 0 ]; then
    exit 1
fi
