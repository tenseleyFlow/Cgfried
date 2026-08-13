#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
capture=$root/scripts/campaign-capture-result.sh
publish=$root/scripts/campaign-publish-reports.sh
variant_lint=$root/scripts/campaign-variants-lint.sh
workflow=$root/.github/workflows/torture-nightly.yml
production_variants=$root/ci/campaigns/nightly-variants.tsv
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-reporting-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

expect_publish_failure() {
    label=$1
    pattern=$2
    reports=$3
    mode=${4:-normal}
    downloaded=${5:-}
    set -- "$publish" "$reports" 'https://ci.example/runs/7' \
        'https://ci.example/runs/7#artifacts'
    if [ -n "$downloaded" ]; then
        set -- "$@" "$downloaded"
    fi
    if CGF_CAMPAIGN_NIGHTLY_VARIANTS=$tmp/variants.tsv \
       CGF_FAKE_GH_MODE=$mode CGF_FAKE_GH_LOG=$tmp/$label.gh.log \
       GH_TOKEN=test-token PATH=$tmp/bin:$PATH \
       "$@" \
           >"$tmp/$label.out" 2>"$tmp/$label.err"; then
        echo "campaign-reporting-meta: $label unexpectedly passed" >&2
        exit 1
    fi
    grep -F "$pattern" "$tmp/$label.err" >/dev/null || {
        cat "$tmp/$label.err" >&2
        exit 1
    }
}

fake_project=zlib
expected=ci/campaigns/zlib.expected
cp "$root/$expected" "$tmp/match.txt"
sed 's/\tPASS\t/\tFAIL\t/' "$root/$expected" >"$tmp/drift.txt"

CGF_CAMPAIGN_REPORT_ROOT=$tmp/reports \
    "$capture" "$fake_project" match-x86_64 "$expected" "$tmp/match.txt" \
    >"$tmp/capture-match.log"
CGF_CAMPAIGN_REPORT_ROOT=$tmp/reports \
    "$capture" "$fake_project" drift-arm64 "$expected" "$tmp/drift.txt" \
    >"$tmp/capture-drift.log"
CGF_CAMPAIGN_REPORT_ROOT=$tmp/reports \
    "$capture" "$fake_project" missing-musl "$expected" "$tmp/missing.txt" \
    >"$tmp/capture-missing.log"

mkdir -p "$tmp/symlink-reports/symlink-x86_64"
printf 'sentinel\n' >"$tmp/sentinel"
ln -s "$tmp/sentinel" "$tmp/symlink-reports/symlink-x86_64/results.txt"
if CGF_CAMPAIGN_REPORT_ROOT=$tmp/symlink-reports \
    "$capture" "$fake_project" symlink-x86_64 "$expected" "$tmp/match.txt" \
    >"$tmp/capture-symlink.out" 2>"$tmp/capture-symlink.err"; then
    echo 'campaign-reporting-meta: symlink destination unexpectedly passed' >&2
    exit 1
fi
grep -F 'result destination must not be a symlink' \
    "$tmp/capture-symlink.err" >/dev/null
test "$(cat "$tmp/sentinel")" = sentinel

reports=$tmp/reports
test "$(sed -n 's/^state=//p' "$reports/match-x86_64/metadata.txt")" = match
test "$(sed -n 's/^state=//p' "$reports/drift-arm64/metadata.txt")" = drift
test "$(sed -n 's/^source=//p' "$reports/missing-musl/metadata.txt")" = synthetic

mkdir -p "$tmp/bin" "$tmp/publish"
cp -R "$reports/match-x86_64" "$tmp/publish/"
cp -R "$reports/drift-arm64" "$tmp/publish/"
cat >"$tmp/bin/gh" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$CGF_FAKE_GH_LOG"
case $1:$2 in
    issue:list)
        case ${CGF_FAKE_GH_MODE:-normal}:$* in
            duplicate:*drift-arm64*)
                printf '17\t[CAMP-ZLIB] nightly drift (drift-arm64)\n'
                printf '18\t[CAMP-ZLIB] nightly drift (drift-arm64)\n'
                ;;
            normal:*drift-arm64*)
                printf '17\t[CAMP-ZLIB] nightly drift (drift-arm64)\n'
                ;;
        esac
        ;;
    issue:edit | issue:create) ;;
    *) exit 1 ;;
