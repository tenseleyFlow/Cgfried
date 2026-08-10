#!/bin/sh
# Deterministic many-translation-unit benchmark corpus.  The output is a
# generated artifact, not a source-tree input: identical COUNT/LINES values
# always produce identical bytes, independent of host, clock, or randomness.
set -eu
LC_ALL=C
export LC_ALL

out=${1:-build/bench/corpus/many-tu}
count=${CGF_BENCH_TU_COUNT:-500}
lines=${CGF_BENCH_TU_LINES:-200}

case $count:$lines in
    *[!0-9:]* | :* | *:) echo "gen-tu-corpus: count and lines must be positive integers" >&2; exit 2 ;;
esac
if [ "$count" -lt 1 ] || [ "$lines" -lt 32 ]; then
    echo "gen-tu-corpus: count must be >=1 and lines must be >=32" >&2
    exit 2
fi
case $out in
    '' | / | . | ..) echo "gen-tu-corpus: refusing unsafe output '$out'" >&2; exit 2 ;;
esac

if [ -e "$out" ] && [ ! -d "$out" ]; then
    echo "gen-tu-corpus: output exists and is not a directory: $out" >&2
    exit 2
fi
if [ -d "$out" ] && [ -n "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
    if [ ! -f "$out/manifest.txt" ] ||
       ! grep -qx 'format=cgf-many-tu-v1' "$out/manifest.txt"; then
        echo "gen-tu-corpus: refusing nonempty directory not owned by this generator: $out" >&2
        exit 2
    fi
fi
mkdir -p "$out"
# Delete only files owned by this generator.  This makes a second run with a
# smaller COUNT byte-for-byte equivalent to a clean generation.
find "$out" -maxdepth 1 -type f \( -name 'tu_*.c' -o -name 'manifest.txt' \) \
    -exec rm -f {} +

i=0
while [ "$i" -lt "$count" ]; do
    file=$(printf '%s/tu_%04d.c' "$out" "$i")
    {
        printf '/* cgf many-tu corpus v1; unit=%d lines=%d */\n' "$i" "$lines"
        printf '#include <stddef.h>\n#include <stdint.h>\n'
        printf '#define TU_SEED %du\n' "$((i * 2654435761 % 4294967296))"
        printf 'typedef struct row_%d { uint32_t key; uint16_t tag; uint8_t data[10]; } row_%d;\n' "$i" "$i"
        printf 'static uint32_t mix_%d(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; return x; }\n' "$i"
        # Six fixed header/declaration lines plus one entry point leave exactly
        # LINES-7 generated functions, so the physical line count is exact.
        n=7
        while [ "$n" -lt "$lines" ]; do
            printf 'static uint32_t f_%d_%03d(uint32_t x) { return mix_%d(x + %du) ^ (TU_SEED >> %d); }\n' \
                "$i" "$n" "$i" "$((i + n * 17))" "$((n % 13))"
            n=$((n + 1))
        done
        printf 'int tu_%04d_entry(int x) { return (int)f_%d_%03d((uint32_t)x); }\n' \
            "$i" "$i" "$((lines - 1))"
    } >"$file"
    i=$((i + 1))
done

{
    echo 'format=cgf-many-tu-v1'
    echo "count=$count"
    echo "lines=$lines"
    echo 'seed=fixed-arithmetic-v1'
} >"$out/manifest.txt"

echo "gen-tu-corpus: generated $count deterministic TUs in $out"
