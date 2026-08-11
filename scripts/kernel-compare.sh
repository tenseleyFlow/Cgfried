#!/bin/sh
# Sprint 53 static cgfried-vs-GCC comparison and optional fleet runtime lane.
#
# Static dashboard (deterministic; the default):
#   scripts/kernel-compare.sh [.benchmarks/kernels-vs-gcc.md]
#
# Fleet runtime measurement (native target only):
#   CGF_KERNEL_RUNTIME=1 scripts/kernel-compare.sh
# Runtime artifact only (no ELF/static dashboard tools are consulted):
#   CGF_KERNEL_RUNTIME_ONLY=1 scripts/kernel-compare.sh
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
runtime_only=${CGF_KERNEL_RUNTIME_ONLY:-0}
runtime_input=${CGF_KERNEL_RUNTIME_INPUT:-}
runs=${CGF_KERNEL_RUNS:-10}
warmup=${CGF_KERNEL_WARMUP:-1}
cooldown=${CGF_KERNEL_COOLDOWN_SECONDS:-120}
timeit=${CGF_KERNEL_TIMEIT:-$build/timeit}
control=${CGF_KERNEL_CONTROL_SCRIPT:-$repo/scripts/bench-control.sh}
sysroot_include=${CGF_FLEET_SYSROOT_INCLUDE:-}
sysroot_crt=${CGF_FLEET_SYSROOT_CRT:-}
dashboard_scope_kind=${CGF_KERNEL_DASHBOARD_SCOPE_KIND:-host_class}
dashboard_scope=${CGF_KERNEL_DASHBOARD_SCOPE:-target-deterministic}
dashboard_protocol=${CGF_KERNEL_DASHBOARD_PROTOCOL:-sprint-53-static-dashboard-v1}

die()
{
    echo "$prog: $*" >&2
    exit 2
}

control_die()
{
    echo "$prog: $*" >&2
    exit 3
}

valid_utc_timestamp()
{
    awk -v timestamp="$1" '
        function'" "'leap_year(year) {
            return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)
        }
        BEGIN {
            if (length(timestamp) != 20 ||
                substr(timestamp, 5, 1) != "-" ||
                substr(timestamp, 8, 1) != "-" ||
                substr(timestamp, 11, 1) != "T" ||
                substr(timestamp, 14, 1) != ":" ||
                substr(timestamp, 17, 1) != ":" ||
                substr(timestamp, 20, 1) != "Z")
                exit 1
            year = substr(timestamp, 1, 4)
            month = substr(timestamp, 6, 2)
            day = substr(timestamp, 9, 2)
            hour = substr(timestamp, 12, 2)
            minute = substr(timestamp, 15, 2)
            second = substr(timestamp, 18, 2)
            if (year !~ /^[0-9][0-9][0-9][0-9]$/ ||
                month !~ /^[0-9][0-9]$/ || day !~ /^[0-9][0-9]$/ ||
                hour !~ /^[0-9][0-9]$/ || minute !~ /^[0-9][0-9]$/ ||
                second !~ /^[0-9][0-9]$/)
                exit 1
            year += 0
            month += 0
            day += 0
            hour += 0
            minute += 0
            second += 0
            if (year < 1 || month < 1 || month > 12 || hour > 23 ||
                minute > 59 || second > 59)
                exit 1
            split("31 28 31 30 31 30 31 31 30 31 30 31", month_days, " ")
            if (leap_year(year))
                month_days[2] = 29
            exit !(day >= 1 && day <= month_days[month])
        }
    ' </dev/null
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
        target:x86_64-linux-gnu | target:arm64-linux | target:arm64-macos | opt:Os | opt:O2 | opt:O3) ;;
        *) die "unsupported $valid_kind '$valid_word'" ;;
        esac
    done
}

