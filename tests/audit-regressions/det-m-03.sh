#!/bin/sh
# RESOLVED(audit): DET-M-03 four baseline-bump commits omit required old-to-new evidence
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT_INPUT=${1:-$SCRIPT_DIR/../..}
ROOT=$(CDPATH= cd -- "$ROOT_INPUT" && pwd) || exit 2
CHECKER=$ROOT/scripts/check_bench_policy.sh
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cgf-det-m-03.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
REPO=$TMP/repo

command -v git >/dev/null 2>&1 || exit 2
[ -x "$CHECKER" ] || exit 2
mkdir -p "$REPO/.benchmarks" || exit 2
git -C "$REPO" init -q || exit 2
git -C "$REPO" config user.name fixture || exit 2
git -C "$REPO" config user.email fixture@example.invalid || exit 2
git -C "$REPO" config commit.gpgsign false || exit 2

cat >"$REPO/.benchmarks/baseline-fixture.txt" <<'EOF'
self.wall_ms_median=100
self.maxrss_kb_max=1000
EOF
git -C "$REPO" add .benchmarks/baseline-fixture.txt || exit 2
git -C "$REPO" commit -q -m 'perf: publish fixture baseline' || exit 2
base=$(git -C "$REPO" rev-parse HEAD) || exit 2

run_policy()
{
    expected=$1
    commit=$2
    shift 2
    status=0
    env "$@" sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
        "$REPO" "$CHECKER" "$commit" >"$TMP/out" 2>"$TMP/err" || status=$?
    if [ "$status" -eq "$expected" ]; then
        return 0
    fi
    if [ "$status" -eq 0 ] || [ "$status" -eq 1 ]; then
        return 1
    fi
    return 2
}

sed -e 's/wall_ms_median=100/wall_ms_median=101/' \
    -e 's/maxrss_kb_max=1000/maxrss_kb_max=1100/' \
    "$REPO/.benchmarks/baseline-fixture.txt" >"$TMP/next" || exit 2
cp "$TMP/next" "$REPO/.benchmarks/baseline-fixture.txt" || exit 2
git -C "$REPO" add .benchmarks/baseline-fixture.txt || exit 2
git -C "$REPO" commit -q -m 'perf: incompletely explain replacement

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 100 -> 101; reason: controlled fixture refresh' || exit 2
incomplete=$(git -C "$REPO" rev-parse HEAD) || exit 2
run_policy 1 "$incomplete"
case $? in 0) ;; 1) echo 'DET-M-03 reproduced: incomplete evidence was accepted'; exit 0 ;; *) exit 2 ;; esac
grep -Fq 'missing evidence for changed metric self.maxrss_kb_max' "$TMP/err" || exit 2

sed -e 's/wall_ms_median=101/wall_ms_median=102/' \
    -e 's/maxrss_kb_max=1100/maxrss_kb_max=1200/' \
    "$REPO/.benchmarks/baseline-fixture.txt" >"$TMP/next" || exit 2
cp "$TMP/next" "$REPO/.benchmarks/baseline-fixture.txt" || exit 2
git -C "$REPO" add .benchmarks/baseline-fixture.txt || exit 2
git -C "$REPO" commit -q -m 'perf: fabricate replacement evidence

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 999 -> 1000; reason: deliberately false fixture values
bench-baseline: .benchmarks/baseline-fixture.txt self.maxrss_kb_max 999 -> 1000; reason: deliberately false fixture values' || exit 2
fabricated=$(git -C "$REPO" rev-parse HEAD) || exit 2
run_policy 1 "$fabricated"
case $? in 0) ;; 1) echo 'DET-M-03 reproduced: fabricated evidence was accepted'; exit 0 ;; *) exit 2 ;; esac
grep -Fq 'evidence does not match actual' "$TMP/err" || exit 2

sed -e 's/wall_ms_median=102/wall_ms_median=103/' \
    -e 's/maxrss_kb_max=1200/maxrss_kb_max=1300/' \
    "$REPO/.benchmarks/baseline-fixture.txt" >"$TMP/next" || exit 2
cp "$TMP/next" "$REPO/.benchmarks/baseline-fixture.txt" || exit 2
git -C "$REPO" add .benchmarks/baseline-fixture.txt || exit 2
git -C "$REPO" commit -q -m 'perf: explain complete replacement

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 102 -> 103; reason: controlled fixture refresh
bench-baseline: .benchmarks/baseline-fixture.txt self.maxrss_kb_max 1200 -> 1300; reason: controlled fixture refresh' || exit 2
complete=$(git -C "$REPO" rev-parse HEAD) || exit 2
run_policy 0 "$complete"
case $? in 0) ;; 1) echo 'DET-M-03 reproduced: complete exact evidence was rejected'; exit 0 ;; *) exit 2 ;; esac

# Range checking must expose either earlier violation even though the tip is
# locally compliant.
run_policy 1 "$complete" GITHUB_EVENT_BASE="$base" GITHUB_EVENT_COMMIT="$complete"
case $? in 0) ;; 1) echo 'DET-M-03 reproduced: a compliant tip hid an earlier violation'; exit 0 ;; *) exit 2 ;; esac

exit 1
