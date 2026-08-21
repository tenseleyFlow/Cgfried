#!/bin/sh
# Sprint 44: end-to-end -fcgf-safe allocator/check/interposition contract.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
BUILD=${2:-build}
MODE=${3:-test}
WORK=${CGF_SAFE_WORK:-$BUILD/safe-runtime}
ROOT=tests/memsafe/rt
PERF=tests/perf/safe
fails=0
passes=0
skips=0

# Rust-free CI checkouts intentionally do not build the bundled assembler.
# Exercise the system-as route unless the caller selected a tool explicitly;
# CGF_AS_PATH still has the driver's documented higher precedence.
: "${CGF_AS:=0}"
export CGF_AS

case $WORK in
*/safe-runtime | */safe-bench) ;;
*)
    echo "safe_runtime: refusing unsafe work directory: $WORK" >&2
    exit 2
    ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

fail()
{
    echo "safe_runtime: FAIL: $*" >&2
    fails=$((fails + 1))
}

compile()
{
    name=$1
    opt=$2
    shift 2
    "$CGF" -fcgf-safe "$opt" "$@" -o "$WORK/$name" \
        >"$WORK/$name.compile.out" 2>"$WORK/$name.compile.err"
}

run_plain()
{
    name=$1
    if "$WORK/$name" >"$WORK/$name.out" 2>"$WORK/$name.err"; then
        passes=$((passes + 1))
    else
        rc=$?
        fail "$name exited $rc; expected 0"
        sed -n '1,12p' "$WORK/$name.err" >&2
    fi
}

run_abort_once()
{
    exe=$1
    out=$2
    err=$3
    sh -c '"$1" >"$2" 2>"$3"' _ "$exe" "$out" "$err" 2>/dev/null
    return $?
}

run_trap_once()
{
    exe=$1
    out=$2
    err=$3
    env CGF_SAFE_ABORT=trap sh -c '"$1" >"$2" 2>"$3"' _ "$exe" "$out" \
        "$err" 2>/dev/null
    return $?
}

run_abort()
{
    name=$1
    pattern=$2
    rc=0
    run_abort_once "$WORK/$name" "$WORK/$name.out" "$WORK/$name.err" || rc=$?
    if [ "$rc" -ne 134 ]; then
        fail "$name exited $rc; expected SIGABRT status 134"
        sed -n '1,12p' "$WORK/$name.err" >&2
        return
    fi
    if ! grep -Fq "$pattern" "$WORK/$name.err"; then
        fail "$name diagnostic lacks '$pattern'"
        sed -n '1,12p' "$WORK/$name.err" >&2
        return
    fi
    if ! grep -Fq "CGF_SAFE_ABORT: aborting" "$WORK/$name.err"; then
        fail "$name diagnostic lacks the flushed abort marker"
        return
    fi

    rc=0
    run_abort_once "$WORK/$name" "$WORK/$name.out.2" \
        "$WORK/$name.err.2" || rc=$?
    if [ "$rc" -ne 134 ] || ! cmp -s "$WORK/$name.err" "$WORK/$name.err.2"; then
        fail "$name diagnostic or signal is not deterministic"
        return
    fi
    passes=$((passes + 1))
}

