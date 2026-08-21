#!/bin/sh
set -eu

raw=
while [ "$#" -gt 0 ] && [ "$1" != -- ]; do
    case $1 in
    -n | -o | -t | -w)
        [ "$#" -ge 2 ] || exit 2
        [ "$1" != -o ] || raw=$2
        shift 2
        ;;
    *) exit 2 ;;
    esac
done
[ "${1:-}" = -- ] || exit 2
shift

"$@" >/dev/null
[ -z "$raw" ] || printf '1.000000 1.000000 0.000000 1\n' >"$raw"
cat <<'EOF'
wall_ms_median=1.000000
wall_ms_mad=0.000000
user_ms_median=1.000000
user_ms_mad=0.000000
sys_ms_median=0.000000
sys_ms_mad=0.000000
cpu_ms_median=1.000000
cpu_ms_mad=0.000000
maxrss_kb_median=1.000000
maxrss_kb_mad=0.000000
maxrss_kb_max=1
EOF
