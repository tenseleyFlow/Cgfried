#!/bin/sh
# Warning false-positive stress pass over musl's x86_64 source corpus.  GNU
# attributes/aliases are erased at the command line because their semantics
# land in Sprint 55; files that still require unsupported GNU syntax are
# counted as deferred, never silently treated as warning-clean parses.
set -eu
LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

CGF=${1:?usage: musl_warn_dryrun.sh path/to/cgfried}
ORACLE=${CGF_MUSL_WARN_GCC:-gcc}
REF=${CGF_MUSL_REF:-.docs/refs/musl}
WORK=${CGF_MUSL_WARN_WORK:-build/musl-warn}
BASELINE=${CGF_MUSL_WARN_BASELINE:-tests/warn/corpus-baseline.txt}
TRIAGE=${CGF_MUSL_WARN_TRIAGE:-tests/warn/corpus-genuine-divergences.txt}

if [ ! -d "$REF/src" ] || [ ! -f "$REF/tools/mkalltypes.sed" ]; then
    echo 'HARNESS_SKIP suite=musl-warn test=corpus count=1 reason="musl reference clone not found"'
    exit 0
fi

rm -rf "$WORK"
mkdir -p "$WORK/include/bits" "$WORK/src/internal"
sed -f "$REF/tools/mkalltypes.sed" \
    "$REF/arch/x86_64/bits/alltypes.h.in" "$REF/include/alltypes.h.in" \
    > "$WORK/include/bits/alltypes.h"
cp "$REF/arch/x86_64/bits/syscall.h.in" "$WORK/include/bits/syscall.h"
sed -n 's/__NR_/SYS_/p' "$REF/arch/x86_64/bits/syscall.h.in" \
    >> "$WORK/include/bits/syscall.h"
printf '#define VERSION "cgfried-s38-dry-run"\n' \
    > "$WORK/src/internal/version.h"

sources=$WORK/sources.txt
observed=$WORK/observed.txt
expected=$WORK/expected.txt
diag=$WORK/diag.txt
oracle_diag=$WORK/oracle-diag.txt
cgf_set=$WORK/cgf-set.txt
oracle_set=$WORK/oracle-set.txt
false_positive=$WORK/false-positive.txt
false_positive_raw=$WORK/false-positive-raw.txt
triage_set=$WORK/triage-set.txt
find "$REF/src" -mindepth 2 -maxdepth 2 -type f -name '*.c' -print | sort \
    > "$sources"
find "$REF/src" -mindepth 3 -maxdepth 3 -type f \
    -path '*/x86_64/*.c' -print | sort >> "$sources"
sort -u "$sources" -o "$sources"

total=0
parsed=0
deferred=0
oracle_failures=0
warning_count=0
: > "$observed"
: > "$WORK/oracle-observed.txt"
: > "$false_positive"
: > "$WORK/parsed-sources.txt"
: > "$WORK/deferred-sources.txt"

warning_set() {
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
        print path ":" location, flag
    }
    ' "$1" | sort -u > "$2"
}

