#!/bin/sh

set -eu
LC_ALL=C
export LC_ALL

script_dir=$(dirname -- "$0")
repo=$(cd "$script_dir/../../.." && pwd)
importer=$repo/scripts/import-torture.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-torture-import-test.XXXXXX")
out=$work/torture
ref=$work/gcc-ref
active_policy=$repo/tests/torture-policy.tsv
pin=7c38c56214bf2809399a2198441bb48ee1b00512
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail()
{
    echo "torture_import_test: $*" >&2
    exit 1
}

tree_digest()
{
    find "$1" -type f -print | LC_ALL=C sort | while IFS= read -r file; do
        sha256sum "$file"
    done | sha256sum | awk '{print $1}'
}

# Build a tiny representative reference from the already-pristine committed
# import.  A fake git probe supplies the pinned revision so this meta-test is
# self-contained on CI; production imports still call the real git binary and
# enforce the same hard-coded pin.
mkdir -p "$ref/gcc/testsuite/gcc.c-torture/compile" \
    "$ref/gcc/testsuite/gcc.c-torture/execute/ieee" \
    "$ref/gcc/testsuite/gcc.c-torture/execute/builtins" "$work/bin"
cp "$repo/tests/torture/compile/20000105-1.c" \
    "$ref/gcc/testsuite/gcc.c-torture/compile/"
cp "$repo/tests/torture/execute/20000112-1.c" \
    "$ref/gcc/testsuite/gcc.c-torture/execute/"
cp "$repo/tests/torture/execute/float-floor.c" \
    "$ref/gcc/testsuite/gcc.c-torture/execute/"
cp "$repo/tests/torture/execute-ieee/20041213-1.c" \
    "$ref/gcc/testsuite/gcc.c-torture/execute/ieee/"
cp "$repo/tests/torture/execute/builtins/memops-asm.c" \
    "$repo/tests/torture/execute/builtins/memops-asm.x" \
    "$ref/gcc/testsuite/gcc.c-torture/execute/builtins/"
cp "$repo/tests/torture/execute/builtins/memset.c" \
    "$ref/gcc/testsuite/gcc.c-torture/execute/builtins/"
cp "$repo/tests/torture/COPYING" "$ref/COPYING"
cp "$repo/tests/torture/COPYING3" "$ref/COPYING3"
cp "$repo/tests/torture/README" "$ref/README"
printf '#include "../../missing/header.h"\nint main(void) { return 0; }\n' \
    >"$ref/gcc/testsuite/gcc.c-torture/execute/escaped-include.c"
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
        FAKE_GIT_STATUS="${FAKE_GIT_STATUS:-}" CGF_GCC_REF="$ref" \
        CGF_TORTURE_OUT="$out" CGF_TORTURE_POLICY="$active_policy" \
        "$importer" "$@"
}

run_import >/dev/null
first=$(tree_digest "$out")
run_import >/dev/null
second=$(tree_digest "$out")
test "$first" = "$second" || fail "second import was not byte-identical"

run_import --verify >/dev/null
tab=$(printf '\t')
grep -Fxq "COPYING${tab}231f7edcc7352d7734a96eef0b8030f77982678c516876fcb81e25b32d68564c${tab}asset${tab}-${tab}-${tab}-${tab}-" \
    "$out/MANIFEST" || fail "pinned COPYING manifest row missing"
grep -Fxq "COPYING3${tab}8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903${tab}asset${tab}-${tab}-${tab}-${tab}-" \
    "$out/MANIFEST" || fail "pinned COPYING3 manifest row missing"
grep -Fxq "README${tab}49306c701a64d02dc25de7c89eac5643a3e73c159b4aa9438b47f6b9d86ba0df${tab}asset${tab}-${tab}-${tab}-${tab}-" \
    "$out/MANIFEST" || fail "pinned README manifest row missing"
