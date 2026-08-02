#!/bin/sh
# Format-warning false-positive pass over TinyCC's top-level translation
# units.  Files still requiring deferred GNU syntax are counted explicitly;
# every file Cgfried accepts must also be accepted by the oracle, and its
# format-family warning set must be a subset of the oracle's set.
set -eu
LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

CGF=${1:?usage: tinycc_warn_dryrun.sh path/to/cgfried}
ORACLE=${CGF_TINYCC_WARN_GCC:-gcc}
REF=${CGF_TINYCC_REF:-.docs/refs/tinycc}
OVERLAY=${CGF_TINYCC_OVERLAY:-tests/warn/tinycc-overlay}
WORK=${CGF_TINYCC_WARN_WORK:-build/tinycc-warn}
BASELINE=${CGF_TINYCC_WARN_BASELINE:-tests/warn/tinycc-corpus-baseline.txt}

if [ ! -f "$REF/tcc.h" ]; then
    echo 'HARNESS_SKIP suite=tinycc-warn test=corpus count=1 reason="TinyCC reference clone not found"'
    exit 0
fi

rm -rf "$WORK"
mkdir -p "$WORK"
sources=$WORK/sources.txt
diag=$WORK/diag.txt
oracle_diag=$WORK/oracle-diag.txt
cgf_set=$WORK/cgf-set.txt
oracle_set=$WORK/oracle-set.txt
false_positive=$WORK/false-positive.txt
observed=$WORK/observed.txt
find "$REF" -maxdepth 1 -type f -name '*.c' -print | sort > "$sources"

total=0
parsed=0
deferred=0
oracle_failures=0
: > "$false_positive"
: > "$observed"
: > "$WORK/parsed-sources.txt"
: > "$WORK/deferred-sources.txt"

format_warning_set() {
    awk -v root="$REF/" '
    /:[0-9][0-9]*:[0-9][0-9]*: (warning|error):/ && /\[-W[^]]*\]/ {
        line = $0
        sub("^" root, "", line)
        path = line
        sub(/:[0-9].*$/, "", path)
        location = line
        sub(/^[^:]*:/, "", location)
        sub(/:.*/, "", location)
        flag = line
        sub(/^.*\[-W/, "", flag)
        sub(/\].*$/, "", flag)
        sub(/^error=/, "", flag)
        sub(/=$/, "", flag)
        if (flag == "format" || flag ~ /^format-/ || flag == "nonnull")
            print path ":" location, flag
    }
    ' "$1" | sort -u > "$2"
}

while IFS= read -r file; do
    total=$((total + 1))
    if "$CGF" -std=gnu17 -fsyntax-only -Wall -DONE_SOURCE=0 \
        '-D__attribute__(x)=' '-D__extension__=' \
        -I "$OVERLAY" -I "$REF" "$file" >/dev/null 2>"$diag"; then
        parsed=$((parsed + 1))
        printf '%s\n' "$file" >> "$WORK/parsed-sources.txt"
        if ! "$ORACLE" -std=gnu17 -fsyntax-only -Wall -DONE_SOURCE=0 \
            '-D__attribute__(x)=' '-D__extension__=' \
            -I "$OVERLAY" -I "$REF" "$file" >/dev/null 2>"$oracle_diag"; then
            oracle_failures=$((oracle_failures + 1))
            printf '%s\n' "$file" >> "$WORK/oracle-failures.txt"
            continue
        fi
        format_warning_set "$diag" "$cgf_set"
        format_warning_set "$oracle_diag" "$oracle_set"
        cat "$cgf_set" >> "$observed"
        comm -23 "$cgf_set" "$oracle_set" >> "$false_positive"
    else
        deferred=$((deferred + 1))
        printf '%s\n' "$file" >> "$WORK/deferred-sources.txt"
    fi
done < "$sources"

sort -u "$observed" -o "$observed"
sort -u "$false_positive" -o "$false_positive"
warning_count=$(wc -l < "$observed" | tr -d ' ')
false_count=$(wc -l < "$false_positive" | tr -d ' ')
{
    echo "sources $total"
    echo "parsed $parsed"
    echo "deferred $deferred"
    echo "oracle-failures $oracle_failures"
    echo "format-warnings $warning_count"
    echo "false-positives $false_count"
} > "$WORK/result.txt"

if [ "$oracle_failures" -ne 0 ]; then
    echo "tinycc_warn: oracle failed for $oracle_failures Cgfried-accepted source(s)" >&2
    cat "$WORK/oracle-failures.txt" >&2
    exit 1
fi
if [ "$false_count" -ne 0 ]; then
    echo "tinycc_warn: false-positive format warning set is nonempty" >&2
    cat "$false_positive" >&2
    exit 1
fi
if [ ! -f "$BASELINE" ]; then
    echo "tinycc_warn: missing baseline $BASELINE" >&2
    cat "$WORK/result.txt" >&2
    exit 1
fi
if ! cmp -s "$BASELINE" "$WORK/result.txt"; then
    echo "tinycc_warn: corpus baseline changed" >&2
    diff -u "$BASELINE" "$WORK/result.txt" >&2 || true
    exit 1
fi

echo "tinycc_warn: $parsed/$total sources parsed; $deferred deferred; $warning_count format warnings; zero false positives"
