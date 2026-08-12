#!/bin/sh

set -eu
LC_ALL=C
export LC_ALL

script_dir=$(dirname -- "$0")
repo=$(cd "$script_dir/../../.." && pwd)
importer=$repo/scripts/import-c-testsuite.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-ctestsuite-import-test.XXXXXX")
out=$work/ctestsuite
ref=$work/c-testsuite-ref
license_bundle=$work/licenses
active_policy=$repo/tests/ctestsuite-policy.tsv
pin=5c7275656d751de0e68b2d340a95b5681858ed07
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail()
{
    echo "ctestsuite_import_test: $*" >&2
    exit 1
}

tree_digest()
{
    find "$1" -type f -print | LC_ALL=C sort | while IFS= read -r file; do
        sha256sum "$file"
    done | sha256sum | awk '{print $1}'
}

mkdir -p "$ref/tests/single-exec" "$work/bin"
find "$repo/tests/ctestsuite" -maxdepth 1 -type f ! -name MANIFEST -print |
while IFS= read -r file; do
    cp "$file" "$ref/tests/single-exec/$(basename -- "$file")"
done
cat >"$ref/tests/single-exec/00001.c" <<'EOF'
int
main()
{
	return 0;
}
EOF
: >"$ref/tests/single-exec/00001.c.expected"
printf 'portable\nc89\n' >"$ref/tests/single-exec/00001.c.tags"
cp -R "$repo/tests/ctestsuite-licenses" "$license_bundle"
cat >"$work/bin/git" <<'EOF'
#!/bin/sh
if [ "$#" -eq 4 ] && [ "$1" = -C ] && [ "$3" = rev-parse ] &&
    [ "$4" = HEAD ]; then
    printf '%s\n' "$FAKE_GIT_HEAD"
    exit 0
fi
if [ "$#" -ge 4 ] && [ "$1" = -C ] && [ "$3" = status ]; then
    test -z "${FAKE_GIT_STATUS:-}" || printf '%s\n' "$FAKE_GIT_STATUS"
    exit 0
fi
exit 2
EOF
chmod +x "$work/bin/git"

run_import()
{
    env PATH="$work/bin:$PATH" FAKE_GIT_HEAD="$pin" \
        FAKE_GIT_STATUS="${FAKE_GIT_STATUS:-}" \
        CGF_CTESTSUITE_REF="$ref" CGF_CTESTSUITE_OUT="$out" \
        CGF_CTESTSUITE_POLICY="$active_policy" \
        CGF_CTESTSUITE_LICENSES="$license_bundle" \
        "$importer" "$@"
}

run_import >/dev/null
first=$(tree_digest "$out")
run_import >/dev/null
second=$(tree_digest "$out")
test "$first" = "$second" || fail "second import was not byte-identical"

run_import --verify >/dev/null
env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" \
    CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify >/dev/null
test ! -e "$out/00001.c" && test ! -e "$out/00001.c.expected" &&
    test ! -e "$out/00001.c.tags" || fail "unprovenanced 00001 case was imported"
grep -Fxq '# excluded-case: 00001.c and sidecars (missing .otags provenance)' \
    "$out/MANIFEST" || fail "00001 exclusion provenance is absent"
test "$(find "$out" -maxdepth 1 -type f -name '*.c' | wc -l | tr -d ' ')" = 219 ||
    fail "imported case count is not 219"
cmp -s "$ref/tests/single-exec/00002.c" "$out/00002.c" ||
    fail "import changed pristine case bytes"

mv "$ref/tests/single-exec/00002.c.otags" "$work/00002.c.otags"
if run_import >"$work/out" 2>"$work/err"; then
    fail "case with missing .otags provenance was imported"
fi
grep -Fq 'case provenance is absent' "$work/err" ||
    fail "missing-.otags diagnostic is absent"
mv "$work/00002.c.otags" "$ref/tests/single-exec/00002.c.otags"
cp "$ref/tests/single-exec/00002.c.otags" "$work/00002.c.otags"
sed '1s/^org=.*/org=unknown.example/' "$work/00002.c.otags" > \
    "$ref/tests/single-exec/00002.c.otags"
if run_import >"$work/out" 2>"$work/err"; then
    fail "case with unknown origin provenance was imported"
fi
grep -Fq 'unknown c-testsuite case provenance' "$work/err" ||
    fail "unknown-origin diagnostic is absent"
