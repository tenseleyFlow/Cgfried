#!/bin/sh
# Sprint 53 static cgfried-vs-GCC comparison and optional fleet runtime lane.
#
# Static dashboard (deterministic; the default):
#   scripts/kernel-compare.sh [.benchmarks/kernels-vs-gcc.md]
#
# Fleet runtime measurement (native target only):
#   CGF_KERNEL_RUNTIME=1 scripts/kernel-compare.sh
#
# Render a previously recorded runtime file:
#   CGF_KERNEL_RUNTIME_INPUT=.benchmarks/runs/<dated>-kernels.txt \
#       scripts/kernel-compare.sh
set -eu

LC_ALL=C
export LC_ALL

prog=kernel_compare
repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
output=${1:-$repo/.benchmarks/kernels-vs-gcc.md}
build=${BUILD:-$repo/build}
work=${CGF_KERNEL_COMPARE_WORK:-$build/kernel-compare}
kernel_dir=${CGF_KERNEL_DIR:-$repo/tests/bench/kernels}
cgf=${CGF_KERNEL_CGF:-$build/cgfried}
targets=${CGF_KERNEL_TARGETS:-x86_64-linux-gnu arm64-linux}
opts=${CGF_KERNEL_OPTS:-Os O2 O3}
minimum=${CGF_KERNEL_MIN:-19}
runtime=${CGF_KERNEL_RUNTIME:-0}
runtime_input=${CGF_KERNEL_RUNTIME_INPUT:-}
runs=${CGF_KERNEL_RUNS:-10}
warmup=${CGF_KERNEL_WARMUP:-1}
timeit=${CGF_KERNEL_TIMEIT:-$build/timeit}

die()
{
    echo "$prog: $*" >&2
    exit 2
}

skip()
{
    echo "HARNESS_SKIP suite=kernel-compare test=$1 count=1 reason=\"$2\""
}

one_line()
{
    "$@" 2>&1 | sed -n '1p'
}

validate_words()
{
    valid_kind=$1
    shift
    for valid_word in "$@"; do
        case $valid_kind:$valid_word in
        target:x86_64-linux-gnu | target:arm64-linux | opt:Os | opt:O2 | opt:O3) ;;
        *) die "unsupported $valid_kind '$valid_word'" ;;
        esac
    done
}

case $runtime in
0 | 1) ;;
*) die "CGF_KERNEL_RUNTIME must be 0 or 1" ;;
esac
case $minimum:$runs:$warmup in
*[!0-9:]*) die "minimum, runs, and warmup must be non-negative integers" ;;
esac
[ "$minimum" -ge 1 ] || die "CGF_KERNEL_MIN must be at least 1"
[ "$runs" -ge 1 ] || die "CGF_KERNEL_RUNS must be at least 1"
[ -x "$cgf" ] || die "compiler is not executable: $cgf"
[ -d "$kernel_dir" ] || die "kernel directory does not exist: $kernel_dir"
# Deliberate splitting: these are documented whitespace-separated enum lists.
# shellcheck disable=SC2086
validate_words target $targets
# shellcheck disable=SC2086
validate_words opt $opts

mkdir -p "$work" "$(dirname "$output")"
manifest=$work/kernels.txt
find "$kernel_dir" -maxdepth 1 -type f -name '*.c' -print | sort >"$manifest"
kernel_count=$(wc -l <"$manifest" | tr -d ' ')
[ "$kernel_count" -ge "$minimum" ] ||
    die "expected at least $minimum kernels, found $kernel_count in $kernel_dir"