case $runtime in
0 | 1) ;;
*) die "CGF_KERNEL_RUNTIME must be 0 or 1" ;;
esac
case $runtime_only in
0 | 1) ;;
*) die "CGF_KERNEL_RUNTIME_ONLY must be 0 or 1" ;;
esac
case ${sysroot_include:+include}:${sysroot_crt:+crt} in
: | include:crt) ;;
*) die "CGF_FLEET_SYSROOT_INCLUDE and CGF_FLEET_SYSROOT_CRT must be set together" ;;
esac
[ "$runtime_only" -eq 0 ] || runtime=1
case $minimum:$runs:$warmup:$cooldown in
*[!0-9:]*) die "minimum, runs, warmup, and cooldown must be non-negative integers" ;;
esac
[ "$minimum" -ge 1 ] || die "CGF_KERNEL_MIN must be at least 1"
[ "$runs" -ge 1 ] || die "CGF_KERNEL_RUNS must be at least 1"
[ -x "$cgf" ] || die "compiler is not executable: $cgf"
[ -d "$kernel_dir" ] || die "kernel directory does not exist: $kernel_dir"
case $dashboard_scope_kind in
host | host_class) ;;
*) die "CGF_KERNEL_DASHBOARD_SCOPE_KIND must be host or host_class" ;;
esac
# Deliberate splitting: these are documented whitespace-separated enum lists.
# shellcheck disable=SC2086
validate_words target $targets
# shellcheck disable=SC2086
validate_words opt $opts

if [ -n "${CGF_KERNEL_DASHBOARD_REV:-}" ]; then
    dashboard_rev=$CGF_KERNEL_DASHBOARD_REV
else
    dashboard_rev=$(git -C "$repo" rev-parse HEAD 2>/dev/null) ||
        die "cannot derive dashboard revision provenance"
fi
if [ -n "${CGF_KERNEL_DASHBOARD_DATE_UTC:-}" ]; then
    dashboard_date=$CGF_KERNEL_DASHBOARD_DATE_UTC
else
    dashboard_date=$(TZ=UTC0 git -C "$repo" show -s \
        --date='format-local:%Y-%m-%dT%H:%M:%SZ' --format=%cd \
        "$dashboard_rev" 2>/dev/null) ||
        die "cannot derive dashboard date provenance"
fi
if [ -n "${CGF_KERNEL_DASHBOARD_TREE_STATE:-}" ]; then
    dashboard_tree=$CGF_KERNEL_DASHBOARD_TREE_STATE
else
    dashboard_tree_lines=$(git -C "$repo" status --porcelain \
        --untracked-files=normal 2>/dev/null) ||
        die "cannot derive dashboard tree-state provenance"
    if [ -n "$dashboard_tree_lines" ]; then
        dashboard_tree=dirty
    else
        dashboard_tree=clean
    fi
fi
case $dashboard_scope in
'' | *[!A-Za-z0-9_.:+-]*) die "dashboard scope must be a nonempty Markdown-safe token" ;;
esac
case $dashboard_rev in
'' | *[!A-Za-z0-9_.:+-]*) die "dashboard revision must be a nonempty Markdown-safe token" ;;
esac
case $dashboard_protocol in
'' | *[!A-Za-z0-9_.:=,+/%\;-]*)
    die "dashboard protocol must be a nonempty Markdown-safe token"
    ;;
esac
valid_utc_timestamp "$dashboard_date" ||
    die "dashboard date provenance must be a valid UTC timestamp"
case $dashboard_tree in
clean | dirty | exported-commit | unavailable) ;;
*) die "dashboard tree provenance is invalid: $dashboard_tree" ;;
esac

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
    arm64-macos:gcc) echo "${CGF_KERNEL_GCC_ARM64_MACOS:-clang}" ;;
    arm64-macos:as) echo "${CGF_KERNEL_AS_ARM64_MACOS:-as}" ;;
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
provenance=$work/provenance.txt

if [ "$runtime_only" -eq 0 ]; then
    case " $targets " in
    *" arm64-macos "*)
        die "arm64-macos supports runtime-only measurement, not the ELF static dashboard"
        ;;
    esac
    : >"$metrics"
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
fi

native_runtime_target()
{
    runtime_system=$(uname -s 2>/dev/null || echo unknown)
    runtime_machine=$(uname -m 2>/dev/null || echo unknown)
    case $runtime_system:$runtime_machine in
    Linux:x86_64) echo x86_64-linux-gnu ;;
    Linux:aarch64 | Linux:arm64) echo arm64-linux ;;
    Darwin:arm64 | Darwin:aarch64) echo arm64-macos ;;
    *) echo unavailable ;;
    esac
}