cp "$work/00002.c.otags" "$ref/tests/single-exec/00002.c.otags"
printf 'extra=field\n' >>"$ref/tests/single-exec/00002.c.otags"
if run_import >"$work/out" 2>"$work/err"; then
    fail "case with malformed .otags provenance was imported"
fi
grep -Fq 'malformed c-testsuite case provenance' "$work/err" ||
    fail "malformed-.otags diagnostic is absent"
cp "$work/00002.c.otags" "$ref/tests/single-exec/00002.c.otags"

printf '\nmutation\n' >>"$license_bundle/scc-LICENSE"
if env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify >"$work/out" 2>"$work/err"; then
    fail "verify accepted a modified license artifact"
fi
grep -Fq 'SCC license hash is invalid' "$work/err" ||
    fail "license-hash diagnostic is absent"
cp "$repo/tests/ctestsuite-licenses/scc-LICENSE" "$license_bundle/scc-LICENSE"
printf '\n# mutation\n' >>"$license_bundle/ORIGINS.tsv"
if env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify >"$work/out" 2>"$work/err"; then
    fail "verify accepted modified origin provenance"
fi
grep -Fq 'origin provenance is invalid' "$work/err" ||
    fail "origin-provenance diagnostic is absent"
cp "$repo/tests/ctestsuite-licenses/ORIGINS.tsv" "$license_bundle/ORIGINS.tsv"

printf '\nmutated imported copy\n' >>"$out/LICENSES/scc-LICENSE"
mutated_hash=$(sha256sum "$out/LICENSES/scc-LICENSE" | awk '{print $1}')
awk -F '\t' -v hash="$mutated_hash" 'BEGIN { OFS="\t" }
    $1 == "file" && $2 == "LICENSES/scc-LICENSE" { $3=hash }
    { print }' "$out/MANIFEST" >"$work/manifest-with-mutated-license"
mv "$work/manifest-with-mutated-license" "$out/MANIFEST"
if env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify >"$work/out" 2>"$work/err"; then
    fail "verify accepted an imported license plus matching manifest mutation"
fi
grep -Fq 'license artifact does not match pinned bundle' "$work/err" ||
    fail "imported-license equivalence diagnostic is absent"
run_import >/dev/null

ln -s 00002.c "$out/unlisted-link"
if env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify >"$work/out" 2>"$work/err"; then
    fail "verify accepted a symlink in the imported tree"
fi
grep -Fq 'symlink or special entry' "$work/err" ||
    fail "symlink-entry diagnostic is absent"
rm -f "$out/unlisted-link"
cp "$out/MANIFEST" "$work/ctestsuite.manifest.good"
awk -F '\t' '$1 == "case" && !removed { removed=1; next } { print }' \
    "$work/ctestsuite.manifest.good" >"$out/MANIFEST"
if env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify \
    >"$work/out" 2>"$work/err"; then
    fail "verify accepted a deleted c-testsuite case"
fi
grep -Fq 'exactly one case' "$work/err" || fail "deleted-case diagnostic missing"
cp "$work/ctestsuite.manifest.good" "$out/MANIFEST"
awk -F '\t' '$1 == "case" { print; exit }' "$work/ctestsuite.manifest.good" \
    >>"$out/MANIFEST"
if env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify \
    >"$work/out" 2>"$work/err"; then
    fail "verify accepted a duplicate c-testsuite case"
fi
grep -Fq 'behavioral schema is invalid' "$work/err" ||
    fail "duplicate-case schema diagnostic missing"
awk -F '\t' 'BEGIN { OFS="\t" }
    $1 == "case" && !changed { NF=5; changed=1 }
    { print }' "$work/ctestsuite.manifest.good" >"$out/MANIFEST"
if env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify \
    >"$work/out" 2>"$work/err"; then
    fail "verify accepted a malformed c-testsuite case"
fi
grep -Fq 'behavioral schema is invalid' "$work/err" ||
    fail "malformed-case schema diagnostic missing"
cp "$work/ctestsuite.manifest.good" "$out/MANIFEST"
printf '\n' >>"$out/00002.c.expected"
if env CGF_CTESTSUITE_OUT="$out" CGF_CTESTSUITE_POLICY="$active_policy" \
    CGF_CTESTSUITE_LICENSES="$license_bundle" "$importer" --verify >"$work/out" 2>"$work/err"; then
    fail "pristine verification accepted a modified expected file"
fi
grep -Fq 'sha256 mismatch' "$work/err" || fail "mutation diagnostic missing"

