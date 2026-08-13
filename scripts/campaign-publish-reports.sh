#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

usage() {
    echo "usage: $0 REPORT-ROOT RUN-URL ARTIFACT-URL [DOWNLOADED-ARTIFACTS]" >&2
    exit 2
}

fail() {
    echo "campaign-publish-reports: $*" >&2
    exit 1
}

[ "$#" -eq 3 ] || [ "$#" -eq 4 ] || usage
reports=$1
run_url=$2
artifact_url=$3
downloaded=${4:-}
[ -d "$reports" ] || fail "report root is not a directory: $reports"
command -v gh >/dev/null 2>&1 || fail "gh is required"
[ -n "${GH_TOKEN:-}" ] || fail "GH_TOKEN is required"

root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
checker=$root/scripts/campaign-check.sh
reporter=$root/scripts/campaign-failure-report.sh
capture=$root/scripts/campaign-capture-result.sh
variant_lint=$root/scripts/campaign-variants-lint.sh
variants=${CGF_CAMPAIGN_NIGHTLY_VARIANTS:-$root/ci/campaigns/nightly-variants.tsv}
[ -r "$variants" ] || fail "nightly variant manifest is not readable: $variants"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-publish.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

"$variant_lint" "$variants" >"$tmp/variants"

if [ -n "$downloaded" ]; then
    [ -d "$downloaded" ] ||
        fail "downloaded artifact root is not a directory: $downloaded"
    cut -f 4 "$tmp/variants" | LC_ALL=C sort -u >"$tmp/allowed-artifacts"
    find "$downloaded" -mindepth 1 -maxdepth 1 -type d -print |
        sed 's,.*/,,' | LC_ALL=C sort >"$tmp/actual-artifacts"
    comm -23 "$tmp/actual-artifacts" "$tmp/allowed-artifacts" \
        >"$tmp/unexpected-artifacts"
    [ ! -s "$tmp/unexpected-artifacts" ] ||
        fail "downloaded artifact inventory contains an unexpected producer"
    if find "$downloaded" -mindepth 1 -maxdepth 1 ! -type d -print |
        grep . >"$tmp/unexpected-download-files"; then
        fail "downloaded artifact root contains an unexpected non-directory"
    fi
    while IFS= read -r artifact_name; do
        artifact=$downloaded/$artifact_name
        [ -e "$artifact" ] || continue
        [ -d "$artifact" ] && [ ! -L "$artifact" ] ||
            fail "downloaded artifact is unsafe: $artifact_name"
        awk -F '\t' -v artifact="$artifact_name" \
            '$4 == artifact { print $2 }' "$tmp/variants" |
            LC_ALL=C sort >"$tmp/$artifact_name.allowed"
        find "$artifact" -mindepth 1 -maxdepth 1 -type d -print |
            sed 's,.*/,,' | LC_ALL=C sort >"$tmp/$artifact_name.actual"
        comm -13 "$tmp/$artifact_name.allowed" "$tmp/$artifact_name.actual" \
            >"$tmp/$artifact_name.unowned"
        [ ! -s "$tmp/$artifact_name.unowned" ] ||
            fail "artifact $artifact_name contains a variant outside its ownership"
        if find "$artifact" -mindepth 1 -maxdepth 1 ! -type d -print |
            grep . >"$tmp/$artifact_name.unexpected-files"; then
            fail "artifact $artifact_name contains an unexpected non-directory"
        fi
        while IFS= read -r variant; do
            [ ! -e "$reports/$variant" ] ||
                fail "duplicate captured result for variant: $variant"
            mv "$artifact/$variant" "$reports/$variant"
        done <"$tmp/$artifact_name.actual"
    done <"$tmp/allowed-artifacts"
fi

# A cancelled runner may never reach its always() capture step. Materialize
# the same tested fail-closed result here so absence itself pages the ledger.
while IFS="$(printf '\t')" read -r project variant expected artifact; do
    if [ ! -r "$reports/$variant/metadata.txt" ] ||
       [ ! -r "$reports/$variant/results.txt" ]; then
        CGF_CAMPAIGN_REPORT_ROOT=$reports \
            "$capture" "$project" "$variant" "$expected" \
                "$reports/$variant/missing-results.txt" >/dev/null
    fi
