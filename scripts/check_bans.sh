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
# numlit.c's lex_fp_interim (debt XD-S08-FPHOST, deleted by Sprint 15),
# and nothing else may convert floating text. Host conversion leaking into
# target constants would break determinism invariant #3.
hits=$(printf '%s\n' "$files" |
    xargs grep -nE '(^|[^a-zA-Z0-9_])strto(d|f|ld)\(' 2>/dev/null |
    grep -v 'check_bans allow' || true)
if [ -n "$hits" ]; then
    echo "check_bans: float conversion outside lex_fp_interim:" >&2
    printf '%s\n' "$hits" >&2
    status=1
fi

[ "$status" -eq 0 ] && echo "check_bans: clean"
exit "$status"
