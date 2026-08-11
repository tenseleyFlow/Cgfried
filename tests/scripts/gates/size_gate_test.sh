#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
gate=$repo/scripts/size_gate.sh
fixtures=$repo/tests/scripts/gates/fixtures/size
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-size-gate-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

pass_case()
{
    case_name=$1
    shift
    if ! "$@" >"$tmp/$case_name.out" 2>"$tmp/$case_name.err"; then
        echo "size_gate_test: expected pass: $case_name" >&2
        sed 's/^/  /' "$tmp/$case_name.err" >&2
        exit 1
    fi
}

fail_case()
{
    case_name=$1
    expected_status=$2
    expected_text=$3
    shift 3
    status=0
    "$@" >"$tmp/$case_name.out" 2>"$tmp/$case_name.err" || status=$?
    if [ "$status" -ne "$expected_status" ]; then
        echo "size_gate_test: $case_name exited $status, expected $expected_status" >&2
        sed 's/^/  /' "$tmp/$case_name.err" >&2
        exit 1
    fi
    if ! grep -F "$expected_text" "$tmp/$case_name.err" >/dev/null; then
        echo "size_gate_test: $case_name lacked diagnostic: $expected_text" >&2
        sed 's/^/  /' "$tmp/$case_name.err" >&2
        exit 1
    fi
}

pass_case pass-14 "$gate" --gate "$fixtures/baseline.txt" \
    "$fixtures/pass-14.txt"
pass_case pass-15 "$gate" --gate "$fixtures/baseline.txt" \
    "$fixtures/pass-15.txt"
pass_case same-file "$gate" --gate "$fixtures/baseline.txt" \
    "$fixtures/baseline.txt"
fail_case fail-16 1 "cgf.size regressed" "$gate" --gate \
    "$fixtures/baseline.txt" "$fixtures/fail-16.txt"
pass_case self-filter-ignores-program env CGF_SIZE_GATE_KIND=self "$gate" \
    --gate "$fixtures/baseline.txt" "$fixtures/fail-program-only.txt"
pass_case program-filter-ignores-self env CGF_SIZE_GATE_KIND=program "$gate" \
    --gate "$fixtures/baseline.txt" "$fixtures/fail-self-only.txt"
fail_case self-filter 1 "cgf.size regressed" env CGF_SIZE_GATE_KIND=self \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/fail-self-only.txt"
fail_case program-filter 1 "alpha.O2.size regressed" env \
    CGF_SIZE_GATE_KIND=program "$gate" --gate "$fixtures/baseline.txt" \
    "$fixtures/fail-program-only.txt"
fail_case missing 3 "missing result metric alpha.Os.rodata" "$gate" --gate \
    "$fixtures/baseline.txt" "$fixtures/missing.txt"
fail_case extra 3 "unexpected result metric extra.O2.size" "$gate" --gate \
    "$fixtures/baseline.txt" "$fixtures/extra.txt"
fail_case duplicate 3 "duplicate metric cgf.size" "$gate" --gate \
    "$fixtures/baseline.txt" "$fixtures/duplicate.txt"
fail_case malformed 3 "must be a non-negative integer" "$gate" --gate \
    "$fixtures/baseline.txt" "$fixtures/malformed.txt"
fail_case incomplete 3 "baseline entry alpha.O2 lacks .rodata" "$gate" --gate \
    "$fixtures/incomplete-baseline.txt" "$fixtures/baseline.txt"
fail_case unreadable 3 "cannot read" "$gate" --gate \
    "$fixtures/does-not-exist.txt" "$fixtures/baseline.txt"
fail_case read-error 3 "cannot read" "$gate" --gate \
    "$fixtures" "$fixtures/baseline.txt"

sed 's/^# target=.*/# target=other-target/' "$fixtures/pass-15.txt" \
    >"$tmp/mismatch-target.txt"
