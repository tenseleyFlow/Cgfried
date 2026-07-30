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

# tests/tinycc-pp is imported third-party test DATA (attribution in its
# README) — exempt from our source bans, like the meta fixtures.
files=$(find src tests \( -name '*.c' -o -name '*.h' \) \
    ! -path 'tests/fixtures/imported/*' | sort)
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
    xargs grep -n '__attribute__' 2>/dev/null || true)
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

[ "$status" -eq 0 ] && echo "check_bans: clean"
exit "$status"