compile_runtime_cgf()
{
    compile_target=$1
    compile_opt=$2
    compile_output=$3
    compile_source=$4
    shift 4

    if [ "$compile_target" = arm64-macos ]; then
        set -- -include \
            "$repo/tests/bench/compat/arm64-macos-self-syntax.h" \
            -I "$repo/tests/bench/compat/arm64-macos-self-overlay" "$@"
    fi
    if [ -n "$as_path" ]; then
        CGF_AS_PATH=$as_path "$cgf" --target="$compile_target" \
            -std=gnu17 "-$compile_opt" "$@" -o "$compile_output" \
            "$compile_source"
    else
        "$cgf" --target="$compile_target" -std=gnu17 "-$compile_opt" \
            "$@" -o "$compile_output" "$compile_source"
    fi
}

measure_runtime()
{
    runtime_cpu_root=${CGF_KERNEL_SYS_CPU_ROOT:-/sys/devices/system/cpu}
    runtime_host=${CGF_KERNEL_RUNTIME_HOST:-$(hostname -s 2>/dev/null || uname -n)}
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
    [ -x "$control" ] || control_die "control helper is not executable: $control"
    runtime_governor=unavailable
    runtime_power_profile=unavailable
    runtime_scaling_driver=unavailable
    runtime_epp=unavailable
    if [ "$runtime_target" != arm64-macos ] && [ -d "$runtime_cpu_root" ]; then
        runtime_governors=$(for f in "$runtime_cpu_root"/cpu[0-9]*/cpufreq/scaling_governor; do
            [ -r "$f" ] && sed -n '1p' "$f"
        done | sort -u | paste -sd, -)
        [ -n "$runtime_governors" ] && runtime_governor=$runtime_governors
        runtime_drivers=$(for f in "$runtime_cpu_root"/cpu[0-9]*/cpufreq/scaling_driver; do
            [ -r "$f" ] && sed -n '1p' "$f"
        done | sort -u | paste -sd, -)
        [ -n "$runtime_drivers" ] && runtime_scaling_driver=$runtime_drivers
        runtime_epps=$(for f in "$runtime_cpu_root"/cpu[0-9]*/cpufreq/energy_performance_preference; do
            [ -r "$f" ] && sed -n '1p' "$f"
        done | sort -u | paste -sd, -)
        [ -n "$runtime_epps" ] && runtime_epp=$runtime_epps
    fi
    if [ "$runtime_target" != arm64-macos ] &&
       command -v powerprofilesctl >/dev/null 2>&1; then
        observed_profile=$(powerprofilesctl get 2>/dev/null | sed -n '1p' || :)
        [ -z "$observed_profile" ] || runtime_power_profile=$observed_profile
    fi
    control_tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-kernel-control.XXXXXX") ||
        control_die "cannot create control provenance workspace"
    trap 'rm -rf "$control_tmp"' EXIT HUP INT TERM
    control_measure=$control_tmp/measure.txt
    control_input=$control_tmp/input.txt
    "$control" measure "$runtime_host" >"$control_measure" ||
        control_die "capacity/idle measurement failed"
    {
        echo "host=$runtime_host"
        echo "governor=$runtime_governor"
        echo "power_profile=$runtime_power_profile"
        echo "scaling_driver=$runtime_scaling_driver"
        echo "energy_performance_preference=$runtime_epp"
        cat "$control_measure"
    } >"$control_input"
    control_status=0
    "$control" classify --require-v2 "$control_input" >/dev/null ||
        control_status=$?
    case $control_status in
    0) ;;
    1)
        if [ "${CGF_KERNEL_FORCE:-0}" = 1 ]; then
            echo "$prog: WARNING: capacity/idle controls are provenance-only; forced recording enabled" >&2
        else
            control_die "capacity/idle controls are provenance-only (set CGF_KERNEL_FORCE=1 to record)"
        fi
        ;;
    3) control_die "malformed v2 capacity/idle provenance" ;;
    *) control_die "control classifier failed with status $control_status" ;;
    esac
    control_value()
    {
        awk -F= -v key="$1" \
            '$1 == key { print substr($0, length(key) + 2) }' \
            "$control_measure"
    }
    runtime_control_protocol=$(control_value control_protocol)
    runtime_logical_cpus=$(control_value logical_cpus)
    runtime_cpu_idle_pct=$(control_value cpu_idle_pct)
    runtime_load=$(control_value load1)

    runtime_stamp=$(date -u '+%Y-%m-%dT%H%M%SZ')
    runtime_output=${CGF_KERNEL_RUNTIME_OUTPUT:-$repo/.benchmarks/runs/$runtime_stamp-$runtime_host-kernels.txt}
    [ ! -e "$runtime_output" ] || die "refusing to overwrite $runtime_output"
    mkdir -p "$(dirname "$runtime_output")" "$work/runtime/raw"
    runtime_tmp=$work/runtime.txt
    gcc_tool=$(tool_for_target "$runtime_target" gcc)
    command -v "$gcc_tool" >/dev/null 2>&1 || die "required tool not found: $gcc_tool"
    as_path=
    if [ "$runtime_target" != arm64-macos ] ||
       [ -n "${CGF_KERNEL_AS_ARM64_MACOS:-}" ]; then
        as_tool=$(tool_for_target "$runtime_target" as)
        command -v "$as_tool" >/dev/null 2>&1 || die "required tool not found: $as_tool"
        as_path=$(command -v "$as_tool")
    fi
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
        echo "power_profile=$runtime_power_profile"
        echo "scaling_driver=$runtime_scaling_driver"
        echo "energy_performance_preference=$runtime_epp"
        echo "control_protocol=$runtime_control_protocol"
        echo "logical_cpus=$runtime_logical_cpus"
        echo "cpu_idle_pct=$runtime_cpu_idle_pct"
        echo "runs=$runs"
        echo "warmup=$warmup"
        echo "cgf_rev=$runtime_rev"
        echo "cgf_tree=$runtime_tree"
        echo "cgf_version=$(one_line "$cgf" --version)"
        echo "gcc_version=$(one_line "$gcc_tool" --version)"
        [ -z "$sysroot_include" ] || echo "sysroot_include=$sysroot_include"
        [ -z "$sysroot_crt" ] || echo "sysroot_crt=$sysroot_crt"
        if [ "$runtime_target" = arm64-macos ]; then
            echo 'cgf_sdk_compat=arm64-macos-kernel-runtime-v1'
        fi
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
            compile_runtime_cgf "$runtime_target" "$opt" "$cgf_exe" \
                "$source"
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
        if [ "$runtime_host" = nomad-1 ] && [ "$cooldown" -gt 0 ]; then
            echo "$prog: nomad-1 thermal cooldown ($cooldown seconds)" >&2
            sleep "$cooldown"
        fi
    done
    mv "$runtime_tmp" "$runtime_output"
    runtime_input=$runtime_output
    echo "$prog: wrote $runtime_output"
}

