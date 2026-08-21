#!/bin/sh
# Meta-tests for Sprint 58's determinism audit and divergence localizer.
set -eu
LC_ALL=C
export LC_ALL

repo=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bootstrap-meta.XXXXXX")
cross_source=build/s58-bootstrap-cross-native.$$
cross_outputs=build/boot/s58-bootstrap-cross-meta.$$
repro_outputs=build/boot/s58-bootstrap-repro-meta.$$
phase_dump_pid_a=
phase_dump_pid_b=

cleanup() {
    cleanup_status=$?
    trap - EXIT HUP INT TERM
    for cleanup_pid in "$phase_dump_pid_a" "$phase_dump_pid_b"; do
        [ -n "$cleanup_pid" ] || continue
        kill "$cleanup_pid" 2>/dev/null || :
        wait "$cleanup_pid" 2>/dev/null || :
    done
    rm -rf "$tmp" "$repo/${cross_source:?}" "$repo/${cross_outputs:?}" \
        "$repo/${repro_outputs:?}"*
    exit "$cleanup_status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() {
    echo "bootstrap meta FAIL: $*" >&2
    exit 1
}

cgf=${CGF_TEST_CC:-build/cgfried}
case $cgf in
/*) ;;
*) cgf=$repo/$cgf ;;
esac
[ -x "$cgf" ] || fail "Cgfried is unavailable at $cgf"

# DET-M-02: pin both halves of the sampling contract. Repetition without a
# forced rebuild would time two no-op make invocations after the first sample.
grep -Eq '"\$stage0/timeit" -n 3 -w 0 ' "$repo/scripts/bootstrap.sh" ||
    fail "bootstrap timing does not request three measured samples"
grep -Eq '"\$make_cmd" -B -s --no-print-directory ' \
    "$repo/scripts/bootstrap.sh" ||
    fail "bootstrap timing samples are not forced rebuilds"

expect_failure() {
    output=$1
    shift
    if "$@" >"$output" 2>&1; then
        fail "command unexpectedly passed: $*"
    fi
}

expect_exact_line() {
    expected=$1
    actual=$2
    printf '%s\n' "$expected" >"$tmp/expected"
    cmp "$tmp/expected" "$actual" || fail "output was not exact: $expected"
}

expect_status() {
    expected=$1
    output=$2
    shift 2
    status=0
    "$@" >"$output" 2>&1 || status=$?
    [ "$status" -eq "$expected" ] || {
        cat "$output" >&2
        fail "expected status $expected, got $status: $*"
    }
}

write_phase_manifest() {
    phase_manifest_dir=$1
    phase_manifest_output=$2
    (
        CDPATH='' cd "$phase_manifest_dir"
        find . -type f -print | sed 's#^\./##' | sort
    ) >"$phase_manifest_output"
}

compare_phase_trees() {
    phase_reference=$1
    phase_actual=$2
    phase_label=$3
    phase_reference_manifest=$4
    phase_actual_manifest=$5

    write_phase_manifest "$phase_reference" "$phase_reference_manifest"
    write_phase_manifest "$phase_actual" "$phase_actual_manifest"
    cmp "$phase_reference_manifest" "$phase_actual_manifest" ||
        fail "$phase_label phase manifests differ"
    while IFS= read -r phase_dump; do
        cmp "$phase_reference/$phase_dump" "$phase_actual/$phase_dump" ||
            fail "$phase_label differs at phase $phase_dump"
    done <"$phase_reference_manifest"
}

fresh_stages() {
    rm -rf "$tmp/stage1" "$tmp/stage2"
    mkdir -p "$tmp/stage1" "$tmp/stage2"
}

put_same_required_artifacts() {
    for stage in stage1 stage2; do
        mkdir -p "$tmp/$stage/obj" "$tmp/$stage/asm"
        printf 'same object\n' >"$tmp/$stage/obj/common.o"
        printf 'same assembly\n' >"$tmp/$stage/asm/common.s"
        printf 'same compiler\n' >"$tmp/$stage/cgfried"
        printf 'same runtime\n' >"$tmp/$stage/libcgf_rt.a"
    done
}

test_audit_detects_every_seeded_sin_exactly() {
    output=$tmp/audit.out
    expect_failure "$output" sh "$repo/scripts/audit-determinism.sh" \
        "$repo/tests/bootstrap/faults"
    cat >"$tmp/audit.expected" <<'EOF'
DETERMINISM_AUDIT fwrite-struct padding_write.c:7
DETERMINISM_AUDIT fwrite-struct seeded_sins.c:9
DETERMINISM_AUDIT memcmp-object padding_compare.c:9
DETERMINISM_AUDIT percent-p pointer_format.c:5
DETERMINISM_AUDIT percent-p seeded_sins.c:7
DETERMINISM_AUDIT pointer-output seeded_sins.c:8
DETERMINISM_AUDIT qsort seeded_sins.c:6
DETERMINISM_AUDIT random seeded_sins.c:10
DETERMINISM_AUDIT readdir seeded_sins.c:12
DETERMINISM_AUDIT readdir unsorted_readdir.c:5
DETERMINISM_AUDIT strcoll seeded_sins.c:11
audit-determinism: 11 finding(s)
EOF
    cmp "$tmp/audit.expected" "$output" || fail "audit detection was not exact"
}

test_empty_stages_fail_closed() {
    fresh_stages
    output=$tmp/empty.out
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2"
    expect_exact_line \
        'bisect-nondet: no object artifacts found in either stage' "$output"
}

test_missing_object_names_stage_and_tu_exactly() {
    fresh_stages
    mkdir -p "$tmp/stage1/obj" "$tmp/stage2/obj"
    printf 'same\n' >"$tmp/stage1/obj/a.o"
    printf 'same\n' >"$tmp/stage2/obj/a.o"
    printf 'only stage1\n' >"$tmp/stage1/obj/b.o"
    output=$tmp/missing-object.out
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2"
    expect_exact_line \
        'bisect-nondet: first missing object: stage2: obj/b.o' "$output"
}

test_missing_assembly_group_fails_closed() {
    fresh_stages
    mkdir -p "$tmp/stage1/obj" "$tmp/stage2/obj" "$tmp/stage1/asm"
    printf 'same\n' >"$tmp/stage1/obj/a.o"
    printf 'same\n' >"$tmp/stage2/obj/a.o"
    printf 'only stage1\n' >"$tmp/stage1/asm/a.s"
    output=$tmp/missing-assembly.out
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2"
    expect_exact_line \
        'bisect-nondet: first missing assembly: stage2: asm/a.s' "$output"
}

test_empty_assembly_group_fails_closed() {
    fresh_stages
    mkdir -p "$tmp/stage1/obj" "$tmp/stage2/obj"
    printf 'same\n' >"$tmp/stage1/obj/a.o"
    printf 'same\n' >"$tmp/stage2/obj/a.o"
    output=$tmp/empty-assembly.out
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2"
    expect_exact_line \
        'bisect-nondet: no assembly artifacts found in either stage' "$output"
}

test_missing_runtime_group_fails_closed() {
    fresh_stages
    mkdir -p "$tmp/stage1/obj" "$tmp/stage2/obj" \
        "$tmp/stage1/asm" "$tmp/stage2/asm"
    printf 'same\n' >"$tmp/stage1/obj/a.o"
    printf 'same\n' >"$tmp/stage2/obj/a.o"
    printf 'same\n' >"$tmp/stage1/asm/a.s"
    printf 'same\n' >"$tmp/stage2/asm/a.s"
    printf 'only stage1\n' >"$tmp/stage1/cgfried"
    output=$tmp/missing-runtime.out
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2"
    expect_exact_line \
        'bisect-nondet: first missing runtime: stage2: cgfried' "$output"
}

test_empty_runtime_group_fails_closed() {
    fresh_stages
    mkdir -p "$tmp/stage1/obj" "$tmp/stage2/obj" \
        "$tmp/stage1/asm" "$tmp/stage2/asm"
    printf 'same\n' >"$tmp/stage1/obj/a.o"
    printf 'same\n' >"$tmp/stage2/obj/a.o"
    printf 'same\n' >"$tmp/stage1/asm/a.s"
    printf 'same\n' >"$tmp/stage2/asm/a.s"
    output=$tmp/empty-runtime.out
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2"
    expect_exact_line \
        'bisect-nondet: no runtime artifacts found in either stage' "$output"
}

test_identical_complete_stages_pass() {
    fresh_stages
    put_same_required_artifacts
    output=$tmp/identical.out
    if ! sh "$repo/scripts/bisect-nondet.sh" "$tmp/stage1" "$tmp/stage2" \
        >"$output" 2>&1; then
        fail "complete identical stages failed"
    fi
    expect_exact_line \
        'bisect-nondet: 1 object(s), 1 assembly file(s), and 2 runtime artifact(s) match' \
        "$output"
}

test_real_compiler_emits_ordered_phase_tree() {
    phase_root=$tmp/real-phase-tree-1
    phase_root_again=$tmp/real-phase-tree-2
    source=$tmp/real-phase-tree.c
    output=$tmp/real-phase-tree.out
    manifest=$tmp/real-phase-tree-1.manifest
    manifest_again=$tmp/real-phase-tree-2.manifest
    optimizer_manifest=$tmp/real-phase-tree-optimizer.manifest

    mkdir "$phase_root" "$phase_root_again"
    printf '%s\n' 'int square(int x) { return x * x; }' \
        'int main(void) { return square(3) != 9; }' >"$source"
    CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$phase_root" \
        "$cgf" -O2 -S "$source" -o "$tmp/real-phase-tree.s"
    CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$phase_root_again" \
        "$cgf" -O2 -S "$source" -o "$tmp/real-phase-tree-again.s"
    compare_phase_trees "$phase_root" "$phase_root_again" \
        "identical O2 compiles" "$manifest" "$manifest_again"

    "$cgf" --dump-ast "$source" >"$tmp/public-ast.txt"
    "$cgf" -fdump-sema "$source" >"$tmp/public-sema.txt"
    cmp "$phase_root/100000-parse-ast.txt" "$tmp/public-ast.txt" ||
        fail "in-tree parse dump differs from the public AST view"
    cmp "$phase_root/200000-sema.txt" "$tmp/public-sema.txt" ||
        fail "in-tree sema dump differs from the public sema view"
    [ -s "$phase_root/300000-ir-post-lowering.cgfir" ] ||
        fail "real compiler omitted the post-lowering dump"
    find "$phase_root" -maxdepth 1 -type f \
        -name '[4-6][0-9][0-9][0-9][0-9][0-9]-ir-fp*-i*-p*-*.cgfir' -print |
        grep . >/dev/null || fail "real compiler omitted per-pass dumps"
    sed -n '/^400[0-9][0-9][0-9]-ir-fp.*\.cgfir$/p' "$manifest" \
        >"$optimizer_manifest"
    [ -s "$optimizer_manifest" ] ||
        fail "real compiler emitted an empty optimizer sequence"
    expected_sequence=400001
    while IFS= read -r dump; do
        sequence=${dump%%-*}
        [ "$sequence" -eq "$expected_sequence" ] ||
            fail "optimizer sequence is not contiguous: expected $expected_sequence, got $sequence"
        expected_sequence=$((expected_sequence + 1))
    done <"$optimizer_manifest"
    # O2 selects no fusion passes, but that empty manager group still consumes
    # fp02.  The fp03 anchor proves fixpoint identity remains global across
    # groups instead of silently restarting at each manager call.
    for anchor in \
        400002-ir-fp01-i01-p01-sccp.cgfir \
        400013-ir-fp01-i02-p01-sccp.cgfir \
        400024-ir-fp01-i03-p01-sccp.cgfir \
        400034-ir-fp03-i01-p00-licm.cgfir; do
        grep -Fx "$anchor" "$optimizer_manifest" >/dev/null ||
            fail "optimizer sequence omitted fixed anchor $anchor"
    done
    [ -s "$phase_root/700000-ir-post-opt-legalized.cgfir" ] ||
        fail "real compiler omitted the post-legalization dump"
    [ -s "$phase_root/800000-mir.txt" ] ||
        fail "real compiler omitted the MIR dump"
    cmp "$phase_root/900000-asm.s" "$tmp/real-phase-tree.s" ||
        fail "final assembly phase dump differs from -S output"

    expect_status 4 "$output" env CGF_DUMP_IR=all \
        CGF_DUMP_IR_DIR="$phase_root" "$cgf" -O2 -S \
        "$source" -o "$tmp/real-phase-tree-collision.s"
    grep -F 'cannot create phase dump' "$output" >/dev/null ||
        fail "phase dump collision did not fail clearly"
}

test_concurrent_real_compiler_phase_trees_are_isolated() {
    source_a=$tmp/concurrent-phase-a.c
    source_b=$tmp/concurrent-phase-b.c
    phase_a=$tmp/concurrent-phase-a
    phase_b=$tmp/concurrent-phase-b
    reference_a=$tmp/concurrent-reference-a
    reference_b=$tmp/concurrent-reference-b
    asm_a=$tmp/concurrent-phase-a.s
    asm_b=$tmp/concurrent-phase-b.s
    reference_asm_a=$tmp/concurrent-reference-a.s
    reference_asm_b=$tmp/concurrent-reference-b.s
    output_a=$tmp/concurrent-phase-a.out
    output_b=$tmp/concurrent-phase-b.out
    reference_manifest_a=$tmp/concurrent-reference-a.manifest
    reference_manifest_b=$tmp/concurrent-reference-b.manifest
    concurrent_manifest_a=$tmp/concurrent-phase-a.manifest
    concurrent_manifest_b=$tmp/concurrent-phase-b.manifest

    printf '%s\n' 'int add_three(int x) { return x + 3; }' \
        'int main(void) { return add_three(4) != 7; }' >"$source_a"
    printf '%s\n' 'int multiply_five(int x) { return x * 5; }' \
        'int main(void) { return multiply_five(3) != 15; }' >"$source_b"
    mkdir "$phase_a" "$phase_b" "$reference_a" "$reference_b"

    env CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$reference_a" \
        "$cgf" -O2 -S "$source_a" -o "$reference_asm_a"
    env CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$reference_b" \
        "$cgf" -O2 -S "$source_b" -o "$reference_asm_b"

    env CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$phase_a" \
        "$cgf" -O2 -S "$source_a" -o "$asm_a" >"$output_a" 2>&1 &
    phase_dump_pid_a=$!
    env CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$phase_b" \
        "$cgf" -O2 -S "$source_b" -o "$asm_b" >"$output_b" 2>&1 &
    phase_dump_pid_b=$!

    status_a=0
    wait "$phase_dump_pid_a" || status_a=$?
    phase_dump_pid_a=
    status_b=0
    wait "$phase_dump_pid_b" || status_b=$?
    phase_dump_pid_b=
    if [ "$status_a" -ne 0 ]; then
        cat "$output_a" >&2
        fail "first concurrent phase-dump compile exited $status_a"
    fi
    if [ "$status_b" -ne 0 ]; then
        cat "$output_b" >&2
        fail "second concurrent phase-dump compile exited $status_b"
    fi

    compare_phase_trees "$reference_a" "$phase_a" \
        "first concurrent compile" "$reference_manifest_a" \
        "$concurrent_manifest_a"
    compare_phase_trees "$reference_b" "$phase_b" \
        "second concurrent compile" "$reference_manifest_b" \
        "$concurrent_manifest_b"
    cmp "$phase_a/900000-asm.s" "$asm_a" ||
        fail "first concurrent final phase dump differs from -S output"
    cmp "$phase_b/900000-asm.s" "$asm_b" ||
        fail "second concurrent final phase dump differs from -S output"
    if cmp -s "$phase_a/100000-parse-ast.txt" \
        "$phase_b/100000-parse-ast.txt"; then
        fail "distinct concurrent translation units emitted identical parse dumps"
    fi
}

test_bisector_localizes_real_phase_tree() {
    source=$tmp/real-localize.c
    output=$tmp/real-localize.out
    wrapper=$repo/tests/bootstrap/helpers/phase-dump-wrapper.sh

    fresh_stages
    put_same_required_artifacts
    printf '%s\n' 'different object evidence' >"$tmp/stage2/obj/common.o"
    printf '%s\n' 'int twice(int x) { return x + x; }' \
        'int main(void) { return twice(4) != 8; }' >"$source"
    ln -s "$wrapper" "$tmp/phase-dump-good"
    ln -s "$wrapper" "$tmp/phase-dump-bad"
    CGF_PHASE_REAL_CC=$cgf
    export CGF_PHASE_REAL_CC
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2" --source "$source" \
        --stage1-cc "$tmp/phase-dump-good" \
        --stage2-cc "$tmp/phase-dump-bad" -- -O2
    grep -Fx 'bisect-nondet: first differing phase boundary: 400013-ir-fp01-i02-p01-sccp.cgfir' \
        "$output" >/dev/null ||
        fail "bisector did not localize the real compiler dump tree"
}

test_bisector_rejects_identically_incomplete_phase_trees() {
    source=$tmp/incomplete-phase-tree.c
    output=$tmp/incomplete-phase-tree.out
    wrapper=$repo/tests/bootstrap/helpers/phase-dump-wrapper.sh

    fresh_stages
    put_same_required_artifacts
    printf '%s\n' 'int main(void) { return 0; }' >"$source"
    ln -s "$wrapper" "$tmp/phase-dump-missing-optimizer"
    CGF_PHASE_REAL_CC=$cgf
    export CGF_PHASE_REAL_CC
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2" --source "$source" \
        --stage1-cc "$tmp/phase-dump-missing-optimizer" \
        --stage2-cc "$tmp/phase-dump-missing-optimizer" -- -O2
    grep -Fx 'bisect-nondet: missing phase group: stage1: optimizer' \
        "$output" >/dev/null ||
        fail "bisector accepted identically incomplete phase trees"
}

test_bisector_accepts_complete_o0_phase_trees_without_passes() {
    source=$tmp/complete-o0-phase-tree.c
    output=$tmp/complete-o0-phase-tree.out

    fresh_stages
    put_same_required_artifacts
    printf '%s\n' 'int main(void) { return 0; }' >"$source"
    if ! sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2" --source "$source" \
        --stage1-cc "$cgf" --stage2-cc "$cgf" -- -O0 \
        >"$output" 2>&1; then
        fail "bisector rejected a complete O0 tree with no pass invocations"
    fi
}

test_bootstrap_refuses_unowned_nonempty_work_directory() {
    unsafe_parent=$tmp/unowned-work
    unsafe_work=$unsafe_parent/O0
    mkdir -p "$unsafe_work"
    printf '%s\n' 'must survive' >"$unsafe_work/sentinel"
    expect_status 2 "$tmp/unowned-work.out" env \
        CGF_BOOTSTRAP_WORK="$unsafe_work" HOSTCC=/bin/true \
        sh "$repo/scripts/bootstrap.sh" O0
    grep -Fx 'must survive' "$unsafe_work/sentinel" >/dev/null ||
        fail "bootstrap modified an unowned work directory"
    grep -F 'refusing nonempty unowned work directory' \
        "$tmp/unowned-work.out" >/dev/null ||
        fail "bootstrap did not explain the unowned-work refusal"
}

test_control_capture_is_canonical_and_controlled() {
    control_root=$tmp/control
    governor_root=$control_root/governors
    proc_root=$control_root/proc
    mkdir -p "$governor_root/cpu0/cpufreq" \
        "$governor_root/cpu1/cpufreq" "$proc_root"
    for control_cpu in cpu0 cpu1; do
        printf '%s\n' powersave > \
            "$governor_root/$control_cpu/cpufreq/scaling_governor"
        printf '%s\n' intel_pstate > \
            "$governor_root/$control_cpu/cpufreq/scaling_driver"
        printf '%s\n' performance > \
            "$governor_root/$control_cpu/cpufreq/energy_performance_preference"
    done
    printf '%s\n' performance >"$control_root/power-profile"
    printf '%s\n' 'cpu  100 0 10 800 0 0 0 0 0 0' >"$proc_root/stat"
    printf '%s\n' 'cpu  105 0 15 890 0 0 0 0 0 0' > \
        "$control_root/stat.after"
    printf '%s\n' '2.80 2.00 1.00 1/100 1' >"$proc_root/loadavg"

    FIXTURE_SYSTEM=Linux FIXTURE_MACHINE=x86_64 \
    FIXTURE_POWER_PROFILE_STATE=$control_root/power-profile \
    FIXTURE_GOVERNOR_ROOT=$governor_root \
    CGF_BOOTSTRAP_UNAME_CMD=$repo/tests/scripts/gates/fixtures/fleet/fake-uname.sh \
    CGF_BOOTSTRAP_GOVERNOR_ROOT=$governor_root \
    CGF_BOOTSTRAP_POWER_PROFILE_CMD=$repo/tests/scripts/gates/fixtures/fleet/fake-powerprofilesctl.sh \
    CGF_BENCH_CONTROL_PROC_ROOT=$proc_root \
    CGF_BENCH_CONTROL_SLEEP_CMD=$repo/tests/bootstrap/helpers/fake-control-sleep.sh \
    CGF_BENCH_CONTROL_GETCONF_CMD=$repo/tests/bootstrap/helpers/fake-control-getconf.sh \
    CGF_BOOTSTRAP_TEST_PROC=$proc_root \
    CGF_BOOTSTRAP_TEST_STAT_AFTER=$control_root/stat.after \
        "$repo/scripts/bootstrap-control.sh" hasu \
        "$control_root/control.txt" >"$control_root/capture.out"

    cat >"$control_root/expected.txt" <<'EOF'
host=hasu
governor=powersave
power_profile=performance
scaling_driver=intel_pstate
energy_performance_preference=performance
control_protocol=fleet-control-v2
logical_cpus=20
cpu_idle_pct=90.00
load1=2.80
EOF
    cmp "$control_root/expected.txt" "$control_root/control.txt" ||
        fail "controlled-host capture was not canonical"
    [ "$(sh "$repo/scripts/bench-control.sh" classify --require-v2 \
        "$control_root/control.txt")" = controlled ] ||
        fail "captured host state was not classified as controlled"
}

write_timing_receipt() {
    timing_file=$1
    timing_host=$2
    timing_wall=$3
    timing_user=$4
    timing_sys=$5
    timing_idle=$6
    timing_cpu=$(awk -v user="$timing_user" -v sys="$timing_sys" \
        'BEGIN { print user + sys }')
    cat >"$timing_file" <<EOF
schema=cgfried.bootstrap-timing.v1
target=x86_64-linux-gnu
host=$timing_host
governor=powersave
power_profile=performance
scaling_driver=intel_pstate
energy_performance_preference=performance
control_protocol=fleet-control-v2
logical_cpus=20
cpu_idle_pct=$timing_idle
load1=0.20
date=2026-08-12T12:00:00Z
cgf_rev=0123456789abcdef
cgf_tree=clean
protocol=cgfried-bootstrap-v1
samples=3
level=O2
jobs=8
normalization=none
sysroot=none
compiler_sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
compiler=/fixture/cgfried
stage1.O2.wall_ms_median=$timing_wall
stage1.O2.wall_ms_mad=0
stage1.O2.user_ms_median=$timing_user
stage1.O2.user_ms_mad=0
stage1.O2.sys_ms_median=$timing_sys
stage1.O2.sys_ms_mad=0
stage1.O2.cpu_ms_median=$timing_cpu
stage1.O2.cpu_ms_mad=0
stage1.O2.maxrss_kb_max=1000
EOF
}

test_time_gate_boundaries_and_control() {
    gate_dir=$tmp/time-gate
    mkdir -p "$gate_dir"
    write_timing_receipt "$gate_dir/base" hasu 100 80 20 99
    write_timing_receipt "$gate_dir/boundary" hasu 130 104 26 99
    expect_status 0 "$gate_dir/boundary.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/boundary"

    write_timing_receipt "$gate_dir/wall-regress" hasu 130.01 80 20 99
    expect_status 1 "$gate_dir/wall-regress.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/wall-regress"
    write_timing_receipt "$gate_dir/cpu-regress" hasu 100 105 26 99
    expect_status 1 "$gate_dir/cpu-regress.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/cpu-regress"

    write_timing_receipt "$gate_dir/marginal-cpu-regress" hasu 100 200 0 99
    sed 's/^stage1.O2.cpu_ms_median=.*/stage1.O2.cpu_ms_median=100/' \
        "$gate_dir/marginal-cpu-regress" >"$gate_dir/paired-cpu-pass"
    expect_status 0 "$gate_dir/paired-cpu-pass.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/paired-cpu-pass"

    sed 's/stage1.O2.wall_ms_mad=0/stage1.O2.wall_ms_mad=10/' \
        "$gate_dir/base" >"$gate_dir/noisy-wall-base"
    sed 's/stage1.O2.wall_ms_mad=0/stage1.O2.wall_ms_mad=10/' \
        "$gate_dir/wall-regress" >"$gate_dir/noisy-wall-result"
    expect_status 0 "$gate_dir/noisy-wall.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/noisy-wall-base" "$gate_dir/noisy-wall-result"

    sed 's/stage1.O2.cpu_ms_mad=0/stage1.O2.cpu_ms_mad=10/' \
        "$gate_dir/base" >"$gate_dir/noisy-cpu-base"
    sed 's/stage1.O2.cpu_ms_mad=0/stage1.O2.cpu_ms_mad=10/' \
        "$gate_dir/cpu-regress" >"$gate_dir/noisy-cpu-result"
    expect_status 0 "$gate_dir/noisy-cpu.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/noisy-cpu-base" "$gate_dir/noisy-cpu-result"

    sed 's/^samples=3$/samples=2/' "$gate_dir/boundary" \
        >"$gate_dir/too-few-samples"
    expect_status 3 "$gate_dir/too-few-samples.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/too-few-samples"
    grep -F 'samples must be at least 3' "$gate_dir/too-few-samples.out" \
        >/dev/null || fail "bootstrap timing accepted fewer than three samples"

    sed '/^samples=/d' "$gate_dir/boundary" >"$gate_dir/missing-samples"
    expect_status 3 "$gate_dir/missing-samples.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/missing-samples"
    grep -F 'missing samples' "$gate_dir/missing-samples.out" >/dev/null ||
        fail "bootstrap timing accepted a receipt without sample count"
    sed '/^samples=/d' "$gate_dir/base" >"$gate_dir/base-missing-samples"
    expect_status 3 "$gate_dir/base-missing-samples.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base-missing-samples" "$gate_dir/boundary"

    sed '/^samples=/d;/^stage1.O2.user_ms_mad=/d;/^stage1.O2.sys_ms_mad=/d;/^stage1.O2.cpu_ms_median=/d;/^stage1.O2.cpu_ms_mad=/d' \
        "$gate_dir/base" >"$gate_dir/legacy-baseline"
    expect_status 0 "$gate_dir/legacy-baseline.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/legacy-baseline" "$gate_dir/boundary"
    grep -F 'evidence-only (legacy baseline lacks paired CPU dispersion)' \
        "$gate_dir/legacy-baseline.out" >/dev/null ||
        fail "legacy bootstrap baseline was not classified evidence-only"
    sed 's/^stage1.O2.cpu_ms_median=.*/stage1.O2.cpu_ms_median=999/' \
        "$gate_dir/boundary" >"$gate_dir/legacy-unsupported-cpu"
    expect_status 0 "$gate_dir/legacy-unsupported-cpu.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/legacy-baseline" "$gate_dir/legacy-unsupported-cpu"
    write_timing_receipt "$gate_dir/legacy-wall-regress" hasu 131 80 20 99
    expect_status 1 "$gate_dir/legacy-wall-regress.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/legacy-baseline" "$gate_dir/legacy-wall-regress"
    expect_status 3 "$gate_dir/legacy-current.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/legacy-baseline"

    write_timing_receipt "$gate_dir/uncontrolled" hasu 100 80 20 84.99
    expect_status 3 "$gate_dir/uncontrolled.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/uncontrolled"
    cp "$gate_dir/boundary" "$gate_dir/duplicate"
    printf '%s\n' 'jobs=8' >>"$gate_dir/duplicate"
    expect_status 3 "$gate_dir/duplicate.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/base" "$gate_dir/duplicate"
    expect_status 0 "$gate_dir/warmup.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/missing" "$gate_dir/boundary"
    printf '%s\n' 'not=a-bootstrap-receipt' >"$gate_dir/malformed-warmup"
    expect_status 3 "$gate_dir/malformed-warmup.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/missing" "$gate_dir/malformed-warmup"
    sed '/^compiler=/d' "$gate_dir/boundary" >"$gate_dir/incomplete-warmup"
    expect_status 3 "$gate_dir/incomplete-warmup.out" \
        "$repo/scripts/bootstrap-time-gate.sh" \
        "$gate_dir/missing" "$gate_dir/incomplete-warmup"
    grep -F 'missing compiler' "$gate_dir/incomplete-warmup.out" >/dev/null ||
        fail "warmup validation did not require complete timing provenance"

    "$repo/scripts/perf-report.sh" --version bootstrap-fixture \
        --output "$gate_dir/report.md" --baseline "$gate_dir/base" \
        --latest "$gate_dir/boundary" \
        --golden "$repo/.benchmarks/golden/kernels-x86_64-linux-gnu.txt" \
        --dashboard "$repo/.benchmarks/kernels-vs-gcc.md" \
        >"$gate_dir/report.out"
    grep -F '| hasu | stage1.O2.wall_ms_median | 100 | 130 | +30.0%' \
        "$gate_dir/report.md" >/dev/null ||
        fail "bootstrap timing did not reach the Sprint 54 report"
}