esac
EOF
chmod +x "$tmp/bin/gh"
{
    printf '# project\tvariant\texpected\tartifact\n'
    printf 'zlib\tdrift-arm64\tci/campaigns/zlib.expected\tcampaign-results-drift-arm64\n'
    printf 'zlib\tmatch-x86_64\tci/campaigns/zlib.expected\tcampaign-results-match-x86_64\n'
    printf 'zlib\tmissing-musl\tci/campaigns/zlib.expected\tcampaign-results-missing-musl\n'
} >"$tmp/variants.tsv"
CGF_CAMPAIGN_NIGHTLY_VARIANTS=$tmp/variants.tsv \
CGF_FAKE_GH_LOG=$tmp/gh.log GH_TOKEN=test-token PATH=$tmp/bin:$PATH \
    "$publish" "$tmp/publish" 'https://ci.example/runs/7' \
        'https://ci.example/runs/7#artifacts' >"$tmp/publish.log"

grep -F 'issue edit 17 --body-file' "$tmp/gh.log" >/dev/null
grep -F 'issue create --title [CAMP-ZLIB] nightly drift (missing-musl)' \
    "$tmp/gh.log" >/dev/null
test "$(grep -c '^issue list ' "$tmp/gh.log")" -eq 2
grep -F -- '--search "[CAMP-ZLIB] nightly drift (drift-arm64)" in:title --limit 1000' \
    "$tmp/gh.log" >/dev/null
grep -F 'captured=3 matched=1 published=2' "$tmp/publish.log" >/dev/null
test "$(sed -n 's/^source=//p' \
    "$tmp/publish/missing-musl/metadata.txt")" = synthetic

cp -R "$tmp/publish" "$tmp/tampered"
sed 's/^project=zlib$/project=curl/' \
    "$tmp/tampered/drift-arm64/metadata.txt" >"$tmp/metadata.tmp"
mv "$tmp/metadata.tmp" "$tmp/tampered/drift-arm64/metadata.txt"
expect_publish_failure metadata-tamper \
    'artifact project disagrees with manifest for drift-arm64' "$tmp/tampered"

cp -R "$tmp/publish" "$tmp/extra-inventory"
mkdir "$tmp/extra-inventory/unexpected-variant"
expect_publish_failure extra-inventory \
    'captured result inventory differs' "$tmp/extra-inventory"

cp -R "$tmp/publish" "$tmp/extra-variant-entry"
printf 'untrusted\n' >"$tmp/extra-variant-entry/drift-arm64/report.md"
expect_publish_failure extra-variant-entry \
    'variant directory contains an unexpected entry: drift-arm64' \
    "$tmp/extra-variant-entry"

cp -R "$tmp/publish" "$tmp/duplicate-issue"
expect_publish_failure duplicate-issue \
    'multiple open issues have the exact title' "$tmp/duplicate-issue" duplicate

grep -F 'group: cgfried-campaign-ledger-${{ github.repository_id }}' \
    "$workflow" >/dev/null
grep -F 'queue: max' "$workflow" >/dev/null
grep -F 'cancel-in-progress: false' "$workflow" >/dev/null
grep -F 'mkdir -p build/nightly-campaign-artifacts' "$workflow" >/dev/null

"$variant_lint" --production "$workflow" "$production_variants" \
    >"$tmp/production-variants"
test "$(wc -l <"$tmp/production-variants" | tr -d ' ')" -eq 15
sed 's/campaign-results-tinycc-x86_64/campaign-results-tinycc-bad/' \
    "$workflow" >"$tmp/bad-workflow.yml"
