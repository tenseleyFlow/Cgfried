#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

repo=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
runner=$repo/scripts/torture-run.sh
fixtures=$repo/tests/scripts/gates/fixtures/torture-runner
fake_cc=$fixtures/fake-cc.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-torture-runner-test.XXXXXX")
cwd_sentinel=$repo/.cgf-torture-runner-cwd-fixture
[ ! -e "$cwd_sentinel" ] || {
    echo "torture_runner_test: cwd sentinel already exists: $cwd_sentinel" >&2
    exit 1
}
trap 'rm -rf "$tmp"; rm -f "$cwd_sentinel"' EXIT HUP INT TERM
tab=$(printf '\t')
cp -R "$fixtures/execute" "$tmp/execute"
cp -R "$fixtures/compile" "$tmp/compile"
cp "$fixtures/ctests/output-pass.c" "$tmp/output-pass.c"
cp "$fixtures/ctests/output-fail.c" "$tmp/output-fail.c"
cp "$fixtures/ctests/output-extra-blank.c" "$tmp/output-extra-blank.c"
cp "$fixtures/ctests/expected.txt" "$tmp/expected.txt"

hash()
{
    sha256sum "$1" | awk '{print $1}'
}

run()
{
    name=$1
    suite=$2
    manifest=$3
    shift 3
    env CGF_TORTURE_TIMEOUT=1 "$@" "$runner" --cc "$fake_cc" \
        --suite "$suite" --level O2 --target x86_64-linux-gnu \
        --manifest "$manifest" --output "$tmp/$name.tsv" --work "$tmp/$name.work"
}

expect_outcome()
{
    result=$1
    file=$2
    expected=$3
    actual=$(awk -F "$tab" -v file="$file" '$1 !~ /^#/ && $3 == file {print $6}' "$result")
    [ "$actual" = "$expected" ] || {
        echo "torture_runner_test: $file outcome $actual, expected $expected" >&2
        sed 's/^/  /' "$result" >&2
        exit 1
    }
}

expect_phase()
{
    result=$1
    file=$2
    expected=$3
    actual=$(awk -F "$tab" -v file="$file" '$1 !~ /^#/ && $3 == file {print $9}' "$result")
    [ "$actual" = "$expected" ] || {
        echo "torture_runner_test: $file phase $actual, expected $expected" >&2
        exit 1
    }
}

torture_manifest=$tmp/torture.MANIFEST
for rel in execute/xfail.c execute/xfail-signal.c execute/xpass.c execute/wrong-exit.c execute/timeout.c \
    execute/skip.c execute/signal.c execute/pass.c execute/ice.c \
    execute/compile-fail.c; do
    disposition=pass
    requirement=-
    reason=-
    case $rel in
    execute/xfail.c | execute/xfail-signal.c) disposition=xfail:TORT-999; reason='known fixture failure' ;;
    execute/xpass.c) disposition=xfail:TORT-998; reason='fixture expected failure now passes' ;;
    execute/skip.c) disposition=skip:fixture; reason='explicit fixture skip' ;;
    esac
    printf '%s\t%s\trun\t%s\t-\t%s\t%s\n' "$rel" \
        "$(hash "$fixtures/$rel")" "$requirement" "$disposition" "$reason"
done > "$torture_manifest"
# Unknown or explicitly unsupported capabilities are policy skips.
printf 'execute/requirement.c\t%s\trun\tlabel_values\t-\tpass\t-\n' \
    "$(hash "$fixtures/execute/pass.c")" >> "$torture_manifest"
printf 'execute/int128-requirement.c\t%s\trun\tint128\t-\tpass\t-\n' \
    "$(hash "$fixtures/execute/pass.c")" >> "$torture_manifest"
# The manifest path itself is authoritative, so give the requirement fixture
# an asset at the spelling used in its row.
cp "$fixtures/execute/pass.c" "$tmp/execute/requirement.c"
cp "$fixtures/execute/pass.c" "$tmp/execute/int128-requirement.c"

