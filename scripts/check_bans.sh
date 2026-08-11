#!/bin/sh
# Enforces the repo-wide bans from Sprint 0/1 over C sources in src/ and
# tests/. Banned:
#   - the libc sort (unstable; glibc/musl disagree on ties -> nondeterminism)
#   - GNU attributes (we are strict C11)
#   - strtok (hidden global state)
#   - rand/srand (nondeterminism; no randomness belongs in a compiler)
# The call-shaped regexes use a non-identifier guard so parse_operand() and
# friends never false-positive.
set -eu
LC_ALL=C
export LC_ALL

# These bans govern OUR OWN source. Exempt: imported third-party corpora
# (tests/fixtures/) and the fixtures under tests/programs/, tests/corpus/,
# tests/warn/, tests/memsafe/, and tests/bench/kernels/ — those are C programs
# we COMPILE, so of course they may contain the constructs we refuse to write
# ourselves.
#
# This list has now been widened THREE TIMES for the same reason, which is worth
# noticing: tests/corpus/ was missing until the first packed program landed
# there, and tests/warn/ + tests/memsafe/ until the first fixture asserting a
# warning ABOUT an attribute did, and tests/bench/kernels/ when Sprint 53 added
# benchmark programs that intentionally call libc and use noinline. The rule
# always covered them; the pattern just did not. A fixture directory holding
# compiled C belongs here the day it is created, not the day a fixture in it
# first uses an extension.
files=$(find src tests \( -name '*.c' -o -name '*.h' \) \
    ! -path 'tests/fixtures/*' ! -path 'tests/programs/*' \
    ! -path 'tests/corpus/*' ! -path 'tests/warn/*' \
    ! -path 'tests/memsafe/*' ! -path 'tests/bench/corpus/*' \
    ! -path 'tests/bench/kernels/*' | sort)
status=0

hits=$(printf '%s\n' "$files" |
    xargs grep -nE '(^|[^a-zA-Z0-9_])(qsort|strtok|s?rand)\(' 2>/dev/null |
    grep -v 'check_bans allow' || true)
if [ -n "$hits" ]; then
    echo "check_bans: banned call(s) found:" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

hits=$(printf '%s\n' "$files" |
    xargs grep -n '__attribute__' 2>/dev/null |
    grep -v 'check_bans allow' || true)
if [ -n "$hits" ]; then
    echo "check_bans: GNU attribute found (strict C11 source):" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

# Compiler/toolchain CGF_* env vars are read ONLY in toolchain.c's
# env_override.  The separately linked Sprint 44 runtime has one deliberate
# process-time control: CGF_SAFE_ABORT selects abort versus trap on failure.
hits=$(grep -rn 'getenv' src | grep 'CGF_' |
    grep -v '^src/driver/toolchain\.c:' |
    grep -v '^src/rt/cgf_safe_diag\.c:.*getenv("CGF_SAFE_ABORT")' || true)
if [ -n "$hits" ]; then
    echo "check_bans: CGF_* env read outside toolchain.c env_override:" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

# Numeric CONVERSION is centralized: strtod/strtof/strtold live only in
# Float conversion must go through src/util/softfp.c, never the host's.
# A host strtod puts the host's rounding into our output, which breaks
# both cross-compilation (folding an arm64 fp128 constant on an x86 host)
# and the byte-identical bootstrap. The seam that used to be allowed here
# (XD-S08-FPHOST) was retired in Sprint 15.
hits=$(printf '%s\n' "$files" |
    xargs grep -nE '\b(strtod|strtof|strtold)\s*\(' 2>/dev/null |
    grep -v 'check_bans allow' || true)
if [ -n "$hits" ]; then
    echo "check_bans: host float conversion (use src/util/softfp.c):" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

# Sprint 22 golden invariant: ONE register allocator at every opt level.
# The moment regalloc.c consults an opt level, a -O0-only (or -O2-only)
# codepath exists and the spill-all lane stops proving anything.
hits=$(grep -n 'opt_level\|OptLevel' src/cg/x86_64/regalloc.c |
    grep -v 'CgOptLevel level' | grep -v '(void)level' |
    grep -v 'check_bans allow' || true)
if [ -n "$hits" ]; then
    echo "check_bans: regalloc.c conditioned on opt level (one allocator" >&2
    echo "at every level is the Sprint 22 invariant):" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

# Sprint 24: no silent assembler workarounds. A "workaround" in emit.c
# must cite a findings-table row (F-<sprint>-<tag> id) so every gap has
# an upstream disposition on record.
hits=$(grep -n 'workaround' src/cg/x86_64/emit.c | grep -v 'F-S[0-9]' || true)
if [ -n "$hits" ]; then
    echo "check_bans: emit.c workaround without a findings-table ID:" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

# Sprint 27 (DoD 8): NO code path reorders link_inputs — archive
# extraction is position-dependent, and a drop-in driver must reproduce
# gcc's order-sensitive failures, never "helpfully" fix them.
hits=$(grep -rn 'link_inputs' src | grep -iE 'sort' || true)
if [ -n "$hits" ]; then
    echo "check_bans: link_inputs must NEVER be reordered (Sprint 27):" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

# Sprint 49 (DoD 6): every armv8.0 ll/sc site carries an UPGRADE marker, so
# the LSE opportunity is recorded where the loop is rather than in a document
# nobody greps. Emitting ldadd/cas unconditionally would fault on an armv8.0
# part, so the upgrade must stay behind feature routing -- the marker is what
# makes those sites findable when that routing lands.
exlusive=$(grep -rln 'ldaxr\|stlxr\|clrex' src || true)
for f in $exlusive; do
    if ! grep -q 'UPGRADE(armv8.1-lse)' "$f"; then
        echo "check_bans: $f emits exclusives without an UPGRADE marker" >&2
        status=1
    fi
done
markers=$(grep -rc 'UPGRADE(armv8.1-lse)' src/cg/arm64/isel.c || echo 0)
if [ "$markers" -lt 2 ]; then
    echo "check_bans: both atomic selection sites need UPGRADE markers" >&2
    status=1
fi

# The test runner's scratch directory is NEVER a literal path. Two runners
# writing the same files is how a lane reports a failure that reproduces
# nowhere: `make -j test-a64-corpus test-a64-spill-all` shares one cgf-test
# and failed nearly the whole corpus with "the assembler rejected
# cgfried-generated assembly ... line 0", and the ppfuzz version of the same
# bug reported 128 differential findings that were one overwritten file.
# The path comes from CGF_TEST_WORK, else from beside the runner binary.
hits=$(grep -n 'build/test-work' tests/runner/*.c || true)
if [ -n "$hits" ]; then
    echo "check_bans: the runner's scratch dir must not be a literal path:" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

[ "$status" -eq 0 ] && echo "check_bans: clean"
exit "$status"
