#!/bin/sh
# shellcheck disable=SC2016

set -eu

# GitHub Actions exports this path to every step. Fixture calls exercise the
# checker's stdout contract unless a test supplies --summary explicitly.
unset GITHUB_STEP_SUMMARY

script_dir=$(dirname -- "$0")
repo=$(cd "$script_dir/../../.." && pwd)
checker=$repo/scripts/check_bench_policy.sh
fixtures=$repo/tests/scripts/gates/fixtures/policy
work=${TMPDIR:-/tmp}/cgf-bench-policy-test.$$
mkdir -p "$work"
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail()
{
    echo "bench_policy_test: $*" >&2
    exit 1
}

run_status()
{
    expected=$1
    shift
    set +e
    "$@" >"$work/out" 2>"$work/err"
    actual=$?
    set -e
    test "$actual" -eq "$expected" || {
        cat "$work/out" "$work/err" >&2
        fail "expected exit $expected, got $actual: $*"
    }
}

run_status 0 "$checker" \
    --message-file "$fixtures/message-skip.txt" \
    --diff-file "$fixtures/diff-docs.txt" --commit fixture-docs
grep -q 'bench skip: accepted' "$work/out" || fail "accepted skip missing from report"

run_status 1 "$checker" \
    --message-file "$fixtures/message-skip.txt" \
    --diff-file "$fixtures/diff-src.txt"
grep -q "diff touches 'src/opt.c'" "$work/err" || fail "lying skip diagnostic missing"

run_status 1 "$checker" \
    --message-file "$fixtures/message-skip.txt" \
    --diff-file "$fixtures/diff-src-markdown.txt"
grep -q "diff touches 'src/README.md'" "$work/err" || fail "nested markdown bypassed policy"

run_status 1 "$checker" \
    --message-file "$fixtures/message-skip.txt" \
    --diff-file "$fixtures/diff-build.txt"
grep -q "diff touches 'Makefile'" "$work/err" || fail "build-file skip diagnostic missing"

run_status 0 "$checker" \
    --message-file "$fixtures/message-override.txt" \
    --diff-file "$fixtures/diff-src.txt" --summary "$work/summary" --commit abc123
grep -q 'perf override: #417' "$work/summary" || fail "override audit record missing"
expected_commit='commit: `abc123`'
grep -Fq "$expected_commit" "$work/summary" || fail "override commit id missing"

cat >"$work/fake-gh" <<'EOF'
#!/bin/sh
test "$1" = issue && test "$2" = view || exit 2
case $3 in
417) echo OPEN ;;
418) echo CLOSED ;;
*) exit 1 ;;
esac
EOF
chmod +x "$work/fake-gh"
run_status 0 env CGF_PERF_VERIFY_ISSUES=1 \
    CGF_PERF_GH_CMD="$work/fake-gh" GITHUB_REPOSITORY=fixture/project \
    "$checker" --message-file "$fixtures/message-override.txt" \
    --diff-file "$fixtures/diff-src.txt"
printf 'perf-override: #418\n' >"$work/closed-override.txt"
run_status 1 env CGF_PERF_VERIFY_ISSUES=1 \
    CGF_PERF_GH_CMD="$work/fake-gh" GITHUB_REPOSITORY=fixture/project \
    "$checker" --message-file "$work/closed-override.txt" \
    --diff-file "$fixtures/diff-src.txt"
grep -Fq 'must name an open tracked issue' "$work/err" || \
    fail "closed override issue was not rejected"

run_status 3 "$checker" \
    --message-file "$fixtures/message-override-malformed.txt" \
    --diff-file "$fixtures/diff-src.txt"
grep -q 'malformed perf-override token' "$work/err" || fail "malformed override diagnostic missing"

run_status 3 "$checker" \
    --message-file "$fixtures/message-override-zero.txt" \
    --diff-file "$fixtures/diff-src.txt"

run_status 3 "$checker" \
    --message-file "$fixtures/missing-message.txt" \
    --diff-file "$fixtures/diff-src.txt"

# DET-M-03: baseline replacements carry local, numeric old-to-new evidence.
printf '%s\n' '.benchmarks/baseline-x86_64-linux-gnu.kasumi.txt' \
    >"$work/diff-baseline.txt"
printf '%s\n' 'perf: refresh controlled baseline' >"$work/message-baseline-missing.txt"
run_status 1 "$checker" --message-file "$work/message-baseline-missing.txt" \
    --diff-file "$work/diff-baseline.txt"
