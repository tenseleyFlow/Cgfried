#!/bin/sh
# Sprint 54 fleet kernel-runtime gate.
#
# Usage:
#   runtime_gate.sh CONFIG BASELINE RUN1 RUN2 RUN3
#   runtime_gate.sh --promote CONFIG HISTORY
#
# CONFIG is a strict ci/gates.d-style file.  Runtime data is the dated v1
# format emitted by kernel-compare.sh.  HISTORY contains YYYY-MM-DD=quiet or
# YYYY-MM-DD=trip rows; fixture dates, never the wall clock, drive promotion.
# When CGF_RUNTIME_GATE_RESULT_FILE is set for an active evaluation, it receives
# exactly `pass` or `trip` before trial-mode suppression changes the exit code.
set -eu

LC_ALL=C
export LC_ALL

prog=runtime_gate

usage()
{
    echo "usage: $0 CONFIG BASELINE RUN1 RUN2 RUN3" >&2
    echo "       $0 --promote CONFIG HISTORY" >&2
    exit 3
}

die()
{
    echo "$prog: $*" >&2
    exit 3
}

read_config()
{
    config=$1
    [ -r "$config" ] || die "cannot read $config"
    awk '
function'" "'fail(message) {
    print "runtime_gate: " FILENAME ":" FNR ": " message > "/dev/stderr"
    bad = 1
}
function'" "'trim(value) {
    sub(/^[[:space:]]+/, "", value)
    sub(/[[:space:]]+$/, "", value)
    return value
}
{
    line = $0
    sub(/[[:space:]]*#.*/, "", line)
    line = trim(line)
    if (line == "")
        next
    equal = index(line, "=")
    if (!equal) {
        fail("expected key=value")
        next
    }
    key = trim(substr(line, 1, equal - 1))
    value = trim(substr(line, equal + 1))
    if (key !~ /^[a-z_][a-z0-9_]*$/ || value == "") {
        fail("malformed configuration entry")
        next
    }
    if (key in seen) {
        fail("duplicate configuration key " key)
        next
    }
    seen[key] = 1
    if (key == "name")
        name = value
    else if (key == "state")
        state = value
    else if (key == "defer_until")
        deferred = value
    else if (key == "where" || key == "when" || key == "threshold" ||
             key == "rationale")
        metadata[key] = value
    else if (key == "quiet_days" || key == "false_trip_limit" ||
             key == "false_trip_window_days") {
        metadata[key] = value
        if (value !~ /^[0-9]+$/ || value + 0 < 1)
            fail(key " must be a positive integer")
    } else if (key == "owner_sprint") {
        metadata[key] = value
        if (value !~ /^Sprint [0-9]+$/)
            fail("owner_sprint must be Sprint N")
    }
    else
        fail("unknown configuration key " key)
}
END {
    if (!("name" in seen))
        fail("missing name")
    if (!("state" in seen))
        fail("missing state")
    if (name !~ /^[a-z0-9][a-z0-9-]*$/)
        fail("invalid gate name")
    if (state != "trial" && state != "blocking" && state != "inactive")
        fail("state must be trial, blocking, or inactive")
    if (state == "inactive") {
        if (deferred !~ /^Sprint [0-9]+$/)
            fail("inactive gate requires defer_until=Sprint N")
    } else if ("defer_until" in seen) {
        fail("defer_until is valid only for inactive gates")
    }
    if (bad)
        exit 3
    print name "|" state "|" deferred
}
' "$config" || exit 3
}

promote()
{
    [ "$#" -eq 2 ] || usage
    promotion_config=$1
    history=$2
    config_info=$(read_config "$promotion_config")
    gate_name=${config_info%%|*}
    config_tail=${config_info#*|}
    gate_state=${config_tail%%|*}
    [ "$gate_state" = trial ] ||
        die "$gate_name: only a trial gate can be promoted (state=$gate_state)"
    [ -r "$history" ] || die "cannot read $history"

    if awk -v gate="$gate_name" '
function'" "'fail(message) {
    print "runtime_gate: " FILENAME ":" FNR ": " message > "/dev/stderr"
    bad = 1
}
function'" "'leap(year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
}
function'" "'ordinal(date,    year, month, day, total, month_index, days) {
    if (date !~ /^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$/)
        return -1
    year = substr(date, 1, 4) + 0
    month = substr(date, 6, 2) + 0
    day = substr(date, 9, 2) + 0
    if (year < 1 || month < 1 || month > 12)
        return -1
    split("31 28 31 30 31 30 31 31 30 31 30 31", days, " ")
    if (leap(year))
        days[2] = 29
    if (day < 1 || day > days[month])
        return -1
    total = 365 * (year - 1) + int((year - 1) / 4) \
            - int((year - 1) / 100) + int((year - 1) / 400)
    for (month_index = 1; month_index < month; month_index++)
        total += days[month_index]
    return total + day
}
{
    line = $0
    sub(/[[:space:]]*#.*/, "", line)
    gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
    if (line == "")
        next
    equal = index(line, "=")
    if (!equal || index(substr(line, equal + 1), "=")) {
        fail("expected YYYY-MM-DD=quiet or YYYY-MM-DD=trip")
        next
    }
    date = substr(line, 1, equal - 1)
    result = substr(line, equal + 1)
    day = ordinal(date)
    if (day < 0 || (result != "quiet" && result != "trip")) {
        fail("malformed promotion-history row")
        next
    }
    if (day in recorded) {
        fail("duplicate history date " date)
        next
    }
    recorded[day] = result
    shown[day] = date
    if (!have_latest || day > latest) {
        latest = day
        have_latest = 1
    }
}
END {
    if (bad)
        exit 3
    if (!have_latest) {
        print "runtime_gate: " gate ": promotion refused: no history" > "/dev/stderr"
        exit 1
    }
    quiet = 0
    for (day = latest; day in recorded && recorded[day] == "quiet"; day--)
        quiet++
    if (quiet < 14) {
        print "runtime_gate: " gate ": promotion refused: " quiet \
              " consecutive quiet days; 14 required" > "/dev/stderr"
        exit 1
    }
    print "runtime_gate: " gate ": promotion eligible (" quiet \
          " consecutive quiet days through " shown[latest] ")"
}
' "$history"; then
        return 0
    else
        promotion_status=$?
        [ "$promotion_status" -eq 1 ] && return 1
        exit 3
    fi
}

if [ "${1:-}" = --promote ]; then
    shift
    promote "$@"
    exit $?
fi

[ "$#" -ge 1 ] || usage
config_info=$(read_config "$1")
gate_name=${config_info%%|*}
config_tail=${config_info#*|}
gate_state=${config_tail%%|*}
deferred_sprint=${config_tail#*|}
shift

if [ "$gate_state" = inactive ]; then
    echo "$prog: $gate_name: inactive (deferred until $deferred_sprint)"
    exit 0
fi

[ "$#" -eq 4 ] || usage
baseline=$1
shift
for input in "$baseline" "$@"; do
    [ -r "$input" ] || die "cannot read $input"
done
result_file=${CGF_RUNTIME_GATE_RESULT_FILE:-}
if [ -n "$result_file" ]; then
    [ ! -d "$result_file" ] || die "result file is a directory: $result_file"
    : >"$result_file" || die "cannot write result file: $result_file"
fi

if awk -v baseline_file="$baseline" -v gate="$gate_name" \
    -v state="$gate_state" -v result_file="$result_file" '
function'" "'fail(message) {
    print "runtime_gate: " message > "/dev/stderr"
    malformed = 1
}
function'" "'median3(a, b, c) {
    if (a > b) { temporary = a; a = b; b = temporary }
    if (b > c) { temporary = b; b = c; c = temporary }
    if (a > b) { temporary = a; a = b; b = temporary }
    return b
}
function'" "'valid_number(value) {
    return value ~ /^[0-9]+([.][0-9]+)?$/
}
function'" "'leap_year(year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
}
function'" "'valid_timestamp(value,    year, month, day, hour, minute, second,
                         month_days) {
    if (length(value) != 20 || substr(value, 5, 1) != "-" ||
        substr(value, 8, 1) != "-" || substr(value, 11, 1) != "T" ||
        substr(value, 14, 1) != ":" || substr(value, 17, 1) != ":" ||
        substr(value, 20, 1) != "Z")
        return 0
    year = substr(value, 1, 4)
    month = substr(value, 6, 2)
    day = substr(value, 9, 2)
    hour = substr(value, 12, 2)
    minute = substr(value, 15, 2)
    second = substr(value, 18, 2)
    if (year !~ /^[0-9][0-9][0-9][0-9]$/ ||
        month !~ /^[0-9][0-9]$/ || day !~ /^[0-9][0-9]$/ ||
        hour !~ /^[0-9][0-9]$/ || minute !~ /^[0-9][0-9]$/ ||
        second !~ /^[0-9][0-9]$/)
        return 0
    year += 0
    month += 0
    day += 0
    hour += 0
    minute += 0
    second += 0
    if (year < 1 || month < 1 || month > 12 || hour > 23 ||
        minute > 59 || second > 59)
        return 0
    split("31 28 31 30 31 30 31 31 30 31 30 31", month_days, " ")
    if (leap_year(year))
        month_days[2] = 29
    return day >= 1 && day <= month_days[month]
}
function'" "'read_line(line, file, line_no,    equal, key, value, slot) {
    if (line ~ /^[[:space:]]*#/ || line ~ /^[[:space:]]*$/)
        return
    equal = index(line, "=")
    if (!equal) {
        fail(file ":" line_no ": expected key=value")
        return
    }
    key = substr(line, 1, equal - 1)
    value = substr(line, equal + 1)
    if (key !~ /^[A-Za-z0-9_.:-]+$/ || value == "") {
        fail(file ":" line_no ": malformed entry")
        return
    }
    slot = file SUBSEP key
    if (slot in seen) {
        fail(file ":" line_no ": duplicate key " key)
        return
    }
    seen[slot] = 1
    values[slot] = value
    if (key == "date") {
        if (!valid_timestamp(value))
            fail(file ":" line_no ": malformed date")
        dates[file] = value
    } else if (key == "host") {
        hosts[file] = value
    } else if (key == "target") {
        targets[file] = value
    } else if (key == "runs") {
        if (value !~ /^[0-9]+$/ || value + 0 < 1)
            fail(file ":" line_no ": runs must be a positive integer")
        run_counts[file] = value
    } else if (key == "warmup") {
        if (value !~ /^[0-9]+$/)
            fail(file ":" line_no ": warmup must be a non-negative integer")
        warmups[file] = value
    } else if (key == "timeit_protocol") {
        protocols[file] = value
    } else if (key == "governor") {
        governors[file] = value
    } else if (key == "load1") {
        loads[file] = value
    }
    if (key ~ /[.]cgf[.]wall_ms_median$/) {
        if (!valid_number(value))
            fail(file ":" line_no ": median must be non-negative numeric")
        metric = key
        sub(/[.]wall_ms_median$/, "", metric)
        med[file SUBSEP metric] = value
        metrics[metric] = 1
        metric_seen[file SUBSEP metric]++
    } else if (key ~ /[.]cgf[.]wall_ms_mad$/) {
        if (!valid_number(value))
            fail(file ":" line_no ": MAD must be non-negative numeric")
        metric = key
        sub(/[.]wall_ms_mad$/, "", metric)
        mad[file SUBSEP metric] = value
        metrics[metric] = 1
        metric_seen[file SUBSEP metric]++
    }
}
{
    read_line($0, FILENAME, FNR)
}
END {
    file_count = 0
    for (file_index = 1; file_index < ARGC; file_index++) {
        file = ARGV[file_index]
        files[++file_count] = file
        if (!(file in dates))
            fail(file ": missing date")
        else if (file != baseline_file) {
            if (dates[file] in run_date)
                fail(file ": duplicate run date " dates[file])
            run_date[dates[file]] = 1
            run_day = substr(dates[file], 1, 10)
            if (run_day in recorded_run_day)
                fail(file ": duplicate UTC run day " run_day)
            recorded_run_day[run_day] = 1
        }
        if (!(file in hosts))
            fail(file ": missing host")
        else if (file != baseline_file && hosts[file] != hosts[baseline_file])
            fail(file ": host does not match baseline")
        if (!(file in targets))
            fail(file ": missing target")
        else if (file != baseline_file && targets[file] != targets[baseline_file])
            fail(file ": target does not match baseline")
        if (!(file in run_counts))
            fail(file ": missing runs")
        else if (file != baseline_file &&
                 run_counts[file] != run_counts[baseline_file])
            fail(file ": runs does not match baseline")
        if (!(file in warmups))
            fail(file ": missing warmup")
        else if (file != baseline_file && warmups[file] != warmups[baseline_file])
            fail(file ": warmup does not match baseline")
        if (!(file in protocols))
            fail(file ": missing timeit_protocol")
        else if (file != baseline_file &&
                 protocols[file] != protocols[baseline_file])
            fail(file ": timeit_protocol does not match baseline")
        if (!(file in governors))
            fail(file ": missing governor")
        else if (hosts[baseline_file] == "nomad-1") {
            if (governors[file] != "performance" &&
                governors[file] != "unavailable")
                fail(file ": nomad-1 governor must be performance or unavailable")
        } else if (governors[file] != "performance")
            fail(file ": non-nomad runtime requires governor=performance (got " governors[file] ")")
        if (file != baseline_file &&
            governors[file] != governors[baseline_file])
            fail(file ": governor does not match baseline")
        if (!(file in loads))
            fail(file ": missing load1")
        else if (hosts[baseline_file] == "nomad-1") {
            if (loads[file] != "unknown" && !valid_number(loads[file]))
                fail(file ": nomad-1 load1 must be numeric or unknown")
            else if (loads[file] != "unknown" && loads[file] + 0 > 0.5)
                fail(file ": load1 exceeds controlled limit 0.5 (got " loads[file] ")")
        } else if (!valid_number(loads[file]))
            fail(file ": non-nomad runtime requires numeric load1")
        else if (loads[file] + 0 > 0.5)
            fail(file ": load1 exceeds controlled limit 0.5 (got " loads[file] ")")
    }
    if (file_count != 4)
        fail("internal file-count mismatch")
    metric_count = 0
    for (metric in metrics) {
        metric_count++
        for (file_index = 1; file_index <= file_count; file_index++) {
            file = files[file_index]
            if (metric_seen[file SUBSEP metric] != 2)
                fail(file ": missing median/MAD pair for " metric)
        }
    }
    if (!metric_count)
        fail("baseline contains no cgf kernel runtime metrics")
    if (malformed)
        exit 3

    regressions = 0
    comparisons = 0
    for (metric in metrics) {
        base_median = med[baseline_file SUBSEP metric] + 0
        base_mad = mad[baseline_file SUBSEP metric] + 0
        new_median = median3(med[files[2] SUBSEP metric] + 0,
                             med[files[3] SUBSEP metric] + 0,
                             med[files[4] SUBSEP metric] + 0)
        new_mad = median3(mad[files[2] SUBSEP metric] + 0,
                          mad[files[3] SUBSEP metric] + 0,
                          mad[files[4] SUBSEP metric] + 0)
        delta = new_median - base_median
        noise = 4 * (base_mad > new_mad ? base_mad : new_mad)
        percent_trip = new_median > base_median * 1.10
        noise_trip = delta > noise
        comparisons++
        if (percent_trip && noise_trip) {
            regressions++
            printf "runtime_gate: %s: REGRESSION %s baseline=%.6f new=%.6f delta=%.6f noise_limit=%.6f\n", \
                   gate, metric, base_median, new_median, delta, noise > "/dev/stderr"
        }
    }
    if (result_file != "") {
        print (regressions ? "trip" : "pass") > result_file
        close(result_file)
    }
    if (regressions) {
        if (state == "trial") {
            print "runtime_gate: " gate ": trial reported " regressions \
                  " regression(s); non-blocking"
            exit 0
        }
        print "runtime_gate: " gate ": blocking failure (" regressions \
              " regression(s))" > "/dev/stderr"
        exit 1
    }
    print "runtime_gate: " gate ": pass (" comparisons \
          " comparisons, state=" state ")"
}
' "$baseline" "$@"; then
    exit 0
else
    gate_status=$?
    [ "$gate_status" -eq 1 ] && exit 1
    exit 3
fi
