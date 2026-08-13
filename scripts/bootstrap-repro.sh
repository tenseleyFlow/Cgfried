#!/bin/sh
# Rebuild a completed bootstrap with one job and a different output root.
# This is the weekly -j1/-jN and build-path independence probe.
set -eu
LC_ALL=C
export LC_ALL

usage()
{
    echo 'usage: bootstrap-repro.sh O0|O2 BOOTSTRAP_ROOT OUTPUT_ROOT' >&2
    exit 2
}

[ "$#" -eq 3 ] || usage
level=$1
bootstrap_root=$2
output_root=$3
case $level in
O0 | O2) ;;
*) usage ;;
esac
case $output_root in
build/boot/*) ;;
*)
    echo "bootstrap-repro: output must be below build/boot: $output_root" >&2
    exit 2
    ;;
esac
case /$output_root/ in
*'/../'* | *'/./'* | *'//'*)
    echo "bootstrap-repro: output root is not canonical: $output_root" >&2
    exit 2
    ;;
esac

repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
case $bootstrap_root in
/*) source_root=$bootstrap_root ;;
*) source_root=$repo/$bootstrap_root ;;
esac
output=$repo/$output_root
stage1=$source_root/stage1/cgfried
reference=$source_root/stage2
[ -x "$stage1" ] || {
    echo "bootstrap-repro: stage1 compiler is missing: $stage1" >&2
    exit 2
}
[ -x "$reference/cgfried" ] || {
    echo "bootstrap-repro: reference stage2 is missing: $reference" >&2
    exit 2
}
report=$source_root/bootstrap-report.txt
[ -r "$report" ] || {
    echo "bootstrap-repro: bootstrap report is missing: $report" >&2
    exit 2
}
report_field()
{
    field=$1
    awk -F= -v field="$field" '
        $1 == field { value=substr($0, length($1) + 2); count++ }
        END { if (count != 1 || value == "") exit 2; print value }
    ' "$report"
}
reference_schema=$(report_field schema) || {
    echo "bootstrap-repro: bootstrap report lacks one schema field" >&2
    exit 2
}
[ "$reference_schema" = cgfried.bootstrap.v1 ] || {
    echo "bootstrap-repro: bootstrap report schema is invalid" >&2
    exit 2
}
reference_target=$(report_field target) || {
    echo "bootstrap-repro: bootstrap report lacks one target field" >&2
    exit 2
}
case $reference_target in
x86_64-linux-gnu | arm64-linux) ;;
*)
    echo "bootstrap-repro: bootstrap report target is invalid: $reference_target" >&2
    exit 2
    ;;
esac
awk -F= '
    $1 == "state" && $2 == "passed" { passed++ }
    $1 == "state" && $2 == "identity-failed" { failed++ }
    END { if (passed != 1 || failed != 0) exit 2 }
' "$report" || {
    echo "bootstrap-repro: reference is not a passed raw fixed point" >&2
    exit 2
}
reference_jobs=$(report_field jobs) || {
    echo "bootstrap-repro: bootstrap report lacks one jobs field" >&2
    exit 2
}
case $reference_jobs in
'' | *[!0-9]* | 0)
    echo "bootstrap-repro: bootstrap report has invalid jobs=$reference_jobs" >&2
    exit 2
    ;;
esac
[ "$reference_jobs" -eq 8 ] || {
    echo "bootstrap-repro: reference must be the specified -j8 lane (got -j$reference_jobs)" >&2
    exit 2
}
reference_level=$(report_field level) || {
    echo "bootstrap-repro: bootstrap report lacks one level field" >&2
    exit 2
}
[ "$reference_level" = "$level" ] || {
    echo "bootstrap-repro: requested $level but reference is $reference_level" >&2
    exit 2
}
reference_normalization=$(report_field normalization) || {
    echo "bootstrap-repro: bootstrap report lacks normalization" >&2
    exit 2
}
[ "$reference_normalization" = none ] || {
    echo "bootstrap-repro: reference applied normalization" >&2
    exit 2
}
recorded_hostcc=$(report_field hostcc) || {
    echo "bootstrap-repro: bootstrap report lacks hostcc" >&2
    exit 2
}
recorded_assembler=$(report_field assembler) || {
    echo "bootstrap-repro: bootstrap report lacks assembler" >&2
    exit 2
}
recorded_archiver=$(report_field archiver) || {
    echo "bootstrap-repro: bootstrap report lacks archiver" >&2
    exit 2
}
recorded_make=$(report_field make) || {
    echo "bootstrap-repro: bootstrap report lacks make" >&2
    exit 2
}
recorded_sysroot=$(report_field sysroot) || {
    echo "bootstrap-repro: bootstrap report lacks sysroot" >&2
    exit 2
}

resolve_tool()
{
    resolved=$(command -v "$1" 2>/dev/null || true)
    if [ -z "$resolved" ] || [ ! -x "$resolved" ]; then
        echo "bootstrap-repro: required tool not found: $1" >&2
        exit 2
    fi
    case $resolved in
    /*) ;;
    *) resolved=$(CDPATH='' cd "${resolved%/*}" && pwd -P)/${resolved##*/} ;;
    esac
    printf '%s\n' "$resolved"
}

