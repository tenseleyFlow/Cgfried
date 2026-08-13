#!/bin/sh
set -eu

raw=
while [ "$#" -gt 0 ]; do
    case $1 in
    -o)
        [ "$#" -ge 2 ] || exit 2
        raw=$2
        shift 2
        ;;
    --)
        shift
        break
        ;;
    *) shift ;;
    esac
done

[ -n "$raw" ] && [ "$#" -gt 0 ] || exit 2
status=0
"$@" || status=$?
printf '%s\n' 'sample=1 wall_ms=1 user_ms=1 sys_ms=0 maxrss_kb=1' >"$raw"
cat <<'EOF'
wall_ms_median=1
wall_ms_mad=0
user_ms_median=1
sys_ms_median=0
maxrss_kb_max=1
EOF
exit "$status"