grep -Fq "requires 'bench-baseline:" "$work/err" || \
    fail "missing baseline evidence diagnostic was absent"

cat >"$work/message-baseline-valid.txt" <<'EOF'
perf: refresh controlled baseline

bench-baseline: .benchmarks/baseline-x86_64-linux-gnu.kasumi.txt self.wall_ms_median 100 -> 101; reason: controlled corpus grew by one translation unit
EOF
run_status 0 "$checker" --message-file "$work/message-baseline-valid.txt" \
    --diff-file "$work/diff-baseline.txt"

sed 's/100 -> 101/old -> new/' "$work/message-baseline-valid.txt" \
    >"$work/message-baseline-nonnumeric.txt"
run_status 1 "$checker" --message-file "$work/message-baseline-nonnumeric.txt" \
    --diff-file "$work/diff-baseline.txt"

cat >"$work/diff-two-baselines.txt" <<'EOF'
.benchmarks/baseline-x86_64-linux-gnu.kasumi.txt
.benchmarks/baseline-x86_64-linux-gnu.hasu.txt
EOF
run_status 1 "$checker" --message-file "$work/message-baseline-valid.txt" \
    --diff-file "$work/diff-two-baselines.txt"
grep -Fq "baseline '.benchmarks/baseline-x86_64-linux-gnu.hasu.txt'" \
    "$work/err" || fail "per-baseline evidence was not enforced"

run_status 0 "$checker" --audit --log-file "$fixtures/audit.tsv"
expected_skip='`a1b2c3` docs: refresh gate prose [bench skip]'
grep -Fq "$expected_skip" "$work/out" || \
    fail "monthly skip entry missing"
expected_override='`d4e5f6` perf: accept regression perf-override: #417'
grep -Fq "$expected_override" "$work/out" || \
    fail "monthly override entry missing"

run_status 3 "$checker" --audit --log-file "$fixtures/audit-malformed.tsv"

# A pull request is the complete base..head range, not only its tip commit.
# A docs-only [bench skip] tip must not hide an earlier source change.
range_repo=$work/range-repo
mkdir -p "$range_repo"
git -C "$range_repo" init -q
git -C "$range_repo" config user.name fixture
git -C "$range_repo" config user.email fixture@example.invalid
# The fixture commits exist only to exercise base..head range handling. Keep
# them independent of a developer's global commit-signing configuration.
git -C "$range_repo" config commit.gpgsign false
mkdir -p "$range_repo/doc" "$range_repo/src"
printf 'base\n' >"$range_repo/doc/note.md"
git -C "$range_repo" add doc/note.md
git -C "$range_repo" commit -q -m 'fixture base'
range_base=$(git -C "$range_repo" rev-parse HEAD)
printf 'code\n' >"$range_repo/src/opt.c"
git -C "$range_repo" add src/opt.c
git -C "$range_repo" commit -q -m 'fixture source change'
printf 'docs\n' >>"$range_repo/doc/note.md"
git -C "$range_repo" add doc/note.md
git -C "$range_repo" commit -q -m 'docs: tip only [bench skip]'
range_head=$(git -C "$range_repo" rev-parse HEAD)
run_status 1 env GITHUB_EVENT_BASE="$range_base" \
    GITHUB_EVENT_COMMIT="$range_head" \
    sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$range_head"
grep -q "diff touches 'src/opt.c'" "$work/err" || \
    fail "multi-commit pull-request range bypassed policy"

# Initial baseline publication has no old value; only later replacements are
# subject to DET-M-03 evidence.
mkdir -p "$range_repo/.benchmarks"
cat >"$range_repo/.benchmarks/baseline-fixture.txt" <<'EOF'
self.wall_ms_median=100
self.maxrss_kb_max=1000
EOF
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: publish initial fixture baseline'
initial_baseline=$(git -C "$range_repo" rev-parse HEAD)
run_status 0 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$initial_baseline"

sed 's/self.wall_ms_median=100/self.wall_ms_median=101/' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: unexplained fixture replacement'
unexplained_baseline=$(git -C "$range_repo" rev-parse HEAD)
run_status 1 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$unexplained_baseline"

sed 's/self.wall_ms_median=101/self.wall_ms_median=102/' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: explain fixture replacement

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 101 -> 102; reason: controlled fixture refresh'
explained_baseline=$(git -C "$range_repo" rev-parse HEAD)
run_status 0 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$explained_baseline"

