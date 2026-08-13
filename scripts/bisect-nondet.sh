#!/bin/sh
# Compare two bootstrap stages and, when given the responsible source TU and
# compilers, localize the first divergence using public frontend dumps and
# one isolated CGF_DUMP_IR=all tree per compiler.
set -eu
LC_ALL=C
export LC_ALL

usage() {
    cat >&2 <<'EOF'
usage: bisect-nondet.sh STAGE1_ROOT STAGE2_ROOT
       [--source TU --stage1-cc CC --stage2-cc CC] [-- COMPILER_FLAGS...]
EOF
    exit 2
}

[ "$#" -ge 2 ] || usage
stage1=$1
stage2=$2
shift 2
[ -d "$stage1" ] || { echo "bisect-nondet: not a directory: $stage1" >&2; exit 2; }
[ -d "$stage2" ] || { echo "bisect-nondet: not a directory: $stage2" >&2; exit 2; }

source_tu=
stage1_cc=
stage2_cc=
while [ "$#" -gt 0 ]; do
    case $1 in
    --source) [ "$#" -ge 2 ] || usage; source_tu=$2; shift 2 ;;
    --stage1-cc) [ "$#" -ge 2 ] || usage; stage1_cc=$2; shift 2 ;;
    --stage2-cc) [ "$#" -ge 2 ] || usage; stage2_cc=$2; shift 2 ;;
    --) shift; break ;;
    *) usage ;;
    esac
done

if [ -n "$source_tu$stage1_cc$stage2_cc" ] &&
    { [ -z "$source_tu" ] || [ -z "$stage1_cc" ] || [ -z "$stage2_cc" ]; }; then
    echo "bisect-nondet: localization needs --source, --stage1-cc, and --stage2-cc" >&2
    exit 2
fi

require_optimizer_dumps=0
for flag in "$@"; do
    case $flag in
    -O0) require_optimizer_dumps=0 ;;
    -O*) require_optimizer_dumps=1 ;;
    esac
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bisect-nondet.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

make_list() {
    root=$1
    kind=$2
    out=$3
    case $kind in
    object)
        (cd "$root" && find . -type f -name '*.o' -print |
            sed 's#^\./##' | sort) >"$out"
        ;;
    assembly)
        (cd "$root" && find . -type f -name '*.s' -print |
            sed 's#^\./##' | sort) >"$out"
        ;;
    runtime)
        (cd "$root" && find . -type f \
            \( -name cgfried -o -name 'libcgf_rt.a' \) -print |
            sed 's#^\./##' | sort) >"$out"
        ;;
    esac
}

compare_group() {
    kind=$1
    plural=$2
    make_list "$stage1" "$kind" "$tmp/one.$kind"
    make_list "$stage2" "$kind" "$tmp/two.$kind"
    sort -u "$tmp/one.$kind" "$tmp/two.$kind" >"$tmp/all.$kind"
    if [ ! -s "$tmp/all.$kind" ]; then
        echo "bisect-nondet: no $kind artifacts found in either stage" >&2
        exit 2
    fi
    while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        if [ ! -f "$stage1/$rel" ]; then
            echo "bisect-nondet: first missing $kind: stage1: $rel"
            return 1
        fi
        if [ ! -f "$stage2/$rel" ]; then
            echo "bisect-nondet: first missing $kind: stage2: $rel"
            return 1
        fi
        if ! cmp -s "$stage1/$rel" "$stage2/$rel"; then
            echo "bisect-nondet: first differing $kind: $rel"
            return 1
        fi
    done <"$tmp/all.$kind"
    count=$(wc -l <"$tmp/all.$kind" | tr -d ' ')
    echo "$count" >"$tmp/count.$plural"
    return 0
}

artifact_mismatch=0
if ! compare_group object objects; then
    artifact_mismatch=1
elif ! compare_group assembly assemblies; then
    artifact_mismatch=1
elif ! compare_group runtime runtime; then
    artifact_mismatch=1
fi

run_dump() {
    cc=$1
    phase=$2
    out=$3
    err=$4
    shift 4
    case $phase in
    pp) "$cc" "$@" -E "$source_tu" >"$out" 2>"$err" ;;
    ast) "$cc" "$@" --dump-ast "$source_tu" >"$out" 2>"$err" ;;
    sema) "$cc" "$@" -fdump-sema "$source_tu" >"$out" 2>"$err" ;;
    esac
}

run_phase_tree() {
    cc=$1
    dir=$2
    asm=$3
    err=$4
    shift 4
    mkdir "$dir"
    env CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$dir" \
        "$cc" "$@" -S "$source_tu" -o "$asm" >"$asm.stdout" 2>"$err"
}

