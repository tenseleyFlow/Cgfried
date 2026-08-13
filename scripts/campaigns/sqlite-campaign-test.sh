#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
campaign_build=${CGF_CAMPAIGN_BUILD:-$root/build/campaigns}
mkdir -p "$campaign_build"
work=$(mktemp -d "$campaign_build/sqlite-meta.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail() {
    echo "sqlite-campaign-test: $*" >&2
    exit 1
}

grep -Fq 'runs=${CGF_CAMPAIGN_SQLITE_RUNS:-10}' \
    "$root/scripts/campaigns/sqlite.sh" || fail "production run count default is not 10"
grep -Fq 'warmup=${CGF_CAMPAIGN_SQLITE_WARMUP:-1}' \
    "$root/scripts/campaigns/sqlite.sh" || fail "production warmup default is not 1"
grep -Fq 'as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}' \
    "$root/scripts/campaigns/sqlite.sh" || fail "campaign does not route native assembler explicitly"
grep -Fq 'export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"' \
    "$root/scripts/campaigns/sqlite.sh" || fail "campaign does not export native binutils routing"
grep -Fq '"-$level" -Wno-attributes -Wno-mem' \
    "$root/scripts/campaigns/sqlite.sh" ||
    fail "campaign does not use the Sprint 52 SQLite scale profile"
grep -Fq 'compile_profile=sprint-52-sqlite-scale-v1' \
    "$root/scripts/campaigns/sqlite.sh" ||
    fail "campaign receipts omit the compile profile"

policy=$root/scripts/campaigns/sqlite-policy-check.sh
production_policy=$root/ci/campaigns/sqlite-baselines.conf
unmeasured_policy=$work/policy-unmeasured.conf
awk '
    /^(kasumi|hasu)\.(O0|O2)\.(wall_ms_median|maxrss_kb_max)=/ {
        sub(/=.*/, "=UNMEASURED")
    }
    { print }
' "$production_policy" >"$unmeasured_policy"
if command -v sha256sum >/dev/null 2>&1; then
    policy_sha=$(sha256sum "$production_policy" | awk '{print $1}')
else
    policy_sha=$(shasum -a 256 "$production_policy" | awk '{print $1}')
fi
policy_detail=$("$policy" "$production_policy")
printf '%s\n' "$policy_detail" | grep -Fq "policy-sha256=$policy_sha" ||
    fail "policy detail omitted the enforced config hash"
case $policy_detail in
*,absolute-o2-ms=60000,state=numeric) ;;
*) fail "production policy is not a numeric closure policy" ;;
esac
if "$policy" --require-numeric "$unmeasured_policy" \
    >"$work/policy-unmeasured.out" 2>"$work/policy-unmeasured.err"; then
    fail "closure policy accepted unmeasured designated hosts"
fi
grep -Fq 'controlled capture is required' "$work/policy-unmeasured.err" ||
    fail "unmeasured closure failure did not name the required capture"
sed 's/UNMEASURED/100/g' "$unmeasured_policy" \
    >"$work/policy-numeric.conf"
