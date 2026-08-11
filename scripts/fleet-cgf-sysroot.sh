#!/bin/sh
# Preserve the compiler argv while injecting the target root prepared by the
# fleet runner. This is intentionally separate from cgfried: a Nix store is a
# host deployment layout, not a new target ABI or a compiler default.
set -eu

: "${CGF_FLEET_REAL_CGF:?CGF_FLEET_REAL_CGF must name the real compiler}"
: "${CGF_FLEET_SYSROOT:?CGF_FLEET_SYSROOT must name the prepared target root}"
[ -x "$CGF_FLEET_REAL_CGF" ] || {
    echo "fleet-cgf-sysroot: compiler is not executable: $CGF_FLEET_REAL_CGF" >&2
    exit 3
}
[ -d "$CGF_FLEET_SYSROOT" ] || {
    echo "fleet-cgf-sysroot: sysroot is not a directory: $CGF_FLEET_SYSROOT" >&2
    exit 3
}

exec "$CGF_FLEET_REAL_CGF" "--sysroot=$CGF_FLEET_SYSROOT" "$@"
