#!/bin/sh
# Sprint 58 weekly host-independence ritual for the arm64-linux target.
set -eu
LC_ALL=C
export LC_ALL

usage()
{
    cat >&2 <<'EOF'
usage: bootstrap-cross.sh emit    O0|O2 COMPILER SYSROOT OUTPUT_ROOT X86_BOOTSTRAP_ROOT
       bootstrap-cross.sh import  O0|O2 NATIVE_STAGE2_ROOT OUTPUT_ROOT
       bootstrap-cross.sh compare O0|O2 X86_OUTPUT_ROOT NATIVE_OUTPUT_ROOT X86_BOOTSTRAP_ROOT
EOF
    exit 2
}

[ "$#" -ge 1 ] || usage
mode=$1
shift
case $mode:$# in
emit:5 | import:3 | compare:4) ;;
*) usage ;;
esac

level=$1
shift
case $level in
O0 | O2) ;;
*) usage ;;
esac

repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
jobs=${CGF_BOOTSTRAP_JOBS:-}
expected_commit=${CGF_BOOTSTRAP_COMMIT:-}
runner_arch=${CGF_BOOTSTRAP_RUNNER_ARCH:-}
header_archive=${CGF_BOOTSTRAP_HEADER_ARCHIVE:-}
x86_run_manifest=${CGF_BOOTSTRAP_X86_RUN_MANIFEST:-${CGF_BOOTSTRAP_RUN_MANIFEST:-}}
native_run_manifest=${CGF_BOOTSTRAP_NATIVE_RUN_MANIFEST:-${CGF_BOOTSTRAP_RUN_MANIFEST:-}}
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
fi
case $jobs in
'' | *[!0-9]* | 0)
    echo "bootstrap-cross: CGF_BOOTSTRAP_JOBS must be a positive integer" >&2
    exit 2
    ;;
esac

resolve_tool()
{
    resolved=$(command -v "$1" 2>/dev/null || true)
    if [ -z "$resolved" ] || [ ! -x "$resolved" ]; then
        echo "bootstrap-cross: required tool not found: $1" >&2
        exit 2
    fi
    case $resolved in
    /*) ;;
    *) resolved=$(CDPATH='' cd "${resolved%/*}" && pwd -P)/${resolved##*/} ;;
    esac
    printf '%s\n' "$resolved"
}