while IFS= read -r file; do
    total=$((total + 1))
    if "$CGF" -std=gnu17 -S -o "$WORK/cgf-out.s" -ffreestanding -nostdinc \
        -Wall -Wextra -D_XOPEN_SOURCE=700 -DFEATURES_H \
        -U__GNUC__ \
        '-D__attribute__(x)=' '-Dweak=' '-Dhidden=' \
        '-Dweak_alias(a,b)=' '-Dstrong_alias(a,b)=' \
        '-D__inline=inline' '-D__restrict=restrict' \
        -I "$REF/arch/x86_64" -I "$REF/arch/generic" \
        -I "$WORK/src/internal" -I "$REF/src/include" \
        -I "$REF/src/internal" -I "$WORK/include" -I "$REF/include" \
        "$file" >/dev/null 2>"$diag"; then
        parsed=$((parsed + 1))
        printf '%s\n' "$file" >> "$WORK/parsed-sources.txt"
        # THE ORACLE MUST BEHAVE LIKE THE PARITY BASELINE, which is gcc 8.
        # gcc 14 made an implicit function declaration an ERROR by default,
        # and `-Dweak=` strips the very declarations musl relies on, so a
        # modern host oracle FAILS on files gcc 8 merely warns about
        # (verified in the gcc:8 container: exit.c warns, rc=0). Those files
        # were invisible until the compiler could parse them; the day it
        # could, three of them turned into "oracle failed" and the lane
        # rightly refused to score them. Restoring the gcc 8 severity is the
        # fix -- the WARNING still appears and is still compared.
        if ! "$ORACLE" -std=gnu17 -S -o "$WORK/oracle-out.s" \
            -ffreestanding -nostdinc \
            -Wno-error=implicit-function-declaration \
            -Wno-error=implicit-int -Wno-error=int-conversion \
            -Wall -Wextra -D_XOPEN_SOURCE=700 -DFEATURES_H \
            -U__GNUC__ \
            '-D__attribute__(x)=' '-Dweak=' '-Dhidden=' \
            '-Dweak_alias(a,b)=' '-Dstrong_alias(a,b)=' \
            '-D__inline=inline' '-D__restrict=restrict' \
            -I "$REF/arch/x86_64" -I "$REF/arch/generic" \
            -I "$WORK/src/internal" -I "$REF/src/include" \
            -I "$REF/src/internal" -I "$WORK/include" -I "$REF/include" \
            "$file" >/dev/null 2>"$oracle_diag"; then
            oracle_failures=$((oracle_failures + 1))
            printf '%s\n' "$file" >> "$WORK/oracle-failures.txt"
            continue
        fi
        warning_set "$diag" "$cgf_set"
        warning_set "$oracle_diag" "$oracle_set"
        cat "$cgf_set" >> "$observed"
        cat "$oracle_set" >> "$WORK/oracle-observed.txt"
        comm -23 "$cgf_set" "$oracle_set" >> "$false_positive"
    else
        deferred=$((deferred + 1))
        printf '%s\n' "$file" >> "$WORK/deferred-sources.txt"
    fi
done < "$sources"

sort -u "$observed" -o "$observed"
sort -u "$false_positive" -o "$false_positive_raw"
if [ ! -f "$TRIAGE" ]; then
    echo "musl_warn: missing genuine-divergence triage $TRIAGE" >&2
    exit 1
fi
sed '/^#/d; /^[[:space:]]*$/d' "$TRIAGE" | sort -u > "$triage_set"
comm -23 "$false_positive_raw" "$triage_set" > "$false_positive"
comm -13 "$false_positive_raw" "$triage_set" > "$WORK/stale-triage.txt"
if [ -s "$WORK/stale-triage.txt" ]; then
    echo "musl_warn: stale genuine-divergence triage" >&2
    cat "$WORK/stale-triage.txt" >&2
    exit 1
fi
warning_count=$(wc -l < "$observed" | tr -d ' ')
false_count=$(wc -l < "$false_positive" | tr -d ' ')
triage_count=$(wc -l < "$triage_set" | tr -d ' ')
{
    echo "sources $total"
    echo "parsed $parsed"
    echo "deferred $deferred"
    echo "oracle-failures $oracle_failures"
    echo "genuine-warnings $warning_count"
    echo "genuine-location-divergences $triage_count"
    echo "false-positives $false_count"
    awk '{ print $2 }' "$observed" | sort | uniq -c | \
        awk '{ print "genuine", $2, $1, "oracle-matched" }'
} > "$WORK/result.txt"

if [ "$oracle_failures" -ne 0 ]; then
    echo "musl_warn: oracle failed for $oracle_failures CGF-accepted source(s)" >&2
    cat "$WORK/oracle-failures.txt" >&2
    exit 1
fi

if [ "$false_count" -ne 0 ]; then
    echo "musl_warn: false-positive warning set is nonempty" >&2
    cat "$false_positive" >&2
    exit 1
fi

if [ ! -f "$BASELINE" ]; then
    echo "musl_warn: missing baseline $BASELINE" >&2
    cat "$WORK/result.txt" >&2
    exit 1
fi

cp "$BASELINE" "$expected"

if ! cmp -s "$expected" "$WORK/result.txt"; then
    echo "musl_warn: corpus baseline changed" >&2
    diff -u "$expected" "$WORK/result.txt" >&2 || true
    exit 1
fi

echo "musl_warn: $parsed/$total sources parsed; $deferred deferred; $warning_count oracle-matched warnings; zero false positives"