done <"$tmp/variants"

find "$reports" -mindepth 1 -maxdepth 1 -type d -print |
    sed 's,.*/,,' | LC_ALL=C sort >"$tmp/actual-variants"
cut -f 2 "$tmp/variants" | LC_ALL=C sort >"$tmp/expected-variants"
cmp -s "$tmp/expected-variants" "$tmp/actual-variants" ||
    fail "captured result inventory differs from $variants"
value() {
    key=$1
    file=$2
    awk -F= -v key="$key" '
        $1 == key { count++; value = substr($0, length(key) + 2) }
        END { if (count != 1) exit 1; print value }
    ' "$file" || fail "missing or duplicate $key in $file"
}

published=0
matched=0
while IFS="$(printf '\t')" read -r manifest_project manifest_variant \
    manifest_expected _manifest_artifact; do
    directory=$reports/$manifest_variant
    metadata=$directory/metadata.txt
    actual=$directory/results.txt
    [ -d "$directory" ] && [ ! -L "$directory" ] ||
        fail "variant directory is missing or unsafe: $directory"
    [ -f "$metadata" ] && [ -r "$metadata" ] && [ ! -L "$metadata" ] ||
        fail "metadata is not a readable regular file: $metadata"
    [ -f "$actual" ] && [ -r "$actual" ] && [ ! -L "$actual" ] ||
        fail "result is not a readable regular file: $actual"
    find "$directory" -mindepth 1 -maxdepth 1 -print |
        sed 's,.*/,,' | LC_ALL=C sort >"$tmp/$manifest_variant.files"
    printf '%s\n' metadata.txt results.txt >"$tmp/variant-files.expected"
    cmp -s "$tmp/variant-files.expected" "$tmp/$manifest_variant.files" ||
        fail "variant directory contains an unexpected entry: $manifest_variant"
    project=$(value project "$metadata")
    variant=$(value variant "$metadata")
    expected=$(value expected "$metadata")
    [ "$project" = "$manifest_project" ] ||
        fail "artifact project disagrees with manifest for $manifest_variant"
    [ "$variant" = "$manifest_variant" ] ||
        fail "artifact variant disagrees with its manifest directory: $manifest_variant"
    [ "$expected" = "$manifest_expected" ] ||
        fail "artifact expected path disagrees with manifest for $manifest_variant"
    expected=$root/$manifest_expected
    [ -r "$expected" ] || fail "expected file is not readable: $expected"

    if "$checker" "$expected" "$actual" >/dev/null 2>&1; then
        matched=$((matched + 1))
        continue
    fi

    body=$tmp/$manifest_variant.report.md
    "$reporter" "$project" "$expected" "$actual" "$artifact_url" \
        "$run_url" >"$body"
    namespace=$(printf '%s' "$project" |
        tr 'abcdefghijklmnopqrstuvwxyz' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')
    title="[CAMP-$namespace] nightly drift ($variant)"
    search=$(printf '"%s" in:title' "$title")
    gh issue list --state open --search "$search" --limit 1000 \
        --json number,title --jq '.[] | [.number, .title] | @tsv' \
        >"$tmp/issues"
    awk -F '\t' -v title="$title" '
        $2 == title && $1 ~ /^[1-9][0-9]*$/ { print $1 }
    ' "$tmp/issues" >"$tmp/exact-issues"
    issue_count=$(wc -l <"$tmp/exact-issues" | tr -d ' ')
    case $issue_count in
        0) gh issue create --title "$title" --body-file "$body" ;;
        1)
            issue=$(sed -n '1p' "$tmp/exact-issues")
            gh issue edit "$issue" --body-file "$body"
            ;;
        *) fail "multiple open issues have the exact title: $title" ;;
    esac
    published=$((published + 1))
done <"$tmp/variants"

printf 'campaign-publish-reports: PASS captured=%s matched=%s published=%s\n' \
    "$((matched + published))" "$matched" "$published"
