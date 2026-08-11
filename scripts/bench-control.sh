#!/bin/sh
# Measure and classify Sprint 54 fleet benchmark control provenance.
set -eu
LC_ALL=C
export LC_ALL

die()
{
    echo "bench-control: $*" >&2
    exit 3
}

usage()
{
    die "usage: $0 measure HOST | classify [--require-v2] FILE"
}

logical_cpus()
{
    getconf_cmd=${CGF_BENCH_CONTROL_GETCONF_CMD:-getconf}
    sysctl_cmd=${CGF_BENCH_CONTROL_SYSCTL_CMD:-sysctl}
    cpus=$("$getconf_cmd" _NPROCESSORS_ONLN 2>/dev/null || true)
    case $cpus in
        *[!0-9]* | '' | 0) cpus=$("$sysctl_cmd" -n hw.logicalcpu 2>/dev/null || true) ;;
    esac
    case $cpus in
        *[!0-9]* | '' | 0) die "could not measure a positive logical CPU count" ;;
    esac
    printf '%s\n' "$cpus"
}

measure_linux()
{
    proc_root=${CGF_BENCH_CONTROL_PROC_ROOT:-/proc}
    sleep_cmd=${CGF_BENCH_CONTROL_SLEEP_CMD:-sleep}
    [ -r "$proc_root/stat" ] || die "cannot read Linux CPU counters"
    [ -r "$proc_root/loadavg" ] || die "cannot read Linux load average"
    before=$(sed -n 's/^cpu[[:space:]]\{1,\}//p' "$proc_root/stat" | sed -n '1p')
    [ -n "$before" ] || die "Linux CPU counters are malformed"
    "$sleep_cmd" 5 || die "Linux CPU sampling delay failed"
    after=$(sed -n 's/^cpu[[:space:]]\{1,\}//p' "$proc_root/stat" | sed -n '1p')
    [ -n "$after" ] || die "Linux CPU counters are malformed"
    cpu_idle_pct=$(awk -v before="$before" -v after="$after" '
        BEGIN {
            nb = split(before, b, /[[:space:]]+/)
            na = split(after, a, /[[:space:]]+/)
            if (nb < 8 || na < 8)
                exit 1
            for (i = 1; i <= 8; i++) {
                if (a[i] !~ /^[0-9]+$/ || b[i] !~ /^[0-9]+$/ || a[i] < b[i])
                    exit 1
                total += a[i] - b[i]
            }
            # Linux field 4 is idle; field 5 (iowait) deliberately remains
            # busy time because an I/O-stalled host is not a controlled host.
            idle = a[4] - b[4]
            if (total <= 0)
                exit 1
            printf "%.2f\n", 100 * idle / total
        }
    ') || die "Linux CPU counter sample is malformed"
    load1=$(awk 'NR == 1 { print $1; exit }' "$proc_root/loadavg")
    printf '%s\n%s\n' "$cpu_idle_pct" "$load1"
}

measure_darwin()
{
    top_cmd=${CGF_BENCH_CONTROL_TOP_CMD:-top}
    sysctl_cmd=${CGF_BENCH_CONTROL_SYSCTL_CMD:-sysctl}
    top_output=$("$top_cmd" -l 2 -s 5 -n 0 2>/dev/null) ||
        die "macOS CPU usage sample failed"
    cpu_idle_pct=$(printf '%s\n' "$top_output" | awk '
        /CPU usage:/ {
            for (i = 1; i <= NF; i++)
                if ($i == "idle") {
                    value = $(i - 1)
                    sub(/%$/, "", value)
                    idle = value
                    seen++
                }
        }
        END {
            if (seen < 2 || idle !~ /^[0-9]+([.][0-9]+)?$/)
                exit 1
            print idle
        }
    ') || die "macOS CPU usage sample is malformed"
    load1=$("$sysctl_cmd" -n vm.loadavg 2>/dev/null |
        sed 's/[{}]//g' | awk 'NR == 1 { print $1; exit }') ||
        die "macOS load average probe failed"
    printf '%s\n%s\n' "$cpu_idle_pct" "$load1"
}

measure()
{
    [ "$#" -eq 1 ] || usage
    host=$1
    case $host in
        kasumi | hasu) sample=$(measure_linux) ;;
        nomad-1) sample=$(measure_darwin) ;;
        *) die "unsupported fleet host '$host'" ;;
    esac
    cpu_count=$(logical_cpus)
    cpu_idle_pct=$(printf '%s\n' "$sample" | sed -n '1p')
    load1=$(printf '%s\n' "$sample" | sed -n '2p')
    awk -v idle="$cpu_idle_pct" -v load_value="$load1" 'BEGIN {
        numeric = "^[0-9]+([.][0-9]+)?$"
        exit !(idle ~ numeric && idle + 0 <= 100 && load_value ~ numeric)
    }' || die "CPU idle percentage or load average is malformed"
    printf '%s\n' \
        'control_protocol=fleet-control-v2' \
        "logical_cpus=$cpu_count" \
        "cpu_idle_pct=$cpu_idle_pct" \
        "load1=$load1"
}

