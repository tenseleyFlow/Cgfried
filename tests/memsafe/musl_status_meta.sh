#!/bin/sh
set -eu

work=${TMPDIR:-/tmp}/cgf-musl-status.$$
trap 'rm -f "$work.cc" "$work.diag" "$work.err"' EXIT HUP INT TERM
printf '#!/bin/sh\nexit "${CGF_FAKE_STATUS:?}"\n' > "$work.cc"
chmod +x "$work.cc"

got=$(CGF_FAKE_STATUS=0 sh scripts/musl_mem_compile.sh \
    "$work.cc" "$work.diag" src/ok.c)
[ "$got" = analyzed ] || exit 1
got=$(CGF_FAKE_STATUS=1 sh scripts/musl_mem_compile.sh \
    "$work.cc" "$work.diag" src/deferred.c)
[ "$got" = deferred ] || exit 1
if CGF_FAKE_STATUS=4 sh scripts/musl_mem_compile.sh \
    "$work.cc" "$work.diag" src/ice.c 2>"$work.err"; then
    echo "musl_status_meta: ICE exit was accepted as a deferral" >&2
    exit 1
fi
grep 'compiler failed for src/ice.c with exit 4' "$work.err" >/dev/null
echo "musl_status_meta: analyzed/deferred/ICE classification passed"