run execute torture-execute "$torture_manifest"
execute=$tmp/execute.tsv
expect_outcome "$execute" pass.c PASS
expect_outcome "$execute" compile-fail.c COMPILE_FAIL
expect_outcome "$execute" wrong-exit.c WRONG_EXIT
expect_outcome "$execute" signal.c SIGNAL
expect_outcome "$execute" timeout.c TIMEOUT
expect_outcome "$execute" ice.c ICE
expect_outcome "$execute" skip.c SKIP
expect_outcome "$execute" xfail.c XFAIL
expect_outcome "$execute" xfail-signal.c XFAIL
expect_outcome "$execute" xpass.c PASS
expect_outcome "$execute" requirement.c SKIP
expect_outcome "$execute" int128-requirement.c SKIP
awk -F "$tab" '$3 == "xfail-signal.c" && $7 == "-" { found=1 } END { exit !found }' "$execute"
awk -F "$tab" '$3 == "xfail.c" && $10 ~ /^xfail:TORT-999 / { found=1 } END { exit !found }' "$execute"
awk -F "$tab" '$3 == "requirement.c" && $10 == "skip-unless:label_values" { found=1 } END { exit !found }' "$execute"
awk -F "$tab" '$3 == "int128-requirement.c" && $10 == "skip-unless:int128" { found=1 } END { exit !found }' "$execute"

[ "$(wc -l < "$execute" | tr -d ' ')" -eq 14 ] || {
    echo "torture_runner_test: execute result did not have two headers plus twelve rows" >&2
    exit 1
}
sed -n '1p' "$execute" | grep -F -x '# cgf-torture-results-v1' >/dev/null
expected_columns=$(printf '# columns=key\tsuite\tfile\tlevel\ttarget\toutcome\tsignal\tfingerprint\tphase\tdetail')
[ "$(sed -n '2p' "$execute")" = "$expected_columns" ] || {
    echo "torture_runner_test: schema header differed" >&2
    exit 1
}
sed -n '3,$p' "$execute" > "$tmp/actual-order"
sort "$tmp/actual-order" > "$tmp/sorted-order"
cmp "$tmp/actual-order" "$tmp/sorted-order" || {
    echo "torture_runner_test: result rows were not sorted" >&2
    exit 1
}

# The diagnostic contains both an absolute source path and locations. A second
# work directory must still produce byte-identical fingerprints and rows.
run execute-repeat torture-execute "$torture_manifest"
cmp "$execute" "$tmp/execute-repeat.tsv" || {
    echo "torture_runner_test: repeated result was not deterministic" >&2
    diff -u "$execute" "$tmp/execute-repeat.tsv" >&2 || true
    exit 1
}

# The frontend does not implement __int128.  The default capability set must
# therefore skip int128-gated imports, while an explicit operator override
# remains available for a future implementation or a specialized compiler.
int128_manifest=$tmp/int128.MANIFEST
printf 'execute/int128-requirement.c\t%s\trun\tint128\t-\tpass\t-\n' \
    "$(hash "$tmp/execute/int128-requirement.c")" >"$int128_manifest"
run int128-supported torture-execute "$int128_manifest" env \
    CGF_TORTURE_CAPABILITIES=int128
expect_outcome "$tmp/int128-supported.tsv" int128-requirement.c PASS
fp=$(awk -F "$tab" '$3 == "compile-fail.c" {print $8}' "$execute")
detail=$(awk -F "$tab" '$3 == "compile-fail.c" {print $10}' "$execute")
normalized_detail=${detail#xfail:TORT-999 }
detail_fp=$(printf '%s\n' "$normalized_detail" | sha256sum | awk '{print $1}')
[ "$detail_fp" = "$fp" ] || {
    echo "torture_runner_test: compile detail and fingerprint used different normalization" >&2
    exit 1
}
case $fp in
[0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) [ "${#fp}" -eq 64 ] ;;
*) false ;;
esac || {
    echo "torture_runner_test: compile failure fingerprint was not SHA-256" >&2
    exit 1
}