sed 's/self.wall_ms_median=102/self.wall_ms_median=103/' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: fabricate fixture evidence

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 999 -> 1000; reason: deliberately wrong fixture values'
fabricated_baseline=$(git -C "$range_repo" rev-parse HEAD)
run_status 1 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$fabricated_baseline"
grep -Fq 'evidence does not match actual' "$work/err" ||
    fail "fabricated baseline evidence was not rejected"

# The earlier unexplained 100 -> 101 replacement remains a violation when a
# later, independently compliant replacement is the range tip.
run_status 1 env GITHUB_EVENT_BASE="$initial_baseline" \
    GITHUB_EVENT_COMMIT="$explained_baseline" \
    sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$explained_baseline"
grep -Fq "baseline '.benchmarks/baseline-fixture.txt' requires" "$work/err" ||
    fail "compliant range tip hid an earlier unexplained replacement"

sed -e 's/self.wall_ms_median=103/self.wall_ms_median=104/' \
    -e 's/self.maxrss_kb_max=1000/self.maxrss_kb_max=1100/' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: incompletely explain two metrics

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 103 -> 104; reason: controlled fixture refresh'
partial_two_metric=$(git -C "$range_repo" rev-parse HEAD)
run_status 1 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$partial_two_metric"
grep -Fq 'missing evidence for changed metric self.maxrss_kb_max' "$work/err" ||
    fail "wall-only evidence hid a changed RSS metric"

sed -e 's/self.wall_ms_median=104/self.wall_ms_median=105/' \
    -e 's/self.maxrss_kb_max=1100/self.maxrss_kb_max=1200/' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: explain every changed metric

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 104 -> 105; reason: controlled fixture refresh
bench-baseline: .benchmarks/baseline-fixture.txt self.maxrss_kb_max 1100 -> 1200; reason: controlled fixture refresh'
full_two_metric=$(git -C "$range_repo" rev-parse HEAD)
run_status 0 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$full_two_metric"

sed 's/self.wall_ms_median=105/self.wall_ms_median=106/' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
printf '%s\n' 'self.cpu_ms_median=90' >>"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: incompletely explain metric addition

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 105 -> 106; reason: controlled fixture refresh'
partial_add=$(git -C "$range_repo" rev-parse HEAD)
run_status 1 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$partial_add"
grep -Fq 'missing evidence for changed metric self.cpu_ms_median' "$work/err" ||
    fail "wall evidence hid an added CPU metric"

sed 's/self.wall_ms_median=106/self.wall_ms_median=107/' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
printf '%s\n' 'self.cpu_ms_mad=1' >>"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: explain metric addition

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 106 -> 107; reason: controlled fixture refresh
bench-baseline: .benchmarks/baseline-fixture.txt self.cpu_ms_mad absent -> 1; reason: begin recording paired CPU dispersion'
documented_add=$(git -C "$range_repo" rev-parse HEAD)
run_status 0 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$documented_add"

sed -e 's/self.wall_ms_median=107/self.wall_ms_median=108/' \
    -e '/^self.maxrss_kb_max=/d' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: incompletely explain metric deletion

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 107 -> 108; reason: controlled fixture refresh'
partial_delete=$(git -C "$range_repo" rev-parse HEAD)
run_status 1 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$partial_delete"
grep -Fq 'missing evidence for changed metric self.maxrss_kb_max' "$work/err" ||
    fail "wall evidence hid a deleted RSS metric"

sed -e 's/self.wall_ms_median=108/self.wall_ms_median=109/' \
    -e '/^self.cpu_ms_median=/d' \
    "$range_repo/.benchmarks/baseline-fixture.txt" >"$work/next-baseline"
cp "$work/next-baseline" "$range_repo/.benchmarks/baseline-fixture.txt"
git -C "$range_repo" add .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: explain metric deletion

bench-baseline: .benchmarks/baseline-fixture.txt self.wall_ms_median 108 -> 109; reason: controlled fixture refresh
bench-baseline: .benchmarks/baseline-fixture.txt self.cpu_ms_median 90 -> absent; reason: retire obsolete CPU aggregate'
documented_delete=$(git -C "$range_repo" rev-parse HEAD)
run_status 0 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$documented_delete"

# Deleting a baseline is explicit in Git history and is not a replacement.
git -C "$range_repo" rm -q .benchmarks/baseline-fixture.txt
git -C "$range_repo" commit -q -m 'perf: remove obsolete fixture baseline'
deleted_baseline=$(git -C "$range_repo" rev-parse HEAD)
run_status 0 sh -c 'cd "$1" && exec "$2" --commit "$3"' sh \
    "$range_repo" "$checker" "$deleted_baseline"

echo "bench policy tests: PASS"
