#!/bin/sh
set -eu

fail() {
    echo "campaign-mbedtls-cc: $*" >&2
    exit 1
}

mode=${CGF_CAMPAIGN_MBEDTLS_CC_MODE:-}
config=${CGF_CAMPAIGN_MBEDTLS_CONFIG:-}
dialect=${CGF_CAMPAIGN_MBEDTLS_DIALECT:-}
[ -n "$config" ] || fail "CGF_CAMPAIGN_MBEDTLS_CONFIG is unset"
[ -f "$config" ] || fail "configuration file is missing: $config"
case $dialect in
    c17 | gnu17) ;;
    *) fail "CGF_CAMPAIGN_MBEDTLS_DIALECT must be c17 or gnu17" ;;
esac

config_define="-DMBEDTLS_CONFIG_FILE=\"$config\""
set -- "-std=$dialect" -O2 "$config_define" "$@"

case $mode in
    cgfried)
        sole=${CGF_CAMPAIGN_MBEDTLS_SOLE:-}
        receipts=${CGF_CAMPAIGN_MBEDTLS_RECEIPTS:-}
        compiler=${CGF_CAMPAIGN_MBEDTLS_CGF:-}
        compat=${CGF_CAMPAIGN_MBEDTLS_COMPAT:-}
        [ -x "$sole" ] || fail "sole-C wrapper is unavailable: $sole"
        [ -x "$compiler" ] || fail "Cgfried compiler is unavailable: $compiler"
        [ -n "$receipts" ] || fail "receipt root is unset"
        if [ -n "$compat" ]; then
            [ -f "$compat" ] || fail "compatibility header is missing: $compat"
            set -- -include "$compat" "$@"
        fi
        exec "$sole" cc "$receipts" "$compiler" "$@"
        ;;
    host)
        compiler=${CGF_CAMPAIGN_MBEDTLS_HOSTCC:-}
        [ -n "$compiler" ] || fail "host compiler is unset"
        exec "$compiler" "$@"
        ;;
    *) fail "CGF_CAMPAIGN_MBEDTLS_CC_MODE must be cgfried or host" ;;
esac