compile_manifest=$tmp/compile.MANIFEST
printf 'compile/compile-pass.c\t%s\tcompile\t-\t-fwrapv\tpass\t-\n' \
    "$(hash "$fixtures/compile/compile-pass.c")" > "$compile_manifest"
run compile torture-compile "$compile_manifest"
expect_outcome "$tmp/compile.tsv" compile-pass.c PASS

phase_manifest=$tmp/phase.MANIFEST
for rel in pp-fail.c parse-fail.c sema-fail.c cg-fail.c compile-exit124.c compile-timeout.c; do
    printf 'execute/%s\t%s\trun\t-\t-\trun\t-\n' "$rel" \
        "$(hash "$fixtures/execute/$rel")"
done > "$phase_manifest"
run phase torture-execute "$phase_manifest"
expect_phase "$tmp/phase.tsv" pp-fail.c pp
expect_phase "$tmp/phase.tsv" parse-fail.c parse
expect_phase "$tmp/phase.tsv" sema-fail.c sema
expect_phase "$tmp/phase.tsv" cg-fail.c cg
expect_outcome "$tmp/phase.tsv" compile-exit124.c COMPILE_FAIL
expect_phase "$tmp/phase.tsv" compile-exit124.c cg
expect_outcome "$tmp/phase.tsv" compile-timeout.c TIMEOUT

large_probe_manifest=$tmp/large-probe.MANIFEST
printf 'execute/large-pp-cg-fail.c\t%s\trun\t-\t-\trun\t-\n' \
    "$(hash "$fixtures/execute/large-pp-cg-fail.c")" > "$large_probe_manifest"
run large-probe torture-execute "$large_probe_manifest" env \
    CGF_TORTURE_OUTPUT_LIMIT=4096
expect_outcome "$tmp/large-probe.tsv" large-pp-cg-fail.c COMPILE_FAIL
expect_phase "$tmp/large-probe.tsv" large-pp-cg-fail.c cg
large_probe_log=$(find "$tmp/large-probe.work/cases" -name phase-pp.stdout \
    -type f | sed -n '1p')
[ -n "$large_probe_log" ] && [ "$(wc -c < "$large_probe_log")" -le 2048 ] || {
    echo "torture_runner_test: large successful phase probe capture was unbounded" >&2
    exit 1
}

liveness_manifest=$tmp/liveness.MANIFEST
for rel in compile-ignore-term.c ignore-term.c spoof-marker.c; do
    printf 'execute/%s\t%s\trun\t-\t-\trun\t-\n' "$rel" \
        "$(hash "$fixtures/execute/$rel")"
done > "$liveness_manifest"
liveness_start=$(date +%s)
run liveness torture-execute "$liveness_manifest" env CGF_TORTURE_KILL_AFTER=1
liveness_end=$(date +%s)
liveness_elapsed=$((liveness_end - liveness_start))
[ "$liveness_elapsed" -le 20 ] || {
    echo "torture_runner_test: TERM-ignoring cases exceeded liveness bound (${liveness_elapsed}s)" >&2
    exit 1
}
expect_outcome "$tmp/liveness.tsv" compile-ignore-term.c TIMEOUT
expect_outcome "$tmp/liveness.tsv" ignore-term.c TIMEOUT
expect_outcome "$tmp/liveness.tsv" spoof-marker.c PASS

output_limit_manifest=$tmp/output-limit.MANIFEST
for rel in compile-output-flood.c output-flood.c large-binary.c \
    large-created-file.c pass.c; do
    printf 'execute/%s\t%s\trun\t-\t-\trun\t-\n' "$rel" \
        "$(hash "$fixtures/execute/$rel")"
done > "$output_limit_manifest"
run output-limit torture-execute "$output_limit_manifest" env \
    CGF_TORTURE_OUTPUT_LIMIT=4096
