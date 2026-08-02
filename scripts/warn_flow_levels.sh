#!/bin/sh
# Sprint 40 invariant: the dedicated mem2reg+simplify-cfg warning pipeline is
# independent of the user's optimization level. Compare complete diagnostic
# streams, not just counts, across every flow fixture and supported level.
set -eu

LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

CGF=${1:?usage: warn_flow_levels.sh path/to/cgfried}
WORK=${CGF_FLOW_LEVEL_WORK:-build/flow-levels}
levels='-O0 -O1 -O2 -O3 -Os'

rm -rf "$WORK"
mkdir -p "$WORK"

files=0
runs=0
for file in $(find tests/warn/flow -type f -name '*.c' | sort); do
    files=$((files + 1))
    flags=$(sed -n 's@^// FLAGS: @@p' "$file" | sed -n '1p')
    base_err="$WORK/$files.base.err"
    base_status="$WORK/$files.base.status"
    first=1
    for level in $levels; do
        err="$WORK/$files.$level.err"
        if "$CGF" $flags "$level" "$file" >/dev/null 2>"$err"; then
            status=0
        else
            status=$?
        fi
        runs=$((runs + 1))
        if [ "$first" -eq 1 ]; then
            cp "$err" "$base_err"
            printf '%s\n' "$status" > "$base_status"
            first=0
            continue
        fi
        if [ "$status" -ne "$(sed -n '1p' "$base_status")" ] ||
            ! cmp -s "$base_err" "$err"; then
            echo "warn_flow_levels: level-dependent diagnostics: $file $level" >&2
            diff -u "$base_err" "$err" >&2 || true
            exit 1
        fi
    done
done

echo "warn_flow_levels: $files fixtures, $runs level-stable compilations"