run_test_suite()
{
    for opt in -O0 -O2; do
        suffix=$(echo "$opt" | tr -d -)
        for spec in \
            'uaf-read:cgf-safe: use-after-free: 4-byte read at p+0' \
            'uaf-write:cgf-safe: use-after-free: 4-byte write at p+0' \
            'double-free:cgf-safe: double-free' \
            'invalid-free:cgf-safe: invalid-free: interior pointer' \
            'heap-overflow-write:cgf-safe: heap-overflow-at-free' \
            'oob-read:cgf-safe: out-of-bounds: 4-byte read at p+16' \
            'far-oob-read:cgf-safe: out-of-bounds pointer index' \
            'far-negative-read:cgf-safe: out-of-bounds pointer index' \
            'origin-cross-allocation:cgf-safe: out-of-bounds pointer index' \
            'select-far-read:cgf-safe: out-of-bounds pointer index' \
            'loop-instance-far-read:cgf-safe: out-of-bounds pointer index' \
            'load-store-far-read:cgf-safe: out-of-bounds pointer index' \
            'helper-call-far-read:cgf-safe: out-of-bounds pointer index' \
            'one-past-read:cgf-safe: out-of-bounds: 1-byte read at p+8' \
            'crossing-read:cgf-safe: out-of-bounds: 4-byte read at p+7' \
            'modular-wrap-derive:cgf-safe: out-of-bounds pointer derivation' \
            'null-origin-derived:cgf-safe: null-pointer-access' \
            'index-int64-min:cgf-safe: out-of-bounds pointer index' \
            'index-uint64-max:cgf-safe: out-of-bounds pointer index' \
            'index-multiply-wrap:cgf-safe: out-of-bounds pointer index' \
            'pointer-plus-multiply-wrap:cgf-safe: out-of-bounds pointer index' \
            'pointer-minus-uint64-max:cgf-safe: out-of-bounds pointer index' \
            'vla-pointer-index-wrap:cgf-safe: out-of-bounds pointer index' \
            'uintptr-far-plus:cgf-safe: out-of-bounds uintptr_t round trip' \
            'uintptr-far-minus:cgf-safe: out-of-bounds uintptr_t round trip' \
            'uintptr-far-tag:cgf-safe: out-of-bounds uintptr_t round trip' \
            'uintptr-far-mask:cgf-safe: out-of-bounds uintptr_t round trip' \
            'va-start-oob:cgf-safe: out-of-bounds: 24-byte write at p+0'
        do
            fixture=${spec%%:*}
            pattern=${spec#*:}
            name=$fixture-$suffix
            if compile "$name" "$opt" "$ROOT/$fixture.c"; then
                run_abort "$name" "$pattern"
            else
                fail "$name did not compile"
                sed -n '1,12p' "$WORK/$name.compile.err" >&2
            fi
        done

        for fixture in churn foreign alignment allocator-family zero-byte-memory \
            uintptr-roundtrip-valid; do
            name=$fixture-$suffix
            if compile "$name" "$opt" "$ROOT/$fixture.c"; then
                run_plain "$name"
            else
                fail "$name did not compile"
                sed -n '1,12p' "$WORK/$name.compile.err" >&2
            fi
        done
    done

    if command -v gcc >/dev/null 2>&1; then
        if gcc -std=c11 -O2 -w -c "$ROOT/mixed-gcc.c" \
            -o "$WORK/mixed-gcc.o" && \
           "$CGF" -O2 -fcgf-safe -c "$ROOT/mixed-cgf.c" \
            -o "$WORK/mixed-cgf.o" >"$WORK/mixed.compile.out" \
            2>"$WORK/mixed.compile.err" && \
           "$CGF" -fcgf-safe "$WORK/mixed-cgf.o" "$WORK/mixed-gcc.o" \
            -o "$WORK/mixed" >>"$WORK/mixed.compile.out" \
            2>>"$WORK/mixed.compile.err"; then
            run_plain mixed
        else
            fail "mixed gcc/cgf translation units did not compile/link"
            sed -n '1,16p' "$WORK/mixed.compile.err" >&2
        fi
    else
        echo 'HARNESS_SKIP suite=safe-runtime test=mixed count=1 reason="no gcc"'
        skips=$((skips + 1))
    fi

    if compile static-wrap -O2 -static "$ROOT/allocator-family.c"; then
        run_plain static-wrap
    else
        echo 'HARNESS_SKIP suite=safe-runtime test=static-wrap count=1 reason="static libc/toolchain unavailable"'
        skips=$((skips + 1))
    fi

    if compile threads -O2 "$ROOT/threads.c" -lpthread; then
        run_plain threads
    else
        fail "threaded registry fixture did not compile"
        sed -n '1,12p' "$WORK/threads.compile.err" >&2
    fi

    if CGF_MEMSAFE_DUMP=1 "$CGF" -fcgf-safe -O2 \
        "$ROOT/annotated-discharge.c" -o "$WORK/annotated-discharge" \
        >"$WORK/annotated-discharge.out" \
        2>"$WORK/annotated-discharge.err" && \
       awk '/^checks: [0-9]+ total, [0-9]+ discharged, [0-9]+ emitted$/ {
                total = $2 + 0
                discharged = $4 + 0
                emitted = $6 + 0
                found = 1
            }
            END { exit !(found && discharged > 0 &&
                         total == discharged + emitted) }
       ' "$WORK/annotated-discharge.err"; then
        passes=$((passes + 1))
    else
        fail "annotated real-C accounting did not preserve static discharge"
        sed -n '1,16p' "$WORK/annotated-discharge.out" >&2
        sed -n '1,16p' "$WORK/annotated-discharge.err" >&2
    fi

    if compile trap -O2 "$ROOT/double-free.c"; then
        rc=0
        run_trap_once "$WORK/trap" "$WORK/trap.out" \
            "$WORK/trap.err" || rc=$?
        case $rc in
        132|133)
            if grep -Fq 'cgf-safe: double-free' "$WORK/trap.err"; then
                passes=$((passes + 1))
            else
                fail "trap mode lost its deterministic diagnostic"
            fi
            ;;
        *) fail "trap mode exited $rc; expected SIGILL/SIGTRAP status 132/133" ;;
        esac
    else
        fail "trap fixture did not compile"
    fi

    [ "$fails" -eq 0 ] || exit 1
    echo "safe_runtime: $passes checks passed, $skips skipped; O0/O2 diagnostics deterministic; foreign/mixed/alignment/interposition green"
}

run_bench_suite()
{
    host_cc=${CC:-cc}
    "$host_cc" -std=c11 -Wall -Wextra -Werror -O2 \
        "$PERF/measure.c" -o "$WORK/measure" || exit 1
    for fixture in alloc-churn pointer-chase memcpy-heavy; do
        "$CGF" -O2 -Wno-mem "$PERF/$fixture.c" \
            -o "$WORK/$fixture.base" || exit 1
        "$CGF" -O2 -Wno-mem -fcgf-safe "$PERF/$fixture.c" \
            -o "$WORK/$fixture.safe" || exit 1
        "$WORK/measure" "$WORK/$fixture.base" \
            >"$WORK/$fixture.base.time" || exit 1
        "$WORK/measure" "$WORK/$fixture.safe" \
            >"$WORK/$fixture.safe.time" || exit 1
        awk -v name="$fixture" '
            NR == FNR { bt=$1; bm=$2; next }
            {
                st=$1; sm=$2
                if (bm < 1) bm=1
                printf "safe-bench: %s time %.2fx rss %.2fx (advisory; budgets <2.5x/<2x)\n", name, st/bt, sm/bm
            }
        ' "$WORK/$fixture.base.time" "$WORK/$fixture.safe.time"
    done
}

case $MODE in
test) run_test_suite ;;
bench) run_bench_suite ;;
*) echo "usage: $0 [compiler] [build-root] [test|bench]" >&2; exit 2 ;;
esac