make_cmd=$(resolve_tool "${MAKE:-$recorded_make}")
hostcc=$(resolve_tool "${HOSTCC:-$recorded_hostcc}")
assembler=$(resolve_tool "${AS:-$recorded_assembler}")
archiver=$(resolve_tool "${AR:-$recorded_archiver}")
sha256=$(resolve_tool sha256sum)
require_same_tool()
{
    actual=$1
    expected=$2
    label=$3
    [ "$actual" = "$expected" ] || {
        echo "bootstrap-repro: $label differs from reference ($actual != $expected)" >&2
        exit 2
    }
}
require_same_tool "$make_cmd" "$recorded_make" make
require_same_tool "$hostcc" "$recorded_hostcc" \
    'host compiler/linker frontend'
require_same_tool "$assembler" "$recorded_assembler" assembler
require_same_tool "$archiver" "$recorded_archiver" archiver
fixed_path=/usr/bin:/bin
for tool in "$make_cmd" "$hostcc" "$assembler" "$archiver" "$sha256"; do
    tool_dir=${tool%/*}
    case :$fixed_path: in
    *:"$tool_dir":*) ;;
    *) fixed_path=$tool_dir:$fixed_path ;;
    esac
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bootstrap-repro.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
validate_stage_manifest()
{
    stage=$1
    label=$2
    manifest=$stage/artifacts.sha256
    actual=$tmp/$label.actual
    hashes=$tmp/$label.hashes
    listed=$tmp/$label.listed

    [ -r "$manifest" ] || {
        echo "bootstrap-repro: $label artifact manifest is missing" >&2
        exit 2
    }
    awk -F= -v expected_target="$reference_target" \
        -v expected_level="$level" '
        $1 == "schema" && $2 == "cgfried.bootstrap-artifacts.v1" { schema++ }
        $1 == "target" && $2 == expected_target { target++ }
        $1 == "level" && $2 == expected_level { level++ }
        END { if (schema != 1 || target != 1 || level != 1) exit 2 }
    ' "$manifest" || {
        echo "bootstrap-repro: $label artifact manifest provenance is invalid" >&2
        exit 2
    }
    [ -d "$stage/compiler" ] && [ -d "$stage/runtime" ] &&
        [ -f "$stage/$reference_target/libcgf_rt.a" ] &&
        [ -f "$stage/cgfried" ] || {
        echo "bootstrap-repro: $label artifact tree is incomplete" >&2
        exit 2
    }
    (cd "$stage" && {
        find compiler runtime -type f \( -name '*.s' -o -name '*.o' \) \
            -print
        printf '%s\n' "$reference_target/libcgf_rt.a" cgfried
    } | LC_ALL=C sort) >"$actual"
    awk '
        NF == 2 && length($1) == 64 && $1 ~ /^[0-9a-f]+$/ {
            path=$2
            if (path ~ /^\// || path ~ /(^|\/)\.\.?($|\/)/)
                bad=1
            else
                print $1, path
        }
        END { if (bad) exit 2 }
    ' "$manifest" >"$hashes" || {
        echo "bootstrap-repro: $label artifact manifest path is unsafe" >&2
        exit 2
    }
    awk '{ print $2 }' "$hashes" | LC_ALL=C sort >"$listed"
    if ! cmp -s "$actual" "$listed"; then
        echo "bootstrap-repro: $label artifact manifest paths disagree" >&2
        exit 2
    fi
    while IFS= read -r path; do
        expected_hash=$(awk -v path="$path" '
            $2 == path { value=$1; count++ }
            END { if (count != 1) exit 2; print value }
        ' "$hashes") || {
            echo "bootstrap-repro: malformed $label artifact hash: $path" >&2
            exit 2
        }
        actual_hash=$($sha256 "$stage/$path" | awk '{print $1}')
        [ "$actual_hash" = "$expected_hash" ] || {
            echo "bootstrap-repro: $label artifact hash mismatch: $path" >&2
            exit 2
        }
    done <"$actual"
}

validate_stage_manifest "$source_root/stage1" stage1
validate_stage_manifest "$reference" stage2

if [ -e "$output" ] &&
   find "$output" -mindepth 1 -print -quit | grep . >/dev/null 2>&1; then
    echo "bootstrap-repro: output root is not empty: $output_root" >&2
    exit 2
fi
mkdir -p "$output"

target=$(env -i PATH="$fixed_path" LC_ALL=C SOURCE_DATE_EPOCH=0 \
    "$stage1" -dumpmachine)
case $target in
x86_64-linux-gnu | arm64-linux) ;;
*)
    echo "bootstrap-repro: unsupported native target: $target" >&2
    exit 2
    ;;
esac
[ "$target" = "$reference_target" ] || {
    echo "bootstrap-repro: stage1 target differs from report ($target != $reference_target)" >&2
    exit 2
}
case $recorded_sysroot in
none) target_flag= ;;
/*)
    [ -d "$recorded_sysroot" ] || {
        echo "bootstrap-repro: recorded sysroot is missing: $recorded_sysroot" >&2
        exit 2
    }
    target_flag=--sysroot=$recorded_sysroot
    ;;
*)
    echo "bootstrap-repro: recorded sysroot is not absolute or none" >&2
    exit 2
    ;;
esac

echo "bootstrap-repro-$level: stage1 rebuilds from a different root with -j1"
(cd "$repo" && env -i PATH="$fixed_path" LC_ALL=C SOURCE_DATE_EPOCH=0 \
    "$make_cmd" -f ci/bootstrap.mk -j1 all \
    BOOTSTRAP_ROOT="$output" BOOTSTRAP_CGF="$stage1" \
    BOOTSTRAP_HOSTCC="$hostcc" BOOTSTRAP_AS="$assembler" \
    BOOTSTRAP_AR="$archiver" BOOTSTRAP_LEVEL="$level" \
    BOOTSTRAP_TARGET="$target" BOOTSTRAP_TARGET_FLAG="$target_flag")

sh "$repo/scripts/bisect-nondet.sh" "$reference" "$output"
{
    echo 'schema=cgfried.bootstrap-repro.v1'
    echo "target=$target"
    echo "level=$level"
    echo 'reference_jobs=8'
    echo 'probe_jobs=1'
    echo 'different_output_root=yes'
    echo 'normalization=none'
    echo "sysroot=$recorded_sysroot"
    echo "hostcc=$hostcc"
    echo "assembler=$assembler"
    echo "archiver=$archiver"
    "$sha256" "$reference/cgfried" "$output/cgfried"
    "$sha256" "$reference/$target/libcgf_rt.a" \
        "$output/$target/libcgf_rt.a"
    echo 'state=passed'
} >"$output/repro-report.txt"
echo "bootstrap-repro-$level: PASS (-j1/-jN and output-root independence are byte-identical)"
