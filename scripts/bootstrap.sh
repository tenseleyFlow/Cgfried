#!/bin/sh
# Sprint 58: deterministic stage0 -> stage1 -> stage2 fixed-point bootstrap.
set -eu
LC_ALL=C
export LC_ALL

usage() {
    echo "usage: scripts/bootstrap.sh O0|O2" >&2
    exit 2
}

[ "$#" -eq 1 ] || usage
level=$1
case $level in
O0 | O2) ;;
*) usage ;;
esac

repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
work_input=${CGF_BOOTSTRAP_WORK:-$repo/build/boot/$level}
jobs=${CGF_BOOTSTRAP_JOBS:-}
hostcc_input=${HOSTCC:-cc}
bootstrap_host=${CGF_BOOTSTRAP_HOST:-}
bootstrap_host_class=${CGF_BOOTSTRAP_HOST_CLASS-}
control_input=${CGF_BOOTSTRAP_CONTROL_FILE:-}
sysroot_input=${CGF_BOOTSTRAP_SYSROOT:-}

if [ -n "$bootstrap_host" ] && [ -n "$bootstrap_host_class" ]; then
    echo "bootstrap: set only one of CGF_BOOTSTRAP_HOST or CGF_BOOTSTRAP_HOST_CLASS" >&2
    exit 2
fi
if [ -z "$bootstrap_host" ] && [ -z "$bootstrap_host_class" ]; then
    bootstrap_host_class=local
fi
if [ -z "$bootstrap_host" ] && [ -n "$control_input" ]; then
    echo "bootstrap: CGF_BOOTSTRAP_CONTROL_FILE requires CGF_BOOTSTRAP_HOST" >&2
    exit 2
fi
case ${bootstrap_host:-$bootstrap_host_class} in
*[!A-Za-z0-9_.:+-]* | '')
    echo "bootstrap: invalid timing host provenance" >&2
    exit 2
    ;;
esac

if [ -n "$sysroot_input" ]; then
    case $sysroot_input in
    /*) ;;
    *)
        echo "bootstrap: CGF_BOOTSTRAP_SYSROOT must be absolute" >&2
        exit 2
        ;;
    esac
    case $sysroot_input in
    *[[:space:]]*)
        echo "bootstrap: CGF_BOOTSTRAP_SYSROOT cannot contain whitespace" >&2
        exit 2
        ;;
    esac
    [ -d "$sysroot_input" ] || {
        echo "bootstrap: CGF_BOOTSTRAP_SYSROOT is not a directory: $sysroot_input" >&2
        exit 2
    }
    sysroot=$(CDPATH='' cd "$sysroot_input" && pwd -P)
else
    sysroot=
fi

case $hostcc_input in
*' '*)
    echo "bootstrap: HOSTCC must name one executable, not contain flags: $hostcc_input" >&2
    exit 2
    ;;
esac

resolve_tool() {
    resolved=$(command -v "$1" 2>/dev/null || true)
    if [ -z "$resolved" ] || [ ! -x "$resolved" ]; then
        echo "bootstrap: required tool not found: $1" >&2
        exit 2
    fi
    case $resolved in
    /*) ;;
    *) resolved=$(CDPATH='' cd "${resolved%/*}" && pwd -P)/${resolved##*/} ;;
    esac
    printf '%s\n' "$resolved"
}

hostcc=$(resolve_tool "$hostcc_input")
make_cmd=$(resolve_tool "${MAKE:-make}")
assembler=$(resolve_tool "${AS:-as}")
archiver=$(resolve_tool "${AR:-ar}")
sha256=$(resolve_tool sha256sum)

if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
fi
case $jobs in
'' | *[!0-9]* | 0)
    echo "bootstrap: CGF_BOOTSTRAP_JOBS must be a positive integer" >&2
    exit 2
    ;;
esac

case $work_input in
/*) work=$work_input ;;
*) work=$repo/$work_input ;;
esac
parent=${work%/*}
base=${work##*/}
case $base in
O0 | O2 | O0-* | O2-* | cgf-bootstrap-*) ;;
*)
    echo "bootstrap: refusing work directory with unsafe basename: $work" >&2
    exit 2
    ;;
esac
mkdir -p "$parent"
parent=$(CDPATH='' cd "$parent" && pwd -P)
work=$parent/$base
if [ "$parent" = / ] || [ "$work" = "$repo" ] || [ -L "$work" ]; then
    echo "bootstrap: refusing unsafe work directory: $work" >&2
    exit 2