tool_for_target()
{
    tool_target=$1
    tool_kind=$2
    native_machine=$(uname -m 2>/dev/null || echo unknown)
    native_system=$(uname -s 2>/dev/null || echo unknown)
    case $tool_target:$tool_kind in
    x86_64-linux-gnu:gcc) echo "${CGF_KERNEL_GCC_X86:-gcc}" ;;
    x86_64-linux-gnu:objdump) echo "${CGF_KERNEL_OBJDUMP_X86:-objdump}" ;;
    x86_64-linux-gnu:readelf) echo "${CGF_KERNEL_READELF_X86:-readelf}" ;;
    x86_64-linux-gnu:as) echo "${CGF_KERNEL_AS_X86:-as}" ;;
    arm64-linux:gcc)
        if [ "$native_system:$native_machine" = Linux:aarch64 ] ||
           [ "$native_system:$native_machine" = Linux:arm64 ]; then
            echo "${CGF_KERNEL_GCC_ARM64:-gcc}"
        else
            echo "${CGF_KERNEL_GCC_ARM64:-aarch64-linux-gnu-gcc}"
        fi
        ;;
    arm64-linux:objdump)
        if [ "$native_system:$native_machine" = Linux:aarch64 ] ||
           [ "$native_system:$native_machine" = Linux:arm64 ]; then
            echo "${CGF_KERNEL_OBJDUMP_ARM64:-objdump}"
        else
            echo "${CGF_KERNEL_OBJDUMP_ARM64:-aarch64-linux-gnu-objdump}"
        fi
        ;;
    arm64-linux:readelf)
        if [ "$native_system:$native_machine" = Linux:aarch64 ] ||
           [ "$native_system:$native_machine" = Linux:arm64 ]; then
            echo "${CGF_KERNEL_READELF_ARM64:-readelf}"
        else
            echo "${CGF_KERNEL_READELF_ARM64:-aarch64-linux-gnu-readelf}"
        fi
        ;;
    arm64-linux:as)
        if [ "$native_system:$native_machine" = Linux:aarch64 ] ||
           [ "$native_system:$native_machine" = Linux:arm64 ]; then
            echo "${CGF_KERNEL_AS_ARM64:-as}"
        else
            echo "${CGF_KERNEL_AS_ARM64:-aarch64-linux-gnu-as}"
        fi
        ;;
    *) die "internal tool lookup failure for $tool_target:$tool_kind" ;;
    esac
}

measure_object()
{
    measure_object_file=$1
    measure_objdump=$2
    measure_readelf=$3

    measure_symbol=$(
        "$measure_readelf" -sW "$measure_object_file" |
            awk '$4 == "FUNC" && $7 != "UND" && $8 == "kernel_run" {
                     count++; size = $3
                 }
                 END { print count + 0, size + 0 }'
    ) || die "cannot read symbol table: $measure_object_file"
    measure_count=${measure_symbol%% *}
    measure_size=${measure_symbol#* }
    [ "$measure_count" -eq 1 ] ||
        die "$measure_object_file: expected exactly one defined kernel_run"
    [ "$measure_size" -gt 0 ] || die "$measure_object_file: kernel_run has zero size"

    measure_icount=$(
        "$measure_objdump" -d --no-show-raw-insn --disassemble=kernel_run \
            "$measure_object_file" |
            awk '
/^[[:space:]]*[[:xdigit:]]+:[[:space:]]+/ {
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
    decoded++
    if (!is_nop)
        icount++
}
END {
    if (!decoded)
        exit 2
    print icount + 0
}'
    ) || die "$measure_object_file: objdump found no kernel_run instructions"

    measure_text_hex=$(
        "$measure_readelf" -SW "$measure_object_file" |
            awk '{
                     for (i = 1; i <= NF; i++) {
                         if ($i == ".text") {
                             print $(i + 4)
                             found++
                         }
                     }
                 }
                 END { if (found != 1) exit 2 }'
    ) || die "$measure_object_file: expected exactly one .text section"
    case $measure_text_hex in
    '' | *[!0-9A-Fa-f]*) die "$measure_object_file: malformed .text size" ;;
    esac
    measure_text=$((0x$measure_text_hex))
    [ "$measure_text" -gt 0 ] || die "$measure_object_file: zero-size .text"
    echo "$measure_icount $measure_text"
}

metrics=$work/static.txt
: >"$metrics"
provenance=$work/provenance.txt
: >"$provenance"
printf 'cgf|%s\n' "$(one_line "$cgf" --version)" >>"$provenance"

