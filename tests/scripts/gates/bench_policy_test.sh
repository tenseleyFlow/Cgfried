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

echo "bench policy tests: PASS"
