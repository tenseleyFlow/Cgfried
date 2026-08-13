#!/bin/sh
set -eu
export LC_ALL=C

root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)

exec "$root/ci/campaigns/check-expected.sh" "$@"
