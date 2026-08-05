#!/bin/sh
# Sprint 49 DoD 7 (and Sprint 48 DoD 6): the e2e corpus, compiled by an
# arm64-linux cgfried and EXECUTED.
#
# The compiler is cross-built with the host's aarch64 gcc, then runs under
# qemu-user; the programs it produces run under qemu-user too. That is two
# levels of emulation, and it is the point: nothing in this lane knows a
# target triple, so what it proves is that the arm64 backend the driver
# selects on an arm64 host produces working programs.
#
# The corpus fixtures carry their own `// CHECK:` and `// EXIT_CODE:`
# expectations, all of them gcc-verified before pinning (tests/corpus/
# README.md), so the runner does the asserting and this script only arranges
# for the right compiler and the right way to execute what it builds.
#
# On a NATIVE arm64 host qemu-run.sh is a passthrough and CGF_A64_CGF can
# name an ordinary build -- the same lane serves both.
set -u
LC_ALL=C
export LC_ALL

CGF=${CGF_A64_CGF:-build-a64/cgfried}
TEST=${2:-build/cgf-test}
WORK=${CGF_A64_CORPUS_WORK:-build/a64-corpus}

CROSS_CC=${CGF_A64_GCC:-aarch64-linux-gnu-gcc}
AS=${CGF_A64_AS:-aarch64-linux-gnu-as}
LD=${CGF_A64_LD:-aarch64-linux-gnu-ld}
SYSROOT=${CGF_QEMU_SYSROOT:-/usr/aarch64-linux-gnu}

host=$(uname -m 2>/dev/null || echo unknown)

missing=
case "$host" in
aarch64 | arm64) ;;
*)
    command -v "$CROSS_CC" >/dev/null 2>&1 || missing="$missing $CROSS_CC"
    command -v "$AS" >/dev/null 2>&1 || missing="$missing $AS"
    command -v "$LD" >/dev/null 2>&1 || missing="$missing $LD"
    # Probe for the EMULATOR, not by running something through it: the
    # obvious `qemu-run.sh /bin/true` hands an x86 binary to an aarch64
    # emulator and fails for the wrong reason.
    if [ -z "${CGF_QEMU:-}" ] &&
        ! command -v qemu-aarch64-static >/dev/null 2>&1 &&
        ! command -v qemu-aarch64 >/dev/null 2>&1; then
        missing="$missing qemu-aarch64"
    fi
    [ -d "$SYSROOT" ] || missing="$missing $SYSROOT"
    ;;
esac
if [ -n "$missing" ]; then
    echo "a64_corpus: skipped (missing:$missing)"
    exit 0
fi

# Cross-build the compiler if the caller has not supplied one. Its own
# `cgf_target_host()` is what selects the arm64 backend -- there is no
# --target flag until Sprint 51, so the compiler's OWN architecture is the
# target, which is exactly why this needs a cross build rather than a flag.
# ALWAYS rebuild, never build-if-absent: make is incremental, so this costs
# nothing when nothing changed, and a stale cross compiler otherwise reports
# a fix as still broken -- or worse, reports a regression as fine. That trap
# (F-S22-MIRCHECK's shape) cost two wrong diagnoses in one afternoon.
if [ "$host" = "aarch64" ] || [ "$host" = "arm64" ]; then
    [ -x "$CGF" ] || {
        echo "a64_corpus: no compiler at $CGF (build it first)" >&2
        exit 1
    }
    # Say so here rather than letting two fixtures fail with an undefined
    # __addtf3: arm64's long double is binary128 and every operation on it
    # is a libcall, so a missing runtime is a build gap, not a codegen bug.
    [ -f "$(dirname "$CGF")/arm64-linux/libcgf_rt.a" ] || {
        echo "a64_corpus: no libcgf_rt.a beside $CGF -- run 'make rt'" >&2
        exit 1
    }
elif [ -z "${CGF_A64_NO_BUILD:-}" ]; then
    # RT_TARGET is normally read from `$(BUILD)/cgfried -dumpmachine`, which
    # a cross build cannot run, so it would silently fall back to the HOST
    # triple and file the arm64 archive under x86_64-linux-gnu/ -- where the
    # driver never looks. Every long-double program then fails to link on
    # __addtf3. Name it explicitly.
    make CC="$CROSS_CC" BUILD=build-a64 RT_TARGET=arm64-linux \
        -j"$(nproc 2>/dev/null || echo 2)" \
        build-a64/cgfried rt >"$WORK.build.log" 2>&1 || {
        echo "a64_corpus: cross build failed" >&2
        tail -20 "$WORK.build.log" >&2
        exit 1
    }