test_fleet_timing_wrapper() {
    fleet_root=$tmp/fleet-root
    mkdir -p "$fleet_root/.git" "$fleet_root/.benchmarks/runs"
    make_log=$tmp/fleet-bootstrap-make.log
    : >"$make_log"
    run_fleet_bootstrap() {
        fleet_stamp=$1
        shift
        FIXTURE_SYSTEM=Linux FIXTURE_MACHINE=x86_64 \
        CGF_BOOTSTRAP_TEST_MAKE_LOG=$make_log \
        CGF_FLEET_ROOT=$fleet_root CGF_FLEET_HOST=hasu \
        CGF_FLEET_STAMP=$fleet_stamp CGF_FLEET_GIT_CMD=/bin/true \
        CGF_FLEET_MAKE_CMD=$repo/tests/bootstrap/helpers/fake-bootstrap-make.sh \
        CGF_FLEET_UNAME_CMD=$repo/tests/scripts/gates/fixtures/fleet/fake-uname.sh \
        CGF_FLEET_CC=/bin/true \
        CGF_FLEET_BOOTSTRAP_GATE=$repo/scripts/bootstrap-time-gate.sh \
        CGF_FLEET_BENCH_CONTROL=$repo/scripts/bench-control.sh \
            "$@" "$repo/scripts/fleet-bootstrap.sh"
    }

    expect_status 0 "$tmp/fleet-warmup.out" \
        run_fleet_bootstrap 2026-08-12T120000Z env
    first=$fleet_root/.benchmarks/runs/2026-08-12T120000Z-hasu-bootstrap.txt
    [ -s "$first" ] || fail "fleet bootstrap warmup artifact is missing"
    grep -Fx 'fleet.bootstrap_time_gate=warmup' "$first" >/dev/null ||
        fail "fleet bootstrap did not record warmup"
    baseline=$fleet_root/.benchmarks/baseline-bootstrap-O2-x86_64-linux-gnu.hasu.txt
    cp "$first" "$baseline"

    expect_status 0 "$tmp/fleet-pass.out" \
        run_fleet_bootstrap 2026-08-13T120000Z env
    second=$fleet_root/.benchmarks/runs/2026-08-13T120000Z-hasu-bootstrap.txt
    grep -Fx 'fleet.bootstrap_time_gate=pass' "$second" >/dev/null ||
        fail "fleet bootstrap did not record a timing pass"

    expect_status 1 "$tmp/fleet-trip.out" \
        run_fleet_bootstrap 2026-08-14T120000Z env \
        CGF_BOOTSTRAP_TEST_WALL=131
    third=$fleet_root/.benchmarks/runs/2026-08-14T120000Z-hasu-bootstrap.txt
    grep -Fx 'fleet.bootstrap_time_gate=trial-trip' "$third" >/dev/null ||
        fail "fleet bootstrap did not preserve its trial trip"
    grep -F 'BOOTSTRAP_JOBS=8' "$make_log" >/dev/null ||
        fail "fleet bootstrap did not pin the -j8 lane"

    expect_status 3 "$tmp/fleet-wrong-jobs.out" \
        run_fleet_bootstrap 2026-08-15T120000Z env \
        CGF_BOOTSTRAP_JOBS=4
}

