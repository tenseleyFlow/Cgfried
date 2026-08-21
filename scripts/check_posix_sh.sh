#!/bin/sh
# Every harness script must PARSE under a strict POSIX shell.
#
# CI's /bin/sh is dash. A bashism costs a full CI round-trip to find and
# has done so twice now: Sprint 15's `echo` backslash handling, and
# Sprint 28's `<(...)` process substitution in rt_diff.sh. Parsing is
# cheap and catches the syntax class outright; the behavior class
# (echo, local, ==) is caught by the greps below.
#
# This gate does NOT run the scripts — `dash -n` only parses. Running
# them under dash is still the reviewer's job when a lane changes.
set -u
LC_ALL=C
export LC_ALL

SH=$(command -v dash || command -v busybox || true)
status=0

if [ -z "$SH" ]; then
    echo "HARNESS_SKIP suite=posixsh test=all count=1" \
        "reason=\"no dash on this host; syntax not verified\""
    exit 0
fi
case "$SH" in
*busybox) SH="busybox sh" ;;
esac

for f in scripts/*.sh scripts/campaigns/*.sh ci/*.sh ci/campaigns/*.sh \
    tests/runner/meta/*.sh tests/scripts/*.sh tests/scripts/gates/*.sh; do
    [ -f "$f" ] || continue
    if ! $SH -n "$f" 2> /tmp/cgf_posix_sh.$$; then
        echo "check_posix_sh: $f is not POSIX-parseable:" >&2
        sed 's/^/  /' /tmp/cgf_posix_sh.$$ >&2
        status=1
    fi
    rm -f /tmp/cgf_posix_sh.$$
    # The parameter-substitution bashism is a runtime error that dash -n cannot
    # see, so check it on CODE only (comments legitimately name it). Shell
    # forms such as process substitution, double-bracket tests, and `function
    # name()` are syntax errors caught by dash -n; grepping for their spelling
    # would also reject valid POSIX awk programs embedded in shell strings.
    # This file is skipped because it must SPELL the remaining pattern, and
    # comment-stripping would eat an allow marker.
    if [ "$f" = "scripts/check_posix_sh.sh" ]; then
        continue
    fi
    hits=$(sed 's/#.*//' "$f" |
        grep -nE '\$\{[A-Za-z_]+/' || true)
    if [ -n "$hits" ]; then
        echo "check_posix_sh: $f uses a bashism:" >&2
        printf '%s\n' "$hits" | sed 's/^/  /' >&2
        status=1
    fi
done

[ "$status" -eq 0 ] && echo "check_posix_sh: all harness scripts parse under a POSIX shell"
exit "$status"
