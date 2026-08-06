#!/bin/sh
# Sprint 51: HOST and TARGET are different questions, and only target.c may
# ask the first one.
#
# `cgf_target_host()` sniffs the machine the compiler is RUNNING on. Every
# other code path wants `cgf_target_selected()` -- the machine it is
# compiling FOR. The two coincide on a native build, which is exactly why
# confusing them is invisible until someone cross-compiles, and then the
# wrong answer arrives with no diagnostic at all: wrong char signedness,
# wrong long-double format, wrong object dialect.
#
# The sema half of this rule has its own older gate (check_sema_target.sh),
# which bans conditional compilation and host sizeof() outright. This one is
# the whole-repo version of the same law.
set -eu
LC_ALL=C
export LC_ALL

status=0

# target.c defines both and uses the host to seed the default. target.h
# declares them. Nothing else may name the host.
hits=$(grep -rn 'cgf_target_host' src/ |
    grep -v '^src/target\.c:' |
    grep -v '^src/target\.h:' || true)
if [ -n "$hits" ]; then
    echo "check_target_seam: cgf_target_host() outside src/target.c:" >&2
    printf '%s\n' "$hits" >&2
    echo "  Use cgf_target_selected(): what are we compiling FOR?" >&2
    echo "  The host is only ever the DEFAULT for that, and target.c" >&2
    echo "  already applies it." >&2
    status=1
fi

# The architecture macros are the other way to ask the same forbidden
# question. target.c owns the host sniff; src/rt/ and src/util/softfp.c are
# compiled INTO the target program rather than into the compiler, so their
# host is the target by construction.
hits=$(grep -rnE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif).*(__x86_64__|__aarch64__|__APPLE__|__linux__|__FreeBSD__)' src/ |
    grep -v '^src/target\.c:' |
    grep -v '^src/rt/' |
    grep -v '^src/util/softfp\.c:' || true)
if [ -n "$hits" ]; then
    echo "check_target_seam: host architecture macros outside src/target.c:" >&2
    printf '%s\n' "$hits" >&2
    echo "  Target properties come from TargetSpec, never from the host." >&2
    status=1
fi

[ "$status" -eq 0 ] && echo "check_target_seam: host and target stay separate"
exit "$status"