build_fault_compilers() {
    host_cc=${CC:-cc}
    command -v "$host_cc" >/dev/null 2>&1 ||
        fail "host C compiler is unavailable: $host_cc"
    mkdir -p "$tmp/fault-compilers"
    for fault_spec in \
        unsorted_readdir:1 padding_write:2 pointer_format:3; do
        fault_name=${fault_spec%%:*}
        fault_kind=${fault_spec#*:}
        for injected in 0 1; do
            "$host_cc" -std=c11 -Wall -Wextra -Werror \
                -DFAULT_KIND="$fault_kind" -DFAULT_INJECTED="$injected" \
                "$repo/tests/bootstrap/helpers/injected-compiler.c" \
                -o "$tmp/fault-compilers/$fault_name-$injected" ||
                fail "cannot build $fault_name fault compiler"
        done
    done

    fault_dir=$tmp/readdir-seed
    mkdir -p "$fault_dir"
    # A broad reverse-ish insertion set defeats both linear-directory and
    # hash-directory enumeration accidentally matching lexical order.
    for fault_name in omega aardvark zebra beta gamma alpha delta epsilon \
        theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon \
        phi chi psi; do
        : >"$fault_dir/$fault_name"
    done
    CGF_BOOTSTRAP_FAULT_DIR=$fault_dir
    export CGF_BOOTSTRAP_FAULT_DIR
}

test_seeded_divergence_names_tu_and_phase_exactly() {
    seed=$1
    phase=$2
    fresh_stages
    put_same_required_artifacts
    source=$repo/tests/bootstrap/faults/$seed.c
    case $phase in
    sema)
        "$tmp/fault-compilers/$seed-0" -fdump-sema "$source" \
            >"$tmp/stage1/obj/$seed.o"
        "$tmp/fault-compilers/$seed-1" -fdump-sema "$source" \
            >"$tmp/stage2/obj/$seed.o"
        ;;
    ir)
        "$tmp/fault-compilers/$seed-0" -emit-ir "$source" \
            >"$tmp/stage1/obj/$seed.o"
        "$tmp/fault-compilers/$seed-1" -emit-ir "$source" \
            >"$tmp/stage2/obj/$seed.o"
        ;;
    asm)
        "$tmp/fault-compilers/$seed-0" -S "$source" \
            -o "$tmp/stage1/obj/$seed.o"
        "$tmp/fault-compilers/$seed-1" -S "$source" \
            -o "$tmp/stage2/obj/$seed.o"
        ;;
    *) fail "unknown injected phase: $phase" ;;
    esac
    cmp -s "$tmp/stage1/obj/$seed.o" "$tmp/stage2/obj/$seed.o" &&
        fail "$seed injection did not actually change its stage artifact"
    output=$tmp/localize-$seed.out
    expect_failure "$output" sh "$repo/scripts/bisect-nondet.sh" \
        "$tmp/stage1" "$tmp/stage2" \
        --source "$source" \
        --stage1-cc "$tmp/fault-compilers/$seed-0" \
        --stage2-cc "$tmp/fault-compilers/$seed-1"

    {
        printf 'bisect-nondet: first differing object: obj/%s.o\n' "$seed"
        printf 'bisect-nondet: phase localization for %s\n' "$source"
        for candidate in pp ast sema; do
            if [ "$candidate" = "$phase" ]; then
                printf 'bisect-nondet: first differing public phase: %s\n' \
                    "$phase"
                break
            fi
            printf 'bisect-nondet: phase %s matches\n' "$candidate"
        done
        if [ "$phase" = ir ]; then
            printf '%s\n' 'bisect-nondet: phase boundary 100000-parse-ast.txt matches'
            printf '%s\n' 'bisect-nondet: phase boundary 200000-sema.txt matches'
            printf '%s\n' 'bisect-nondet: first differing phase boundary: 300000-ir-post-lowering.cgfir'
        elif [ "$phase" = asm ]; then
            for boundary in \
                100000-parse-ast.txt \
                200000-sema.txt \
                300000-ir-post-lowering.cgfir \
                400001-ir-fp01-i01-p00-seeded.cgfir \
                700000-ir-post-opt-legalized.cgfir \
                800000-mir.txt; do
                printf 'bisect-nondet: phase boundary %s matches\n' "$boundary"
            done
            printf '%s\n' 'bisect-nondet: first differing phase boundary: 900000-asm.s'
        fi
    } >"$tmp/localize.expected"
    cmp "$tmp/localize.expected" "$output" ||
        fail "$seed did not name its exact TU and $phase phase"
}