classify()
{
    require_v2=0
    if [ "${1:-}" = --require-v2 ]; then
        require_v2=1
        shift
    fi
    [ "$#" -eq 1 ] || usage
    file=$1
    [ "$file" = - ] || [ -r "$file" ] || die "cannot read $file"

    exec awk -v require_v2="$require_v2" '
        function'" "'schema_error(message) {
            print "bench-control: " message > "/dev/stderr"
            malformed = 1
        }
        function'" "'strip_space(value) {
            sub(/^[[:space:]]+/, "", value)
            sub(/[[:space:]]+$/, "", value)
            return value
        }
        {
            line = $0
            sub(/[[:space:]]*#.*/, "", line)
            line = strip_space(line)
            if (line == "")
                next
            equals = index(line, "=")
            if (!equals)
                next
            key = strip_space(substr(line, 1, equals - 1))
            value = strip_space(substr(line, equals + 1))
            if (key == "host" || key == "governor" || key == "load1" ||
                key == "power_profile" || key == "scaling_driver" ||
                key == "energy_performance_preference" ||
                key == "control_protocol" || key == "logical_cpus" ||
                key == "cpu_idle_pct") {
                counts[key]++
                values[key] = value
            }
        }
        END {
            base[1] = "host"
            base[2] = "governor"
            base[3] = "load1"
            for (i = 1; i <= 3; i++) {
                key = base[i]
                if (counts[key] != 1 || values[key] == "")
                    schema_error("input has no unique " key " provenance")
            }
            if (malformed)
                exit 3
            host = values["host"]
            governor = values["governor"]
            load = values["load1"]
            if (host != "kasumi" && host != "hasu" && host != "nomad-1")
                schema_error("unsupported fleet host " host)
            if (governor !~ /^[A-Za-z0-9_.:+,-]+$/)
                schema_error("invalid governor provenance")

            v2_keys[1] = "control_protocol"
            v2_keys[2] = "logical_cpus"
            v2_keys[3] = "cpu_idle_pct"
            for (i = 1; i <= 3; i++)
                if (counts[v2_keys[i]] != 0)
                    v2_count++
            if (v2_count != 0 && v2_count != 3)
                schema_error("partial fleet-control-v2 provenance")
            v2 = v2_count == 3
            if (v2) {
                for (i = 1; i <= 3; i++) {
                    key = v2_keys[i]
                    if (counts[key] != 1 || values[key] == "")
                        schema_error("input has no unique " key " provenance")
                }
                if (values["control_protocol"] != "fleet-control-v2")
                    schema_error("unsupported control_protocol " values["control_protocol"])
                if (values["logical_cpus"] !~ /^[1-9][0-9]*$/)
                    schema_error("logical_cpus must be a positive integer")
                if (values["cpu_idle_pct"] !~ /^[0-9]+([.][0-9]+)?$/ ||
                    values["cpu_idle_pct"] + 0 > 100)
                    schema_error("cpu_idle_pct must be numeric in 0..100")
            } else if (require_v2) {
                schema_error("fleet-control-v2 provenance is required")
            }
            if (v2 || host != "nomad-1") {
                if (load !~ /^[0-9]+([.][0-9]+)?$/)
                    schema_error("invalid load1 provenance")
            } else if (load != "unknown" &&
                       load !~ /^[0-9]+([.][0-9]+)?$/) {
                schema_error("invalid legacy nomad-1 load1 provenance")
            }

            power_keys[1] = "power_profile"
            power_keys[2] = "scaling_driver"
            power_keys[3] = "energy_performance_preference"
            for (i = 1; i <= 3; i++)
                if (counts[power_keys[i]] != 0)
                    power_count++
            if (power_count != 0 && power_count != 3)
                schema_error("partial power-control provenance")
            have_power = power_count == 3
            if (have_power) {
                for (i = 1; i <= 3; i++) {
                    key = power_keys[i]
                    if (counts[key] != 1 ||
                        values[key] !~ /^[A-Za-z0-9_.:+,-]+$/)
                        schema_error("input has invalid or non-unique " key " provenance")
                }
            }
            if (host == "nomad-1" && have_power &&
                (values["power_profile"] != "unavailable" ||
                 values["scaling_driver"] != "unavailable" ||
                 values["energy_performance_preference"] != "unavailable"))
                schema_error("nomad-1 power-control provenance must be unavailable")
            if (host == "nomad-1" && governor != "performance" &&
                governor != "unavailable")
                schema_error("nomad-1 governor must be performance or unavailable")
            if (malformed)
                exit 3

            controlled = have_power
            if (v2)
                controlled = controlled && load / values["logical_cpus"] <= 0.20 &&
                    values["cpu_idle_pct"] + 0 >= 85
            else
                controlled = controlled && load != "unknown" && load + 0 <= 0.5
            if (host != "nomad-1")
                controlled = controlled && values["power_profile"] == "performance" &&
                    (governor == "performance" ||
                     (values["scaling_driver"] == "intel_pstate" &&
                      governor == "powersave" &&
                      values["energy_performance_preference"] == "performance"))
            if (controlled) {
                print "controlled"
                exit 0
            }
            print "provenance-only"
            exit 1
        }
    ' "$file"
}

case ${1:-} in
    measure) shift; measure "$@" ;;
    classify) shift; classify "$@" ;;
    *) usage ;;
esac