run_import >/dev/null
first_case=$(awk -F '\t' '$1 == "case" { print; exit }' "$out/MANIFEST")
test "$first_case" = "case$(printf '\t')00002.c$(printf '\t')00002.c.expected$(printf '\t')c89,portable$(printf '\t')run$(printf '\t')-" ||
    fail "expected/tags case mapping is incorrect: $first_case"
awk -F '\t' '$1 == "file" { if (cases) exit 1; if ($2 < file_previous) exit 1; file_previous=$2; next }
    $1 == "case" { cases=1; if ($2 < case_previous) exit 1; case_previous=$2 }' \
    "$out/MANIFEST" || fail "file/case rows are not deterministically ordered"

policy_file=$work/ctestsuite-policy.tsv
printf '# cgf-ctestsuite-policy-v1\n00002.c\txfail:TORT-998\tfixture durable xfail\n' \
    >"$policy_file"
active_policy=$policy_file
run_import >/dev/null
run_import >/dev/null
grep -Fq "case$(printf '\t')00002.c$(printf '\t')00002.c.expected$(printf '\t')c89,portable$(printf '\t')xfail:TORT-998$(printf '\t')fixture durable xfail" \
    "$out/MANIFEST" || fail "durable c-testsuite xfail did not survive re-import"
env CGF_CTESTSUITE_REF="$work/absent-ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" --verify >/dev/null

foreign=$(mktemp -d "$work/foreign.XXXXXX")
printf 'preserve me\n' >"$foreign/sentinel"
if env PATH="$work/bin:$PATH" FAKE_GIT_HEAD="$pin" FAKE_GIT_STATUS= \
    CGF_CTESTSUITE_REF="$ref" CGF_CTESTSUITE_OUT="$foreign" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" >"$work/out" 2>"$work/err"; then
    fail "unowned output directory was replaced"
fi
grep -Fxq 'preserve me' "$foreign/sentinel" || fail "unowned output was modified"
grep -Fq 'refusing to replace unowned directory' "$work/err" ||
    fail "unowned-output diagnostic missing"
file_out=$work/file-output
printf 'preserve file\n' >"$file_out"
if env PATH="$work/bin:$PATH" FAKE_GIT_HEAD="$pin" FAKE_GIT_STATUS= \
    CGF_CTESTSUITE_REF="$ref" CGF_CTESTSUITE_OUT="$file_out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" >"$work/out" 2>"$work/err"; then
    fail "non-directory output was replaced"
fi
grep -Fxq 'preserve file' "$file_out" || fail "non-directory output was modified"
link_out=$work/link-output
ln -s "$out" "$link_out"
if env PATH="$work/bin:$PATH" FAKE_GIT_HEAD="$pin" FAKE_GIT_STATUS= \
    CGF_CTESTSUITE_REF="$ref" CGF_CTESTSUITE_OUT="$link_out" \
    CGF_CTESTSUITE_POLICY="$active_policy" CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" >"$work/out" 2>"$work/err"; then
    fail "symlink output was replaced"
fi
test -L "$link_out" || fail "symlink output was modified"

printf '\n' >>"$ref/tests/single-exec/00002.c"
if FAKE_GIT_STATUS=' M tests/single-exec/00002.c' \
    run_import >"$work/out" 2>"$work/err"; then
    fail "modified c-testsuite source was imported"
fi
grep -Fq 'local modifications' "$work/err" || fail "modified-source diagnostic missing"
cp "$repo/tests/ctestsuite/00002.c" "$ref/tests/single-exec/00002.c"
printf 'untracked\n' >"$ref/tests/single-exec/untracked.fixture"
if FAKE_GIT_STATUS='?? tests/single-exec/untracked.fixture' \
    run_import >"$work/out" 2>"$work/err"; then
    fail "untracked c-testsuite source asset was imported"
fi
grep -Fq 'local modifications' "$work/err" || fail "untracked-source diagnostic missing"
rm -f "$ref/tests/single-exec/untracked.fixture"

if env PATH="$work/bin:$PATH" FAKE_GIT_HEAD=bad-revision \
    CGF_CTESTSUITE_REF="$ref" CGF_CTESTSUITE_OUT="$out" \
    CGF_CTESTSUITE_POLICY="$active_policy" \
    CGF_CTESTSUITE_LICENSES="$license_bundle" \
    "$importer" >"$work/out" 2>"$work/err"; then
    fail "bad c-testsuite revision was accepted"
fi
grep -Fq 'c-testsuite revision mismatch' "$work/err" || fail "bad-pin diagnostic missing"

echo 'c-testsuite import fixture PASS'
