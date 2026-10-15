#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cgf=${CGF_TEST_CC:-$root/build/cgfried}
fake_as=$root/tests/fixtures/driver/staging-as.sh
fake_ld=$root/tests/fixtures/driver/staging-ld.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-driver-staging.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
stage_log=$tmp/stages.log
ld_log=$tmp/ld.log

fail()
{
    echo "driver-staging: $*" >&2
    exit 1
}

stage_field()
{
    case_name=$1
    field=$2
    awk -F '|' -v case_name="$case_name" -v field="$field" '
        $1 == case_name { print $field; exit }
    ' "$stage_log"
}

[ -x "$cgf" ] || fail "compiler is unavailable at $cgf"
[ -x "$fake_as" ] || fail "assembler fixture is not executable"
[ -x "$fake_ld" ] || fail "linker fixture is not executable"

printf '%s\n' 'int staging_value(void) { return 7; }' >"$tmp/input.c"
: >"$stage_log"

# A successful -c must leave the old deterministic sidecar untouched and
# remove only the unique stage that the compiler created.
compile_output=$tmp/compile-output.o
compile_sentinel=$compile_output.cgf.s
printf '%s\n' COMPILE-SENTINEL >"$compile_sentinel"
cp "$compile_sentinel" "$tmp/compile-sentinel.expected"
CGF_STAGE_LOG=$stage_log CGF_STAGE_CASE=compile CGF_AS_PATH=$fake_as \
    "$cgf" --target=x86_64-linux-gnu -c "$tmp/input.c" -o "$compile_output" \
    >"$tmp/compile.out" 2>"$tmp/compile.err" ||
    fail "successful compile path failed"
cmp "$tmp/compile-sentinel.expected" "$compile_sentinel" ||
    fail "compile path changed the pre-existing deterministic sidecar"
compile_stage=$(stage_field compile 2)
[ -n "$compile_stage" ] || fail "assembler did not record the compile stage"
case "$compile_stage" in
    "$compile_output".cgf.s.??????) ;;
    *) fail "compile stage is not an owned unique sibling: $compile_stage" ;;
esac
[ ! -e "$compile_stage" ] ||
    fail "successful compile leaked its owned assembly stage"
grep -Fq FAKE-OBJECT "$compile_output" ||
    fail "controlled assembler did not produce the requested object"

# Preprocessed assembly follows the same staging path. Keep its former
# deterministic sidecar intact and clean the unique stage after success.
printf '%s\n' '.text' >"$tmp/input.S"
asm_pp_output=$tmp/asm-pp-output.o
asm_pp_sentinel=$asm_pp_output.cgf.s
printf '%s\n' ASM-PP-SENTINEL >"$asm_pp_sentinel"
cp "$asm_pp_sentinel" "$tmp/asm-pp-sentinel.expected"
CGF_STAGE_LOG=$stage_log CGF_STAGE_CASE=asm-pp CGF_AS_PATH=$fake_as \
    "$cgf" --target=x86_64-linux-gnu -c "$tmp/input.S" -o "$asm_pp_output" \
    >"$tmp/asm-pp.out" 2>"$tmp/asm-pp.err" ||
    fail "successful preprocessed-assembly path failed"
cmp "$tmp/asm-pp-sentinel.expected" "$asm_pp_sentinel" ||
    fail "preprocessed assembly changed the deterministic sidecar"
asm_pp_stage=$(stage_field asm-pp 2)
case "$asm_pp_stage" in
    "$asm_pp_output".cgf.s.??????) ;;
    *) fail "preprocessed-assembly stage is not unique: $asm_pp_stage" ;;
esac
[ ! -e "$asm_pp_stage" ] ||
    fail "preprocessed assembly leaked its owned stage"

# Rejection keeps the unique assembly reproducer and still never touches the
# historical deterministic name. The line parser must work with the suffix.
reject_output=$tmp/reject-output.o
reject_sentinel=$reject_output.cgf.s
printf '%s\n' REJECT-SENTINEL >"$reject_sentinel"
cp "$reject_sentinel" "$tmp/reject-sentinel.expected"
reject_status=0
CGF_STAGE_LOG=$stage_log CGF_STAGE_CASE=reject CGF_FAKE_AS_FAIL=1 \
    CGF_AS_PATH=$fake_as "$cgf" --target=x86_64-linux-gnu \
    -c "$tmp/input.c" -o "$reject_output" \
    >"$tmp/reject.out" 2>"$tmp/reject.err" || reject_status=$?
[ "$reject_status" -eq 4 ] ||
    fail "assembler rejection returned $reject_status instead of 4"
cmp "$tmp/reject-sentinel.expected" "$reject_sentinel" ||
    fail "rejection path changed the pre-existing deterministic sidecar"
reject_stage=$(stage_field reject 2)
[ -f "$reject_stage" ] ||
    fail "assembler rejection did not retain its unique reproducer"
grep -Fq "kept at '$reject_stage', line 7:" "$tmp/reject.err" ||
    fail "rejection diagnostic lost the retained path or source line"

