#!/bin/sh
# Optional developer comparison.  This records verdicts; it does not make GCC
# an oracle for Cgfried's deliberately narrower zero-FP policy.
set -eu
LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

cgf=${1:-build/cgfried}
gcc=${CGF_FANALYZER_GCC:-gcc}
cases=${2:-tests/memsafe/fanalyzer}
work=${CGF_FANALYZER_WORK:-build/mem-fanalyzer}

if [ ! -x "$cgf" ]; then
    echo "fanalyzer_mem_compare: compiler not executable: $cgf" >&2
    exit 1
fi
if ! command -v "$gcc" >/dev/null 2>&1 || \
    ! "$gcc" -fanalyzer -x c -S -o /dev/null /dev/null >/dev/null 2>&1; then
    echo 'HARNESS_SKIP suite=mem-fanalyzer test=overlap count=20 reason="GCC -fanalyzer unavailable"'
    exit 0
fi

mkdir -p "$work"
sources="$work/sources.txt"
report="$work/comparison.tsv"
find "$cases" -maxdepth 1 -type f -name '*.c' -print | sort > "$sources"
count=$(wc -l < "$sources" | tr -d ' ')
if [ "$count" -ne 20 ]; then
    echo "fanalyzer_mem_compare: expected exactly 20 cases, found $count" >&2
    exit 1
fi

printf 'case\tcategory\tcgf-Wmem\tgcc-fanalyzer\n' > "$report"
while IFS= read -r source; do
    name=$(basename "$source" .c)
    category=$(sed -n 's|^// CATEGORY: ||p' "$source")
    if [ -z "$category" ]; then
        echo "fanalyzer_mem_compare: missing CATEGORY in $source" >&2
        exit 1
    fi
    case "$category" in
        use-after-free)
            cgf_flag='-Wmem-use-after-free'
            gcc_flag='-Wanalyzer-use-after-free'
            ;;
        double-free)
            cgf_flag='-Wmem-double-free'
            gcc_flag='-Wanalyzer-double-free'
            ;;
        leak)
            cgf_flag='-Wmem-leak'
            gcc_flag='-Wanalyzer-malloc-leak'
            ;;
        out-of-bounds)
            cgf_flag='-Wmem-out-of-bounds'
            gcc_flag='-Wanalyzer-out-of-bounds'
            ;;
        free-nonheap)
            cgf_flag='-Wmem-free-nonheap'
            gcc_flag='-Wanalyzer-free-of-non-heap'
            ;;
        *)
            echo "fanalyzer_mem_compare: unknown CATEGORY in $source: $category" >&2
            exit 1
            ;;
    esac
    if "$cgf" -Wmem -fsyntax-only "$source" >"$work/$name.cgf.out" \
        2>"$work/$name.cgf.err"; then
        :
    else
        status=$?
        echo "fanalyzer_mem_compare: Cgfried failed for $source (status $status)" >&2
        sed -n '1,120p' "$work/$name.cgf.err" >&2
        exit 1
    fi
    if "$gcc" -fanalyzer -S -o /dev/null "$source" \
        >"$work/$name.gcc.out" 2>"$work/$name.gcc.err"; then
        :
    else
        status=$?
        echo "fanalyzer_mem_compare: GCC failed for $source (status $status)" >&2
        sed -n '1,120p' "$work/$name.gcc.err" >&2
        exit 1
    fi
    if [ -s "$work/$name.cgf.out" ] || \
        grep -E ': (fatal )?error:|internal compiler error|(^|[^A-Za-z])ICE([^A-Za-z]|$)' \
            "$work/$name.cgf.err" >/dev/null || \
        grep -F '[-Wunknown-warning-option]' "$work/$name.cgf.err" >/dev/null; then
        echo "fanalyzer_mem_compare: invalid Cgfried analysis for $source" >&2
        sed -n '1,120p' "$work/$name.cgf.err" >&2
        exit 1
    fi
    if [ -s "$work/$name.gcc.out" ] || \
        grep -E ': (fatal )?error:|internal compiler error|(^|[^A-Za-z])ICE([^A-Za-z]|$)' \
            "$work/$name.gcc.err" >/dev/null; then
        echo "fanalyzer_mem_compare: invalid GCC analysis for $source" >&2
        sed -n '1,120p' "$work/$name.gcc.err" >&2
        exit 1
    fi
    cgf_verdict=no
    gcc_verdict=no
    if grep -F "[$cgf_flag]" "$work/$name.cgf.err" >/dev/null; then
        cgf_verdict=yes
    fi
    if grep -F "[$gcc_flag]" "$work/$name.gcc.err" >/dev/null; then
        gcc_verdict=yes
    fi
    printf '%s\t%s\t%s\t%s\n' "$name" "$category" \
        "$cgf_verdict" "$gcc_verdict" >> "$report"
done < "$sources"

rows=$(awk 'NR > 1 { n++ } END { print n + 0 }' "$report")
if [ "$rows" -ne 20 ]; then
    echo "fanalyzer_mem_compare: report has $rows cases, expected 20" >&2
    exit 1
fi

cat "$report"
echo "fanalyzer_mem_compare: recorded exactly 20 overlap verdicts in $report"