fail_case mismatch-target 3 "provenance header target differs" "$gate" \
    --gate "$fixtures/baseline.txt" "$tmp/mismatch-target.txt"
sed 's/^# corpus=.*/# corpus=other-corpus/' "$fixtures/pass-15.txt" \
    >"$tmp/mismatch-corpus.txt"
fail_case mismatch-corpus 3 "provenance header corpus differs" "$gate" \
    --gate "$fixtures/baseline.txt" "$tmp/mismatch-corpus.txt"
sed 's/^# corpus_count=.*/# corpus_count=2/' "$fixtures/pass-15.txt" \
    >"$tmp/mismatch-corpus-count.txt"
fail_case mismatch-corpus-count 3 "provenance header corpus_count differs" \
    "$gate" --gate "$fixtures/baseline.txt" \
    "$tmp/mismatch-corpus-count.txt"
sed 's/^# protocol=.*/# protocol=other-protocol/' "$fixtures/pass-15.txt" \
    >"$tmp/mismatch-protocol.txt"
fail_case mismatch-protocol 3 "provenance header protocol differs" "$gate" \
    --gate "$fixtures/baseline.txt" "$tmp/mismatch-protocol.txt"
sed 's/^# compiler_flags=.*/# compiler_flags=-fother/' \
    "$fixtures/pass-15.txt" >"$tmp/mismatch-compiler-flags.txt"
fail_case mismatch-compiler-flags 3 \
    "provenance header compiler_flags differs" "$gate" --gate \
    "$fixtures/baseline.txt" "$tmp/mismatch-compiler-flags.txt"
sed '/^# protocol=/d' "$fixtures/pass-15.txt" >"$tmp/missing-provenance.txt"
fail_case missing-provenance 3 "result lacks provenance header protocol" \
    "$gate" --gate "$fixtures/baseline.txt" "$tmp/missing-provenance.txt"
{
    sed -n '1,$p' "$fixtures/pass-15.txt"
    echo '# target=fixture-target'
} >"$tmp/duplicate-provenance.txt"
fail_case duplicate-provenance 3 "duplicate provenance header target" \
    "$gate" --gate "$fixtures/baseline.txt" "$tmp/duplicate-provenance.txt"
sed 's/^# corpus_count=.*/# corpus_count=0/' "$fixtures/pass-15.txt" \
    >"$tmp/malformed-provenance.txt"
fail_case malformed-provenance 3 "corpus_count must be a positive integer" \
    "$gate" --gate "$fixtures/baseline.txt" "$tmp/malformed-provenance.txt"

measure()
{
    target=$1
    output=$2
    CGF_SIZE_KERNEL_DIR=$fixtures/kernels \
    CGF_SIZE_MIN_KERNELS=2 \
    CGF_SIZE_WORK=$tmp/work-$target \
    CGF_SIZE_SIZE=$fixtures/fake-size.sh \
    CGF_SIZE_STRIP=$fixtures/fake-strip.sh \
    CGF_SIZE_SELF_SIZE=$fixtures/fake-size.sh \
    CGF_SIZE_SELF_STRIP=$fixtures/fake-strip.sh \
    CGF_SIZE_AS=$fixtures/fake-size.sh \
    CGF_SIZE_LD=$fixtures/fake-size.sh \
    CGF_SIZE_SYSROOT='' \
    CGF_SIZE_HOST=fixture-host \
    CGF_SIZE_HOST_ARCH=fixture-arch \
    CGF_SIZE_HOST_CLASS=fixture-ci \
    CGF_SIZE_DATE_UTC=2000-01-02T03:04:05Z \
    CGF_SIZE_REV=0123456789abcdef \
    CGF_SIZE_TREE_STATE=fixture-clean \
    CGF_SIZE_COMPILER_ID='fixture-cgf 1.0' \
    CGF_SIZE_CORPUS_ID=fixture-kernels-v1 \
        "$gate" --measure "$fixtures/fake-compiler.sh" "$target" "$output"
}

