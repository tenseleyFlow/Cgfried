#!/bin/sh
# -dM differential: every macro cgf predefines must exist in gcc's -dM
# output with an IDENTICAL value (allowlist: our honest scope answers and
# frozen date/time), and __GNUC__ must be ABSENT from ours (the no-__GNUC__
# policy — see pp_predefine_all).
set -eu
LC_ALL=C
export LC_ALL
CGF=${1:?usage: pp_dm_check.sh path/to/cgfried}
command -v gcc >/dev/null 2>&1 || { echo "pp_dm_check: gcc required" >&2; exit 1; }
work=$(dirname "$CGF")
: > "$work/dm_empty.c"
"$CGF" -E -dM "$work/dm_empty.c" > "$work/ours.dm"
gcc -E -dM "$work/dm_empty.c" > "$work/gcc.dm"
if grep -q '__GNUC__' "$work/ours.dm"; then
    echo "pp_dm_check: __GNUC__ must NOT be defined (policy)" >&2
    exit 1
fi
fails=0
while read -r _ name rest; do
    case "$name" in
    __STDC_NO_* | __DATE__ | __TIME__ | __STDC_HOSTED__ | __STDC__ | __STDC_VERSION__)
        continue ;; # honest scope answers / frozen / std-dependent
    esac
    g=$(grep "^#define $name " "$work/gcc.dm") || {
        echo "pp_dm_check: '$name' not in gcc -dM" >&2
        fails=$((fails + 1))
        continue
    }
    [ "$g" = "#define $name $rest" ] || {
        echo "pp_dm_check: value differs: ours='$name $rest' gcc='${g#\#define }'" >&2
        fails=$((fails + 1))
    }
done < "$work/ours.dm"
[ "$fails" -eq 0 ] && echo "pp_dm_check: $(wc -l < "$work/ours.dm") predefines match gcc; __GNUC__ absent"
exit "$fails"