fi
owned_marker=$work/.cgfried-bootstrap-owned
repo_build=$repo/build
owned=0
case $work in
"$repo_build"/*) owned=1 ;;
esac
if [ "$owned" -eq 0 ] && [ -f "$owned_marker" ]; then
    marker_schema=$(sed -n '1p' "$owned_marker")
    marker_path=$(sed -n '2p' "$owned_marker")
    marker_extra=$(sed -n '3p' "$owned_marker")
    if [ "$marker_schema" = 'schema=cgfried.bootstrap-work.v1' ] &&
        [ "$marker_path" = "path=$work" ] && [ -z "$marker_extra" ]; then
        owned=1
    fi
fi
if [ -e "$work" ]; then
    [ -d "$work" ] || {
        echo "bootstrap: work path is not a directory: $work" >&2
        exit 2
    }
    if [ "$owned" -eq 1 ]; then
        rm -rf -- "$work"
    elif find "$work" -mindepth 1 -print -quit | grep . >/dev/null 2>&1; then
        echo "bootstrap: refusing nonempty unowned work directory: $work" >&2
        exit 2
    fi
fi
mkdir -p "$work"
{
    echo 'schema=cgfried.bootstrap-work.v1'
    echo "path=$work"
} >"$owned_marker"
# Recreate the installed-layout include relationship for every stage binary:
# each executable lives one directory below this immutable header snapshot,
# so its ordinary `<exedir>/../include` discovery works without a CGF_*
# environment override.
cp -R "$repo/include" "$work/include"

control_file=$work/timing-control.txt
prepare_control() {
    [ -n "$bootstrap_host" ] || return 0
    if [ -n "$control_input" ]; then
        [ -r "$control_input" ] || {
            echo "bootstrap: cannot read timing control file: $control_input" >&2
            exit 2
        }
        control_source=$control_input
    else
        control_source=$work/timing-control.captured
        "$repo/scripts/bootstrap-control.sh" "$bootstrap_host" \
            "$control_source"
    fi
    control_script=$repo/scripts/bench-control.sh
    control_status=0
    control_class=$($control_script classify --require-v2 "$control_source") ||
        control_status=$?
    case $control_status:$control_class in
    0:controlled) ;;
    1:provenance-only)
        echo "bootstrap: named-host timing control is provenance-only" >&2
        exit 2
        ;;
    3:*) exit 2 ;;
    *)
        echo "bootstrap: timing control classifier failed" >&2
        exit 2
        ;;
    esac
    canonical=$work/timing-control.canonical
    awk -F= -v expected_host="$bootstrap_host" '
        BEGIN {
            order[1]="host"; order[2]="governor"; order[3]="power_profile"
            order[4]="scaling_driver"
            order[5]="energy_performance_preference"
            order[6]="control_protocol"; order[7]="logical_cpus"
            order[8]="cpu_idle_pct"; order[9]="load1"
        }
        /^[[:space:]]*($|#)/ { next }
        {
            key=$1
            if (!(key=="host" || key=="governor" || key=="power_profile" ||
                  key=="scaling_driver" ||
                  key=="energy_performance_preference" ||
                  key=="control_protocol" || key=="logical_cpus" ||
                  key=="cpu_idle_pct" || key=="load1") || NF != 2 ||
                $2 == "" || ++count[key] != 1)
                bad=1
            value[key]=$2
        }
        END {
            for (i=1; i<=9; i++)
                if (count[order[i]] != 1) bad=1
            if (value["host"] != expected_host) bad=1
            if (bad) exit 2
            for (i=1; i<=9; i++)
                print order[i] "=" value[order[i]]
        }
    ' "$control_source" >"$canonical" || {
        echo "bootstrap: timing control file is not canonical" >&2
        exit 2
    }
    mv "$canonical" "$control_file"
}

fixed_path=/usr/bin:/bin
for tool in "$hostcc" "$make_cmd" "$assembler" "$archiver" "$sha256"; do
    tool_dir=${tool%/*}
    case :$fixed_path: in
    *:"$tool_dir":*) ;;
    *) fixed_path=$tool_dir:$fixed_path ;;
    esac
done

stage0=$work/stage0
stage1=$work/stage1
stage2=$work/stage2
report=$work/bootstrap-report.txt
cgf_rev=$(git -C "$repo" rev-parse --verify HEAD 2>/dev/null || echo unavailable)
if [ -z "$(git -C "$repo" status --porcelain --untracked-files=normal \
    --ignore-submodules=none 2>/dev/null || echo unavailable)" ]; then
    cgf_tree=clean
else
    cgf_tree=dirty
fi

{
    echo 'schema=cgfried.bootstrap.v1'
    echo "level=$level"
    echo 'normalization=none'
    echo 'locale=C'
    echo 'source_date_epoch=0'
    echo "jobs=$jobs"
    if [ -n "$sysroot" ]; then
        echo "sysroot=$sysroot"
    else
        echo 'sysroot=none'
    fi
    echo "hostcc=$hostcc"
    echo "assembler=$assembler"
    echo "archiver=$archiver"
    echo "make=$make_cmd"
    printf 'hostcc_version='
    "$hostcc" --version 2>/dev/null | sed -n '1p'
    printf 'assembler_version='
    "$assembler" --version 2>/dev/null | sed -n '1p'
    printf 'archiver_version='
    "$archiver" --version 2>/dev/null | sed -n '1p'
    echo 'state=building-stage0'
} >"$report"

echo "bootstrap-$level: host compiler builds stage0"
(cd "$repo" && env -i PATH="$fixed_path" LC_ALL=C SOURCE_DATE_EPOCH=0 \
    "$make_cmd" -s -j"$jobs" BUILD="$stage0" CC="$hostcc" \
    "$stage0/cgfried" "$stage0/timeit")

target=$(env -i PATH="$fixed_path" LC_ALL=C SOURCE_DATE_EPOCH=0 \
    "$stage0/cgfried" -dumpmachine)
case $target in
x86_64-linux-gnu | arm64-linux) ;;
*)
    echo "bootstrap: native fixed-link bootstrap is unsupported for target $target" >&2
    exit 2
    ;;
esac

if [ -n "$sysroot" ]; then
    target_flag=--sysroot=$sysroot
else
    target_flag=
fi
stage_build() {
    compiler=$1
    output=$2
    (cd "$repo" && env -i PATH="$fixed_path" LC_ALL=C SOURCE_DATE_EPOCH=0 \
        "$make_cmd" -f ci/bootstrap.mk -j"$jobs" all \
        BOOTSTRAP_ROOT="$output" BOOTSTRAP_CGF="$compiler" \
        BOOTSTRAP_HOSTCC="$hostcc" BOOTSTRAP_AS="$assembler" \
        BOOTSTRAP_AR="$archiver" BOOTSTRAP_LEVEL="$level" \
        BOOTSTRAP_TARGET="$target" BOOTSTRAP_TARGET_FLAG="$target_flag")
}

echo 'state=building-stage1' >>"$report"
echo "bootstrap-$level: stage0 Cgfried builds stage1 compiler and runtime"
stage_build "$stage0/cgfried" "$stage1"

prepare_control
echo 'state=building-stage2' >>"$report"
echo "bootstrap-$level: stage1 Cgfried self-builds stage2 (timed)"
env -i PATH="$fixed_path" LC_ALL=C SOURCE_DATE_EPOCH=0 \
    "$stage0/timeit" -n 1 -w 0 -t 7200 -o "$work/stage1-time.raw" -- \
    "$make_cmd" -s --no-print-directory -C "$repo" -f ci/bootstrap.mk \
    -j"$jobs" all \
    BOOTSTRAP_ROOT="$stage2" BOOTSTRAP_CGF="$stage1/cgfried" \
    BOOTSTRAP_HOSTCC="$hostcc" BOOTSTRAP_AS="$assembler" \
    BOOTSTRAP_AR="$archiver" BOOTSTRAP_LEVEL="$level" \
    BOOTSTRAP_TARGET="$target" BOOTSTRAP_TARGET_FLAG="$target_flag" \
    >"$work/stage1-time.txt"

mv "$work/stage1-time.txt" "$work/stage1-time.metrics"
{
    echo 'schema=cgfried.bootstrap-timing.v1'
    echo "target=$target"
    if [ -n "$bootstrap_host" ]; then
        echo "host=$bootstrap_host"
        sed '/^host=/d' "$control_file"
    else
        echo "host_class=$bootstrap_host_class"
    fi
    date -u '+date=%Y-%m-%dT%H:%M:%SZ'
    echo "cgf_rev=$cgf_rev"
    echo "cgf_tree=$cgf_tree"
    echo 'protocol=cgfried-bootstrap-v1'
    echo "level=$level"
    echo "jobs=$jobs"
    echo 'normalization=none'
    if [ -n "$sysroot" ]; then
        echo "sysroot=$sysroot"
    else
        echo 'sysroot=none'
    fi
    echo "compiler=$stage1/cgfried"
    echo "compiler_sha256=$($sha256 "$stage1/cgfried" | awk '{print $1}')"
    sed "s/^wall_ms_median=/stage1.$level.wall_ms_median=/; \
         s/^wall_ms_mad=/stage1.$level.wall_ms_mad=/; \
         s/^user_ms_median=/stage1.$level.user_ms_median=/; \
         s/^sys_ms_median=/stage1.$level.sys_ms_median=/; \
         s/^maxrss_kb_max=/stage1.$level.maxrss_kb_max=/" \
        "$work/stage1-time.metrics"
    echo "stage1.$level.raw_samples=$work/stage1-time.raw"
} >"$work/stage1-time.txt"
rm -f "$work/stage1-time.metrics"

stage_manifest() {
    stage=$1
    out=$2
    {
        echo 'schema=cgfried.bootstrap-artifacts.v1'
        echo "target=$target"
        echo "level=$level"
        (cd "$stage" && find compiler runtime -type f \
            \( -name '*.s' -o -name '*.o' \) -print | LC_ALL=C sort |
            while IFS= read -r path; do
                "$sha256" "$path"
            done
            "$sha256" "$target/libcgf_rt.a" cgfried)
    } >"$out"
}

stage_manifest "$stage1" "$stage1/artifacts.sha256"
stage_manifest "$stage2" "$stage2/artifacts.sha256"

compare_tree_kind() {
    label=$1
    root=$2
    pattern=$3
    one=$work/stage1-$label.list
    two=$work/stage2-$label.list
    (cd "$stage1/$root" && find . -type f -name "$pattern" -print |
        sed 's#^\./##' | LC_ALL=C sort) >"$one"
    (cd "$stage2/$root" && find . -type f -name "$pattern" -print |
        sed 's#^\./##' | LC_ALL=C sort) >"$two"
    if ! cmp -s "$one" "$two"; then
        echo "bootstrap-$level: $label path manifests differ" >&2
        diff -u "$one" "$two" >&2 || true
        return 1
    fi
    count=0
    while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        count=$((count + 1))
        if ! cmp -s "$stage1/$root/$rel" "$stage2/$root/$rel"; then
            echo "bootstrap-$level: first differing $label: $root/$rel" >&2
            return 1
        fi
    done <"$one"
    if [ "$count" -eq 0 ]; then
        echo "bootstrap-$level: no $label artifacts were produced" >&2
        return 1
    fi
    echo "$label=$count" >>"$work/comparison.txt"
}

: >"$work/comparison.txt"
comparison_failed=0
# Causal order: compiler output before external container formats.
compare_tree_kind compiler_assembly compiler '*.s' || comparison_failed=1
if [ "$comparison_failed" -eq 0 ]; then
    compare_tree_kind runtime_assembly runtime '*.s' || comparison_failed=1
fi
if [ "$comparison_failed" -eq 0 ]; then
    compare_tree_kind compiler_objects compiler '*.o' || comparison_failed=1
fi
if [ "$comparison_failed" -eq 0 ]; then
    compare_tree_kind runtime_objects runtime '*.o' || comparison_failed=1
fi
if [ "$comparison_failed" -eq 0 ] &&
    ! cmp -s "$stage1/$target/libcgf_rt.a" "$stage2/$target/libcgf_rt.a"; then
    echo "bootstrap-$level: runtime archives differ" >&2
    comparison_failed=1
fi
if [ "$comparison_failed" -eq 0 ] &&
    ! cmp -s "$stage1/cgfried" "$stage2/cgfried"; then
    echo "bootstrap-$level: compiler binaries differ" >&2
    comparison_failed=1
fi
if [ "$comparison_failed" -ne 0 ]; then
    echo 'state=identity-failed' >>"$report"
    sh "$repo/scripts/bisect-nondet.sh" "$stage1" "$stage2" || true
    exit 1
fi

echo 'runtime_archive=1' >>"$work/comparison.txt"
echo 'compiler_binary=1' >>"$work/comparison.txt"

echo "bootstrap-$level: stage2 compiler smoke"
env -i PATH="$fixed_path" LC_ALL=C SOURCE_DATE_EPOCH=0 CGF_AS=0 \
    CGF_SMOKE_WORK="$work/smoke" \
    sh "$repo/scripts/smoke.sh" "$stage2/cgfried"

{
    echo "target=$target"
    "$sha256" "$stage1/cgfried" "$stage2/cgfried"
    "$sha256" "$stage1/$target/libcgf_rt.a" \
        "$stage2/$target/libcgf_rt.a"
    echo 'state=passed'
} >>"$report"

assembly_count=$(awk -F= '/_assembly=/{n += $2} END {print n + 0}' \
    "$work/comparison.txt")
object_count=$(awk -F= '/_objects=/{n += $2} END {print n + 0}' \
    "$work/comparison.txt")
echo "bootstrap-$level: PASS ($assembly_count assembly files, $object_count objects, runtime archive, and compiler are byte-identical; normalization=none)"