env CGF_GCC_REF="$work/absent-ref" CGF_TORTURE_OUT="$out" \
    CGF_TORTURE_POLICY="$active_policy" \
    "$importer" --verify >/dev/null
cp "$out/MANIFEST" "$work/torture.manifest.good"
awk -F '\t' 'BEGIN { OFS="\t" }
    $1 == "compile/20000105-1.c" { $3="asset" }
    { print }' "$work/torture.manifest.good" >"$out/MANIFEST"
if env CGF_GCC_REF="$work/absent-ref" CGF_TORTURE_OUT="$out" \
    CGF_TORTURE_POLICY="$active_policy" "$importer" --verify \
    >"$work/out" 2>"$work/err"; then
    fail "verify accepted a compile source classified as asset"
fi
grep -Fq 'behavioral schema is invalid' "$work/err" ||
    fail "invalid-mode schema diagnostic missing"
cp "$work/torture.manifest.good" "$out/MANIFEST"
ln -s compile/20000105-1.c "$out/hidden-source-link"
if env CGF_GCC_REF="$work/absent-ref" CGF_TORTURE_OUT="$out" \
    CGF_TORTURE_POLICY="$active_policy" "$importer" --verify \
    >"$work/out" 2>"$work/err"; then
    fail "verify accepted a symlink hidden from the file manifest"
fi
grep -Fq 'non-regular entry' "$work/err" || fail "symlink-entry diagnostic missing"
rm -f "$out/hidden-source-link"
printf '# ratchet fixture\ncompile/example.c@O0@x86_64-linux\n' >"$out/passing.txt"
run_import >/dev/null
grep -Fxq 'compile/example.c@O0@x86_64-linux' "$out/passing.txt" ||
    fail "re-import did not preserve passing.txt"
env CGF_GCC_REF="$work/absent-ref" CGF_TORTURE_OUT="$out" \
    CGF_TORTURE_POLICY="$active_policy" \
    "$importer" --verify >/dev/null
printf '\n/* local mutation */\n' >>"$out/compile/20000105-1.c"
if env CGF_TORTURE_OUT="$out" "$importer" --verify >"$work/out" 2>"$work/err"; then
    fail "pristine verification accepted a modified source"
fi
grep -Fq 'sha256 mismatch' "$work/err" || fail "mutation diagnostic missing"

run_import >/dev/null
test -d "$out/execute-ieee" || fail "execute/ieee was not split"
test ! -e "$out/execute/ieee" || fail "execute/ieee remained nested"
grep -q "$(printf '\t')run-ieee$(printf '\t')" "$out/MANIFEST" ||
    fail "run-ieee manifest rows missing"
awk -F '\t' '!/^#/ { if ($1 < previous) exit 1; previous=$1 }' "$out/MANIFEST" ||
    fail "manifest rows are not path-sorted"
grep -q "$(printf '\t')skip$(printf '\t')unsupported DejaGNU .x control$" "$out/MANIFEST" ||
    fail ".x-controlled test was not explicitly skipped"
grep -q "^execute/builtins/memset.c$(printf '\t').*$(printf '\t')skip$(printf '\t')requires DejaGNU builtins multi-source harness$" \
    "$out/MANIFEST" || fail "builtins multi-source case was not explicitly skipped"
grep -Fq "execute/escaped-include.c$(printf '\t')" "$out/MANIFEST" ||
    fail "escaped-include fixture is absent from manifest"
grep -q "^execute/escaped-include.c$(printf '\t').*$(printf '\t')skip$(printf '\t')quoted include unavailable: ../../missing/header.h$" \
    "$out/MANIFEST" || fail "escaped quoted include was not explicitly skipped"

policy_file=$work/torture-policy.tsv
printf '# cgf-torture-policy-v1\nexecute/20000112-1.c\txfail:TORT-999\tfixture durable xfail\n' \
    >"$policy_file"
