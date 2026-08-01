#!/bin/sh
set -eu

CGF=${1:-build/cgfried}
GCC=${CGF_DIFF_GCC:-gcc}
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-s36-seeds.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

count_packed()
{
    awk '
        /^[[:space:]]*[a-z]/ {
            op = $1
            if (op ~ /^(padd|psub|pmullw|pand|por|pxor|addps|addpd|subps|subpd|mulps|mulpd|divps|divpd|movdqu|movups|movupd)$/)
                n++
        }
        END { print n + 0 }
    ' "$1"
}

printf 'kernel\tlevel\toutput\tcgf-packed\tgcc-packed\n'
for kernel in vector_add int_sum fp_dot matrix_inner; do
    level=-O3
    if [ "$kernel" = fp_dot ]; then
        level=-Ofast
    fi
    src=tests/perf/s36/$kernel.c
    CGF_AS=0 "$CGF" "$level" "$src" -o "$work/$kernel.cgf"
    "$GCC" "$level" "$src" -o "$work/$kernel.gcc"
    CGF_AS=0 "$CGF" "$level" -S "$src" -o "$work/$kernel.cgf.s"
    "$GCC" "$level" -S "$src" -o "$work/$kernel.gcc.s"
    cgf_out=$("$work/$kernel.cgf")
    gcc_out=$("$work/$kernel.gcc")
    if [ "$cgf_out" != "$gcc_out" ]; then
        echo "s36_vector_seeds: $kernel output mismatch" >&2
        exit 1
    fi
    printf '%s\t%s\t%s\t%s\t%s\n' "$kernel" "$level" "$cgf_out" \
        "$(count_packed "$work/$kernel.cgf.s")" \
        "$(count_packed "$work/$kernel.gcc.s")"
done
