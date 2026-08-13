#!/bin/sh
set -eu

fail() {
    echo "sqlite-baseline: $*" >&2
    exit 1
}

absolute_only=0
if [ "${1:-}" = --absolute-only ]; then
    absolute_only=1
    shift
fi
[ "$#" -eq 4 ] ||
    fail "usage: $0 [--absolute-only] CONFIG HOST O0-RECEIPT O2-RECEIPT"
config=$1
host=$2
o0=$3
o2=$4
[ -r "$config" ] || fail "config is not readable: $config"

value() {
    key=$1
    file=$2
    awk -F= -v key="$key" '
        $1 == key { count++; value = substr($0, length(key) + 2) }
        END { if (count != 1) exit 1; print value }
    ' "$file" || fail "missing or duplicate key $key in $file"
}

[ "$(value version "$config")" = 1 ] || fail "unsupported config version"
[ "$(value sqlite_release "$config")" = 3.46.1 ] ||
    fail "baseline source release does not match the campaign pin"
designated=$(value designated_hosts "$config")
wall_pct=$(value wall_regression_pct "$config")
rss_pct=$(value rss_regression_pct "$config")
o2_absolute_wall=$(value o2_absolute_wall_ms "$config")
for threshold in "$wall_pct" "$rss_pct" "$o2_absolute_wall"; do
    case $threshold in
        '' | *[!0-9]*) fail "thresholds must be integers" ;;
    esac
done

for pair in O0:$o0 O2:$o2; do
    level=${pair%%:*}
    receipt=${pair#*:}
    [ -r "$receipt" ] || fail "$level receipt is not readable: $receipt"
    for metric in wall_ms_median wall_ms_mad user_ms_median sys_ms_median maxrss_kb_max; do
        actual=$(value "$metric" "$receipt")
        printf '%s\n' "$actual" | awk '
            /^[0-9]+([.][0-9]+)?$/ { ok = 1 }
            END { exit !ok }
        ' || fail "$level receipt has nonnumeric $metric=$actual"
    done
done

o2_wall=$(value wall_ms_median "$o2")
awk -v actual="$o2_wall" -v limit="$o2_absolute_wall" \
    'BEGIN { exit !(actual <= limit) }' ||
    fail "O2 wall_ms_median exceeds absolute gate: limit=${o2_absolute_wall}ms actual=${o2_wall}ms"

if [ "$absolute_only" -eq 1 ]; then
    printf 'sqlite-baseline: PASS host=%s absolute-o2=passed relative-gate=deferred-for-initial-controlled-capture\n' \
        "$host"
    exit 0
fi

case ,$designated, in
    *,$host,*) ;;
    *)
        printf 'sqlite-baseline: PASS host=%s absolute-o2=passed relative-gate=not-applicable designated=%s\n' \
            "$host" "$designated"
        exit 0
        ;;
esac

for pair in O0:$o0 O2:$o2; do
    level=${pair%%:*}
    receipt=${pair#*:}
    for metric in wall_ms_median maxrss_kb_max; do
        baseline=$(value "$host.$level.$metric" "$config")
        [ "$baseline" != UNMEASURED ] ||
            fail "$host $level $metric has no committed controlled-host baseline"
        printf '%s\n' "$baseline" | awk '
            /^[0-9]+([.][0-9]+)?$/ { ok = 1 }
            END { exit !ok }
        ' || fail "invalid baseline $host.$level.$metric=$baseline"
        actual=$(value "$metric" "$receipt")
        case $metric in
            wall_ms_median) threshold=$wall_pct ;;
            maxrss_kb_max) threshold=$rss_pct ;;
        esac
        awk -v actual="$actual" -v baseline="$baseline" -v pct="$threshold" \
            'BEGIN { exit !(actual * 100 <= baseline * (100 + pct)) }' ||
            fail "$host $level $metric regressed: baseline=$baseline actual=$actual threshold=${threshold}%"
    done
done
printf 'sqlite-baseline: PASS host=%s scope=designated levels=O0,O2\n' "$host"
