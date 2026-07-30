#!/bin/sh
# Sprint 0 driver smoke checks. Usage: sh scripts/smoke.sh build/cgfried
set -u

BIN=${1:?usage: smoke.sh path/to/cgfried}
fails=0
fail() { echo "SMOKE FAIL: $*" >&2; fails=$((fails + 1)); }

out=$("$BIN" --version) || fail "--version exit code"
case $out in
"cgfried 0.1.0 (x86_64-linux-gnu)" | "cgfried 0.1.0 (arm64-linux)" | \
"cgfried 0.1.0 (arm64-macos)" | "cgfried 0.1.0 (x86_64-linux-musl)" | \
"cgfried 0.1.0 (x86_64-freebsd)") ;;
*) fail "--version output: '$out'" ;;
esac

out=$("$BIN" -dumpversion) || fail "-dumpversion exit code"
[ "$out" = "0.1.0" ] || fail "-dumpversion output: '$out'"

"$BIN" --help >/dev/null || fail "--help exit code"
[ -n "$("$BIN" --help)" ] || fail "--help printed nothing"

"$BIN" --definitely-not-a-flag >/dev/null 2>&1
[ $? -eq 1 ] || fail "unknown option should exit 1"

err=$("$BIN" foo.c 2>&1)
[ $? -eq 1 ] || fail "input file should exit 1"
case $err in
*"only -E"*) ;;
*) fail "non--E input should say only -E is supported: '$err'" ;;
esac

"$BIN" -E /nonexistent-cgf.c >/dev/null 2>&1
[ $? -eq 3 ] || fail "-E on a missing file should exit 3 (I/O)"

"$BIN" >/dev/null 2>&1
[ $? -eq 1 ] || fail "no args should exit 1"

esc=$(printf '\033')

err=$(NO_COLOR=1 "$BIN" foo.c 2>&1)
case $err in
*"$esc"*) fail "NO_COLOR=1 output contains an escape byte" ;;
*) ;;
esac

err=$(CLICOLOR_FORCE=1 "$BIN" foo.c 2>&1)
case $err in
*"$esc"*) ;;
*) fail "CLICOLOR_FORCE=1 piped output should contain colors" ;;
esac

a=$("$BIN" foo.c 2>&1)
b=$("$BIN" foo.c 2>&1)
[ "$a" = "$b" ] || fail "diagnostic stderr is nondeterministic"

if [ "$fails" -ne 0 ]; then
    echo "smoke: $fails check(s) failed" >&2
    exit 1
fi
echo "smoke: all driver checks passed"
