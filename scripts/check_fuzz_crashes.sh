#!/bin/sh
# A file in tests/fuzz/crashes/ is a bug that has not been fixed yet, so
# its presence fails the build. The workflow is deliberate: the fuzzer
# writes a minimized reproducer there, the fix lands together with a
# permanent fixture under tests/programs/, and only then is the crash file
# deleted. That ordering is what stops a finding from being quietly
# forgotten once the fuzzer stops hitting it.
set -eu
LC_ALL=C
export LC_ALL

dir=${1:-tests/fuzz/crashes}
[ -d "$dir" ] || { echo "check_fuzz_crashes: clean (no crashes dir)"; exit 0; }

found=$(find "$dir" -name '*.c' | sort)
if [ -n "$found" ]; then
    echo "check_fuzz_crashes: unfixed fuzzer findings present:" >&2
    printf '%s\n' "$found" >&2
    echo "  Fix each one, add a permanent fixture, then delete the file." >&2
    exit 1
fi
echo "check_fuzz_crashes: clean"