# A generated-assembly rejection exits through the ICE path. In link mode it
# must discard the reserved object before terminating while retaining only the
# assembly reproducer named by the diagnostic.
link_reject_output=$tmp/link-reject-output
link_reject_sentinel=$link_reject_output.cgf.0.o
printf '%s\n' LINK-REJECT-SENTINEL >"$link_reject_sentinel"
cp "$link_reject_sentinel" "$tmp/link-reject-sentinel.expected"
link_reject_status=0
CGF_STAGE_LOG=$stage_log CGF_STAGE_CASE=link-reject CGF_FAKE_AS_FAIL=1 \
    CGF_AS_PATH=$fake_as "$cgf" --target=x86_64-linux-gnu -nostdlib \
    "$tmp/input.c" -o "$link_reject_output" \
    >"$tmp/link-reject.out" 2>"$tmp/link-reject.err" ||
    link_reject_status=$?
[ "$link_reject_status" -eq 4 ] ||
    fail "link-mode assembler rejection returned $link_reject_status instead of 4"
cmp "$tmp/link-reject-sentinel.expected" "$link_reject_sentinel" ||
    fail "link-mode rejection changed the deterministic object sentinel"
link_reject_stage=$(stage_field link-reject 2)
link_reject_object=$(stage_field link-reject 3)
[ -f "$link_reject_stage" ] ||
    fail "link-mode rejection did not retain its assembly reproducer"
[ ! -e "$link_reject_object" ] ||
    fail "link-mode rejection leaked its reserved object"
grep -Fq "kept at '$link_reject_stage', line 7:" "$tmp/link-reject.err" ||
    fail "link-mode rejection diagnostic lost its path or source line"

# Link-mode objects are reserved uniquely before assembly and removed after a
# failed linker. The former deterministic object must remain byte-identical.
link_output=$tmp/link-output
link_sentinel=$link_output.cgf.0.o
printf '%s\n' LINK-SENTINEL >"$link_sentinel"
cp "$link_sentinel" "$tmp/link-sentinel.expected"
link_status=0
CGF_STAGE_LOG=$stage_log CGF_STAGE_CASE=link CGF_AS_PATH=$fake_as \
    CGF_LD_PATH=$fake_ld CGF_LD_LOG=$ld_log \
    "$cgf" --target=x86_64-linux-gnu -nostdlib "$tmp/input.c" \
    -o "$link_output" >"$tmp/link.out" 2>"$tmp/link.err" || link_status=$?
[ "$link_status" -eq 2 ] ||
    fail "intentional linker rejection returned $link_status instead of 2"
cmp "$tmp/link-sentinel.expected" "$link_sentinel" ||
    fail "link path changed the pre-existing deterministic object"
link_asm_stage=$(stage_field link 2)
link_object=$(stage_field link 3)
case "$link_object" in
    "$link_output".cgf.0.o.??????) ;;
    *) fail "link object is not an owned unique sibling: $link_object" ;;
esac
[ ! -e "$link_asm_stage" ] ||
    fail "link compile leaked its owned assembly stage"
[ ! -e "$link_object" ] ||
    fail "failed link leaked its owned object stage"
grep -Fxq "$link_object" "$ld_log" ||
    fail "controlled linker did not receive the unique object"

# A frontend failure happens after reservation but before the assembler. Its
# owned object must still be cleaned up.
printf '%s\n' 'int broken( {' >"$tmp/broken.c"
parse_status=0
CGF_STAGE_LOG=$stage_log CGF_STAGE_CASE=parse CGF_AS_PATH=$fake_as \
    CGF_LD_PATH=$fake_ld CGF_LD_LOG=$ld_log \
    "$cgf" --target=x86_64-linux-gnu -nostdlib "$tmp/broken.c" \
    -o "$tmp/parse-output" >"$tmp/parse.out" 2>"$tmp/parse.err" ||
    parse_status=$?
[ "$parse_status" -eq 1 ] ||
    fail "invalid source returned $parse_status instead of 1"
if find "$tmp" -type f -name 'parse-output.cgf.0.o.*' -print |
    grep . >/dev/null
then
    fail "frontend failure leaked its reserved link object"
fi

# -### is side-effect free but must describe the unique templates that the
# live compile and link paths use.
dry_output=$tmp/dry-output
CGF_AS_PATH=$fake_as CGF_LD_PATH=$fake_ld \
    "$cgf" --target=x86_64-linux-gnu -nostdlib -### "$tmp/input.c" \
    -o "$dry_output" >"$tmp/dry.out" 2>"$tmp/dry.err" ||
    fail "dry-run staging plan failed"
grep -Fq "$dry_output.cgf.0.o.XXXXXX.cgf.s.XXXXXX" "$tmp/dry.err" ||
    fail "dry-run plan omitted the unique assembly template"
grep -Fq "$dry_output.cgf.0.o.XXXXXX" "$tmp/dry.err" ||
    fail "dry-run plan omitted the unique object template"
if find "$tmp" -type f -name 'dry-output.cgf.*' -print | grep . >/dev/null
then
    fail "dry-run staging plan created a file"
fi

echo "driver-staging: PASS (exclusive stages, owned cleanup, retained failure)"