expect_outcome "$tmp/output-limit.tsv" compile-output-flood.c COMPILE_FAIL
expect_outcome "$tmp/output-limit.tsv" output-flood.c OUTPUT_FAIL
expect_outcome "$tmp/output-limit.tsv" large-binary.c PASS
expect_outcome "$tmp/output-limit.tsv" large-created-file.c PASS
expect_outcome "$tmp/output-limit.tsv" pass.c PASS
awk -F "$tab" '$3 == "compile-output-flood.c" && $10 == "output limit exceeded" { compile=1 }
    $3 == "output-flood.c" && $10 == "output limit exceeded" { run=1 }
    END { exit !(compile && run) }' "$tmp/output-limit.tsv"
for output_case_dir in "$tmp/output-limit.work/cases"/*; do
    output_bytes=0
    for output_log in "$output_case_dir"/*.stdout "$output_case_dir"/*.stderr; do
        [ -f "$output_log" ] || continue
        output_bytes=$((output_bytes + $(wc -c < "$output_log")))
    done
    [ "$output_bytes" -le 4096 ] || {
        echo "torture_runner_test: captured output exceeded cap in $output_case_dir" >&2
        exit 1
    }
done
large_binary=$(find "$tmp/output-limit.work/cases" -path '*large-binary.c*' \
    -name program -type f | sed -n '1p')
[ -n "$large_binary" ] && [ "$(wc -c < "$large_binary")" -gt 4096 ] || {
    echo "torture_runner_test: compiler artifact was truncated by log cap" >&2
    exit 1
}
large_created=$(find "$tmp/output-limit.work/cases" -path '*large-created-file.c*' \
    -name created-large.bin -type f | sed -n '1p')
[ -n "$large_created" ] && [ "$(wc -c < "$large_created")" -gt 4096 ] || {
    echo "torture_runner_test: program-created file was truncated by log cap" >&2
    exit 1
}

fingerprint_manifest=$tmp/fingerprint.MANIFEST
for rel in identifier-alpha.c identifier-beta.c punct-semicolon.c punct-rparen.c source-error-line.c; do
    printf 'execute/%s\t%s\trun\t-\t-\trun\t-\n' "$rel" \
        "$(hash "$fixtures/execute/$rel")"
done > "$fingerprint_manifest"
run fingerprints torture-execute "$fingerprint_manifest"
alpha_fp=$(awk -F "$tab" '$3 == "identifier-alpha.c" {print $8}' "$tmp/fingerprints.tsv")
beta_fp=$(awk -F "$tab" '$3 == "identifier-beta.c" {print $8}' "$tmp/fingerprints.tsv")
[ "$alpha_fp" = "$beta_fp" ] || {
    echo "torture_runner_test: quoted identifier spellings did not converge" >&2
    exit 1
}
semicolon_fp=$(awk -F "$tab" '$3 == "punct-semicolon.c" {print $8}' "$tmp/fingerprints.tsv")
rparen_fp=$(awk -F "$tab" '$3 == "punct-rparen.c" {print $8}' "$tmp/fingerprints.tsv")
[ "$semicolon_fp" != "$rparen_fp" ] || {
    echo "torture_runner_test: distinct quoted punctuation diagnostics merged" >&2
    exit 1
}
awk -F "$tab" '$3 == "punct-semicolon.c" && $10 ~ /'\'';'\''/ { semi=1 }
    $3 == "punct-rparen.c" && $10 ~ /'\'')'\''/ { rparen=1 }
    END { exit !(semi && rparen) }' "$tmp/fingerprints.tsv"
source_error_detail=$(awk -F "$tab" '$3 == "source-error-line.c" {print $10}' "$tmp/fingerprints.tsv")
case $source_error_detail in
*"expected ';'"*) ;;
*)
    echo "torture_runner_test: rendered source line displaced the structured diagnostic" >&2
    exit 1
    ;;
esac
case $source_error_detail in
*rendered_source_only*)
    echo "torture_runner_test: source text containing error was fingerprinted" >&2
    exit 1
    ;;
esac

link_manifest=$tmp/link-classes.MANIFEST
for rel in link-tentative.c link-support.c link-deliberate.c link-undefined.c \
    link-undefined-alt.c link-libm.c link-alloca.c; do
    printf 'execute/%s\t%s\trun\t-\t-\trun\t-\n' "$rel" \
        "$(hash "$fixtures/execute/$rel")"
done > "$link_manifest"
run link-classes torture-execute "$link_manifest"
link_fingerprints=
for mapping in \
    link-tentative.c:tentative-global \
    link-support.c:missing-entrypoint-support \
    link-deliberate.c:deliberate-link-error \
    link-undefined.c:undefined-symbol \
    link-libm.c:missing-libm \
    link-alloca.c:missing-alloca; do
    link_file=${mapping%%:*}
    link_class=${mapping#*:}
    expect_outcome "$tmp/link-classes.tsv" "$link_file" COMPILE_FAIL
    expect_phase "$tmp/link-classes.tsv" "$link_file" ld
    link_row=$(awk -F "$tab" -v file="$link_file" '$3 == file {print $8 "\t" $10}' \
        "$tmp/link-classes.tsv")
    link_fp=${link_row%%"$tab"*}
    link_detail=${link_row#*"$tab"}
    case $link_detail in
    "[ld-class=$link_class] "*) ;;
    *)
        echo "torture_runner_test: missing linker class $link_class for $link_file" >&2
        exit 1
        ;;
    esac
    case $link_detail in
    *sibling_global* | *link_error*)
        echo "torture_runner_test: linker identifier leaked into normalized detail" >&2
        exit 1
        ;;
    esac
    [ "$(printf '%s\n' "$link_detail" | sha256sum | awk '{print $1}')" = "$link_fp" ] || {
        echo "torture_runner_test: linker class was absent from fingerprint for $link_file" >&2
        exit 1
    }
    case " $link_fingerprints " in
    *" $link_fp "*)
        echo "torture_runner_test: distinct linker semantic classes merged" >&2
        exit 1
        ;;
    esac
    link_fingerprints="$link_fingerprints $link_fp"
done
expect_outcome "$tmp/link-classes.tsv" link-undefined-alt.c COMPILE_FAIL
expect_phase "$tmp/link-classes.tsv" link-undefined-alt.c ld
undefined_fp=$(awk -F "$tab" '$3 == "link-undefined.c" { print $8 }' \
    "$tmp/link-classes.tsv")
undefined_alt_row=$(awk -F "$tab" '$3 == "link-undefined-alt.c" { print $8 "\t" $10 }' \
    "$tmp/link-classes.tsv")
undefined_alt_fp=${undefined_alt_row%%"$tab"*}
undefined_alt_detail=${undefined_alt_row#*"$tab"}
[ "$undefined_fp" = "$undefined_alt_fp" ] || {
    echo 'torture_runner_test: real-ld quoting or section offsets split one linker class' >&2
    exit 1
}
case $undefined_alt_detail in
*other_consumer* | *other_global* | *0x2a*)
    echo 'torture_runner_test: real-ld identifier or section offset leaked into normalized detail' >&2
    exit 1
    ;;
esac

class_manifest=$tmp/classes.MANIFEST
for rel in class-gcc-builtin.c class-gcc-builtin-2.c class-implemented-builtin.c class-nested.c class-complex.c \
    class-computed.c class-asm-goto.c class-vector.c; do
    printf 'execute/%s\t%s\trun\t-\t-\trun\t-\n' "$rel" \
        "$(hash "$fixtures/execute/$rel")"
done > "$class_manifest"
run classes torture-execute "$class_manifest"
for mapping in \
    class-gcc-builtin.c:gcc-builtin \
    class-gcc-builtin-2.c:gcc-builtin \
    class-nested.c:nested-functions \
    class-complex.c:complex \
    class-computed.c:computed-goto \
    class-asm-goto.c:asm-goto \
    class-vector.c:vector-mode-attribute; do
    class_file=${mapping%%:*}
    class_token=${mapping#*:}
    awk -F "$tab" -v file="$class_file" -v prefix="[class=$class_token]" \
        '$3 == file && index($10, prefix) == 1 { found=1; print $8 "\t" $10 } END { exit !found }' \
        "$tmp/classes.tsv" > "$tmp/class-row"
    class_fp=$(cut -f1 "$tmp/class-row")
    class_detail=$(cut -f2- "$tmp/class-row")
    class_diagnostic=${class_detail#*] }
    [ "$(printf '%s\n' "$class_diagnostic" | sha256sum | awk '{print $1}')" = "$class_fp" ] || {
        echo "torture_runner_test: class metadata contaminated fingerprint for $class_file" >&2
        exit 1
    }
done
implemented_detail=$(awk -F "$tab" '$3 == "class-implemented-builtin.c" {print $10}' "$tmp/classes.tsv")
case $implemented_detail in
*'[class='*)
    echo "torture_runner_test: implemented builtin diagnostic was pretriaged as refused" >&2
    exit 1
    ;;
esac
printf '%s\n' "$implemented_detail" | grep -F '__builtin_va_arg' >/dev/null || {
    # The identifier is intentionally sanitized, but the lack of class metadata
    # remains the contract; this branch documents that sanitization is expected.
    printf '%s\n' "$implemented_detail" | grep -F '<id>' >/dev/null
}

exit_manifest=$tmp/exit-and-cwd.MANIFEST
for rel in exit124.c timeout.c cwd.c; do
    printf 'execute/%s\t%s\trun\t-\t-\trun\t-\n' "$rel" \
        "$(hash "$fixtures/execute/$rel")"
done > "$exit_manifest"
run exit-and-cwd torture-execute "$exit_manifest"
expect_outcome "$tmp/exit-and-cwd.tsv" exit124.c WRONG_EXIT
expect_outcome "$tmp/exit-and-cwd.tsv" timeout.c TIMEOUT
[ ! -e "$cwd_sentinel" ] || {
    echo "torture_runner_test: program polluted repository cwd" >&2
    exit 1
}
find "$tmp/exit-and-cwd.work/cases" -name .cgf-torture-runner-cwd-fixture \
    -type f | grep . >/dev/null || {
    echo "torture_runner_test: program did not run in its case directory" >&2
    exit 1
}

# A diagnostic from a sibling header must not retain its checkout root.
for root_name in sibling-a sibling-b; do
    mkdir -p "$tmp/$root_name/execute"
    cp "$fixtures/execute/sibling-path.c" "$tmp/$root_name/execute/sibling-path.c"
    printf 'execute/sibling-path.c\t%s\trun\t-\t-\trun\t-\n' \
        "$(hash "$fixtures/execute/sibling-path.c")" > "$tmp/$root_name/MANIFEST"
    run "$root_name" torture-execute "$tmp/$root_name/MANIFEST"
done
cmp "$tmp/sibling-a.tsv" "$tmp/sibling-b.tsv" || {
    echo "torture_runner_test: sibling diagnostic retained its checkout root" >&2
    diff -u "$tmp/sibling-a.tsv" "$tmp/sibling-b.tsv" >&2 || true
    exit 1
}

ct_manifest=$tmp/ctestsuite.MANIFEST
for rel in output-pass.c output-fail.c output-extra-blank.c expected.txt; do
    printf 'file\t%s\t%s\n' "$rel" "$(hash "$tmp/$rel")"
done > "$ct_manifest"
{
    printf 'case\toutput-pass.c\texpected.txt\t-\trun\t-\n'
    printf 'case\toutput-fail.c\texpected.txt\tgnu,math\trun\t-\n'
    printf 'case\toutput-extra-blank.c\texpected.txt\t-\trun\t-\n'
} >> "$ct_manifest"
run ctestsuite ctestsuite "$ct_manifest"
expect_outcome "$tmp/ctestsuite.tsv" output-pass.c PASS
expect_outcome "$tmp/ctestsuite.tsv" output-fail.c OUTPUT_FAIL
expect_outcome "$tmp/ctestsuite.tsv" output-extra-blank.c OUTPUT_FAIL
awk -F "$tab" '$3 == "output-fail.c" && $10 == "[tags=gnu,math] stdout differs from expected output" { found=1 }
    END { exit !found }' "$tmp/ctestsuite.tsv"
output_fail_fp=$(awk -F "$tab" '$3 == "output-fail.c" {print $8}' "$tmp/ctestsuite.tsv")
output_blank_fp=$(awk -F "$tab" '$3 == "output-extra-blank.c" {print $8}' "$tmp/ctestsuite.tsv")
[ "$output_fail_fp" != - ] && [ "$output_blank_fp" != - ] && \
    [ "$output_fail_fp" != "$output_blank_fp" ] || {
    echo "torture_runner_test: distinct stdout mismatches collapsed" >&2
    exit 1
}
run ctestsuite-repeat ctestsuite "$ct_manifest"
cmp "$tmp/ctestsuite.tsv" "$tmp/ctestsuite-repeat.tsv" || {
    echo "torture_runner_test: stdout mismatch fingerprints were not stable" >&2
    diff -u "$tmp/ctestsuite.tsv" "$tmp/ctestsuite-repeat.tsv" >&2 || true
    exit 1
}

if sed -n '3,$p' "$execute" "$tmp/compile.tsv" "$tmp/ctestsuite.tsv" |
    awk -F "$tab" '$1 !~ /^#/ && $9 !~ /^(pp|parse|sema|ir-verify|opt|cg|as|ld|run|ICE|policy)$/ { exit 1 }'; then
    :
else
    echo "torture_runner_test: result used an unlocked phase value" >&2
    exit 1
fi

run wrapper torture-execute "$torture_manifest" env \
    CGF_TORTURE_RUN="$fixtures/wrapper-unavailable.sh"
expect_outcome "$tmp/wrapper.tsv" pass.c SKIP
awk -F "$tab" '$3 == "pass.c" && $9 == "policy" && $10 == "execution wrapper unavailable" { found=1 }
    END { exit !found }' "$tmp/wrapper.tsv"

status=0
FAKE_TARGET=arm64-linux CGF_TORTURE_TIMEOUT=1 "$runner" --cc "$fake_cc" \
    --suite torture-execute --level O2 --target x86_64-linux-gnu \
    --manifest "$torture_manifest" --output "$tmp/mismatch.tsv" \
    --work "$tmp/mismatch.work" >"$tmp/mismatch.out" 2>"$tmp/mismatch.err" || status=$?
if [ "$status" -ne 3 ] ||
    ! grep -F 'compiler target mismatch' "$tmp/mismatch.err" >/dev/null; then
    echo "torture_runner_test: target mismatch was not rejected" >&2
    exit 1
fi

unsafe=$tmp/unsafe.MANIFEST
printf 'compile/compile-pass.c\t%s\tcompile\t-\t-fwrapv;touch\tpass\t-\n' \
    "$(hash "$fixtures/compile/compile-pass.c")" > "$unsafe"
status=0
CGF_TORTURE_TIMEOUT=1 "$runner" --cc "$fake_cc" --suite torture-compile \
    --level O0 --target x86_64-linux-gnu --manifest "$unsafe" \
    --output "$tmp/unsafe.tsv" --work "$tmp/unsafe.work" \
    >"$tmp/unsafe.out" 2>"$tmp/unsafe.err" || status=$?
if [ "$status" -ne 3 ] ||
    ! grep -F 'unsafe or unsupported flags' "$tmp/unsafe.err" >/dev/null; then
    echo "torture_runner_test: unsafe flags were not rejected" >&2
    exit 1
fi

echo 'torture_runner_test: classification, safety, ordering, and determinism passed'