# The loop order is the dashboard order, and all inputs are sorted or closed
# enums, so the generated static artifact is byte-identical across runs.
# Deliberate splitting of validated target/optimization enum lists.
# shellcheck disable=SC2086
for target in $targets; do
    gcc_tool=$(tool_for_target "$target" gcc)
    objdump_tool=$(tool_for_target "$target" objdump)
    readelf_tool=$(tool_for_target "$target" readelf)
    as_tool=$(tool_for_target "$target" as)
    for tool in "$gcc_tool" "$objdump_tool" "$readelf_tool" "$as_tool"; do
        command -v "$tool" >/dev/null 2>&1 || die "required tool not found: $tool"
    done
    as_path=$(command -v "$as_tool")
    {
        printf '%s.gcc|%s: %s\n' "$target" "$gcc_tool" \
            "$(one_line "$gcc_tool" --version)"
        printf '%s.objdump|%s: %s\n' "$target" "$objdump_tool" \
            "$(one_line "$objdump_tool" --version)"
        printf '%s.readelf|%s: %s\n' "$target" "$readelf_tool" \
            "$(one_line "$readelf_tool" --version)"
        printf '%s.as|%s: %s\n' "$target" "$as_tool" \
            "$(one_line "$as_tool" --version)"
    } >>"$provenance"

    # shellcheck disable=SC2086
    for opt in $opts; do
        while IFS= read -r source; do
            name=${source##*/}
            name=${name%.c}
            target_work=$work/static/$target/$opt
            mkdir -p "$target_work"
            cgf_object=$target_work/$name.cgf.o
            gcc_object=$target_work/$name.gcc.o
            if ! CGF_AS_PATH=$as_path "$cgf" --target="$target" -std=gnu17 \
                "-$opt" -c -o "$cgf_object" "$source" \
                >"$target_work/$name.cgf.out" 2>"$target_work/$name.cgf.err"; then
                sed 's/^/  /' "$target_work/$name.cgf.err" >&2
                die "$target/$opt/$name: cgf compilation failed"
            fi
            if ! "$gcc_tool" -std=gnu17 "-$opt" -c -o "$gcc_object" \
                "$source" >"$target_work/$name.gcc.out" \
                2>"$target_work/$name.gcc.err"; then
                sed 's/^/  /' "$target_work/$name.gcc.err" >&2
                die "$target/$opt/$name: gcc compilation failed"
            fi
            cgf_measure=$(measure_object "$cgf_object" "$objdump_tool" "$readelf_tool")
            gcc_measure=$(measure_object "$gcc_object" "$objdump_tool" "$readelf_tool")
            printf '%s|%s|%s|%s|%s|%s|%s\n' "$target" "$opt" "$name" \
                "${cgf_measure%% *}" "${cgf_measure#* }" \
                "${gcc_measure%% *}" "${gcc_measure#* }" >>"$metrics"
        done <"$manifest"
    done
done

native_runtime_target()
{
    runtime_system=$(uname -s 2>/dev/null || echo unknown)
    runtime_machine=$(uname -m 2>/dev/null || echo unknown)
    case $runtime_system:$runtime_machine in
    Linux:x86_64) echo x86_64-linux-gnu ;;
    Linux:aarch64 | Linux:arm64) echo arm64-linux ;;
    *) echo unavailable ;;
    esac
}

