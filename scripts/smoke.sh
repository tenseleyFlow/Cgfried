#!/bin/sh
# Sprint 0 driver smoke checks. Usage: sh scripts/smoke.sh build/cgfried
set -u

BIN=${1:?usage: smoke.sh path/to/cgfried}
work=${CGF_SMOKE_WORK:-build/smoke}
fails=0
fail() { echo "SMOKE FAIL: $*" >&2; fails=$((fails + 1)); }

mkdir -p "$work"
printf 'int main(void) { return @; }\n' >"$work/error.c"

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

err=$("$BIN" /nonexistent-cgf.c 2>&1)
[ $? -eq 3 ] || fail "missing input file should exit 3"
case $err in
*"cannot open"*) ;;
*) fail "missing input should name the open failure: '$err'" ;;
esac

"$BIN" -E /nonexistent-cgf.c >/dev/null 2>&1
[ $? -eq 3 ] || fail "-E on a missing file should exit 3 (I/O)"

"$BIN" >/dev/null 2>&1
[ $? -eq 1 ] || fail "no args should exit 1"

esc=$(printf '\033')

err=$(NO_COLOR=1 "$BIN" -fsyntax-only "$work/error.c" 2>&1)
case $err in
*"$esc"*) fail "NO_COLOR=1 output contains an escape byte" ;;
*) ;;
esac

err=$(NO_COLOR= CLICOLOR_FORCE=1 "$BIN" -fsyntax-only "$work/error.c" 2>&1)
case $err in
*"$esc"*) ;;
*) fail "CLICOLOR_FORCE=1 piped output should contain colors" ;;
esac

a=$("$BIN" -fsyntax-only "$work/error.c" 2>&1)
b=$("$BIN" -fsyntax-only "$work/error.c" 2>&1)
[ "$a" = "$b" ] || fail "diagnostic stderr is nondeterministic"

if [ "$fails" -ne 0 ]; then
    echo "smoke: $fails check(s) failed" >&2
    exit 1
fi
echo "smoke: all driver checks passed"