compare_phase_trees() {
    one=$1
    two=$2
    (cd "$one" && find . -type f -print | sed 's#^\./##' | sort) \
        >"$tmp/one.phases"
    (cd "$two" && find . -type f -print | sed 's#^\./##' | sort) \
        >"$tmp/two.phases"
    if [ ! -s "$tmp/one.phases" ] || [ ! -s "$tmp/two.phases" ]; then
        echo "bisect-nondet: phase-boundary dump tree is empty" >&2
        return 2
    fi
    for stage in stage1 stage2; do
        case $stage in
        stage1) dir=$one ;;
        stage2) dir=$two ;;
        esac
        for required in \
            'parse:100000-parse-ast.txt' \
            'sema:200000-sema.txt' \
            'lowering:300000-ir-post-lowering.cgfir' \
            'legalization:700000-ir-post-opt-legalized.cgfir' \
            'mir:800000-mir.txt' \
            'assembly:900000-asm.s'; do
            group=${required%%:*}
            file=${required#*:}
            if [ ! -f "$dir/$file" ]; then
                echo "bisect-nondet: missing phase group: $stage: $group" >&2
                return 2
            fi
        done
        if [ "$require_optimizer_dumps" -eq 1 ] &&
            ! find "$dir" -maxdepth 1 -type f \
                -name '[4-6][0-9][0-9][0-9][0-9][0-9]-ir-fp*-i*-p*-*.cgfir' \
                -print | grep . >/dev/null; then
            echo "bisect-nondet: missing phase group: $stage: optimizer" >&2
            return 2
        fi
    done
    sort -u "$tmp/one.phases" "$tmp/two.phases" >"$tmp/all.phases"
    while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        if [ ! -f "$one/$rel" ]; then
            echo "bisect-nondet: first missing phase boundary: stage1: $rel"
            return 1
        fi
        if [ ! -f "$two/$rel" ]; then
            echo "bisect-nondet: first missing phase boundary: stage2: $rel"
            return 1
        fi
        if ! cmp -s "$one/$rel" "$two/$rel"; then
            echo "bisect-nondet: first differing phase boundary: $rel"
            return 1
        fi
        echo "bisect-nondet: phase boundary $rel matches"
    done <"$tmp/all.phases"
    return 0
}

phase_mismatch=0
unavailable=0
if [ -n "$source_tu" ]; then
    [ -f "$source_tu" ] || { echo "bisect-nondet: source not found: $source_tu" >&2; exit 2; }
    echo "bisect-nondet: phase localization for $source_tu"
    for phase in pp ast sema; do
        rc1=0
        rc2=0
        run_dump "$stage1_cc" "$phase" "$tmp/one.$phase" \
            "$tmp/one.$phase.err" "$@" || rc1=$?
        run_dump "$stage2_cc" "$phase" "$tmp/two.$phase" \
            "$tmp/two.$phase.err" "$@" || rc2=$?
        if [ "$rc1" -ne 0 ] || [ "$rc2" -ne 0 ]; then
            echo "bisect-nondet: phase $phase unavailable (stage1=$rc1 stage2=$rc2)"
            unavailable=1
            break
        fi
        if ! cmp -s "$tmp/one.$phase" "$tmp/two.$phase"; then
            echo "bisect-nondet: first differing public phase: $phase"
            phase_mismatch=1
            break
        fi
        echo "bisect-nondet: phase $phase matches"
    done
    if [ "$phase_mismatch" -eq 0 ] && [ "$unavailable" -eq 0 ]; then
        rc1=0
        rc2=0
        run_phase_tree "$stage1_cc" "$tmp/one.dump" "$tmp/one.s" \
            "$tmp/one.dump.err" "$@" || rc1=$?
        run_phase_tree "$stage2_cc" "$tmp/two.dump" "$tmp/two.s" \
            "$tmp/two.dump.err" "$@" || rc2=$?
        if [ "$rc1" -ne 0 ] || [ "$rc2" -ne 0 ]; then
            echo "bisect-nondet: phase-boundary dumps unavailable (stage1=$rc1 stage2=$rc2)"
            unavailable=1
        elif ! compare_phase_trees "$tmp/one.dump" "$tmp/two.dump"; then
            phase_mismatch=1
        fi
    fi
    if [ "$phase_mismatch" -eq 0 ] && [ "$unavailable" -eq 0 ] &&
        [ "$artifact_mismatch" -ne 0 ]; then
        echo "bisect-nondet: phase dumps match through final asm; divergence begins at assembler/object or link/runtime emission"
    elif [ "$phase_mismatch" -eq 0 ] && [ "$unavailable" -ne 0 ]; then
        echo "bisect-nondet: localization incomplete because one or more phase dumps failed"
    fi
fi

if [ "$artifact_mismatch" -ne 0 ] || [ "$phase_mismatch" -ne 0 ]; then
    exit 1
fi

objects=$(cat "$tmp/count.objects" 2>/dev/null || echo 0)
assemblies=$(cat "$tmp/count.assemblies" 2>/dev/null || echo 0)
runtime=$(cat "$tmp/count.runtime" 2>/dev/null || echo 0)
echo "bisect-nondet: $objects object(s), $assemblies assembly file(s), and $runtime runtime artifact(s) match"
