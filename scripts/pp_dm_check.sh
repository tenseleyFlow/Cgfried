#!/bin/sh
# -dM differential: every shared macro cgf predefines must exist in gcc's
# -dM output with an IDENTICAL value (allowlist: our honest scope answers and
# frozen date/time).  The one implementation marker, __CGFRIED__, must be 1,
# and compiler identity is checked in both dialect directions: absent in c17,
# exactly GCC 8.3.0 in gnu17.
set -eu
LC_ALL=C
export LC_ALL
CGF=${1:?usage: pp_dm_check.sh path/to/cgfried}
command -v gcc >/dev/null 2>&1 || { echo "pp_dm_check: gcc required" >&2; exit 1; }
work=$(dirname "$CGF")
: > "$work/dm_empty.c"
"$CGF" -std=c17 -E -dM "$work/dm_empty.c" > "$work/ours.dm"
gcc -std=c17 -E -dM "$work/dm_empty.c" > "$work/gcc.dm"
if grep -q '__GNUC__' "$work/ours.dm"; then
    echo "pp_dm_check: __GNUC__ must not be defined in c17" >&2
    exit 1
fi
if ! grep -qx '#define __CGFRIED__ 1' "$work/ours.dm"; then
    echo "pp_dm_check: __CGFRIED__ must be defined as 1" >&2
    exit 1
fi
fails=0
while read -r _ name rest; do
    case "$name" in
    __CGFRIED__)
        continue ;; # implementation identity, consumed by shipped headers
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

"$CGF" -std=gnu17 -E -dM "$work/dm_empty.c" > "$work/ours-gnu.dm"
grep -qx '#define __GNUC__ 8' "$work/ours-gnu.dm" || {
    echo "pp_dm_check: gnu17 must define __GNUC__ as 8" >&2
    fails=$((fails + 1))
}
grep -qx '#define __GNUC_MINOR__ 3' "$work/ours-gnu.dm" || {
    echo "pp_dm_check: gnu17 must define __GNUC_MINOR__ as 3" >&2
    fails=$((fails + 1))
}
grep -qx '#define __GNUC_PATCHLEVEL__ 0' "$work/ours-gnu.dm" || {
    echo "pp_dm_check: gnu17 must define __GNUC_PATCHLEVEL__ as 0" >&2
    fails=$((fails + 1))
}
grep -qx '#define __GNUC_STDC_INLINE__ 1' "$work/ours-gnu.dm" || {
    echo "pp_dm_check: gnu17 must advertise ISO inline semantics" >&2
    fails=$((fails + 1))
}
grep -qx '#define __USER_LABEL_PREFIX__$' "$work/ours-gnu.dm" || {
    echo "pp_dm_check: native ELF __USER_LABEL_PREFIX__ must be empty" >&2
    fails=$((fails + 1))
}
"$CGF" -std=gnu17 -U__GNUC__ -E -dM "$work/dm_empty.c" \
    > "$work/ours-gnu-u.dm" 2> "$work/ours-gnu-u.err"
if grep -q '^#define __GNUC__ ' "$work/ours-gnu-u.dm"; then
    echo "pp_dm_check: command-line -U__GNUC__ did not remove the builtin" >&2
    fails=$((fails + 1))
fi
if [ -s "$work/ours-gnu-u.err" ]; then
    echo "pp_dm_check: command-line -U of a builtin must be warning-free" >&2
    cat "$work/ours-gnu-u.err" >&2
    fails=$((fails + 1))
fi

[ "$fails" -eq 0 ] && echo "pp_dm_check: $(wc -l < "$work/ours.dm") strict predefines checked; GNU identity=8.3.0; ISO identity absent"
exit "$fails"
