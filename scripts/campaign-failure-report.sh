#!/bin/sh
set -eu
export LC_ALL=C

usage() {
    echo "usage: $0 PROJECT EXPECTED ACTUAL ARTIFACT_URL RUN_URL" >&2
    exit 2
}

fail() {
    echo "campaign-failure-report: $*" >&2
    exit 1
}

[ "$#" -eq 5 ] || usage
project=$1
expected=$2
actual=$3
artifact_url=$4
run_url=$5

case $project in
    ''|*[!a-z0-9-]*) fail "invalid project: $project" ;;
esac

validate_url() {
    label=$1
    value=$2
    case $value in
        https://*) ;;
        *) fail "$label must be an https URL" ;;
    esac
    case $value in
        *[!abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._~:/?#@!\$\&\'\(\)\*+,\;=%-]*)
            fail "$label contains unsupported characters"
            ;;
    esac
}

validate_url artifact-url "$artifact_url"
validate_url run-url "$run_url"

root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
checker=$root/scripts/campaign-check.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-failure-report.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

if "$checker" "$expected" "$actual" >"$tmp/check.out" 2>"$tmp/check.err"; then
    fail 'expected result drift, but expected and actual match'
fi
if ! grep -F 'campaign-expected: result drift:' "$tmp/check.err" >/dev/null; then
    cat "$tmp/check.err" >&2
    fail 'cannot report malformed campaign results'
fi

sed '1,2d' "$expected" >"$tmp/expected.rows"
sed '1,2d' "$actual" >"$tmp/actual.rows"
comm -23 "$tmp/expected.rows" "$tmp/actual.rows" >"$tmp/missing"
comm -13 "$tmp/expected.rows" "$tmp/actual.rows" >"$tmp/unexpected"

identity=$(printf '%s' "$project" | tr 'abcdefghijklmnopqrstuvwxyz' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')

printf '# Campaign drift: %s (CAMP-%s)\n\n' "$project" "$identity"
printf -- '- Project: `%s`\n' "$project"
printf -- '- Finding namespace: `CAMP-%s`\n' "$identity"
printf -- '- Run: <%s>\n' "$run_url"
printf -- '- Artifacts: <%s>\n' "$artifact_url"
print_indented() {
    file=$1
    if [ -s "$file" ]; then
        sed 's/^/    /' "$file"
    else
        echo '    (none)'
    fi
}

printf '\n## Missing expected rows\n\n'
print_indented "$tmp/missing"
printf '\n## Unexpected actual rows\n\n'
print_indented "$tmp/unexpected"
printf '\n'
printf 'This diff is bidirectional: regressions and unrecorded improvements both fail the campaign ratchet.\n'
printf 'Classify the result under `CAMP-%s-NNN`; update the expected file only with matching finding and regression evidence.\n' "$identity"
