#!/bin/sh
# Static tripwires for source patterns that can leak host/process state into
# compiler output.  This is deliberately a small grep audit, not a C parser.
set -eu
LC_ALL=C
export LC_ALL

root=${1:-.}
if [ ! -d "$root" ]; then
    echo "audit-determinism: not a directory: $root" >&2
    exit 2
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-audit-determinism.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

(
    cd "$root"
    if [ -d src ]; then
        find src -type f \( -name '*.c' -o -name '*.h' \) -print
    else
        find . -type f \( -name '*.c' -o -name '*.h' \) -print |
            sed 's#^\./##'
    fi
) | sort >"$tmp/files"

# A finding may be waived only by a nearby source comment of the form
#   determinism-audit allow CATEGORY: concrete reason
# This keeps exceptional cases review-visible instead of growing a silent
# filename allowlist.
marked_allow() {
    file=$1
    line=$2
    category=$3
    first=$((line > 1 ? line - 1 : 1))
    sed -n "${first},${line}p" "$root/$file" |
        grep -F "determinism-audit allow $category:" >/dev/null 2>&1
}

# POSIX grep has no portable always-print-filename flag for a single file.
# Keep the scanner explicit and append stable, path:line-only findings.
scan() {
    category=$1
    pattern=$2
    while IFS= read -r file; do
        [ -n "$file" ] || continue
        grep -nE "$pattern" "$root/$file" 2>/dev/null |
            while IFS=: read -r line text; do
                if ! marked_allow "$file" "$line" "$category"; then
                    printf '%s\t%s\t%s\n' "$category" "$file" "$line" \
                        >>"$tmp/hits"
                fi
            done || true
    done <"$tmp/files"
}

: >"$tmp/hits"
scan qsort '(^|[^A-Za-z0-9_])qsort[[:space:]]*\('
scan pointer-output \
    '(printf|fprintf|sprintf|snprintf|dprintf)[[:space:]]*\([^;]*(u?intptr_t|\(void[[:space:]]*\*\))'
scan percent-p '%p'
scan fwrite-struct '(^|[^A-Za-z0-9_])fwrite[[:space:]]*\([[:space:]]*&'
scan memcmp-object \
    '(^|[^A-Za-z0-9_])memcmp[[:space:]]*\([^;]*sizeof[[:space:]]*\('
scan random \
    '(^|[^A-Za-z0-9_])(arc4random|drand48|lrand48|mrand48|rand|random|seed48|srand|srandom)[[:space:]]*\('
scan strcoll '(^|[^A-Za-z0-9_])strcoll[[:space:]]*\('

# The only current readdir consumer selects the lexicographically greatest
# readable libgcc path with strcmp.  Its result is therefore independent of
# enumeration order.  Allow exactly its two loops, not the whole file.
while IFS= read -r file; do
    [ -n "$file" ] || continue
    grep -nE '(^|[^A-Za-z0-9_])readdir[[:space:]]*\(' "$root/$file" \
        2>/dev/null | while IFS=: read -r line text; do
        case "$file:$text" in
        'src/driver/toolchain.c:    while ((tri = readdir(top)) != NULL) {' | \
        'src/driver/toolchain.c:        while ((ver = readdir(vers)) != NULL) {')
            # Explicit allowlist: locate_libgcc_dir compares every candidate
            # and chooses max(path), so directory order cannot reach output.
            ;;
        *)
            if ! marked_allow "$file" "$line" readdir; then
                printf '%s\t%s\t%s\n' readdir "$file" "$line" >>"$tmp/hits"
            fi
            ;;
        esac
    done || true
done <"$tmp/files"

if [ -s "$tmp/hits" ]; then
    sort -t '	' -k1,1 -k2,2 -k3,3n "$tmp/hits" |
        while IFS='	' read -r category file line; do
            printf 'DETERMINISM_AUDIT %s %s:%s\n' "$category" "$file" "$line"
        done
    count=$(wc -l <"$tmp/hits" | tr -d ' ')
    echo "audit-determinism: $count finding(s)"
    exit 1
fi

echo "audit-determinism: clean"
