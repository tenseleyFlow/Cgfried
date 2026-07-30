#!/bin/sh
# Format check. clang-format is an OPTIONAL developer tool, never a build
# dependency: `make` must succeed on a machine without it. The major version
# is pinned so drift can't merge — CI installs the pin; locally, absence
# skips with a loud warning (set CGF_FORMAT_REQUIRED=1 to forbid skipping,
# as CI does).
set -eu
LC_ALL=C
export LC_ALL

PIN_MAJOR=22
BIN=""
for cand in "clang-format-$PIN_MAJOR" clang-format; do
    if command -v "$cand" >/dev/null 2>&1; then
        major=$("$cand" --version | sed -n \
            's/.*clang-format version \([0-9]*\)\..*/\1/p')
        if [ "$major" = "$PIN_MAJOR" ]; then
            BIN=$cand
            break
        fi
    fi
done

if [ -z "$BIN" ]; then
    if [ "${CGF_FORMAT_REQUIRED:-0}" = "1" ]; then
        echo "check_format: clang-format $PIN_MAJOR required but not found" >&2
        exit 1
    fi
    echo "check_format: WARNING: clang-format $PIN_MAJOR not found;" \
        "skipping (CI enforces this)" >&2
    exit 0
fi

# tests/runner/meta/ holds fixture DATA (directive bytes are load-bearing),
# not code — never formatted.
find src tests/runner tests/unit tests/fuzz \( -name '*.c' -o -name '*.h' \) \
    ! -path 'tests/runner/meta/*' | sort |
    xargs "$BIN" --dry-run -Werror
echo "check_format: clean ($BIN)"
