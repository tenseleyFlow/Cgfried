#!/bin/sh
# Classify one musl sweep compiler invocation.  Only the compiler's ordinary
# user-diagnostic exit is a supported-syntax deferral; ICE/tool/signal exits
# are harness failures and must never disappear into the pinned deferral set.
set -u

cc=${1:?compiler required}
diag=${2:?diagnostic path required}
identity=${3:?source identity required}
shift 3

status=0
"$cc" "$@" >/dev/null 2>"$diag" || status=$?
case "$status" in
    0) printf '%s\n' analyzed ;;
    1) printf '%s\n' deferred ;;
    *)
        echo "musl_mem_warn: compiler failed for $identity with exit $status" >&2
        sed -n '1,20p' "$diag" >&2
        exit 1
        ;;
esac
