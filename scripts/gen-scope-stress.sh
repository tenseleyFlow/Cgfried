#!/bin/sh
# Generate the long file-scope declaration fixture that exposed quadratic
# parser/sema lookup.  Output belongs in a build directory, never in the tree.
set -eu
LC_ALL=C
export LC_ALL

out=${1:-build/bench/scope-10000.c}
count=${CGF_SCOPE_STRESS_DECLS:-10000}

case $count in
    '' | *[!0-9]*)
        echo 'gen-scope-stress: declaration count must be a positive integer' >&2
        exit 2
        ;;
esac
[ "$count" -ge 1 ] || {
    echo 'gen-scope-stress: declaration count must be >=1' >&2
    exit 2
}
case $out in
    '' | / | . | ..)
        echo "gen-scope-stress: refusing unsafe output '$out'" >&2
        exit 2
        ;;
esac

mkdir -p "$(dirname "$out")"
tmp=$out.tmp.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM
awk -v count="$count" 'BEGIN {
    for (i = 0; i < count; i++)
        printf "int v%05d;\n", i
    printf "int main(void) { return v%05d; }\n", count - 1
}' >"$tmp"
mv "$tmp" "$out"
trap - EXIT HUP INT TERM
echo "gen-scope-stress: wrote $count declarations to $out"
