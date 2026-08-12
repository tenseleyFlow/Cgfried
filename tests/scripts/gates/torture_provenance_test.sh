#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
helper_source=$root/scripts/torture-provenance.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-torture-provenance-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "torture_provenance_test: $*" >&2
    exit 1
}

setup_repo()
{
    case_root=$1
    mkdir -p "$case_root/src" "$case_root/include" "$case_root/ci" \
        "$case_root/scripts" "$case_root/manifests"
    cp "$helper_source" "$case_root/scripts/torture-provenance.sh"
    printf 'all:\n\t@:\n' >"$case_root/Makefile"
    printf 'int compiler_source(void) { return 0; }\n' >"$case_root/src/unit.c"
    printf '#define COMPILER_HEADER 1\n' >"$case_root/include/unit.h"
    printf '# matrix contract\n' >"$case_root/ci/torture.mk"
    printf '#!/bin/sh\nexit 0\n' >"$case_root/scripts/torture-run.sh"
    printf '#!/bin/sh\nexit 0\n' >"$case_root/scripts/triage-torture.sh"
    printf '#!/bin/sh\nexit 0\n' >"$case_root/scripts/qemu-run.sh"
    printf '#!/bin/sh\nexit 0\n' >"$case_root/compiler"
    printf '#!/bin/sh\nexit 0\n' >"$case_root/driver"
    printf 'torture manifest\n' >"$case_root/manifests/torture"
    printf 'ctestsuite manifest\n' >"$case_root/manifests/ctestsuite"
    sh "$case_root/scripts/torture-provenance.sh" \
        --write-receipt "$case_root/compiler.provenance" \
        --compiler "$case_root/compiler"
}

emit()
{
    case_root=$1
    destination=$2
    sh "$case_root/scripts/torture-provenance.sh" \
        --receipt "$case_root/compiler.provenance" \
        --driver "$case_root/driver" --compiler "$case_root/compiler" \
        --runner "$case_root/scripts/torture-run.sh" \
        --target x86_64-linux-gnu \
        --torture-manifest "$case_root/manifests/torture" \
        --ctestsuite-manifest "$case_root/manifests/ctestsuite" \
        >"$destination"
}

baseline=$tmp/baseline
setup_repo "$baseline"
emit "$baseline" "$tmp/baseline.header"
[ "$(wc -l <"$tmp/baseline.header" | tr -d ' ')" -eq 8 ] ||
    fail 'real helper did not emit exactly eight provenance lines'
grep -Eq '^# source-revision=(unversioned|[0-9a-f]{40}|[0-9a-f]{64})$' \
    "$tmp/baseline.header" || fail 'source revision line was malformed'
for line in 2 3 4 5 7 8; do
    sed -n "${line}p" "$tmp/baseline.header" |
        grep -Eq '^# [a-z-]+-sha256=[0-9a-f]{64}$' ||
        fail "hash line $line was malformed"
done
[ "$(sed -n '6p' "$tmp/baseline.header")" = '# target=x86_64-linux-gnu' ] ||
    fail 'target line was malformed'

# Compiler-source changes invalidate the link-time receipt rather than being
# silently blessed against an old binary.
source_case=$tmp/source-change
cp -R "$baseline" "$source_case"
printf 'int changed_source;\n' >>"$source_case/src/unit.c"
status=0
emit "$source_case" "$tmp/source-change.header" \
    2>"$tmp/source-change.err" || status=$?
[ "$status" -ne 0 ] || fail 'changed compiler source was accepted by a stale receipt'
grep -F 'compiler provenance receipt is stale or tampered' \
    "$tmp/source-change.err" >/dev/null ||
    fail 'changed compiler source lacked the stale-receipt diagnostic'

compiler_case=$tmp/compiler-change
cp -R "$baseline" "$compiler_case"
printf '# changed compiler binary\n' >>"$compiler_case/compiler"
status=0
emit "$compiler_case" "$tmp/compiler-change.header" \
    2>"$tmp/compiler-change.err" || status=$?
[ "$status" -ne 0 ] || fail 'changed compiler binary was accepted by a stale receipt'