active_policy=$policy_file
run_import >/dev/null
run_import >/dev/null
grep -q "^execute/20000112-1.c$(printf '\t').*$(printf '\t')xfail:TORT-999$(printf '\t')fixture durable xfail$" \
    "$out/MANIFEST" || fail "durable xfail did not survive re-import"
env CGF_GCC_REF="$work/absent-ref" CGF_TORTURE_OUT="$out" \
    CGF_TORTURE_POLICY="$active_policy" "$importer" --verify >/dev/null

foreign=$(mktemp -d "$work/foreign.XXXXXX")
printf 'preserve me\n' >"$foreign/sentinel"
if env PATH="$work/bin:$PATH" FAKE_GIT_HEAD="$pin" FAKE_GIT_STATUS= \
    CGF_GCC_REF="$ref" CGF_TORTURE_OUT="$foreign" \
    CGF_TORTURE_POLICY="$active_policy" "$importer" >"$work/out" 2>"$work/err"; then
    fail "unowned output directory was replaced"
fi
grep -Fxq 'preserve me' "$foreign/sentinel" || fail "unowned output was modified"
grep -Fq 'refusing to replace unowned directory' "$work/err" ||
    fail "unowned-output diagnostic missing"
file_out=$work/file-output
printf 'preserve file\n' >"$file_out"
if env PATH="$work/bin:$PATH" FAKE_GIT_HEAD="$pin" FAKE_GIT_STATUS= \
    CGF_GCC_REF="$ref" CGF_TORTURE_OUT="$file_out" \
    CGF_TORTURE_POLICY="$active_policy" "$importer" >"$work/out" 2>"$work/err"; then
    fail "non-directory output was replaced"
fi
grep -Fxq 'preserve file' "$file_out" || fail "non-directory output was modified"
link_out=$work/link-output
ln -s "$out" "$link_out"
if env PATH="$work/bin:$PATH" FAKE_GIT_HEAD="$pin" FAKE_GIT_STATUS= \
    CGF_GCC_REF="$ref" CGF_TORTURE_OUT="$link_out" \
    CGF_TORTURE_POLICY="$active_policy" "$importer" >"$work/out" 2>"$work/err"; then
    fail "symlink output was replaced"
fi
test -L "$link_out" || fail "symlink output was modified"

printf '\n/* dirty tracked fixture */\n' >>"$ref/gcc/testsuite/gcc.c-torture/compile/20000105-1.c"
if FAKE_GIT_STATUS=' M gcc/testsuite/gcc.c-torture/compile/20000105-1.c' \
    run_import >"$work/out" 2>"$work/err"; then
    fail "modified GCC source was imported"
fi
grep -Fq 'local modifications' "$work/err" || fail "modified-source diagnostic missing"
cp "$repo/tests/torture/compile/20000105-1.c" \
    "$ref/gcc/testsuite/gcc.c-torture/compile/20000105-1.c"
printf 'untracked\n' >"$ref/gcc/testsuite/gcc.c-torture/execute/untracked.fixture"
if FAKE_GIT_STATUS='?? gcc/testsuite/gcc.c-torture/execute/untracked.fixture' \
    run_import >"$work/out" 2>"$work/err"; then
    fail "untracked GCC source asset was imported"
fi
grep -Fq 'local modifications' "$work/err" || fail "untracked-source diagnostic missing"
rm -f "$ref/gcc/testsuite/gcc.c-torture/execute/untracked.fixture"

if env PATH="$work/bin:$PATH" FAKE_GIT_HEAD=bad-revision \
    CGF_GCC_REF="$ref" CGF_TORTURE_OUT="$out" CGF_TORTURE_POLICY="$active_policy" \
    "$importer" >"$work/out" 2>"$work/err"; then
    fail "bad GCC revision was accepted"
fi
grep -Fq 'GCC revision mismatch' "$work/err" || fail "bad-pin diagnostic missing"

echo 'torture import fixture PASS'