if "$variant_lint" --production "$tmp/bad-workflow.yml" \
    "$production_variants" >"$tmp/bad-workflow.out" \
    2>"$tmp/bad-workflow.err"; then
    echo 'campaign-reporting-meta: bad production workflow unexpectedly passed' >&2
    exit 1
fi
grep -F 'workflow campaign result producers do not match the manifest' \
    "$tmp/bad-workflow.err" >/dev/null
sed \
    -e 's#path: build/nightly-campaign-artifacts/campaign-results-qbe-x86_64#path: CAMPAIGN_SWAP#' \
    -e 's#path: build/nightly-campaign-artifacts/campaign-results-qbe-arm64#path: build/nightly-campaign-artifacts/campaign-results-qbe-x86_64#' \
    -e 's#path: CAMPAIGN_SWAP#path: build/nightly-campaign-artifacts/campaign-results-qbe-arm64#' \
    "$workflow" >"$tmp/bad-download-pairs.yml"
awk '
    $0 == "          name: campaign-results-qbe-x86_64" { want_x86 = 1; next }
    $0 == "          name: campaign-results-qbe-arm64" { want_arm = 1; next }
    want_x86 && $0 == "          path: build/nightly-campaign-artifacts/campaign-results-qbe-arm64" {
        saw_x86_swap = 1
        want_x86 = 0
    }
    want_arm && $0 == "          path: build/nightly-campaign-artifacts/campaign-results-qbe-x86_64" {
        saw_arm_swap = 1
        want_arm = 0
    }
    END { exit !(saw_x86_swap && saw_arm_swap) }
' "$tmp/bad-download-pairs.yml" || {
    echo 'campaign-reporting-meta: failed to construct a true download destination swap' >&2
    exit 1
}
if "$variant_lint" --production "$tmp/bad-download-pairs.yml" \
    "$production_variants" >"$tmp/bad-download-pairs.out" \
    2>"$tmp/bad-download-pairs.err"; then
    echo 'campaign-reporting-meta: swapped download destinations unexpectedly passed' >&2
    exit 1
fi
grep -F 'workflow campaign result download pairs do not match the manifest' \
    "$tmp/bad-download-pairs.err" >/dev/null
sed '/^zlib[[:space:]]zlib-arm64[[:space:]]/d' "$production_variants" \
    >"$tmp/bad-production-variants.tsv"
if "$variant_lint" --production "$workflow" \
    "$tmp/bad-production-variants.tsv" >"$tmp/bad-production.out" \
    2>"$tmp/bad-production.err"; then
    echo 'campaign-reporting-meta: incomplete production manifest unexpectedly passed' >&2
    exit 1
fi
grep -F 'production manifest must contain the exact 15-variant topology' \
    "$tmp/bad-production.err" >/dev/null

awk '
    !changed && $0 == "            arch: arm64" {
        print "            arch: qbe-arm64-removed"
        changed = 1
        next
    }
    { print }
' "$workflow" >"$tmp/bad-qbe-workflow.yml"
if "$variant_lint" --production "$tmp/bad-qbe-workflow.yml" \
    "$production_variants" >"$tmp/bad-qbe.out" 2>"$tmp/bad-qbe.err"; then
    echo 'campaign-reporting-meta: incomplete QBE matrix unexpectedly passed' >&2
    exit 1
fi
grep -F 'workflow QBE matrix must contain exact x86-64 and ARM64 lanes' \
    "$tmp/bad-qbe.err" >/dev/null

mkdir -p "$tmp/zero-publish" "$tmp/zero-artifacts"
CGF_CAMPAIGN_NIGHTLY_VARIANTS=$tmp/variants.tsv \
CGF_FAKE_GH_LOG=$tmp/zero.gh.log GH_TOKEN=test-token PATH=$tmp/bin:$PATH \
    "$publish" "$tmp/zero-publish" 'https://ci.example/runs/8' \
        'https://ci.example/runs/8#artifacts' "$tmp/zero-artifacts" \
        >"$tmp/zero.log"