test_full_bootstrap_gate_rejects_seeded_fault() {
    seed=$1
    work=$tmp/O0-$seed
    output=$tmp/bootstrap-gate-$seed.out

    expect_status 1 "$output" env \
        MAKE="$repo/tests/bootstrap/helpers/fake-fixed-point-make.sh" \
        HOSTCC=/bin/true CGF_BOOTSTRAP_JOBS=8 \
        CGF_BOOTSTRAP_HOST_CLASS=bootstrap-meta \
        CGF_BOOTSTRAP_WORK="$work" \
        sh "$repo/scripts/bootstrap.sh" O0
    grep -F "bootstrap-O0: first differing compiler_assembly: compiler/tests/bootstrap/faults/$seed.s" \
        "$output" >/dev/null ||
        fail "$seed did not trip the full bootstrap assembly gate"
    grep -F "bisect-nondet: first differing object: compiler/tests/bootstrap/faults/$seed.o" \
        "$output" >/dev/null ||
        fail "$seed did not identify the first differing bootstrap TU"
}

test_repro_rejects_failed_or_tampered_reference() {
    source_root=$cross_source/repro-O2
    stage1=$repo/$source_root/stage1
    stage2=$repo/$source_root/stage2
    target=x86_64-linux-gnu
    mkdir -p "$stage1/compiler" "$stage1/runtime" "$stage1/$target" \
        "$stage2/compiler" "$stage2/runtime" "$stage2/$target"
    for stage in "$stage1" "$stage2"; do
        printf '%s\n' '#!/bin/sh' 'echo x86_64-linux-gnu' >"$stage/cgfried"
        chmod +x "$stage/cgfried"
        printf '%s\n' 'compiler assembly' >"$stage/compiler/a.s"
        printf '%s\n' 'compiler object' >"$stage/compiler/a.o"
        printf '%s\n' 'runtime assembly' >"$stage/runtime/b.s"
        printf '%s\n' 'runtime object' >"$stage/runtime/b.o"
        printf '%s\n' 'runtime archive' >"$stage/$target/libcgf_rt.a"
        {
            echo 'schema=cgfried.bootstrap-artifacts.v1'
            echo "target=$target"
            echo 'level=O2'
            (cd "$stage" && sha256sum compiler/a.o compiler/a.s \
                runtime/b.o runtime/b.s "$target/libcgf_rt.a" cgfried)
        } >"$stage/artifacts.sha256"
    done
    make_path=$(command -v make)
    cc_path=$(command -v "${CC:-cc}")
    as_path=$(command -v as)
    ar_path=$(command -v ar)
    write_repro_report() {
        report_state=$1
        cat >"$repo/$source_root/bootstrap-report.txt" <<EOF
schema=cgfried.bootstrap.v1
target=x86_64-linux-gnu
level=O2
normalization=none
jobs=8
sysroot=none
hostcc=$cc_path
assembler=$as_path
archiver=$ar_path
make=$make_path
state=building-stage0
state=building-stage1
state=building-stage2
state=$report_state
EOF
    }

    write_repro_report identity-failed
    expect_status 2 "$tmp/repro-failed-state.out" \
        "$repo/scripts/bootstrap-repro.sh" O2 "$source_root" \
        "$repro_outputs-failed"
    grep -F 'reference is not a passed raw fixed point' \
        "$tmp/repro-failed-state.out" >/dev/null ||
        fail "repro accepted a failed fixed-point report"

    write_repro_report passed
    cp "$stage1/cgfried" "$tmp/repro-stage1-cgfried"
    printf '%s\n' tampered >>"$stage1/cgfried"
    expect_status 2 "$tmp/repro-stage1-tamper.out" \
        "$repo/scripts/bootstrap-repro.sh" O2 "$source_root" \
        "$repro_outputs-stage1-tamper"
    grep -F 'stage1 artifact hash mismatch: cgfried' \
        "$tmp/repro-stage1-tamper.out" >/dev/null ||
        fail "repro accepted a tampered stage1 compiler"
    cp "$tmp/repro-stage1-cgfried" "$stage1/cgfried"
    chmod +x "$stage1/cgfried"

    printf '%s\n' tampered >>"$stage2/compiler/a.s"
    expect_status 2 "$tmp/repro-stage2-tamper.out" \
        "$repo/scripts/bootstrap-repro.sh" O2 "$source_root" \
        "$repro_outputs-stage2-tamper"
    grep -F 'stage2 artifact hash mismatch: compiler/a.s' \
        "$tmp/repro-stage2-tamper.out" >/dev/null ||
        fail "repro accepted a tampered stage2 artifact"
}

