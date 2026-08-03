#!/bin/sh
# Sprint 46: compile and smoke the whole compiler under its own -fsafe policy.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
BUILD=${2:-build}
WORK=${CGF_SAFE_DOGFOOD_WORK:-$BUILD/safe-dogfood}
ALLOWLIST=ci/safe-mode-allowlist.txt

: "${CGF_AS:=0}"
export CGF_AS

case $WORK in
*/safe-dogfood) ;;
*)
    echo "safe_dogfood: refusing unsafe work directory: $WORK" >&2
    exit 2
    ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK/obj"

sh scripts/check_safe_allowlist.sh "$ALLOWLIST" \
    ci/safe-mode-allowlist.baseline.txt

sources=$(find src -name '*.c' ! -path 'src/rt/*' | sort)
objects=
count=0
for source in $sources; do
    object=$WORK/obj/${source#src/}
    object=${object%.c}.o
    mkdir -p "${object%/*}"
    "$CGF" -fsafe -Isrc -D_POSIX_C_SOURCE=200809L \
        -c "$source" -o "$object"
    readelf -S "$object" | grep -Fq '.note.cgf.safe'
    objects="$objects $object"
    count=$((count + 1))
done

target=$($CGF -dumpmachine)
runtime=$BUILD/$target/libcgf_rt.a
test -f "$runtime"

# The compiler currently emits non-PIE relocations; match its normal final-link
# model while the host linker supplies libc for this bootstrap-only gate.
# shellcheck disable=SC2086 -- deterministic sorted object list from find.
${CC:-cc} -no-pie -o "$WORK/cgfried-safe" $objects "$runtime" \
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free \
    -Wl,--wrap=realloc -Wl,--wrap=reallocarray -Wl,--wrap=strdup \
    -Wl,--wrap=strndup -Wl,--wrap=aligned_alloc \
    -Wl,--wrap=posix_memalign

"$WORK/cgfried-safe" --version >"$WORK/version.out"
CGF_SMOKE_WORK="$WORK/smoke" sh scripts/smoke.sh "$WORK/cgfried-safe"

echo "safe_dogfood: $count compiler TUs built with -fsafe; zero exemptions; safe-built smoke green"
