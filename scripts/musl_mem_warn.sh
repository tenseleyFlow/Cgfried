#!/bin/sh
# Sprint 42's false-positive budget: default -Wmem must be silent over every
# musl translation unit the current front end can lower to analysis IR.  The
# pinned baseline makes both analyzed coverage and unsupported-GNU deferrals
# explicit; a front-end change must move that baseline deliberately.
set -eu
LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

cc=${1:-build/cgfried}
ref=${CGF_MUSL_REF:-.docs/refs/musl}
work=${CGF_MUSL_MEM_WORK:-build/musl-mem-warn}
baseline=${CGF_MUSL_MEM_BASELINE:-tests/memsafe/musl-baseline.txt}
expected_commit=b306b16af15c89a04d8e0c55cac2dadbeb39c083
start=$(date +%s)

if [ ! -x "$cc" ]; then
    echo "musl_mem_warn: compiler not executable: $cc" >&2
    exit 1
fi
if ! command -v sha256sum >/dev/null 2>&1; then
    echo "musl_mem_warn: sha256sum is required for the identity-set pin" >&2
    exit 1
fi
if [ ! -d "$ref/src" ] || [ ! -f "$ref/tools/mkalltypes.sed" ]; then
    echo 'HARNESS_SKIP suite=musl-mem-warn test=corpus count=1 reason="musl reference clone not found"'
    exit 0
fi
if [ ! -f "$baseline" ]; then
    echo "musl_mem_warn: missing baseline: $baseline" >&2
    exit 1
fi
actual_commit=$(git -C "$ref" rev-parse HEAD 2>/dev/null || true)
if [ "$actual_commit" != "$expected_commit" ]; then
    echo "musl_mem_warn: musl commit $actual_commit, expected $expected_commit" >&2
    exit 1
fi
if [ -z "$work" ] || [ "$work" = / ]; then
    echo "musl_mem_warn: refusing unsafe work directory: $work" >&2
    exit 1
fi
mkdir -p "$work"
work=$(mktemp -d "$work/run.XXXXXX")
mkdir -p "$work/include/bits" "$work/src/internal"
sed -f "$ref/tools/mkalltypes.sed" \
    "$ref/arch/x86_64/bits/alltypes.h.in" "$ref/include/alltypes.h.in" \
    > "$work/include/bits/alltypes.h"
cp "$ref/arch/x86_64/bits/syscall.h.in" "$work/include/bits/syscall.h"
sed -n 's/__NR_/SYS_/p' "$ref/arch/x86_64/bits/syscall.h.in" \
    >> "$work/include/bits/syscall.h"
printf '#define VERSION "cgfried-s42-mem-sweep"\n' \
    > "$work/src/internal/version.h"

sources="$work/sources.txt"
diag="$work/diag.txt"
hits="$work/memory-diagnostics.txt"
find "$ref/src" -mindepth 2 -maxdepth 2 -type f -name '*.c' -print | sort \
    > "$sources"
find "$ref/src" -mindepth 3 -maxdepth 3 -type f \
    -path '*/x86_64/*.c' -print | sort >> "$sources"
sort -u "$sources" -o "$sources"

total=0
parsed=0
deferred=0
: > "$hits"
: > "$work/parsed-sources.txt"
: > "$work/deferred-sources.txt"

while IFS= read -r file; do
    total=$((total + 1))
    case "$file" in
        "$ref"/*) identity=${file#"$ref"/} ;;
        *)
            echo "musl_mem_warn: source is outside reference root: $file" >&2
            exit 1
            ;;
    esac
    outcome=$(sh scripts/musl_mem_compile.sh "$cc" "$diag" "$identity" \
        -std=gnu17 -fsyntax-only -ffreestanding -nostdinc -Wmem \
        -D_XOPEN_SOURCE=700 -DFEATURES_H -U__GNUC__ \
        '-D__attribute__(x)=' '-Dweak=' '-Dhidden=' \
        '-Dweak_alias(a,b)=' '-Dstrong_alias(a,b)=' \
        '-D__inline=inline' '-D__restrict=restrict' \
        -I "$ref/arch/x86_64" -I "$ref/arch/generic" \
        -I "$work/src/internal" -I "$ref/src/include" \
        -I "$ref/src/internal" -I "$work/include" -I "$ref/include" \
        "$file")
    if [ "$outcome" = analyzed ]; then
        parsed=$((parsed + 1))
        printf '%s\n' "$identity" >> "$work/parsed-sources.txt"
    elif [ "$outcome" = deferred ]; then
        deferred=$((deferred + 1))
        printf '%s\n' "$identity" >> "$work/deferred-sources.txt"
    else
        echo "musl_mem_warn: invalid compiler outcome '$outcome'" >&2
        exit 1
    fi
    if grep '\[-Wmem\([^]]*\)\]' "$diag" >/dev/null; then
        printf '==> %s <==\n' "$file" >> "$hits"
        grep '\[-Wmem\([^]]*\)\]' "$diag" >> "$hits"
    fi
done < "$sources"

# Pin the exact normalized identity sets, not merely their cardinalities.  A
# file moving between analyzed and deferred changes one or both digests even
# when the aggregate counts happen to remain identical.
sort -u "$work/parsed-sources.txt" -o "$work/parsed-sources.txt"
sort -u "$work/deferred-sources.txt" -o "$work/deferred-sources.txt"
analyzed_identity_count=$(wc -l < "$work/parsed-sources.txt" | tr -d ' ')
deferred_identity_count=$(wc -l < "$work/deferred-sources.txt" | tr -d ' ')
if [ "$analyzed_identity_count" -ne "$parsed" ] || \
    [ "$deferred_identity_count" -ne "$deferred" ]; then
    echo "musl_mem_warn: duplicate or missing normalized source identity" >&2
    exit 1
fi
analyzed_identities=$(sha256sum "$work/parsed-sources.txt" | awk '{ print $1 }')
deferred_identities=$(sha256sum "$work/deferred-sources.txt" | awk '{ print $1 }')

if [ "$total" -lt 1300 ]; then
    echo "musl_mem_warn: corpus too small: $total sources (need >=1300)" >&2
    exit 1
fi
if [ "$parsed" -lt 700 ]; then
    echo "musl_mem_warn: parse coverage too small: $parsed sources (need >=700)" >&2
    exit 1
fi
if [ $((parsed + deferred)) -ne "$total" ]; then
    echo "musl_mem_warn: accounting mismatch: $parsed + $deferred != $total" >&2
    exit 1
fi
if [ -s "$hits" ]; then
    echo "musl_mem_warn: default -Wmem emitted diagnostics" >&2
    cat "$hits" >&2
    exit 1
fi

result="$work/result.txt"
{
    echo "musl-commit $actual_commit"
    echo "sources $total"
    echo "analyzed $parsed"
    echo "deferred $deferred"
    echo "analyzed-identities-sha256 $analyzed_identities"
    echo "deferred-identities-sha256 $deferred_identities"
    echo "memory-warnings 0"
} > "$result"
if ! cmp -s "$baseline" "$result"; then
    echo "musl_mem_warn: analyzed/deferred baseline changed" >&2
    diff -u "$baseline" "$result" >&2 || true
    exit 1
fi

finish=$(date +%s)
elapsed=$((finish - start))
if [ "$elapsed" -ge 90 ]; then
    echo "musl_mem_warn: runtime budget exceeded: ${elapsed}s (must be <90s)" >&2
    exit 1
fi

echo "musl_mem_warn: $parsed/$total analyzed; $deferred pinned deferrals; zero -Wmem diagnostics; ${elapsed}s"