numeric_detail=$("$policy" --require-numeric "$work/policy-numeric.conf")
case $numeric_detail in
*,state=numeric) ;;
*) fail "numeric controlled-host policy did not reach closure state" ;;
esac
expected_result_detail=$(awk -F '\t' '
    $1 == "compile.baseline-policy" && $2 == "PASS" { print $3 }
' "$root/ci/campaigns/sqlite.expected")
numeric_result_detail=$("$policy" --result-detail "$work/policy-numeric.conf")
[ "$numeric_result_detail" = "$expected_result_detail" ] ||
    fail "numeric policy public result detail does not match sqlite.expected"
if "$policy" --result-detail "$unmeasured_policy" \
    >"$work/result-unmeasured.out" 2>"$work/result-unmeasured.err"; then
    fail "public policy result accepted unmeasured designated hosts"
fi
grep -Fq 'controlled capture is required' "$work/result-unmeasured.err" ||
    fail "public policy result did not enforce controlled numeric capture"
for key in wall_regression_pct rss_regression_pct o2_absolute_wall_ms; do
    sed "s/^$key=.*/$key=30:999/" \
        "$root/ci/campaigns/sqlite-baselines.conf" \
        >"$work/policy-malformed-$key.conf"
    if "$policy" "$work/policy-malformed-$key.conf" \
        >"$work/policy-malformed-$key.out" 2>&1; then
        fail "policy checker accepted colon-bearing $key"
    fi
    grep -Fq 'policy thresholds must be integers' \
        "$work/policy-malformed-$key.out" ||
        fail "malformed $key failure did not identify the threshold contract"
done
cp "$work/policy-numeric.conf" "$work/policy-partial.conf"
sed -i 's/^kasumi\.O0\.wall_ms_median=100$/kasumi.O0.wall_ms_median=UNMEASURED/' \
    "$work/policy-partial.conf"
if "$policy" "$work/policy-partial.conf" >"$work/policy-partial.out" \
    2>"$work/policy-partial.err"; then
    fail "policy checker accepted a partial host baseline"
fi
grep -Fq 'kasumi has a partial numeric baseline' "$work/policy-partial.err" ||
    fail "partial policy failure did not name the affected host"
grep -Fq "policy_detail=\$(\"\$policy_check\" \"\$baseline_config\")" \
    "$root/scripts/campaigns/sqlite.sh" ||
    fail "campaign result provenance is not derived from the policy checker"
grep -Fq 'policy_result_detail=$("$policy_check" --result-detail "$baseline_config")' \
    "$root/scripts/campaigns/sqlite.sh" ||
    fail "numeric campaign result is not derived from the stable policy schema"

cat >"$work/fake-timeit.sh" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$@" >"$SQLITE_TEST_ARGS"
while [ "$1" != -- ]; do
    if [ "$1" = -o ]; then
        shift
        raw=$1
    fi
    shift
done
: >"$raw"
i=1
while [ "$i" -le 10 ]; do
    printf 'sample=%s wall_ms=10 user_ms=6 sys_ms=4 maxrss_kb=100\n' \
        "$i" >>"$raw"
    i=$((i + 1))
done
printf 'wall_ms_median=10\nwall_ms_mad=0\nuser_ms_median=6\nsys_ms_median=4\nmaxrss_kb_max=100\n'
EOF
chmod +x "$work/fake-timeit.sh"
: >"$work/source.c"
SQLITE_TEST_ARGS=$work/args.txt
export SQLITE_TEST_ARGS
"$root/scripts/campaigns/sqlite-measure.sh" "$work/fake-timeit.sh" \
    10 1 300 "$work/raw.txt" "$work/receipt.txt" "$work/log.txt" -- \
    fake-compiler -O2 -c "$work/source.c"
expected_args=$(cat <<EOF
-n
10
-w
1
-t
300
-o
$work/raw.txt
--
fake-compiler
-O2
-c
$work/source.c
EOF
)
[ "$(cat "$work/args.txt")" = "$expected_args" ] ||
    fail "measurement helper changed the Sprint 52 invocation"
test "$(grep -c '^sample=' "$work/raw.txt")" -eq 10 ||
    fail "raw samples were not retained"

cat >"$work/bad-timeit.sh" <<'EOF'
#!/bin/sh
set -eu
while [ "$1" != -- ]; do
    if [ "$1" = -o ]; then shift; raw=$1; fi
    shift
done
printf 'sample=1 wall_ms=10 user_ms=6 sys_ms=4 maxrss_kb=100\n' >"$raw"
printf 'wall_ms_median=10\nwall_ms_mad=0\nuser_ms_median=6\nsys_ms_median=4\nmaxrss_kb_max=100\n'
EOF
chmod +x "$work/bad-timeit.sh"
if "$root/scripts/campaigns/sqlite-measure.sh" "$work/bad-timeit.sh" \
    10 1 300 "$work/bad.raw" "$work/bad.receipt" "$work/bad.log" -- \
    fake-compiler -O2 -c "$work/source.c" >"$work/bad.out" 2>&1; then
    fail "measurement helper accepted an incomplete raw sample set"
fi
grep -Fq 'timer raw samples are malformed or incomplete' "$work/bad.out" ||
    fail "incomplete raw sample failure did not name the evidence defect"

write_receipt() {
    file=$1
    wall=$2
    cat >"$file" <<EOF
wall_ms_median=$wall
wall_ms_mad=1
user_ms_median=10
sys_ms_median=2
maxrss_kb_max=100
EOF
}
write_receipt "$work/O0.txt" 100
write_receipt "$work/O2.txt" 60000
for key in wall_regression_pct rss_regression_pct o2_absolute_wall_ms; do
    malformed=$work/baseline-malformed-$key.conf
    sed "s/^$key=.*/$key=30:999/" \
        "$root/ci/campaigns/sqlite-baselines.conf" >"$malformed"
    if "$root/scripts/campaigns/sqlite-baseline-check.sh" \
        "$malformed" fixture-host "$work/O0.txt" "$work/O2.txt" \
        >"$work/baseline-malformed-$key.out" 2>&1; then
        fail "baseline checker accepted colon-bearing $key"
    fi
    grep -Fq 'thresholds must be integers' \
        "$work/baseline-malformed-$key.out" ||
        fail "baseline checker did not identify malformed $key"
done
"$root/scripts/campaigns/sqlite-baseline-check.sh" \
    "$root/ci/campaigns/sqlite-baselines.conf" fixture-host \
    "$work/O0.txt" "$work/O2.txt" >"$work/non-designated.out"
grep -Fq 'absolute-o2=passed relative-gate=not-applicable' \
    "$work/non-designated.out" || fail "non-designated result is not truthful"

write_receipt "$work/O2.txt" 60000.001
if "$root/scripts/campaigns/sqlite-baseline-check.sh" \
    "$root/ci/campaigns/sqlite-baselines.conf" fixture-host \
    "$work/O0.txt" "$work/O2.txt" >"$work/absolute.out" 2>&1; then
    fail "absolute O2 wall gate accepted a value above 60000ms"
fi
grep -Fq 'O2 wall_ms_median exceeds absolute gate' "$work/absolute.out" ||
    fail "absolute O2 failure did not name the gate"

write_receipt "$work/O2.txt" 60000
"$root/scripts/campaigns/sqlite-baseline-check.sh" --absolute-only \
    "$root/ci/campaigns/sqlite-baselines.conf" kasumi \
    "$work/O0.txt" "$work/O2.txt" >"$work/absolute-only.out"
grep -Fq 'host=kasumi absolute-o2=passed relative-gate=deferred-for-initial-controlled-capture' \
    "$work/absolute-only.out" || fail "absolute-only capture lost real host identity"

cp "$unmeasured_policy" "$work/baselines.conf"
sed -i 's/UNMEASURED/100/g' "$work/baselines.conf"
write_receipt "$work/O0.txt" 130
write_receipt "$work/O2.txt" 130
"$root/scripts/campaigns/sqlite-baseline-check.sh" "$work/baselines.conf" \
    kasumi "$work/O0.txt" "$work/O2.txt" >"$work/relative-boundary.out"
grep -Fq 'scope=designated levels=O0,O2' "$work/relative-boundary.out" ||
    fail "designated-host relative boundary did not pass"
write_receipt "$work/O2.txt" 130.001
if "$root/scripts/campaigns/sqlite-baseline-check.sh" "$work/baselines.conf" \
    kasumi "$work/O0.txt" "$work/O2.txt" >"$work/relative-fail.out" 2>&1; then
    fail "relative wall gate accepted a value above 30 percent"
fi
grep -Fq 'kasumi O2 wall_ms_median regressed' "$work/relative-fail.out" ||
    fail "relative wall failure did not name its host, level, and metric"

mkdir -p "$work/good/root" "$work/bad/root"
: >"$work/good/root/member"
: >"$work/bad/root/member"
: >"$work/bad/escape"
(cd "$work/good" && zip -q "$work/good.zip" root/member)
(cd "$work/bad" && zip -q "$work/bad.zip" root/member escape)
"$root/scripts/campaigns/sqlite-archive-check.sh" "$work/good.zip" root \
    >"$work/archive-good.out"
if "$root/scripts/campaigns/sqlite-archive-check.sh" "$work/bad.zip" root \
    >"$work/archive-bad.out" 2>&1; then
    fail "archive root checker accepted an out-of-root member"
fi
grep -Fq 'unsafe member: escape' "$work/archive-bad.out" ||
    fail "archive root failure did not identify the unsafe member"

mkdir -p "$work/symlink/root"
: >"$work/symlink/root/target"
ln -s target "$work/symlink/root/link"
(cd "$work/symlink" && zip -qry "$work/symlink.zip" root)
if "$root/scripts/campaigns/sqlite-archive-check.sh" \
    "$work/symlink.zip" root >"$work/archive-symlink.out" 2>&1; then
    fail "archive type checker accepted a symlink member"
fi
grep -Fq 'archive contains a link or special-file member' \
    "$work/archive-symlink.out" ||
    fail "archive type failure did not name the unsafe entry class"

printf 'sqlite-campaign-test: PASS\n'