grep -F 'captured=3 matched=0 published=3' "$tmp/zero.log" >/dev/null
test "$(find "$tmp/zero-publish" -name metadata.txt -exec \
    grep -l '^source=synthetic$' {} \; | wc -l | tr -d ' ')" -eq 3

mkdir -p "$tmp/one-publish" \
    "$tmp/one-artifacts/campaign-results-match-x86_64/match-x86_64"
cp "$reports/match-x86_64/"* \
    "$tmp/one-artifacts/campaign-results-match-x86_64/match-x86_64/"
CGF_CAMPAIGN_NIGHTLY_VARIANTS=$tmp/variants.tsv \
CGF_FAKE_GH_LOG=$tmp/one.gh.log GH_TOKEN=test-token PATH=$tmp/bin:$PATH \
    "$publish" "$tmp/one-publish" 'https://ci.example/runs/9' \
        'https://ci.example/runs/9#artifacts' "$tmp/one-artifacts" \
        >"$tmp/one.log"
grep -F 'captured=3 matched=1 published=2' "$tmp/one.log" >/dev/null

{
    printf '# project\tvariant\texpected\tartifact\n'
    printf 'zlib\tdrift-arm64\tci/campaigns/zlib.expected\tcampaign-results-combined\n'
    printf 'zlib\tmatch-x86_64\tci/campaigns/zlib.expected\tcampaign-results-combined\n'
    printf 'zlib\tmissing-musl\tci/campaigns/zlib.expected\tcampaign-results-combined\n'
} >"$tmp/combined-variants.tsv"
mkdir -p "$tmp/partial-publish" \
    "$tmp/partial-artifacts/campaign-results-combined/match-x86_64"
cp "$reports/match-x86_64/"* \
    "$tmp/partial-artifacts/campaign-results-combined/match-x86_64/"
CGF_CAMPAIGN_NIGHTLY_VARIANTS=$tmp/combined-variants.tsv \
CGF_FAKE_GH_LOG=$tmp/partial.gh.log GH_TOKEN=test-token PATH=$tmp/bin:$PATH \
    "$publish" "$tmp/partial-publish" 'https://ci.example/runs/10' \
        'https://ci.example/runs/10#artifacts' "$tmp/partial-artifacts" \
        >"$tmp/partial.log"
grep -F 'captured=3 matched=1 published=2' "$tmp/partial.log" >/dev/null

mkdir -p "$tmp/artifact-publish" \
    "$tmp/artifacts/campaign-results-drift-arm64/drift-arm64" \
    "$tmp/artifacts/campaign-results-match-x86_64/match-x86_64"
cp "$reports/drift-arm64/"* \
    "$tmp/artifacts/campaign-results-drift-arm64/drift-arm64/"
cp "$reports/match-x86_64/"* \
    "$tmp/artifacts/campaign-results-match-x86_64/match-x86_64/"
CGF_CAMPAIGN_NIGHTLY_VARIANTS=$tmp/variants.tsv \
CGF_FAKE_GH_LOG=$tmp/artifacts.gh.log GH_TOKEN=test-token PATH=$tmp/bin:$PATH \
    "$publish" "$tmp/artifact-publish" 'https://ci.example/runs/8' \
        'https://ci.example/runs/8#artifacts' "$tmp/artifacts" \
        >"$tmp/artifacts.log"
grep -F 'captured=3 matched=1 published=2' "$tmp/artifacts.log" >/dev/null

mkdir -p "$tmp/spoof-publish" \
    "$tmp/spoof-artifacts/campaign-results-drift-arm64/drift-arm64" \
    "$tmp/spoof-artifacts/campaign-results-drift-arm64/match-x86_64"
expect_publish_failure artifact-spoof \
    'artifact campaign-results-drift-arm64 contains a variant outside its ownership' \
    "$tmp/spoof-publish" normal "$tmp/spoof-artifacts"

printf 'campaign-reporting-meta: PASS\n'
