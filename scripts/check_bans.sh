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
# (tests/fixtures/) and the corpus fixtures under tests/programs/ — those
# are C programs we COMPILE, so of course they may contain the constructs
# we refuse to write ourselves.
files=$(find src tests \( -name '*.c' -o -name '*.h' \) \
    ! -path 'tests/fixtures/*' ! -path 'tests/programs/*' | sort)
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

# CGF_* toolchain env vars are read ONLY in toolchain.c's env_override —
# that single choke point is what keeps the documented routing table true.
hits=$(grep -rn 'getenv' src | grep 'CGF_' |
    grep -v '^src/driver/toolchain\.c:' || true)
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

[ "$status" -eq 0 ] && echo "check_bans: clean"
exit "$status"