test_cross_import_and_manifest_validation() {
    native=$repo/$cross_source/O2
    stage=$native/stage2
    imported=$cross_outputs/native
    x86=$cross_outputs/x86
    x86_bootstrap=$cross_source/x86-O2
    fixture_commit=0123456789abcdef0123456789abcdef01234567
    native_run=$cross_source/native-run.manifest
    x86_run=$cross_source/x86-run.manifest
    header_archive=$cross_outputs/arm64-headers.tar
    mkdir -p "$repo/${header_archive%/*}"
    printf '%s\n' 'canonical ARM64 header snapshot' >"$repo/$header_archive"
    write_run_manifest() {
        run_path=$1
        run_lane=$2
        run_commit=$3
        cat >"$repo/$run_path" <<EOF
schema=cgfried.bootstrap-run.v1
lane=$run_lane
level=O2
commit=$run_commit
runner_arch=fixture
normalization=none
host_cc=fixture cc
assembler=fixture as
linker=fixture ld
outcome=success
EOF
    }
    cross_import() {
        CGF_BOOTSTRAP_COMMIT=$fixture_commit \
        CGF_BOOTSTRAP_NATIVE_RUN_MANIFEST=$native_run \
        CGF_BOOTSTRAP_HEADER_ARCHIVE=$header_archive \
            "$repo/scripts/bootstrap-cross.sh" import O2 "$@"
    }
    cross_emit() {
        CGF_BOOTSTRAP_JOBS=8 CGF_BOOTSTRAP_COMMIT=$fixture_commit \
        CGF_BOOTSTRAP_X86_RUN_MANIFEST=$x86_run \
        CGF_BOOTSTRAP_HEADER_ARCHIVE=$header_archive \
            "$repo/scripts/bootstrap-cross.sh" emit O2 "$@"
    }
    cross_compare() {
        MAKE=$repo/tests/bootstrap/helpers/fake-cross-make.sh \
        CGF_BOOTSTRAP_CROSS_REPORT=$tmp/cross-report.manifest \
        CGF_BOOTSTRAP_COMMIT=$fixture_commit \
        CGF_BOOTSTRAP_X86_RUN_MANIFEST=$x86_run \
        CGF_BOOTSTRAP_HEADER_ARCHIVE=$header_archive \
            "$repo/scripts/bootstrap-cross.sh" compare O2 "$@"
    }
    mkdir -p "$stage/compiler/src" "$stage/runtime/src/rt"
    printf '%s\n' 'native compiler assembly' >"$stage/compiler/src/a.s"
    printf '%s\n' 'native runtime assembly' >"$stage/runtime/src/rt/b.s"
    cat >"$native/bootstrap-report.txt" <<'EOF'
schema=cgfried.bootstrap.v1
target=arm64-linux
level=O2
normalization=none
state=passed
EOF
    {
        echo 'schema=cgfried.bootstrap-artifacts.v1'
        echo 'target=arm64-linux'
        echo 'level=O2'
        (cd "$stage" && sha256sum compiler/src/a.s runtime/src/rt/b.s)
    } >"$stage/artifacts.sha256"

    write_run_manifest "$native_run" arm64-linux-native wrong-commit
    expect_status 2 "$tmp/cross-native-commit.out" cross_import \
        "$cross_source/O2/stage2" "$cross_outputs/native-wrong-commit"
    grep -F 'arm64-linux-native run manifest provenance is invalid' \
        "$tmp/cross-native-commit.out" >/dev/null ||
        fail "cross import accepted a native manifest from another commit"
    write_run_manifest "$native_run" arm64-linux-native "$fixture_commit"
    cross_import \
        "$cross_source/O2/stage2" "$imported" >"$tmp/cross-import.out"
    cmp "$stage/compiler/src/a.s" "$repo/$imported/compiler/src/a.s" ||
        fail "cross import changed native compiler assembly"
    grep -Fx 'source=native-fixed-point-stage2' \
        "$repo/$imported/artifacts.sha256" >/dev/null ||
        fail "cross import lost fixed-point provenance"
    grep -Fx "commit=$fixture_commit" \
        "$repo/$imported/artifacts.sha256" >/dev/null ||
        fail "cross import lost commit provenance"
    [ -r "$repo/$imported/provenance/native-run.manifest" ] ||
        fail "cross import did not retain its native run manifest"

    mkdir -p "$repo/$x86_bootstrap/stage1"
    cp "$tmp/fault-compilers/pointer_format-0" \
        "$repo/$x86_bootstrap/stage1/cgfried"
    chmod +x "$repo/$x86_bootstrap/stage1/cgfried"
    write_x86_report() {
        x86_state=$1
        cat >"$repo/$x86_bootstrap/bootstrap-report.txt" <<EOF
schema=cgfried.bootstrap.v1
target=x86_64-linux-gnu
level=O2
normalization=none
state=$x86_state
EOF
    }
    write_x86_manifest() {
        compiler_hash=$1
        cat >"$repo/$x86_bootstrap/stage1/artifacts.sha256" <<EOF
schema=cgfried.bootstrap-artifacts.v1
target=x86_64-linux-gnu
level=O2
$compiler_hash  cgfried
EOF
    }
    actual_compiler_hash=$(sha256sum \
        "$repo/$x86_bootstrap/stage1/cgfried" | awk '{print $1}')
    write_run_manifest "$x86_run" x86_64-linux "$fixture_commit"
    write_x86_report identity-failed
    write_x86_manifest "$actual_compiler_hash"
    expect_status 2 "$tmp/cross-failed-x86.out" \
        cross_emit \
        "$repo/$x86_bootstrap/stage1/cgfried" \
        "$cross_outputs/sysroot" "$cross_outputs/x86-failed" \
        "$x86_bootstrap"
    grep -F 'x86 source is not a passed raw fixed point' \
        "$tmp/cross-failed-x86.out" >/dev/null ||
        fail "cross emit accepted a failed x86 fixed point"

    write_x86_report passed
    write_x86_manifest \
        0000000000000000000000000000000000000000000000000000000000000000
    expect_status 2 "$tmp/cross-wrong-compiler.out" \
        cross_emit \
        "$repo/$x86_bootstrap/stage1/cgfried" \
        "$cross_outputs/sysroot" "$cross_outputs/x86-wrong-compiler" \
        "$x86_bootstrap"
    grep -F 'x86 stage1 compiler hash mismatch' \
        "$tmp/cross-wrong-compiler.out" >/dev/null ||
        fail "cross emit accepted an unauthenticated x86 compiler"

    write_x86_manifest "$actual_compiler_hash"
    mkdir -p "$repo/$cross_outputs/sysroot/usr/include"
    cross_emit \
        "$repo/$x86_bootstrap/stage1/cgfried" \
        "$cross_outputs/sysroot" "$x86" "$x86_bootstrap" \
        >"$tmp/cross-emit.out"
    grep -Fx 'source=x86-hosted-stage1' \
        "$repo/$x86/artifacts.sha256" >/dev/null ||
        fail "cross emit lost x86-host provenance"
    grep -E '^compiler_sha256=[0-9a-f]{64}$' \
        "$repo/$x86/artifacts.sha256" >/dev/null ||
        fail "cross emit lost compiler identity"
    [ -r "$repo/$x86/provenance/x86-run.manifest" ] ||
        fail "cross emit did not retain its x86 run manifest"

    x86_pass=$cross_outputs/x86-pass
    native_pass_source=$cross_source/native-pass-O2
    native_pass_stage=$repo/$native_pass_source/stage2
    native_pass=$cross_outputs/native-pass
    cp -R "$repo/$x86" "$repo/$x86_pass"
    mkdir -p "$native_pass_stage"
    cp -R "$repo/$x86/compiler" "$repo/$x86/runtime" "$native_pass_stage/"
    cat >"$repo/$native_pass_source/bootstrap-report.txt" <<'EOF'
schema=cgfried.bootstrap.v1
target=arm64-linux
level=O2
normalization=none
state=passed
EOF
    {
        echo 'schema=cgfried.bootstrap-artifacts.v1'
        echo 'target=arm64-linux'
        echo 'level=O2'
        (cd "$native_pass_stage" &&
            find compiler runtime -type f -name '*.s' -print | sort |
            while IFS= read -r assembly; do
                sha256sum "$assembly"
            done)
    } >"$native_pass_stage/artifacts.sha256"
    cross_import "$native_pass_source/stage2" "$native_pass" \
        >"$tmp/cross-pass-import.out"
    cross_compare "$x86_pass" "$native_pass" "$x86_bootstrap" \
        >"$tmp/cross-pass.out"
    grep -Fx 'schema=cgfried.bootstrap-run.v1' \
        "$tmp/cross-report.manifest" >/dev/null ||
        fail "cross comparison did not emit the run-manifest schema"
    grep -Fx 'lane=arm64-linux-cross' "$tmp/cross-report.manifest" \
        >/dev/null || fail "cross comparison did not name its lane"
    grep -Fx "commit=$fixture_commit" "$tmp/cross-report.manifest" \
        >/dev/null || fail "cross comparison did not bind its commit"
    for provenance_field in \
        native_run_manifest_sha256 native_bootstrap_report_sha256 \
        native_stage2_manifest_sha256 x86_run_manifest_sha256 \
        x86_bootstrap_report_sha256 x86_stage1_manifest_sha256 \
        header_archive_sha256; do
        grep -E "^$provenance_field=[0-9a-f]{64}$" \
            "$tmp/cross-report.manifest" >/dev/null ||
            fail "cross report omitted $provenance_field"
    done
    grep -Fx 'outcome=success' "$tmp/cross-report.manifest" >/dev/null ||
        fail "cross comparison did not record success"

    cp "$repo/$x86/artifacts.sha256" "$tmp/cross-x86-manifest"
    sed '/^compiler_sha256=/d' "$tmp/cross-x86-manifest" \
        >"$repo/$x86/artifacts.sha256"
    expect_status 2 "$tmp/cross-missing-compiler.out" \
        cross_compare "$x86" "$imported" \
        "$x86_bootstrap"
    grep -F 'assembly manifest provenance is invalid' \
        "$tmp/cross-missing-compiler.out" >/dev/null ||
        fail "cross comparison accepted missing compiler identity"

    sed 's/^compiler_sha256=.*/compiler_sha256=0000000000000000000000000000000000000000000000000000000000000000/' \
        "$tmp/cross-x86-manifest" >"$repo/$x86/artifacts.sha256"
    expect_status 2 "$tmp/cross-wrong-identity.out" \
        cross_compare "$x86" "$imported" \
        "$x86_bootstrap"
    grep -F 'assembly manifest provenance is invalid' \
        "$tmp/cross-wrong-identity.out" >/dev/null ||
        fail "cross comparison accepted wrong compiler identity"
    cp "$tmp/cross-x86-manifest" "$repo/$x86/artifacts.sha256"

    printf '%s\n' 'tampered after manifest' >>"$repo/$x86/compiler/src/diag.s"
    expect_status 2 "$tmp/cross-tamper.out" \
        cross_compare "$x86" "$imported" \
        "$x86_bootstrap"
    grep -F "assembly hash mismatch: $x86/compiler/src/diag.s" \
        "$tmp/cross-tamper.out" >/dev/null ||
        fail "cross comparison did not reject a tampered assembly artifact"

    printf '%s\n' 'tampered native source' >>"$stage/runtime/src/rt/b.s"
    expect_status 2 "$tmp/cross-import-tamper.out" \
        cross_import \
        "$cross_source/O2/stage2" "$cross_outputs/native-tampered"
    grep -F 'native stage2 hash mismatch: runtime/src/rt/b.s' \
        "$tmp/cross-import-tamper.out" >/dev/null ||
        fail "cross import did not reject a tampered native fixed point"
}