pass_case measure-x86 measure x86_64-linux-gnu "$tmp/x86-first.txt"
pass_case measure-x86-repeat measure x86_64-linux-gnu "$tmp/x86-second.txt"
cmp "$tmp/x86-first.txt" "$tmp/x86-second.txt" || {
    echo "size_gate_test: repeated measurement was not deterministic" >&2
    exit 1
}
{
    echo '# cgfried binary size metrics v1'
    echo '# target=x86_64-linux-gnu'
    echo '# host=fixture-host'
    echo '# host_arch=fixture-arch'
    echo '# host_class=fixture-ci'
    echo '# date=2000-01-02T03:04:05Z'
    echo '# cgf_rev=0123456789abcdef'
    echo '# cgf_tree=fixture-clean'
    echo "# compiler_path=$fixtures/fake-compiler.sh"
    echo '# compiler_id=fixture-cgf 1.0'
    echo '# compiler_runner=none'
    echo "# size_tool=$fixtures/fake-size.sh"
    echo "# strip_tool=$fixtures/fake-strip.sh"
    echo "# self_size_tool=$fixtures/fake-size.sh"
    echo "# self_strip_tool=$fixtures/fake-strip.sh"
    echo "# as_tool=$fixtures/fake-size.sh"
    echo "# ld_tool=$fixtures/fake-size.sh"
    echo '# sysroot=none'
    echo '# corpus=fixture-kernels-v1'
    echo '# corpus_count=2'
    echo '# size_flags=-A -d'
    echo '# strip_flags=--strip-all'
    echo '# compiler_flags=none'
    echo '# protocol=opts=O2,Os;whole-file-after-strip-gate=+15%;unstripped-and-sections=report-only'
} >"$tmp/expected-header.txt"
sed -n '/^[^#]/q;p' "$tmp/x86-first.txt" >"$tmp/actual-header.txt"
cmp "$tmp/expected-header.txt" "$tmp/actual-header.txt" || {
    echo "size_gate_test: provenance header differed" >&2
    diff -u "$tmp/expected-header.txt" "$tmp/actual-header.txt" >&2 || true
    exit 1
}
pass_case measure-arm measure arm64-linux "$tmp/arm.txt"
sed '/^# target=/d' "$tmp/x86-first.txt" >"$tmp/x86-targetless.txt"
sed '/^# target=/d' "$tmp/arm.txt" >"$tmp/arm-targetless.txt"
cmp "$tmp/x86-targetless.txt" "$tmp/arm-targetless.txt" || {
    echo "size_gate_test: configurable target fixture metrics differed" >&2
    exit 1
}

expected_keys='cgf.size
cgf.size_unstripped
cgf.text
cgf.data
cgf.rodata
alpha.O2.size
alpha.O2.size_unstripped
alpha.O2.text
alpha.O2.data
alpha.O2.rodata
alpha.Os.size
alpha.Os.size_unstripped
alpha.Os.text
alpha.Os.data
alpha.Os.rodata
beta.O2.size
beta.O2.size_unstripped
beta.O2.text
beta.O2.data
beta.O2.rodata
beta.Os.size
beta.Os.size_unstripped
beta.Os.text
beta.Os.data
beta.Os.rodata'
printf '%s\n' "$expected_keys" >"$tmp/expected-keys.txt"
sed -n '/^[^#]/s/=.*//p' "$tmp/x86-first.txt" >"$tmp/actual-keys.txt"
cmp "$tmp/expected-keys.txt" "$tmp/actual-keys.txt" || {
    echo "size_gate_test: measurement key set or order differed" >&2
    exit 1
}
if grep -v '^#' "$tmp/x86-first.txt" |
    grep -Ev '^[A-Za-z0-9_.-]+=[0-9]+$'; then
    echo "size_gate_test: measurement was not flat integer key=value data" >&2
    exit 1
fi

echo "size_gate_test: 26 gate/measurement cases passed"
