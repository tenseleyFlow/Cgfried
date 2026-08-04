#!/bin/sh
set -eu

doc=${1:-doc/safe-mode.md}

grep -q '^| Guarantee | Mechanism | Limit |$' "$doc"
grep -q '^| Construct | Why rejected | Alternative |$' "$doc"
grep -q '1024 blocks or 8 MiB' "$doc"
grep -q 'not a security-hardening boundary' "$doc"
grep -q '\.note\.cgf\.safe' "$doc"
grep -q -- '-fsafe-allow-unsafe=' "$doc"

counts=$(awk '
    /^## Guarantees$/ {
        table = "guarantee"
        table_rows = 0
        next
    }
    /^## Rejected constructs$/ {
        table = "rejection"
        table_rows = 0
        next
    }
    table != "" && /^## / { table = "" }
    table != "" && /^\|/ {
        table_rows++
        if (table_rows <= 2)
            next

        reason = ""
        n = split($0, fields, "|")
        for (i = 1; i <= n; i++) {
            sub(/^[[:space:]]+/, "", fields[i])
            sub(/[[:space:]]+$/, "", fields[i])
        }
        if (n != 5 || fields[1] != "" || fields[5] != "")
            reason = "expected exactly 3 cells"
        else if (fields[2] == "" || fields[3] == "")
            reason = "cells must be nonempty"
        else if (fields[4] == "")
            reason = table == "guarantee" ? "Limit cell is empty" : \
                "Alternative cell is empty"

        if (reason != "") {
            printf "%s:%d: malformed %s row: %s\n", FILENAME, NR, table, \
                reason > "/dev/stderr"
            bad = 1
        } else if (table == "guarantee")
            guarantees++
        else
            rejections++
        next
    }
    table == "" { table_rows = 0 }
    END {
        if (bad)
            exit 1
        print guarantees + 0, rejections + 0
    }
' "$doc")
guarantees=${counts%% *}
rejections=${counts#* }

[ "$guarantees" -ge 4 ] || {
    echo "safe-mode doc: expected at least four guarantee rows" >&2
    exit 1
}
[ "$rejections" -ge 6 ] || {
    echo "safe-mode doc: expected at least six rejection rows" >&2
    exit 1
}

echo "safe-mode doc: $guarantees guarantees, $rejections rejections"
