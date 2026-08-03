#!/bin/sh
# Sprint 45: source-copy fix-its, auto-init layering, and annotation ratchet.
set -eu
LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

cc=${1:-build/cgfried}
build=${2:-build}
fixtures=tests/memsafe/autofix
work=${CGF_AUTOFIX_WORK:-$build/autofix-transforms}
case $cc in
/*) cc_abs=$cc ;;
*) cc_abs=$(pwd)/$cc ;;
esac

fail()
{
    echo "autofix_transforms: FAIL: $*" >&2
    exit 1
}

case $work in
*/autofix-transforms) ;;
*) fail "refusing unsafe work directory: $work" ;;
esac
if [ ! -x "$cc" ]; then
    fail "compiler not executable: $cc"
fi

rm -rf "$work"
mkdir -p "$work"
for source in "$fixtures"/*.c; do
    cp "$source" "$work/"
done

# Rust-free compiler lanes use the system assembler unless the caller chose a
# tool explicitly; CGF_AS_PATH retains the driver's documented precedence.
: "${CGF_AS:=0}"
export CGF_AS

copy=$work/copy.c
cp "$copy" "$copy.original"
"$cc" -fsyntax-only -Wmem-unbounded-copy -Wno-mem-leak \
    -fdiagnostics-parseable-fixits \
    -fdiagnostics-apply-fixits=all "$copy" \
    >"$work/copy.out" 2>"$work/copy.err" ||
    fail "copy transform compile failed"
[ -f "$copy.cgf-fixed" ] || fail "copy transform wrote no fixed copy"
cmp -s "$copy" "$copy.original" || fail "copy transform rewrote its source"
[ "$(grep -c '^fix-it:' "$work/copy.err")" -eq 2 ] ||
    fail "copy transform did not emit exactly two edits"
grep -Fqx 'fix-it:'"\"$copy\""':{11:5-11:22}:"snprintf(name, sizeof name, \"%s\", src)"' \
    "$work/copy.err" || fail "strcpy parseable edit changed"
grep -Fqx 'fix-it:'"\"$copy\""':{12:5-12:29}:"snprintf(name, sizeof name, \"%s\", src)"' \
    "$work/copy.err" || fail "sprintf parseable edit changed"
[ "$(grep -c 'snprintf(name, sizeof name' "$copy.cgf-fixed")" -eq 2 ] ||
    fail "copy transform did not use the true array bound twice"
if grep -q 'fix-it:.*strcat\|strncpy' "$work/copy.err"; then
    fail "copy transform suggested strcat arithmetic or strncpy"
fi
[ "$(grep -c "unbounded call to 'strcpy'" "$work/copy.err")" -eq 2 ] ||
    fail "known and unknown strcpy diagnostics did not both fire"
"$cc" -fsyntax-only -x c "$copy.cgf-fixed" \
    >"$work/copy-fixed.out" 2>"$work/copy-fixed.err" ||
    fail "fixed copy transform source does not compile"

local_copy=$work/copy-local.c
"$cc" -fsyntax-only -Wmem-unbounded-copy \
    -fdiagnostics-parseable-fixits "$local_copy" \
    >"$work/copy-local.out" 2>"$work/copy-local.err" ||
    fail "project-local strcpy fixture failed to compile"
if grep -q 'Wmem-unbounded-copy\|^fix-it:' "$work/copy-local.err"; then
    fail "project-local strcpy was treated as the external API"
fi

bad_snprintf=$work/copy-bad-snprintf.c
"$cc" -fsyntax-only -Wmem-unbounded-copy \
    -fdiagnostics-parseable-fixits -fdiagnostics-apply-fixits=all \
    "$bad_snprintf" >"$work/copy-bad-snprintf.out" \
    2>"$work/copy-bad-snprintf.err" ||
    fail "incompatible snprintf fixture failed to compile"
grep -Fq '[-Wmem-unbounded-copy]' "$work/copy-bad-snprintf.err" ||
    fail "incompatible snprintf fixture did not retain the warning"
if grep -q '^fix-it:' "$work/copy-bad-snprintf.err" ||
    [ -e "$bad_snprintf.cgf-fixed" ]; then
    fail "copy edit used an incompatible visible snprintf declaration"
fi

shadow_snprintf=$work/copy-shadow-snprintf.c
"$cc" -fsyntax-only -Wmem-unbounded-copy \
    -fdiagnostics-parseable-fixits -fdiagnostics-apply-fixits=all \
    "$shadow_snprintf" >"$work/copy-shadow-snprintf.out" \
    2>"$work/copy-shadow-snprintf.err" ||
    fail "shadowed snprintf fixture failed to compile"
grep -Fq '[-Wmem-unbounded-copy]' "$work/copy-shadow-snprintf.err" ||
    fail "shadowed snprintf fixture did not retain the warning"
if grep -q '^fix-it:' "$work/copy-shadow-snprintf.err" ||
    [ -e "$shadow_snprintf.cgf-fixed" ]; then
    fail "copy edit ignored a block-local snprintf shadow"
fi

macro_snprintf=$work/copy-macro-snprintf.c
"$cc" -fsyntax-only -Wmem-unbounded-copy \
    -fdiagnostics-parseable-fixits -fdiagnostics-apply-fixits=all \
    "$macro_snprintf" >"$work/copy-macro-snprintf.out" \
    2>"$work/copy-macro-snprintf.err" ||
    fail "macro snprintf fixture failed to compile"
grep -Fq '[-Wmem-unbounded-copy]' "$work/copy-macro-snprintf.err" ||
    fail "macro snprintf fixture did not retain the warning"
if grep -q '^fix-it:' "$work/copy-macro-snprintf.err" ||
    [ -e "$macro_snprintf.cgf-fixed" ]; then
    fail "copy edit ignored a point-visible snprintf macro"
fi

sizeof_src=$work/sizeof.c
cp "$sizeof_src" "$sizeof_src.original"
"$cc" -fsyntax-only -Wmem-sizeof-mismatch -Wno-mem-leak \
    -fdiagnostics-parseable-fixits \
    -fdiagnostics-apply-fixits=all "$sizeof_src" \
    >"$work/sizeof.out" 2>"$work/sizeof.err" ||
    fail "sizeof transform compile failed"
[ -f "$sizeof_src.cgf-fixed" ] || fail "sizeof transform wrote no fixed copy"
cmp -s "$sizeof_src" "$sizeof_src.original" ||
    fail "sizeof transform rewrote its source"
[ "$(grep -c '^fix-it:' "$work/sizeof.err")" -eq 2 ] ||
    fail "sizeof transform did not emit exactly two edits"
grep -Fqx 'fix-it:'"\"$sizeof_src\""':{9:22-9:31}:"sizeof *p"' \
    "$work/sizeof.err" || fail "pointer-size parseable edit changed"
grep -Fqx 'fix-it:'"\"$sizeof_src\""':{10:31-10:43}:"sizeof *q"' \
    "$work/sizeof.err" || fail "element-size parseable edit changed"
grep -Fq 'Item *p = malloc(sizeof *p);' "$sizeof_src.cgf-fixed" ||
    fail "pointer-size allocation was not repaired"
grep -Fq 'Item **q = malloc(count * sizeof *q);' "$sizeof_src.cgf-fixed" ||
    fail "element-type mismatch was not repaired"
grep -Fq 'Item *exact = malloc(sizeof(Item));' "$sizeof_src.cgf-fixed" ||
    fail "exact-type allocation was changed"
grep -Fq 'Item *many = malloc(sizeof(Item[4]));' "$sizeof_src.cgf-fixed" ||
    fail "intentional array allocation was changed"
grep -Fq 'Item *with_tail = malloc(sizeof(Item[4]) + count);' \
    "$sizeof_src.cgf-fixed" || fail "composite allocation was changed"
grep -Fq 'void *opaque = malloc(sizeof(opaque));' "$sizeof_src.cgf-fixed" ||
    fail "void-pointer allocation was changed"
grep -Fq 'char *bytes = malloc(count);' "$sizeof_src.cgf-fixed" ||
    fail "byte-count allocation was changed"

null_src=$work/null.c
"$cc" -fsyntax-only -Wmem-strict -Wno-mem-leak \
    -fdiagnostics-parseable-fixits \
    -fdiagnostics-apply-fixits=all "$null_src" \
    >"$work/null.out" 2>"$work/null.err" ||
    fail "null-check suggestion compile failed"
[ "$(grep -c '^fix-it:' "$work/null.err")" -eq 1 ] ||
    fail "null-check suggestion count changed"
[ ! -e "$null_src.cgf-fixed" ] ||
    fail "all mode applied an advisory null-check suggestion"
if "$cc" -fsyntax-only -Wmem-strict -Wno-mem-leak \
    -fdiagnostics-apply-fixits=interactive "$null_src" \
    </dev/null >"$work/interactive.out" 2>"$work/interactive.err"; then
    fail "interactive mode accepted non-TTY stdin"
fi
grep -Fq 'interactive fix-it application requires a TTY' \
    "$work/interactive.err" || fail "interactive mode reported the wrong error"

leak_src=$work/leak.c
"$cc" -fsyntax-only -Wmem -fdiagnostics-parseable-fixits \
    -fdiagnostics-apply-fixits=all "$leak_src" \
    >"$work/leak.out" 2>"$work/leak.err" ||
    fail "leak suggestion compile failed"
grep -Fq 'fix-it:'"\"$leak_src\""':{9:9-9:9}:"free(buf);\n        "' \
    "$work/leak.err" || fail "early-return free suggestion changed"
[ "$(grep -c '^fix-it:' "$work/leak.err")" -eq 2 ] ||
    fail "leak advice was not limited to the two early returns"
grep -Fq 'free(right);\n        ' "$work/leak.err" ||
    fail "same-line allocation was bound to the wrong declaration"
if grep -q 'free(unrelated)\|free(tail)' "$work/leak.err"; then
    fail "leak advice targeted an unrelated binding or final return"
fi
if grep -q 'free(reassigned)\|free(shadowed)' "$work/leak.err"; then
    fail "leak advice targeted a reassigned or shadowed binding"
fi
[ ! -e "$leak_src.cgf-fixed" ] ||
    fail "all mode applied an advisory free suggestion"

shadow_free=$work/leak-shadow-free.c
"$cc" -fsyntax-only -Wmem -fdiagnostics-parseable-fixits \
    "$shadow_free" >"$work/leak-shadow-free.out" \
    2>"$work/leak-shadow-free.err" ||
    fail "shadowed free fixture failed to compile"
[ "$(grep -c '\[-Wmem-leak\]' "$work/leak-shadow-free.err")" -eq 4 ] ||
    fail "shadowed free fixture did not retain its leak warnings"
if grep -q '^fix-it:' "$work/leak-shadow-free.err"; then
    fail "leak advice ignored a local declaration or point-visible macro"
fi

late_free=$work/leak-late-free.c
"$cc" -fsyntax-only -Wmem -fdiagnostics-parseable-fixits \
    "$late_free" >"$work/leak-late-free.out" \
    2>"$work/leak-late-free.err" ||
    fail "late free prototype fixture failed to compile"
[ "$(grep -c '\[-Wmem-leak\]' "$work/leak-late-free.err")" -eq 2 ] ||
    fail "late free prototype fixture did not retain its leak warnings"
if grep -q '^fix-it:' "$work/leak-late-free.err"; then
    fail "a later free prototype authorized an earlier suggestion"
fi

for opt in O0 O2; do
    exe=$work/auto-init-$opt
    "$cc" -Wall -"$opt" -ftrivial-auto-var-init=zero \
        "$work/auto-init.c" -o "$exe" \
        >"$exe.compile.out" 2>"$exe.compile.err" ||
        fail "zero auto-init failed to compile at -$opt"
    grep -Fq '[-Wuninitialized]' "$exe.compile.err" ||
        fail "zero auto-init silenced -Wuninitialized at -$opt"
    "$exe" >"$exe.run.out" 2>"$exe.run.err" ||
        fail "zero auto-init program failed at -$opt"
    [ "$(cat "$exe.run.out")" = 0 ] ||
        fail "zero auto-init did not produce zero at -$opt"
done
cmp -s "$work/auto-init-O0.run.out" "$work/auto-init-O2.run.out" ||
    fail "zero auto-init differs between -O0 and -O2"

"$cc" -O0 -ftrivial-auto-var-init=pattern "$work/pattern.c" \
    -o "$work/pattern" >"$work/pattern.compile.out" \
    2>"$work/pattern.compile.err" || fail "pattern auto-init failed to compile"
pattern_rc=0
sh -c '"$1" >"$2" 2>"$3"' _ "$work/pattern" \
    "$work/pattern.run.out" "$work/pattern.run.err" 2>/dev/null ||
    pattern_rc=$?
[ "$pattern_rc" -ne 0 ] ||
    fail "pattern-initialized pointer unexpectedly dereferenced successfully"

annot=$work/annotations.c
cp "$annot" "$annot.original"
"$cc" -fsyntax-only -Wmem-suggest-annotations \
    -fdiagnostics-parseable-fixits -fdiagnostics-apply-fixits=all "$annot" \
    >"$work/annotations.out" 2>"$work/annotations.err" ||
    fail "annotation inference compile failed"
[ -f "$annot.cgf-fixed" ] || fail "annotation inference wrote no fixed copy"
cmp -s "$annot" "$annot.original" ||
    fail "annotation inference rewrote its source"
[ "$(grep -c '^fix-it:' "$work/annotations.err")" -eq 2 ] ||
    fail "annotation inference did not emit exactly two must-fact edits"
grep -Fq 'CGF_RETURNS_OWNED void *make_buffer(void);' "$annot.cgf-fixed" ||
    fail "returns-owned annotation was not applied"
grep -Fq 'CGF_TAKES_OWNERSHIP(1) void consume_buffer' "$annot.cgf-fixed" ||
    fail "takes-ownership annotation was not applied"
if grep -q 'maybe_consume.*must ownership facts' "$work/annotations.err"; then
    fail "a may-free path produced an ownership annotation"
fi
"$cc" -fsyntax-only -x c -Wmem-annotation-mismatch \
    "$annot.cgf-fixed" >"$work/ratchet.out" 2>"$work/ratchet.err" ||
    fail "annotated fixed copy does not recompile"
if grep -q '\[-Wmem-annotation-mismatch\]' "$work/ratchet.err"; then
    fail "inferred annotations disagree with mismatch analysis"
fi

for advisory in annotations-after-include annotations-no-header annotations-undef annotations-fake; do
    advisory_src=$work/$advisory.c
    "$cc" -fsyntax-only -Wmem-suggest-annotations \
        -fdiagnostics-parseable-fixits -fdiagnostics-apply-fixits=all \
        "$advisory_src" >"$work/$advisory.out" \
        2>"$work/$advisory.err" ||
        fail "$advisory annotation visibility fixture failed"
    grep -Fq 'must ownership facts' "$work/$advisory.err" ||
        fail "$advisory did not retain advisory annotation guidance"
    grep -q '^fix-it:' "$work/$advisory.err" ||
        fail "$advisory did not render its advisory edit"
    [ ! -e "$advisory_src.cgf-fixed" ] ||
        fail "$advisory annotation edit was incorrectly auto-applied"
done

# Stdin and pseudo-files can produce diagnostics, but there is no stable
# source path on which the source-copy contract can operate.
mkdir -p "$work/stdin-dir"
if (cd "$work/stdin-dir" &&
    printf 'int main(void) { return 0 }\n' |
        "$cc_abs" -x c - -fsyntax-only -fdiagnostics-apply-fixits=all \
            >stdin.out 2>stdin.err); then
    fail "missing-semicolon stdin fixture unexpectedly compiled"
fi
[ ! -e "$work/stdin-dir/<stdin>.cgf-fixed" ] ||
    fail "fix-it application invented a source-copy path for stdin"

# GCC is the parseable-format oracle.  Compare the exact range/replacement
# suffix for the same insertion; the unit suite separately feeds overlapping
# and adjacent ranges through the common conflict reader.
missing=$work/missing-semi.c
cp tests/programs/diag/missing_semi_stmt.c "$missing"
if "$cc" -fsyntax-only -fdiagnostics-parseable-fixits "$missing" \
    >"$work/missing-cgf.out" 2>"$work/missing-cgf.err"; then
    fail "missing-semicolon fixture unexpectedly compiled"
fi
cgf_record=$(grep '^fix-it:' "$work/missing-cgf.err" || true)
[ "$cgf_record" = 'fix-it:'"\"$missing\""':{4:20-4:20}:";"' ] ||
    fail "cgfried insertion record does not match GCC format"
if command -v gcc >/dev/null 2>&1; then
    if gcc -fsyntax-only -fdiagnostics-parseable-fixits "$missing" \
        >"$work/missing-gcc.out" 2>"$work/missing-gcc.err"; then
        fail "GCC missing-semicolon oracle unexpectedly compiled"
    fi
    gcc_record=$(grep '^fix-it:' "$work/missing-gcc.err" || true)
    cgf_suffix=${cgf_record#*\":}
    gcc_suffix=${gcc_record#*\":}
    [ "$cgf_suffix" = "$gcc_suffix" ] ||
        fail "parseable insertion differs from GCC: $gcc_suffix"
fi

echo "autofix_transforms: source-copy, transforms, auto-init, and ratchet passed"
