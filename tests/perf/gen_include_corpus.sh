#!/bin/sh
# Generates the guard fast-path benchmark corpus: N guarded headers plus a
# TU that includes all of them REPEATS times over. Deterministic content
# (no clock, no randomness) so the benchmark is reproducible.
set -eu
DIR=${1:?usage: gen_include_corpus.sh <dir> [count] [repeats]}
N=${2:-1000}
REPEATS=${3:-4}

mkdir -p "$DIR"
i=1
while [ "$i" -le "$N" ]; do
    {
        echo "#ifndef CGF_BENCH_H_$i"
        echo "#define CGF_BENCH_H_$i"
        j=1
        while [ "$j" -le 12 ]; do
            echo "typedef struct bench_${i}_$j { int a, b, c; } bench_${i}_$j;"
            echo "#define BENCH_M_${i}_$j(x) ((x) + $i * $j)"
            j=$((j + 1))
        done
        echo "#endif"
    } > "$DIR/h$i.h"
    i=$((i + 1))
done

{
    r=1
    while [ "$r" -le "$REPEATS" ]; do
        i=1
        while [ "$i" -le "$N" ]; do
            echo "#include \"h$i.h\""
            i=$((i + 1))
        done
        r=$((r + 1))
    done
    echo "int main(void) { return 0; }"
} > "$DIR/all.c"
