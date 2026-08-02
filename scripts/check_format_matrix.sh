#!/bin/sh
set -eu
LC_ALL=C
export LC_ALL

manifest=tests/warn/format/matrix.tsv
tree=tests/warn/format/matrix
work=${CGF_FORMAT_MATRIX_WORK:-build/format-matrix-check}

rows=$(awk '$1 !~ /^#/ && NF {
    if (NF != 6 || $1 !~ /^(printf|scanf)$/ || $2 !~ /^[a-z0-9_]+$/) bad=1
    n++; seen[$1]++; key=$1 "/" $2; if (keys[key]++) dup=1
}
END {
    if (bad || dup || seen["printf"] != 32 || seen["scanf"] != 32) exit 1
    print n
}' "$manifest") || {
    echo 'check_format_matrix: bad schema, duplicate key, or family count is not 32/32' >&2
    exit 1
}
[ "$rows" -eq 64 ] || {
    echo "check_format_matrix: expected 64 semantic rows, found $rows" >&2
    exit 1
}
files=$(find "$tree" -type f -name '*.c' | wc -l | tr -d ' ')
[ "$files" -eq 128 ] || {
    echo "check_format_matrix: expected 128 generated fixtures, found $files" >&2
    exit 1
}

rm -rf "$work"
mkdir -p "$work"
sh scripts/gen_format_matrix.sh "$manifest" "$work/matrix"
diff -ru "$tree" "$work/matrix"
echo 'check_format_matrix: 64 semantic rows, 128 fire/nofire fixtures verified'