measure_runtime()
{
    runtime_host=$(hostname -s 2>/dev/null || uname -n)
    case $runtime_host in
    kasumi | hasu | nomad-1) ;;
    *)
        if [ "${CGF_KERNEL_RUNTIME_ALLOW_HOST:-0}" != 1 ]; then
            die "runtime measurement is fleet-only (host is $runtime_host)"
        fi
        ;;
    esac
    runtime_target=$(native_runtime_target)
    if [ "$runtime_target" = unavailable ]; then
        # shellcheck disable=SC2086
        for requested_target in $targets; do
            skip "$requested_target-runtime" "no native Linux target on $runtime_host"
        done
        return 0
    fi
    # A runtime number is meaningful only on its native ISA/OS.  Keep every
    # requested-but-foreign target visible rather than silently omitting it.
    # shellcheck disable=SC2086
    for requested_target in $targets; do
        if [ "$requested_target" != "$runtime_target" ]; then
            skip "$requested_target-runtime" \
                "target is not native on $runtime_host ($runtime_target)"
        fi
    done
    case " $targets " in
    *" $runtime_target "*) ;;
    *) skip "$runtime_target-runtime" "native target omitted from CGF_KERNEL_TARGETS"; return 0 ;;
    esac
    [ -x "$timeit" ] || die "timer is not executable: $timeit"

    if [ -r /proc/loadavg ]; then
        IFS=' ' read -r runtime_load _rest </proc/loadavg
        if awk -v n="$runtime_load" 'BEGIN { exit !(n > 0.5) }'; then
            [ "${CGF_KERNEL_FORCE:-0}" = 1 ] ||
                die "1-minute load $runtime_load exceeds 0.5 (set CGF_KERNEL_FORCE=1 for a noisy run)"
        fi
    else
        runtime_load=unknown
    fi
    runtime_governor=unavailable
    if [ -d /sys/devices/system/cpu ]; then
        runtime_governors=$(for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
            [ -r "$f" ] && sed -n '1p' "$f"
        done | sort -u | paste -sd, -)
        [ -n "$runtime_governors" ] && runtime_governor=$runtime_governors
    fi
    if [ "$runtime_governor" != unavailable ] &&
       [ "$runtime_governor" != performance ]; then
        echo "$prog: WARNING: governor is '$runtime_governor'; runtime is provenance-only" >&2
    fi

    runtime_stamp=$(date -u '+%Y-%m-%dT%H%M%SZ')
    runtime_output=${CGF_KERNEL_RUNTIME_OUTPUT:-$repo/.benchmarks/runs/$runtime_stamp-$runtime_host-kernels.txt}
    [ ! -e "$runtime_output" ] || die "refusing to overwrite $runtime_output"
    mkdir -p "$(dirname "$runtime_output")" "$work/runtime/raw"
    runtime_tmp=$work/runtime.txt
    gcc_tool=$(tool_for_target "$runtime_target" gcc)
    as_tool=$(tool_for_target "$runtime_target" as)
    command -v "$gcc_tool" >/dev/null 2>&1 || die "required tool not found: $gcc_tool"
    command -v "$as_tool" >/dev/null 2>&1 || die "required tool not found: $as_tool"
    as_path=$(command -v "$as_tool")
    if git -C "$repo" rev-parse HEAD >/dev/null 2>&1; then
        runtime_rev=$(git -C "$repo" rev-parse HEAD)
        if [ -n "$(git -C "$repo" status --porcelain --untracked-files=normal)" ]; then
            runtime_tree=dirty
        else
            runtime_tree=clean
        fi
    else
        runtime_rev=unknown
        runtime_tree=unavailable
    fi
    {
        echo '# cgfried kernel runtime metrics v1'
        echo "date=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        echo "host=$runtime_host"
        echo "target=$runtime_target"
        echo "governor=$runtime_governor"
        echo "load1=$runtime_load"
        echo "runs=$runs"
        echo "warmup=$warmup"
        echo "cgf_rev=$runtime_rev"
        echo "cgf_tree=$runtime_tree"
        echo "cgf_version=$(one_line "$cgf" --version)"
        echo "gcc_version=$(one_line "$gcc_tool" --version)"
        echo 'timeit_protocol=sprint-52-median-mad-v1'
    } >"$runtime_tmp"
    # shellcheck disable=SC2086
    for opt in $opts; do
        while IFS= read -r source; do
            name=${source##*/}
            name=${name%.c}
            runtime_work=$work/runtime/$runtime_target/$opt
            mkdir -p "$runtime_work"
            cgf_exe=$runtime_work/$name.cgf
            gcc_exe=$runtime_work/$name.gcc
            CGF_AS_PATH=$as_path "$cgf" --target="$runtime_target" \
                -std=gnu17 "-$opt" -o "$cgf_exe" "$source"
            "$gcc_tool" -std=gnu17 "-$opt" -o "$gcc_exe" "$source"

            "$cgf_exe" || die "$runtime_target/$opt/$name: cgf checksum failed"
            "$gcc_exe" || die "$runtime_target/$opt/$name: gcc checksum failed"

            for compiler_name in cgf gcc; do
                if [ "$compiler_name" = cgf ]; then
                    executable=$cgf_exe
                else
                    executable=$gcc_exe
                fi
                raw=$work/runtime/raw/$runtime_target.$name.$opt.$compiler_name.txt
                summary=$runtime_work/$name.$compiler_name.metrics
                if [ "$runtime_host" = nomad-1 ]; then
                    "$timeit" -t 300 -n "$runs" -w "$warmup" -o "$raw" -- \
                        "$executable" >"$summary"
                else
                    "$timeit" -n "$runs" -w "$warmup" -o "$raw" -- \
                        "$executable" >"$summary"
                fi
                awk -v prefix="$runtime_target.$name.$opt.$compiler_name." '
                    /^(wall_ms_median|wall_ms_mad)=/ { print prefix $0 }
                ' "$summary" >>"$runtime_tmp"
            done
        done <"$manifest"
        if [ "$runtime_host" = nomad-1 ]; then
            echo "$prog: nomad-1 thermal cooldown (120 seconds)" >&2
            sleep 120
        fi
    done
    mv "$runtime_tmp" "$runtime_output"
    runtime_input=$runtime_output
    echo "$prog: wrote $runtime_output"
}

if [ "$runtime" -eq 1 ]; then
    measure_runtime
fi

runtime_value()
{
    value_key=$1
    [ -n "$runtime_input" ] || { echo ''; return; }
    awk -F= -v key="$value_key" '
        $1 == key { count++; value = $2 }
        END { if (count == 1) print value; else if (count > 1) exit 2 }
    ' "$runtime_input" || die "duplicate runtime metric $value_key"
}

