#!/bin/sh
# XFAIL(audit): DET-M-01 blocking compile gates have no per-metric noise evidence
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=${1:-$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)}
GATE="$ROOT/scripts/benchmark_gate.sh"

[ -r "$GATE" ] || exit 2

# Establish that user+sys and max-RSS are active percentage gates.
grep -Fq 'check_limit(prefix "user+sys_ms_median"' "$GATE" || exit 2
grep -Fq 'check_limit(key, baseline[key], current[key], 20)' "$GATE" || exit 2

checked=0
missing=0
for baseline in \
    "$ROOT/.benchmarks/baseline-x86_64-linux-gnu.kasumi.txt" \
    "$ROOT/.benchmarks/baseline-x86_64-linux-gnu.hasu.txt" \
    "$ROOT/.benchmarks/baseline-arm64-macos.nomad-1.txt"
do
    [ -r "$baseline" ] || exit 2
    for lane in sqlite3 self many-tu; do
        for metric in user_ms sys_ms; do
            grep -Eq "^$lane[.]${metric}_median=[0-9]" "$baseline" || exit 2
            checked=$((checked + 1))
            if ! grep -Eq "^$lane[.]${metric}_mad=[0-9]" "$baseline"; then
                missing=$((missing + 1))
            fi
        done
        grep -Eq "^$lane[.]maxrss_kb_max=[0-9]" "$baseline" || exit 2
        checked=$((checked + 1))
        if ! grep -Eq "^$lane[.]maxrss_kb_(mad|median)=[0-9]" "$baseline"; then
            missing=$((missing + 1))
        fi
    done
done

[ "$checked" -eq 27 ] || exit 2
if [ "$missing" -eq 0 ]; then
    exit 1
fi

echo "DET-M-01 reproduced: $missing/27 gated user, sys, and RSS metric families lack a recorded noise statistic"
exit 0
