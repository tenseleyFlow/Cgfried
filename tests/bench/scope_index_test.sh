#!/bin/sh
set -eu
LC_ALL=C
export LC_ALL

ROOT=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
CGF=${1:-$ROOT/build/cgfried}
WORK_ROOT=${CGF_SCOPE_TEST_WORK:-$ROOT/build/scope-index-test}
mkdir -p "$WORK_ROOT"
WORK=$(mktemp -d "$WORK_ROOT/run.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

[ -x "$CGF" ] || {
    echo "scope_index_test: compiler is not executable: $CGF" >&2
    exit 1
}
CGF_SCOPE_STRESS_DECLS=10000 \
    "$ROOT/scripts/gen-scope-stress.sh" "$WORK/scope.c" >/dev/null
[ "$(wc -l <"$WORK/scope.c")" -eq 10001 ] || {
    echo 'scope_index_test: generator did not emit 10,000 declarations' >&2
    exit 1
}
"$CGF" -Wno-mem -Wno-return-type -fsyntax-only "$WORK/scope.c" \
    >"$WORK/stdout" 2>"$WORK/stderr"
[ ! -s "$WORK/stdout" ] && [ ! -s "$WORK/stderr" ] || {
    echo 'scope_index_test: long-scope fixture produced diagnostics' >&2
    exit 1
}

echo 'scope_index_test: 10,000-declaration parser/sema fixture passed'