make_cmd=$(resolve_tool "${MAKE:-make}")
sha256=$(resolve_tool sha256sum)
fixed_path=/usr/bin:/bin
for tool in "$make_cmd" "$sha256"; do
    tool_dir=${tool%/*}
    case :$fixed_path: in
    *:"$tool_dir":*) ;;
    *) fixed_path=$tool_dir:$fixed_path ;;
    esac
done

validate_root()
{
    root=$1
    case $root in
    build/boot/*) ;;
    *)
        echo "bootstrap-cross: root must be below build/boot: $root" >&2
        exit 2
        ;;
    esac
    case /$root/ in
    *'/../'* | *'/./'* | *'//'*)
        echo "bootstrap-cross: root is not canonical: $root" >&2
        exit 2
        ;;
    esac
}

validate_source_root()
{
    root=$1
    case $root in
    build/*) ;;
    *)
        echo "bootstrap-cross: source root must be below build: $root" >&2
        exit 2
        ;;
    esac
    case /$root/ in
    *'/../'* | *'/./'* | *'//'* )
        echo "bootstrap-cross: source root is not canonical: $root" >&2
        exit 2
        ;;
    esac
}

validate_source_file()
{
    source_file=$1
    case $source_file in
    build/*) ;;
    *)
        echo "bootstrap-cross: evidence file must be below build: $source_file" >&2
        exit 2
        ;;
    esac
    case /$source_file/ in
    *'/../'* | *'/./'* | *'//'*)
        echo "bootstrap-cross: evidence file is not canonical: $source_file" >&2
        exit 2
        ;;
    esac
    [ -r "$repo/$source_file" ] || {
        echo "bootstrap-cross: evidence file is missing: $source_file" >&2
        exit 2
    }
}

manifest_field()
{
    field_manifest=$1
    field_name=$2
    awk -F= -v name="$field_name" '
        $1 == name { value=substr($0, length(name) + 2); count++ }
        END { if (count != 1 || value == "") exit 2; print value }
    ' "$field_manifest"
}

validate_run_manifest()
{
    run_manifest=$1
    expected_lane=$2
    if [ -z "$run_manifest" ]; then
        [ -z "$expected_commit" ] || {
            echo "bootstrap-cross: hosted $expected_lane run manifest is required" >&2
            exit 2
        }
        validated_run_sha=none
        validated_run_commit=local-unverified
        return
    fi

    validate_source_file "$run_manifest"
    validated_run_commit=$(awk -F= -v expected_lane="$expected_lane" \
        -v expected_level="$level" -v expected_commit="$expected_commit" '
        $1 == "schema" && $2 == "cgfried.bootstrap-run.v1" { schema++ }
        $1 == "lane" && $2 == expected_lane { lane++ }
        $1 == "level" && $2 == expected_level { level++ }
        $1 == "commit" &&
            (expected_commit == "" || $2 == expected_commit) {
            commit++; commit_value=$2
        }
        $1 == "runner_arch" && length($2) != 0 { runner++ }
        $1 == "normalization" && $2 == "none" { normalization++ }
        $1 == "host_cc" && length($0) > length($1) + 1 { host_cc++ }
        $1 == "assembler" && length($0) > length($1) + 1 { assembler++ }
        $1 == "linker" && length($0) > length($1) + 1 { linker++ }
        $1 == "outcome" && $2 == "success" { outcome++ }
        END {
            if (schema != 1 || lane != 1 || level != 1 || commit != 1 ||
                runner != 1 || normalization != 1 || host_cc != 1 ||
                assembler != 1 || linker != 1 || outcome != 1)
                exit 2
            print commit_value
        }
    ' "$repo/$run_manifest") || {
        echo "bootstrap-cross: $expected_lane run manifest provenance is invalid" >&2
        exit 2
    }
    validated_run_sha=$($sha256 "$repo/$run_manifest" | awk '{print $1}')
}

validate_header_archive()
{
    if [ -z "$header_archive" ]; then
        [ -z "$expected_commit" ] || {
            echo 'bootstrap-cross: hosted ARM64 header archive is required' >&2
            exit 2
        }
        validated_header_sha=none
        return
    fi
    validate_source_file "$header_archive"
    validated_header_sha=$($sha256 "$repo/$header_archive" | awk '{print $1}')
}

copy_provenance()
{
    provenance_output=$1
    provenance_prefix=$2
    provenance_report=$3
    provenance_stage_manifest=$4
    provenance_run_manifest=$5
    mkdir -p "$repo/$provenance_output/provenance"
    cp "$provenance_report" \
        "$repo/$provenance_output/provenance/$provenance_prefix-bootstrap-report.txt"
    cp "$provenance_stage_manifest" \
        "$repo/$provenance_output/provenance/$provenance_prefix-stage-artifacts.sha256"
    if [ -n "$provenance_run_manifest" ]; then
        cp "$repo/$provenance_run_manifest" \
            "$repo/$provenance_output/provenance/$provenance_prefix-run.manifest"
    fi
}

validate_x86_bootstrap()
{
    x86_evidence_root=$1
    validate_source_root "$x86_evidence_root"
    x86_evidence_report=$repo/$x86_evidence_root/bootstrap-report.txt
    x86_evidence_manifest=$repo/$x86_evidence_root/stage1/artifacts.sha256
    x86_evidence_compiler=$repo/$x86_evidence_root/stage1/cgfried

    [ -r "$x86_evidence_report" ] || {
        echo "bootstrap-cross: x86 bootstrap report is missing: $x86_evidence_root" >&2
        exit 2
    }
    awk -F= -v expected_level="$level" '
        $1 == "schema" && $2 == "cgfried.bootstrap.v1" { schema++ }
        $1 == "target" && $2 == "x86_64-linux-gnu" { target++ }
        $1 == "level" && $2 == expected_level { level++ }
        $1 == "normalization" && $2 == "none" { normalization++ }
        $1 == "state" && $2 == "passed" { passed++ }
        $1 == "state" && $2 == "identity-failed" { failed++ }
        END {
            if (schema != 1 || target != 1 || level != 1 ||
                normalization != 1 || passed != 1 || failed != 0)
                exit 2
        }
    ' "$x86_evidence_report" || {
        echo "bootstrap-cross: x86 source is not a passed raw fixed point" >&2
        exit 2
    }
    [ -r "$x86_evidence_manifest" ] && [ -f "$x86_evidence_compiler" ] || {
        echo "bootstrap-cross: x86 stage1 compiler evidence is missing" >&2
        exit 2
    }
    awk -F= -v expected_level="$level" '
        $1 == "schema" && $2 == "cgfried.bootstrap-artifacts.v1" { schema++ }
        $1 == "target" && $2 == "x86_64-linux-gnu" { target++ }
        $1 == "level" && $2 == expected_level { level++ }
        END { if (schema != 1 || target != 1 || level != 1) exit 2 }
    ' "$x86_evidence_manifest" || {
        echo "bootstrap-cross: x86 stage1 manifest provenance is invalid" >&2
        exit 2
    }
    trusted_compiler_sha=$(awk '
        $2 == "cgfried" {
            if (length($1) != 64 || $1 !~ /^[0-9a-f]+$/) bad=1
            value=$1
            count++
        }
        END { if (bad || count != 1) exit 2; print value }
    ' "$x86_evidence_manifest") || {
        echo "bootstrap-cross: x86 stage1 manifest lacks one compiler hash" >&2
        exit 2
    }
    actual_compiler_sha=$($sha256 "$x86_evidence_compiler" | awk '{print $1}')
    [ "$actual_compiler_sha" = "$trusted_compiler_sha" ] || {
        echo "bootstrap-cross: x86 stage1 compiler hash mismatch" >&2
        exit 2
    }
    trusted_report_sha=$($sha256 "$x86_evidence_report" | awk '{print $1}')
    trusted_manifest_sha=$($sha256 "$x86_evidence_manifest" | awk '{print $1}')
    trusted_compiler=$x86_evidence_compiler
    validate_run_manifest "$x86_run_manifest" x86_64-linux
    trusted_run_sha=$validated_run_sha
    trusted_run_commit=$validated_run_commit
    validate_header_archive
    trusted_header_sha=$validated_header_sha
}

require_empty_root()
{
    root=$1
    validate_root "$root"
    if [ -e "$repo/$root" ] &&
       find "$repo/$root" -mindepth 1 -print -quit | grep . >/dev/null 2>&1; then
        echo "bootstrap-cross: output root is not empty: $root" >&2
        exit 2
    fi
    mkdir -p "$repo/$root"
}

run_cross_make()
{
    compiler=$1
    sysroot=$2
    output=$3
    (cd "$repo" && env -i PATH="$fixed_path" LC_ALL=C SOURCE_DATE_EPOCH=0 \
        "$make_cmd" -f ci/bootstrap-cross.mk -j"$jobs" assembly \
        BOOTSTRAP_CGF="$compiler" BOOTSTRAP_LEVEL="$level" \
        BOOTSTRAP_CROSS_SYSROOT="$sysroot" \
        BOOTSTRAP_CROSS_OUTPUT="$output")
}

case $mode in
emit)
    requested_compiler=$(resolve_tool "$1")
    sysroot=$2
    output=$3
    x86_bootstrap=$4
    validate_x86_bootstrap "$x86_bootstrap"
    [ "$requested_compiler" = "$trusted_compiler" ] || {
        echo "bootstrap-cross: emitter is not the authenticated x86 stage1 compiler" >&2
        exit 2
    }
    validate_root "$sysroot"
    [ -d "$repo/$sysroot/usr/include" ] || {
        echo "bootstrap-cross: ARM64 header sysroot is missing: $sysroot" >&2
        exit 2
    }
    require_empty_root "$output"
    echo "bootstrap-cross-$level: compiler emits arm64-linux assembly"
    run_cross_make "$requested_compiler" "$sysroot" "$output"
    copy_provenance "$output" x86 "$x86_evidence_report" \
        "$x86_evidence_manifest" "$x86_run_manifest"
    {
        echo 'schema=cgfried.bootstrap-cross-assembly.v1'
        echo 'target=arm64-linux'
        echo "level=$level"
        echo 'normalization=none'
        echo 'source=x86-hosted-stage1'
        echo "commit=$trusted_run_commit"
        echo "compiler_sha256=$trusted_compiler_sha"
        echo "bootstrap_report_sha256=$trusted_report_sha"
        echo "stage1_manifest_sha256=$trusted_manifest_sha"
        echo "run_manifest_sha256=$trusted_run_sha"
        echo "header_archive_sha256=$trusted_header_sha"
        (cd "$repo/$output" && find compiler runtime -type f -name '*.s' \
            -print | LC_ALL=C sort | while IFS= read -r path; do
                "$sha256" "$path"
            done)
    } >"$repo/$output/artifacts.sha256"
    ;;
import)
    native_stage=$1
    output=$2
    validate_source_root "$native_stage"
    case $native_stage in
    */stage2) ;;
    *)
        echo "bootstrap-cross: native source must be an exact stage2 root" >&2
        exit 2
        ;;
    esac
    require_empty_root "$output"
    [ -d "$repo/$native_stage/compiler" ] &&
        [ -d "$repo/$native_stage/runtime" ] || {
        echo "bootstrap-cross: native stage2 assembly tree is missing: $native_stage" >&2
        exit 2
    }
    native_report=${native_stage%/stage2}/bootstrap-report.txt
    [ -r "$repo/$native_report" ] || {
        echo "bootstrap-cross: native fixed-point report is missing: $native_report" >&2
        exit 2
    }
    awk -F= -v expected_level="$level" '
        $1 == "schema" && $2 == "cgfried.bootstrap.v1" { schema++ }
        $1 == "target" && $2 == "arm64-linux" { target++ }
        $1 == "level" && $2 == expected_level { level++ }
        $1 == "normalization" && $2 == "none" { normalization++ }
        $1 == "state" && $2 == "passed" { passed++ }
        END {
            if (schema != 1 || target != 1 || level != 1 ||
                normalization != 1 || passed != 1)
                exit 2
        }
    ' "$repo/$native_report" || {
        echo "bootstrap-cross: native stage2 is not a passed raw fixed point" >&2
        exit 2
    }
    native_manifest=$repo/$native_stage/artifacts.sha256
    [ -r "$native_manifest" ] || {
        echo "bootstrap-cross: native stage2 manifest is missing" >&2
        exit 2
    }
    awk -F= -v expected_level="$level" '
        $1 == "schema" && $2 == "cgfried.bootstrap-artifacts.v1" { schema++ }
        $1 == "target" && $2 == "arm64-linux" { target++ }
        $1 == "level" && $2 == expected_level { level++ }
        END { if (schema != 1 || target != 1 || level != 1) exit 2 }
    ' "$native_manifest" || {
        echo "bootstrap-cross: native stage2 manifest provenance is invalid" >&2
        exit 2
    }
    validate_run_manifest "$native_run_manifest" arm64-linux-native
    native_run_sha=$validated_run_sha
    native_run_commit=$validated_run_commit
    validate_header_archive
    native_header_sha=$validated_header_sha
    native_report_sha=$($sha256 "$repo/$native_report" | awk '{print $1}')
    native_manifest_sha=$($sha256 "$native_manifest" | awk '{print $1}')
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bootstrap-import.XXXXXX")
    trap 'rm -rf "$tmp"' EXIT HUP INT TERM
    (cd "$repo/$native_stage" && find compiler runtime -type f -name '*.s' \
        -print | LC_ALL=C sort) >"$tmp/assembly.list"
    [ -s "$tmp/assembly.list" ] || {
        echo 'bootstrap-cross: native stage2 has no assembly artifacts' >&2
        exit 2
    }
    while IFS= read -r path; do
        expected_hash=$(awk -v path="$path" '
            $2 == path { value=$1; count++ }
            END { if (count != 1 || value !~ /^[0-9a-f]{64}$/) exit 2;
                  print value }
        ' "$native_manifest") || {
            echo "bootstrap-cross: native manifest lacks one hash for $path" >&2
            exit 2
        }
        actual_hash=$($sha256 "$repo/$native_stage/$path" | awk '{print $1}')
        [ "$actual_hash" = "$expected_hash" ] || {
            echo "bootstrap-cross: native stage2 hash mismatch: $path" >&2
            exit 2
        }
        mkdir -p "$repo/$output/${path%/*}"
        cp "$repo/$native_stage/$path" "$repo/$output/$path"
    done <"$tmp/assembly.list"
    copy_provenance "$output" native "$repo/$native_report" \
        "$native_manifest" "$native_run_manifest"
    {
        echo 'schema=cgfried.bootstrap-cross-assembly.v1'
        echo 'target=arm64-linux'
        echo "level=$level"
        echo 'normalization=none'
        echo 'source=native-fixed-point-stage2'
        echo "source_root=$native_stage"
        echo "commit=$native_run_commit"
        echo "bootstrap_report_sha256=$native_report_sha"
        echo "stage2_manifest_sha256=$native_manifest_sha"
        echo "run_manifest_sha256=$native_run_sha"
        echo "header_archive_sha256=$native_header_sha"
        (cd "$repo/$output" && while IFS= read -r path; do
            "$sha256" "$path"
        done <"$tmp/assembly.list")
    } >"$repo/$output/artifacts.sha256"
    count=$(wc -l <"$tmp/assembly.list" | tr -d ' ')
    echo "bootstrap-cross-$level: imported $count exact native stage2 assembly files"
    ;;
