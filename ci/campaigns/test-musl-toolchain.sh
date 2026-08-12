#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
runner=$root/scripts/campaigns/libc-test-static-make.sh
prepare=$root/scripts/campaigns/libc-test-static-prepare.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-musl-toolchain.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$tmp/tree" "$tmp/build"
cat >"$tmp/tree/Makefile" <<'EOF'
B := src
-include config.mak

# This reproduces libc-test's parse order: config.mak is read first, then the
# suite defaults are assigned before BINS is expanded.
functional.BINS_TEMPL := bin.exe bin-static.exe
regression.BINS_TEMPL := bin.exe bin-static.exe
math.BINS_TEMPL := bin.exe
musl.BINS_TEMPL := bin.exe bin-static.exe

functional.BINS := $(functional.BINS_TEMPL:bin%=$(B)/functional/probe%)
regression.BINS := $(regression.BINS_TEMPL:bin%=$(B)/regression/probe%)
math.BINS := $(math.BINS_TEMPL:bin%=$(B)/math/probe%)
musl.BINS := $(musl.BINS_TEMPL:bin%=$(B)/musl/probe%)
BINS := $(functional.BINS) $(regression.BINS) $(math.BINS) $(musl.BINS)

$(B)/$(1)-static.err: $(B)/$(1).err

print:
	@printf '%s\n' '$(functional.BINS)' '$(regression.BINS)' \
	    '$(math.BINS)' '$(musl.BINS)'

all run: $(B)/REPORT
EOF
cat >"$tmp/tree/config.mak" <<'EOF'
functional.BINS_TEMPL := bin-static.exe
regression.BINS_TEMPL := bin-static.exe
math.BINS_TEMPL := bin-static.exe
musl.BINS_TEMPL := bin-static.exe
EOF

"$prepare" "$tmp/tree"
grep -F 'static-only lane omits the duplicate-run prerequisite' \
    "$tmp/tree/Makefile" >/dev/null
grep -F 'report the exact static-only binary manifest' \
    "$tmp/tree/Makefile" >/dev/null
# This is the literal make prerequisite that the helper must remove.
# shellcheck disable=SC2016
if grep -F '$(B)/$(1)-static.err: $(B)/$(1).err' \
    "$tmp/tree/Makefile" >/dev/null; then
    echo "campaign-musl-toolchain-meta: duplicate-run prerequisite survived" >&2
    exit 1
fi

cat >"$tmp/expected" <<EOF
$tmp/build/functional/probe-static.exe
$tmp/build/regression/probe-static.exe
$tmp/build/math/probe-static.exe
$tmp/build/musl/probe-static.exe
EOF
"$runner" "$tmp/tree" "$tmp/build" -s print >"$tmp/actual"
cmp "$tmp/expected" "$tmp/actual"
"$runner" "$tmp/tree" "$tmp/build" -s cgfried-static-manifest |
    LC_ALL=C sort >"$tmp/manifest"
LC_ALL=C sort "$tmp/expected" >"$tmp/expected-manifest"
diff -u "$tmp/expected-manifest" "$tmp/manifest"

if "$runner" >"$tmp/usage.out" 2>"$tmp/usage.err"; then
    echo "campaign-musl-toolchain-meta: missing arguments unexpectedly passed" >&2
    exit 1
fi
grep -F 'usage: TREE BUILD' "$tmp/usage.err" >/dev/null

if "$prepare" >"$tmp/prepare-usage.out" 2>"$tmp/prepare-usage.err"; then
    echo "campaign-musl-toolchain-meta: prepare missing arguments unexpectedly passed" >&2
    exit 1
fi
grep -F 'usage: TREE' "$tmp/prepare-usage.err" >/dev/null

printf 'campaign-musl-toolchain-meta: PASS\n'