if [ -n "$runtime_input" ]; then
    [ -r "$runtime_input" ] || die "cannot read runtime input: $runtime_input"
    runtime_date=$(runtime_value date)
    runtime_host=$(runtime_value host)
    runtime_target_recorded=$(runtime_value target)
    [ -n "$runtime_date" ] || die "runtime input lacks date"
    [ -n "$runtime_host" ] || die "runtime input lacks host"
    [ -n "$runtime_target_recorded" ] || die "runtime input lacks target"
else
    runtime_date=none
    runtime_host=none
    runtime_target_recorded=none
fi

dashboard_tmp=$work/dashboard.tmp.md
{
    echo '# Cgfried kernel code-generation comparison'
    echo
    # Backticks below are Markdown literals, not shell substitutions.
    # shellcheck disable=SC2016
    echo 'Static instruction counts cover only `kernel_run`; alignment nops are excluded. Text is the complete object `.text` size in bytes. GCC is a visibility reference, not a gate. Runtime ratios above 1.5x are marked for issue follow-up; they never fail this generator.'
    echo
    echo '## Provenance'
    echo
    while IFS='|' read -r provenance_key provenance_value; do
        # shellcheck disable=SC2016
        printf -- '- `%s`: `%s`\n' "$provenance_key" "$provenance_value"
    done <"$provenance"
    if [ -n "$runtime_input" ]; then
        # shellcheck disable=SC2016
        printf -- '- Runtime: `%s` on `%s` (`%s`, target `%s`)\n' \
            "${runtime_input##*/}" "$runtime_host" "$runtime_date" \
            "$runtime_target_recorded"
    else
        echo '- Runtime: not recorded (fleet-only; static columns remain deterministic)'
    fi

    # shellcheck disable=SC2086
    for target in $targets; do
        echo
        printf '## %s\n\n' "$target"
        echo '| Kernel | Opt | cgf insns | gcc insns | cgf .text | gcc .text | runtime cgf/gcc |'
        echo '|---|---:|---:|---:|---:|---:|---:|'
        # shellcheck disable=SC2086
        for opt in $opts; do
            while IFS= read -r source; do
                name=${source##*/}
                name=${name%.c}
                row=$(awk -F'|' -v target="$target" -v opt="$opt" -v name="$name" '
                    $1 == target && $2 == opt && $3 == name { print; found++ }
                    END { if (found != 1) exit 2 }
                ' "$metrics") || die "missing or duplicate static row $target/$opt/$name"
                old_ifs=$IFS
                IFS='|'
                set -- $row
                IFS=$old_ifs
                cgf_icount=$4
                cgf_text=$5
                gcc_icount=$6
                gcc_text=$7
                cgf_wall=$(runtime_value "$target.$name.$opt.cgf.wall_ms_median")
                cgf_mad=$(runtime_value "$target.$name.$opt.cgf.wall_ms_mad")
                gcc_wall=$(runtime_value "$target.$name.$opt.gcc.wall_ms_median")
                gcc_mad=$(runtime_value "$target.$name.$opt.gcc.wall_ms_mad")
                if [ -n "$cgf_wall" ] && [ -n "$cgf_mad" ] &&
                   [ -n "$gcc_wall" ] && [ -n "$gcc_mad" ]; then
                    ratio=$(awk -v cgf="$cgf_wall" -v cgf_mad="$cgf_mad" \
                        -v gcc="$gcc_wall" -v gcc_mad="$gcc_mad" '
                        BEGIN {
                            if (cgf !~ /^[0-9]+([.][0-9]+)?$/ ||
                                cgf_mad !~ /^[0-9]+([.][0-9]+)?$/ ||
                                gcc !~ /^[0-9]+([.][0-9]+)?$/ ||
                                gcc_mad !~ /^[0-9]+([.][0-9]+)?$/ || gcc == 0)
                                exit 2
                            value = cgf / gcc
                            printf "%.3fx%s", value, (value > 1.5 ? " ⚠" : "")
                        }') || die "malformed runtime values for $target/$opt/$name"
                elif [ -n "$cgf_wall$cgf_mad$gcc_wall$gcc_mad" ]; then
                    die "incomplete runtime median/MAD pair for $target/$opt/$name"
                else
                    ratio='—'
                fi
                printf '| %s | -%s | %s | %s | %s | %s | %s |\n' \
                    "$name" "$opt" "$cgf_icount" "$gcc_icount" \
                    "$cgf_text" "$gcc_text" "$ratio"
            done <"$manifest"
        done
    done
} >"$dashboard_tmp"

mv "$dashboard_tmp" "$output"
echo "$prog: wrote $output ($kernel_count kernels)"