# Every harness constituent must perturb the shared harness identity without
# changing the compiler receipt.
for harness_file in ci/torture.mk scripts/torture-run.sh \
    scripts/triage-torture.sh scripts/qemu-run.sh; do
    harness_name=$(printf '%s\n' "$harness_file" | tr / -)
    harness_case=$tmp/harness-$harness_name
    cp -R "$baseline" "$harness_case"
    emit "$harness_case" "$tmp/$harness_name.before"
    printf '# changed harness\n' >>"$harness_case/$harness_file"
    emit "$harness_case" "$tmp/$harness_name.after"
    [ "$(sed -n '3p' "$tmp/$harness_name.before")" != \
      "$(sed -n '3p' "$tmp/$harness_name.after")" ] ||
        fail "$harness_file did not change the harness digest"
    sed -n '1,2p;4,8p' "$tmp/$harness_name.before" >"$tmp/$harness_name.before-rest"
    sed -n '1,2p;4,8p' "$tmp/$harness_name.after" >"$tmp/$harness_name.after-rest"
    cmp "$tmp/$harness_name.before-rest" "$tmp/$harness_name.after-rest" >/dev/null ||
        fail "$harness_file changed an unrelated provenance field"
done

for manifest_kind in torture ctestsuite; do
    manifest_case=$tmp/manifest-$manifest_kind
    cp -R "$baseline" "$manifest_case"
    emit "$manifest_case" "$tmp/$manifest_kind.before"
    printf 'changed manifest\n' >>"$manifest_case/manifests/$manifest_kind"
    emit "$manifest_case" "$tmp/$manifest_kind.after"
    case $manifest_kind in torture) manifest_line=4 ;; ctestsuite) manifest_line=5 ;; esac
    [ "$(sed -n "${manifest_line}p" "$tmp/$manifest_kind.before")" != \
      "$(sed -n "${manifest_line}p" "$tmp/$manifest_kind.after")" ] ||
        fail "$manifest_kind manifest did not change its digest"
done

driver_case=$tmp/driver-change
cp -R "$baseline" "$driver_case"
emit "$driver_case" "$tmp/driver.before"
printf '# changed driver\n' >>"$driver_case/driver"
emit "$driver_case" "$tmp/driver.after"
[ "$(sed -n '8p' "$tmp/driver.before")" != \
  "$(sed -n '8p' "$tmp/driver.after")" ] ||
    fail 'driver change did not change the driver digest'
sed -n '1,7p' "$tmp/driver.before" >"$tmp/driver.before-common"
sed -n '1,7p' "$tmp/driver.after" >"$tmp/driver.after-common"
cmp "$tmp/driver.before-common" "$tmp/driver.after-common" >/dev/null ||
    fail 'driver change altered shared provenance'

# The real Makefile path-set trigger must rebuild the receipt when the source
# set returns to a previously seen state.  Permanent digest-named stamps miss
# the final C -> B deletion because the old B stamp and every remaining input
# can all predate the compiler produced for C.
make_case=$tmp/make-source-set
mkdir -p "$make_case/src" "$make_case/include" "$make_case/ci" \
    "$make_case/scripts"
cp "$root/Makefile" "$make_case/Makefile"
cp "$root/ci/torture.mk" "$make_case/ci/torture.mk"
cp "$helper_source" "$make_case/scripts/torture-provenance.sh"
printf 'int main(void) { return 0; }\n' >"$make_case/src/main.c"

make_receipt()
{
    make --no-print-directory -C "$make_case" build/cgfried.provenance \
        >"$tmp/make-source-set.out"
}

make_receipt
printf '#define SOURCE_SET_B 1\n' >"$make_case/include/b.h"
make_receipt
printf '#define SOURCE_SET_C 1\n' >"$make_case/include/c.h"
make_receipt
rm "$make_case/include/c.h"
make_receipt
sh "$make_case/scripts/torture-provenance.sh" \
    --write-receipt "$make_case/build/expected.provenance" \
    --compiler "$make_case/build/cgfried"
cmp "$make_case/build/cgfried.provenance" \
    "$make_case/build/expected.provenance" >/dev/null ||
    fail 'Makefile missed the A -> B -> C -> B source-set transition'

echo 'torture_provenance_test: receipt and real mutation coverage passed'
