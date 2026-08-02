#!/bin/sh
# Keep the Sprint 37 GCC 8 warning parity matrix complete and actionable.
set -eu
LC_ALL=C
export LC_ALL

matrix=.docs/warnings-matrix.md
registry=src/warn/warnings.def

if [ ! -f "$matrix" ]; then
    echo "check_warn_matrix: missing $matrix" >&2
    exit 1
fi

awk -F '|' '
/^## Registry spellings/ {
    supplemental = 1
}
/^\|[[:space:]]*-W/ {
    flag = $2
    status = $4
    fixture = $5
    sub(/^[[:space:]]+/, "", flag)
    sub(/[[:space:]]+$/, "", flag)
    sub(/^[[:space:]]+/, "", status)
    sub(/[[:space:]]+$/, "", status)
    sub(/^[[:space:]]+/, "", fixture)
    sub(/[[:space:]]+$/, "", fixture)
    rows++
    if (!supplemental)
        raw_rows++

    if (flag !~ /^-W[^[:space:]|]*$/) {
        printf "check_warn_matrix: malformed flag on line %d: %s\n", NR, flag > "/dev/stderr"
        bad = 1
    }
    if (status != "done" && status !~ /^out-of-scope: .+/) {
        printf "check_warn_matrix: invalid or missing status on line %d: %s\n", NR, status > "/dev/stderr"
        bad = 1
    }
    if (status == "done") {
        if (fixture == "" || fixture == "—" ||
            fixture !~ /^[A-Za-z0-9_.\/-]+$/ || system("test -f " fixture) != 0) {
            printf "check_warn_matrix: done row lacks an existing fixture on line %d: %s\n", NR, fixture > "/dev/stderr"
            bad = 1
        }
    }
}
END {
    if (raw_rows != 222) {
        printf "check_warn_matrix: expected 222 raw GCC 8 Warning rows, found %d\n", raw_rows > "/dev/stderr"
        bad = 1
    }
    exit bad
}
' "$matrix"

# The registry is created by Sprint 37. Extract its first quoted field on
# each X-macro row; this deliberately does not depend on the macro name or
# on how many typed fields follow the flag string.
if [ -f "$registry" ]; then
    awk '
    /^[[:space:]]*[A-Z_][A-Z0-9_]*[[:space:]]*\(/ {
        if (match($0, /"[^"]+"/))
            print substr($0, RSTART + 1, RLENGTH - 2)
    }
    ' "$registry" | while IFS= read -r flag; do
        if ! grep -Fq -- "| -W$flag |" "$matrix"; then
            echo "check_warn_matrix: registry flag -W$flag is absent from $matrix" >&2
            exit 1
        fi
    done
fi

echo "check_warn_matrix: 222 raw GCC 8 C Warning rows accounted for"