fi

mkdir -p "$WORK"

# A crashing fixture is expected debt here, and qemu writes a core file into
# the CWD for each one -- which is the repository root. Suppress them: the
# runner already reports the signal, and the cores are pure litter.
ulimit -c 0 2>/dev/null || true

case "$host" in
aarch64 | arm64)
    # Native: qemu-run.sh is a passthrough and the compiler's own tool
    # discovery is already right -- EXCEPT that its default assembler is the
    # BUNDLED afs-as, so an unbuilt one has to fall back to system gas the
    # same way the Makefile's AS_LANE does. Left out, the driver reports
    # "assembler not found" on a host that has a perfectly good `as`.
    as_mode=
    [ -x afs-as/target/release/afs-as ] || as_mode="CGF_AS=0"
    cat >"$WORK/cgf-a64" <<EOF
#!/bin/sh
exec env $as_mode "$PWD/$CGF" "\$@"
EOF
    ;;
*)
    # The compiler spawns \`as\` and \`ld\`. Under qemu those exec calls reach
    # the HOST, so they must be routed to the cross tools by absolute path --
    # left alone they would silently produce x86 objects.
    as_path=$(command -v "$AS")
    ld_path=$(command -v "$LD")
    cat >"$WORK/cgf-a64" <<EOF
#!/bin/sh
# Generated by scripts/a64_corpus_lane.sh -- an arm64 cgfried wearing the
# host's cross binutils.
CGF_AS_PATH='$as_path' \\
CGF_LD_PATH='$ld_path' \\
CGF_CRT_DIR='$SYSROOT/lib' \\
exec sh "$PWD/scripts/qemu-run.sh" "$PWD/$CGF" "\$@"
EOF
    ;;
esac
chmod +x "$WORK/cgf-a64"

# Smoke the wrapper before handing it a 50-program corpus: a broken routing
# variable otherwise reads as 50 compiler bugs.
printf 'int main(void) { return 42; }\n' >"$WORK/smoke.c"
if ! "$WORK/cgf-a64" "$WORK/smoke.c" -o "$WORK/smoke" 2>"$WORK/smoke.err"; then
    echo "a64_corpus: the cross compiler cannot build a trivial program" >&2
    head -10 "$WORK/smoke.err" >&2
    exit 1
fi
sh scripts/qemu-run.sh "$WORK/smoke"
rc=$?
if [ "$rc" -ne 42 ]; then
    echo "a64_corpus: smoke program exited $rc, expected 42" >&2
    exit 1
fi

CGF_TEST_CC="$WORK/cgf-a64" \
    CGF_TEST_RUN="$PWD/scripts/qemu-run.sh" \
    CGF_TEST_TARGET=arm64-linux \
    CGF_TEST_TIMEOUT=${CGF_A64_TIMEOUT:-120} \
    "$TEST" --profile linux-arm64 tests/corpus/x86_64 >"$WORK/run.log" 2>&1
status=$?
cat "$WORK/run.log"

# The corpus fixtures are x86-verified expectations shared by both targets,
# so arm64 debt lives in a ledger rather than in 51 shared files. Enforce it
# EXACTLY: an unlisted failure fails the lane, and so does a ledger entry
# that has started passing.
ledger=${CGF_A64_LEDGER:-ci/expected_a64_corpus_failures.txt}
sed -e 's/#.*//' -e '/^[[:space:]]*$/d' -e 's/[[:space:]]*$//' "$ledger" |
    sort >"$WORK/expected.txt"
grep '^FAIL ' "$WORK/run.log" | sed -e 's/^FAIL //' -e 's/:.*//' |
    sort -u >"$WORK/actual.txt"

if ! cmp -s "$WORK/expected.txt" "$WORK/actual.txt"; then
    echo "a64_corpus: the failure set does not match $ledger" >&2
    echo "  (< expected, > actual)" >&2
    diff "$WORK/expected.txt" "$WORK/actual.txt" >&2
    exit 1
fi

expected_count=$(wc -l <"$WORK/expected.txt" | tr -d ' ')
summary=$(grep '^cgf-test:' "$WORK/run.log" | tail -1)
if [ "$expected_count" -eq 0 ]; then
    exit "$status"
fi
echo "a64_corpus: $summary ($expected_count pinned in $ledger)"