compare)
    x86_root=$1
    native_root=$2
    x86_bootstrap=$3
    validate_x86_bootstrap "$x86_bootstrap"
    validate_root "$x86_root"
    validate_root "$native_root"
    for root in "$x86_root" "$native_root"; do
        if find "$repo/$root" -type f \
           \( -name '*.o' -o -name '*.a' -o -name cgfried \) \
           -print -quit | grep . >/dev/null 2>&1; then
            echo "bootstrap-cross: comparison root already has finalized artifacts: $root" >&2
            exit 2
        fi
    done

    tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bootstrap-cross.XXXXXX")
    trap 'rm -rf "$tmp"' EXIT HUP INT TERM
    validate_embedded_provenance()
    (
        embedded_root=$1
        embedded_source=$2
        embedded_manifest=$repo/$embedded_root/artifacts.sha256
        case $embedded_source in
        x86-hosted-stage1)
            embedded_prefix=x86
            embedded_lane=x86_64-linux
            embedded_stage_field=stage1_manifest_sha256
            embedded_expected_report=$trusted_report_sha
            embedded_expected_stage=$trusted_manifest_sha
            ;;
        native-fixed-point-stage2)
            embedded_prefix=native
            embedded_lane=arm64-linux-native
            embedded_stage_field=stage2_manifest_sha256
            embedded_expected_report=
            embedded_expected_stage=
            ;;
        *) exit 2 ;;
        esac
        embedded_report=$repo/$embedded_root/provenance/$embedded_prefix-bootstrap-report.txt
        embedded_stage=$repo/$embedded_root/provenance/$embedded_prefix-stage-artifacts.sha256
        embedded_run_rel=$embedded_root/provenance/$embedded_prefix-run.manifest
        [ -r "$embedded_report" ] && [ -r "$embedded_stage" ] || {
            echo "bootstrap-cross: retained $embedded_prefix provenance is missing" >&2
            exit 2
        }
        embedded_actual_report=$($sha256 "$embedded_report" | awk '{print $1}')
        embedded_actual_stage=$($sha256 "$embedded_stage" | awk '{print $1}')
        embedded_recorded_report=$(manifest_field "$embedded_manifest" \
            bootstrap_report_sha256) || {
            echo "bootstrap-cross: retained $embedded_prefix report hash is missing" >&2
            exit 2
        }
        embedded_recorded_stage=$(manifest_field "$embedded_manifest" \
            "$embedded_stage_field") || {
            echo "bootstrap-cross: retained $embedded_prefix stage hash is missing" >&2
            exit 2
        }
        [ "$embedded_actual_report" = "$embedded_recorded_report" ] &&
            [ "$embedded_actual_stage" = "$embedded_recorded_stage" ] || {
            echo "bootstrap-cross: retained $embedded_prefix provenance hash mismatch" >&2
            exit 2
        }
        if [ -n "$embedded_expected_report" ]; then
            [ "$embedded_actual_report" = "$embedded_expected_report" ] &&
                [ "$embedded_actual_stage" = "$embedded_expected_stage" ] || {
                echo "bootstrap-cross: retained x86 provenance is not the authenticated fixed point" >&2
                exit 2
            }
        fi

        if [ -r "$repo/$embedded_run_rel" ]; then
            validate_run_manifest "$embedded_run_rel" "$embedded_lane"
        else
            validate_run_manifest '' "$embedded_lane"
        fi
        embedded_recorded_run=$(manifest_field "$embedded_manifest" \
            run_manifest_sha256) || {
            echo "bootstrap-cross: retained $embedded_prefix run hash is missing" >&2
            exit 2
        }
        embedded_recorded_commit=$(manifest_field "$embedded_manifest" commit) || {
            echo "bootstrap-cross: retained $embedded_prefix commit is missing" >&2
            exit 2
        }
        [ "$embedded_recorded_run" = "$validated_run_sha" ] &&
            [ "$embedded_recorded_commit" = "$validated_run_commit" ] || {
            echo "bootstrap-cross: retained $embedded_prefix run provenance mismatch" >&2
            exit 2
        }
        embedded_recorded_header=$(manifest_field "$embedded_manifest" \
            header_archive_sha256) || {
            echo "bootstrap-cross: retained $embedded_prefix header hash is missing" >&2
            exit 2
        }
        [ "$embedded_recorded_header" = "$trusted_header_sha" ] || {
            echo "bootstrap-cross: retained $embedded_prefix header snapshot mismatch" >&2
            exit 2
        }
    )

    validate_assembly_manifest()
    {
        root=$1
        expected_source=$2
        list=$3
        manifest=$repo/$root/artifacts.sha256

        [ -s "$manifest" ] || {
            echo "bootstrap-cross: assembly manifest is missing: $root" >&2
            exit 2
        }
        awk -F= -v expected_level="$level" \
            -v expected_source="$expected_source" \
            -v expected_compiler="$trusted_compiler_sha" \
            -v expected_report="$trusted_report_sha" \
            -v expected_manifest="$trusted_manifest_sha" \
            -v expected_commit="$trusted_run_commit" \
            -v expected_run="$trusted_run_sha" \
            -v hosted_commit="$expected_commit" \
            -v expected_header="$trusted_header_sha" '
            $1 == "schema" && $2 == "cgfried.bootstrap-cross-assembly.v1" { schema++ }
            $1 == "target" && $2 == "arm64-linux" { target++ }
            $1 == "level" && $2 == expected_level { level++ }
            $1 == "normalization" && $2 == "none" { normalization++ }
            $1 == "source" && $2 == expected_source { source++ }
            $1 == "commit" && $2 == expected_commit { commit++ }
            expected_source == "x86-hosted-stage1" &&
                $1 == "run_manifest_sha256" && $2 == expected_run { run++ }
            expected_source == "native-fixed-point-stage2" &&
                $1 == "run_manifest_sha256" &&
                ((hosted_commit == "" && $2 == "none") ||
                 (hosted_commit != "" && length($2) == 64 &&
                  $2 ~ /^[0-9a-f]+$/)) { run++ }
            $1 == "header_archive_sha256" && $2 == expected_header { header++ }
            $1 == "bootstrap_report_sha256" &&
                length($2) == 64 && $2 ~ /^[0-9a-f]+$/ { report_hash++ }
            expected_source == "x86-hosted-stage1" &&
                $1 == "compiler_sha256" && $2 == expected_compiler { compiler++ }
            expected_source == "x86-hosted-stage1" &&
                $1 == "bootstrap_report_sha256" && $2 == expected_report { report++ }
            expected_source == "x86-hosted-stage1" &&
                $1 == "stage1_manifest_sha256" && $2 == expected_manifest { stage++ }
            expected_source == "native-fixed-point-stage2" &&
                $1 == "stage2_manifest_sha256" && length($2) == 64 &&
                $2 ~ /^[0-9a-f]+$/ { stage++ }
            END {
                if (schema != 1 || target != 1 || level != 1 ||
                    normalization != 1 || source != 1 || commit != 1 ||
                    run != 1 || header != 1 || report_hash != 1 || stage != 1 ||
                    (expected_source == "x86-hosted-stage1" &&
                     (compiler != 1 || report != 1)))
                    exit 2
            }
        ' "$manifest" || {
            echo "bootstrap-cross: assembly manifest provenance is invalid: $root" >&2
            exit 2
        }
        (cd "$repo/$root" && find compiler runtime -type f -name '*.s' \
            -print | LC_ALL=C sort) >"$list"
        awk '
            NF == 2 && length($1) == 64 && $1 ~ /^[0-9a-f]+$/ {
                print $2
            }
        ' "$manifest" | LC_ALL=C sort >"$list.manifest"
        if ! cmp -s "$list" "$list.manifest"; then
            echo "bootstrap-cross: assembly manifest paths disagree: $root" >&2
            exit 2
        fi
        while IFS= read -r path; do
            expected_hash=$(awk -v path="$path" '
                $2 == path { value=$1; count++ }
                END {
                    if (count != 1 || length(value) != 64 ||
                        value !~ /^[0-9a-f]+$/)
                        exit 2
                    print value
                }
            ' "$manifest") || {
                echo "bootstrap-cross: malformed assembly hash: $root/$path" >&2
                exit 2
            }
            actual_hash=$($sha256 "$repo/$root/$path" | awk '{print $1}')
            [ "$actual_hash" = "$expected_hash" ] || {
                echo "bootstrap-cross: assembly hash mismatch: $root/$path" >&2
                exit 2
            }
        done <"$list"
        validate_embedded_provenance "$root" "$expected_source"
    }

    validate_assembly_manifest "$x86_root" x86-hosted-stage1 "$tmp/x86.list"
    validate_assembly_manifest "$native_root" native-fixed-point-stage2 \
        "$tmp/native.list"
    [ -s "$tmp/x86.list" ] || {
        echo 'bootstrap-cross: no assembly artifacts were produced' >&2
        exit 2
    }
    if ! cmp -s "$tmp/x86.list" "$tmp/native.list"; then
        echo 'bootstrap-cross: assembly path manifests differ' >&2
        diff -u "$tmp/x86.list" "$tmp/native.list" >&2 || true
        exit 1
    fi
    while IFS= read -r path; do
        if ! cmp -s "$repo/$x86_root/$path" "$repo/$native_root/$path"; then
            echo "bootstrap-cross: first differing assembly: $path" >&2
            exit 1
        fi
    done <"$tmp/x86.list"

    hostcc=$(resolve_tool "${HOSTCC:-cc}")
    assembler=$(resolve_tool "${AS:-as}")
    archiver=$(resolve_tool "${AR:-ar}")
    linker=$(resolve_tool "${LD:-ld}")
    for tool in "$hostcc" "$assembler" "$archiver" "$linker"; do
        tool_dir=${tool%/*}
        case :$fixed_path: in
        *:"$tool_dir":*) ;;
        *) fixed_path=$tool_dir:$fixed_path ;;
        esac
    done
    finalize()
    {
        root=$1
        (cd "$repo" && env -i PATH="$fixed_path" LC_ALL=C \
            SOURCE_DATE_EPOCH=0 "$make_cmd" -f ci/bootstrap.mk -j"$jobs" all \
            BOOTSTRAP_ROOT="$repo/$root" BOOTSTRAP_CGF=/bin/false \
            BOOTSTRAP_HOSTCC="$hostcc" BOOTSTRAP_AS="$assembler" \
            BOOTSTRAP_AR="$archiver" BOOTSTRAP_LEVEL="$level" \
            BOOTSTRAP_TARGET=arm64-linux BOOTSTRAP_TARGET_FLAG= \
            BOOTSTRAP_FROZEN_ASSEMBLY=1)
    }
    echo "bootstrap-cross-$level: one ARM64 toolchain finalizes both streams"
    finalize "$x86_root"
    finalize "$native_root"
    sh "$repo/scripts/bisect-nondet.sh" "$repo/$x86_root" \
        "$repo/$native_root"

    report=${CGF_BOOTSTRAP_CROSS_REPORT:-$repo/build/boot/cross/cross-report.txt}
    mkdir -p "${report%/*}"
    count=$(wc -l <"$tmp/x86.list" | tr -d ' ')
    [ -n "$runner_arch" ] || runner_arch=$(uname -m)
    native_report_sha=$(manifest_field \
        "$repo/$native_root/artifacts.sha256" bootstrap_report_sha256)
    native_stage_sha=$(manifest_field \
        "$repo/$native_root/artifacts.sha256" stage2_manifest_sha256)
    native_run_sha=$(manifest_field \
        "$repo/$native_root/artifacts.sha256" run_manifest_sha256)
    {
        echo 'schema=cgfried.bootstrap-run.v1'
        echo 'lane=arm64-linux-cross'
        echo 'target=arm64-linux'
        echo "level=$level"
        echo "commit=$trusted_run_commit"
        echo "runner_arch=$runner_arch"
        echo 'normalization=none'
        echo "assembly_files=$count"
        echo "host_cc=$hostcc"
        echo "assembler=$assembler"
        echo "linker=$linker"
        echo "archiver=$archiver"
        echo "native_run_manifest_sha256=$native_run_sha"
        echo "native_bootstrap_report_sha256=$native_report_sha"
        echo "native_stage2_manifest_sha256=$native_stage_sha"
        echo "x86_run_manifest_sha256=$trusted_run_sha"
        echo "x86_bootstrap_report_sha256=$trusted_report_sha"
        echo "x86_stage1_manifest_sha256=$trusted_manifest_sha"
        echo "header_archive_sha256=$trusted_header_sha"
        "$sha256" "$repo/$x86_root/cgfried" "$repo/$native_root/cgfried"
        "$sha256" "$repo/$x86_root/arm64-linux/libcgf_rt.a" \
            "$repo/$native_root/arm64-linux/libcgf_rt.a"
        echo 'outcome=success'
    } >"$report"
    echo "bootstrap-cross-$level: PASS ($count raw assembly files and same-toolchain objects/archive/compiler are byte-identical)"
    ;;
esac
