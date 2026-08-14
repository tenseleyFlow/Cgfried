#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
ledger=$root/scripts/campaigns/curl-probe-ledger.sh
runner=$root/scripts/campaigns/curl.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-curl-meta.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail() {
    echo "campaign-curl-meta: $*" >&2
    exit 1
}

cat >"$tmp/host-only.txt" <<'EOF'
ac_cv_header_stdatomic_h=yes
curl_cv_def___GNUC__=13
curl_cv_have_def___GNUC__=yes
EOF
cat >"$tmp/cgf-only.txt" <<'EOF'
ac_cv_header_stdatomic_h=no
curl_cv_have_def___GNUC__=no
EOF
"$ledger" "$tmp/host-only.txt" "$tmp/cgf-only.txt" "$tmp/actual.txt"
cat >"$tmp/expected.txt" <<'EOF'
CAMP-CURL-001	cgfried-only	ac_cv_header_stdatomic_h=no
CAMP-CURL-001	host-gcc-only	ac_cv_header_stdatomic_h=yes
CAMP-CURL-002	cgfried-only	curl_cv_have_def___GNUC__=no
CAMP-CURL-002	host-gcc-only	curl_cv_def___GNUC__=defined
CAMP-CURL-002	host-gcc-only	curl_cv_have_def___GNUC__=yes
EOF
cmp -s "$tmp/expected.txt" "$tmp/actual.txt" ||
    fail "canonical five-row configure deviation ledger changed"
actual_sha=$(sha256sum "$tmp/actual.txt" | awk '{print $1}')
[ "$actual_sha" = 5b3997ad9bae1ceb6f1808c0d692f5e522139b4c38785d56a0cc1d91f047e0f6 ] ||
    fail "canonical configure deviation hash changed: $actual_sha"

printf 'ac_cv_header_sys_param_h=yes\n' >>"$tmp/host-only.txt"
if "$ledger" "$tmp/host-only.txt" "$tmp/cgf-only.txt" \
    "$tmp/unknown.txt" >"$tmp/unknown.out" 2>"$tmp/unknown.err"; then
    fail "unknown configure deviation unexpectedly passed"
fi
grep -F 'unclassified host-GCC configure deviation' \
    "$tmp/unknown.err" >/dev/null ||
    fail "unknown deviation failure did not name the offending class"
[ ! -e "$tmp/unknown.txt" ] ||
    fail "failed deviation classification published a partial ledger"

grep -F '"$probe_ledger" "$host_only" "$cgf_only"' "$runner" >/dev/null ||
    fail "Curl runner does not use the fail-closed ledger helper"
grep -F 'compat_policy=opaque-u64x2-align16-v1' "$runner" >/dev/null ||
    fail "Curl runner does not record the ARM64 header policy"
grep -F 'upstream-uint128-scan.err' "$runner" >/dev/null ||
    fail "Curl runner does not retain source-audit diagnostics"
grep -F 'u128_scan_status=$?' "$runner" >/dev/null ||
    fail "Curl runner does not fail closed on source-audit errors"

printf 'campaign-curl-meta: PASS\n'