test_audit_detects_every_seeded_sin_exactly
test_empty_stages_fail_closed
test_missing_object_names_stage_and_tu_exactly
test_missing_assembly_group_fails_closed
test_empty_assembly_group_fails_closed
test_missing_runtime_group_fails_closed
test_empty_runtime_group_fails_closed
test_identical_complete_stages_pass
test_real_compiler_emits_ordered_phase_tree
test_concurrent_real_compiler_phase_trees_are_isolated
test_bisector_localizes_real_phase_tree
test_bisector_rejects_identically_incomplete_phase_trees
test_bisector_accepts_complete_o0_phase_trees_without_passes
test_bootstrap_refuses_unowned_nonempty_work_directory
test_control_capture_is_canonical_and_controlled
test_time_gate_boundaries_and_control
test_fleet_timing_wrapper
build_fault_compilers
test_full_bootstrap_gate_rejects_seeded_fault unsorted_readdir
test_full_bootstrap_gate_rejects_seeded_fault padding_write
test_full_bootstrap_gate_rejects_seeded_fault pointer_format
test_repro_rejects_failed_or_tampered_reference
test_cross_import_and_manifest_validation
test_seeded_divergence_names_tu_and_phase_exactly unsorted_readdir sema
test_seeded_divergence_names_tu_and_phase_exactly padding_write ir
test_seeded_divergence_names_tu_and_phase_exactly pointer_format asm

echo "bootstrap meta: exact audit, controlled timing/gates, deterministic isolated phase trees, exact localization, and 3 injected faults localized"