if [ "$runtime" -eq 1 ]; then
    measure_runtime
fi

if [ "$runtime_only" -eq 1 ]; then
    exit 0
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
    printf '<!-- cgf-dashboard-provenance %s=%s -->\n' \
        "$dashboard_scope_kind" "$dashboard_scope"
    printf '<!-- cgf-dashboard-provenance date_utc=%s -->\n' "$dashboard_date"
    printf '<!-- cgf-dashboard-provenance cgf_rev=%s -->\n' "$dashboard_rev"
    printf '<!-- cgf-dashboard-provenance cgf_tree=%s -->\n' "$dashboard_tree"
    printf '<!-- cgf-dashboard-provenance protocol=%s -->\n' "$dashboard_protocol"
    echo
    echo '# Cgfried kernel code-generation comparison'
    echo
    # Backticks below are Markdown literals, not shell substitutions.
    # shellcheck disable=SC2016
    echo 'Static instruction counts cover only `kernel_run`; alignment nops are excluded. Text is the complete object `.text` size in bytes. GCC is a visibility reference, not a gate. Runtime ratios above 1.5x are marked for issue follow-up; they never fail this generator.'
    echo
    echo '## Provenance'
    echo
    # Backticks below are Markdown literals, not shell substitutions.
    # shellcheck disable=SC2016
    printf -- '- `dashboard.%s`: `%s`\n' "$dashboard_scope_kind" "$dashboard_scope"
    # shellcheck disable=SC2016
    printf -- '- `dashboard.date_utc`: `%s`\n' "$dashboard_date"
    # shellcheck disable=SC2016
    printf -- '- `dashboard.cgf_rev`: `%s`\n' "$dashboard_rev"
    # shellcheck disable=SC2016
    printf -- '- `dashboard.cgf_tree`: `%s`\n' "$dashboard_tree"
    # shellcheck disable=SC2016
    printf -- '- `dashboard.protocol`: `%s`\n' "$dashboard_protocol"
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
