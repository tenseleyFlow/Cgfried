#!/bin/sh
set -eu

fail() {
    echo "campaign-git-cc: $*" >&2
    exit 1
}

[ "${CGF_CAMPAIGN_GIT_CC_MODE:-}" = build ] ||
    fail "CGF_CAMPAIGN_GIT_CC_MODE must be build"
[ -n "${CGF_CAMPAIGN_GIT_CGF:-}" ] ||
    fail "CGF_CAMPAIGN_GIT_CGF is unset"
[ -n "${CGF_CAMPAIGN_GIT_SOLE:-}" ] ||
    fail "CGF_CAMPAIGN_GIT_SOLE is unset"
[ -n "${CGF_CAMPAIGN_GIT_RECEIPTS:-}" ] ||
    fail "CGF_CAMPAIGN_GIT_RECEIPTS is unset"

exec "$CGF_CAMPAIGN_GIT_SOLE" cc "$CGF_CAMPAIGN_GIT_RECEIPTS" \
    "$CGF_CAMPAIGN_GIT_CGF" "$@"
