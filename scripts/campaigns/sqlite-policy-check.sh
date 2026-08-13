#!/bin/sh
# Validate and describe the reviewed SQLite compile-baseline policy.  The
# default mode is suitable for campaign provenance; --require-numeric is the
# Sprint 59 phase-closure gate and refuses a partially or wholly unmeasured
# designated fleet. --result-detail emits the stable public expected-row
# detail after enforcing the same numeric closure condition; mutable policy
# hashes and thresholds remain in the compile receipt.
set -eu

LC_ALL=C
export LC_ALL

fail()
{
    echo "sqlite-policy: $*" >&2
    exit 1
}

mode=provenance
case ${1:-} in
--require-numeric)
    mode=require-numeric
    shift
    ;;
--result-detail)
    mode=result-detail
    shift
    ;;
esac
[ "$#" -eq 1 ] ||
    fail "usage: $0 [--require-numeric|--result-detail] CONFIG"
config=$1
[ -f "$config" ] && [ -r "$config" ] && [ ! -L "$config" ] ||
    fail "config is not a readable regular file: $config"

value()
{
    key=$1
    awk -F= -v key="$key" '
        $1 == key { count++; value = substr($0, length(key) + 2) }
        END { if (count != 1 || value == "") exit 1; print value }
    ' "$config" || fail "config has no unique $key"
}

sha256()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$config" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$config" | awk '{print $1}'
    else
        fail 'sha256sum or shasum is required'
    fi
}

version=$(value version)
release=$(value sqlite_release)
hosts=$(value designated_hosts)
wall_pct=$(value wall_regression_pct)
rss_pct=$(value rss_regression_pct)
absolute_ms=$(value o2_absolute_wall_ms)
[ "$version" = 1 ] || fail "unsupported config version: $version"
[ "$release" = 3.46.1 ] || fail "unexpected SQLite release: $release"
[ "$hosts" = kasumi,hasu ] ||
    fail "designated_hosts must be exactly kasumi,hasu"
for threshold in "$wall_pct" "$rss_pct" "$absolute_ms"; do
    case $threshold in
    '' | *[!0-9]*) fail 'policy thresholds must be integers' ;;
    esac
done

state=numeric
for host in kasumi hasu; do
    have_numeric=0
    have_unmeasured=0
    for level in O0 O2; do
        for metric in wall_ms_median maxrss_kb_max; do
            metric_value=$(value "$host.$level.$metric")
            case $metric_value in
            UNMEASURED) have_unmeasured=1 ;;
            *)
                printf '%s\n' "$metric_value" | awk '
                    /^[0-9]+([.][0-9]+)?$/ { ok = 1 }
                    END { exit !ok }
                ' || fail "invalid baseline $host.$level.$metric=$metric_value"
                have_numeric=1
                ;;
            esac
        done
    done
    [ "$have_numeric:$have_unmeasured" != 1:1 ] ||
        fail "$host has a partial numeric baseline"
    if [ "$have_unmeasured" -eq 1 ]; then
        state=unmeasured
    fi
done

if [ "$mode" != provenance ] && [ "$state" != numeric ]; then
    fail 'designated-host baselines are not numeric; controlled capture is required'
fi
if [ "$mode" = result-detail ]; then
    printf 'absolute-o2-ms=%s,config=sqlite-baselines-v%s,designated-hosts=%s\n' \
        "$absolute_ms" "$version" "$hosts"
else
    policy_sha=$(sha256)
    printf 'config=sqlite-baselines-v1,policy-sha256=%s,release=%s,designated-hosts=kasumi+hasu,wall-regression-pct=%s,rss-regression-pct=%s,absolute-o2-ms=%s,state=%s\n' \
        "$policy_sha" "$release" "$wall_pct" "$rss_pct" "$absolute_ms" \
        "$state"
fi
