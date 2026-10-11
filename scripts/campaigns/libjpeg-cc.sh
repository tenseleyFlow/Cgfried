#!/bin/sh
set -eu

fail() {
    echo "campaign-libjpeg-cc: $*" >&2
    exit 1
}

mode=${CGF_CAMPAIGN_LIBJPEG_CC_MODE:-}
compiler=${CGF_CAMPAIGN_LIBJPEG_CGF:-}
[ -x "$compiler" ] || fail "Cgfried compiler is unavailable: $compiler"

case $mode in
    configure)
        exec "$compiler" "$@"
        ;;
    build)
        sole=${CGF_CAMPAIGN_LIBJPEG_SOLE:-}
        receipts=${CGF_CAMPAIGN_LIBJPEG_RECEIPTS:-}
        [ -x "$sole" ] || fail "sole-C wrapper is unavailable: $sole"
        [ -n "$receipts" ] || fail "receipt root is unset"
        exec "$sole" cc "$receipts" "$compiler" "$@"
        ;;
    *) fail "CGF_CAMPAIGN_LIBJPEG_CC_MODE must be configure or build" ;;
esac
