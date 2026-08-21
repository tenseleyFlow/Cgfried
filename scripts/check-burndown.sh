#!/bin/sh
# Sprint 61: prove that the final audit burndown row reflects the live finding
# lifecycle, rather than merely containing the desired numbers.
set -eu

LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
AUDIT_DIR=${CGF_BURNDOWN_AUDIT_DIR:-"$ROOT/.docs/audits"}
BURNDOWN=${CGF_BURNDOWN_FILE:-"$AUDIT_DIR/burndown.md"}
MANIFEST=${CGF_BURNDOWN_MANIFEST:-"$ROOT/tests/audit-regressions/manifest.tsv"}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cgf-check-burndown.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

fail()
{
    echo "burndown check: $*" >&2
    exit 1
}

[ -r "$BURNDOWN" ] || fail "missing burndown: $BURNDOWN"
[ -r "$MANIFEST" ] || fail "missing lifecycle manifest: $MANIFEST"

set -- "$AUDIT_DIR"/audit-[0-9][0-9]-*.md
[ -e "$1" ] || fail "no front ledgers found in $AUDIT_DIR"

# A front-ledger entry is resolved only when both durable parts of the ledger
# convention agree: its ID line is struck through and it carries one explicit
# RESOLVED line. Severity comes from the finding ID, not descriptive prose.
awk '
function finish_entry() {
    if (id == "")
        return
    if (struck && resolutions != 1) {
        printf "burndown check: struck finding %s must have one RESOLVED line (%s)\n", id, entry_file > "/dev/stderr"
        bad = 1
    }
    if (!struck && resolutions != 0) {
        printf "burndown check: open finding %s has a RESOLVED line (%s)\n", id, entry_file > "/dev/stderr"
        bad = 1
    }
    state = struck && resolutions == 1 ? "PASS" : "OPEN"
    severity = id
    sub(/-[0-9][0-9]$/, "", severity)
    sub(/^.*-/, "", severity)
    print id "\t" state "\t" severity
}
/^(~~)?ID: `/ {
    finish_entry()
    line = $0
    struck = line ~ /^~~ID:/
    sub(/^~~/, "", line)
    sub(/^ID: `/, "", line)
    sub(/`.*/, "", line)
    id = line
    entry_file = FILENAME
    resolutions = 0
    if (id !~ /^[A-Z0-9]+-[CHML]-[0-9][0-9]$/) {
        printf "burndown check: malformed finding ID %s (%s)\n", id, FILENAME > "/dev/stderr"
        bad = 1
    }
    next
}
/^Resolution: RESOLVED([[:space:]]|$)/ {
    if (id == "") {
        printf "burndown check: orphan RESOLVED line (%s)\n", FILENAME > "/dev/stderr"
        bad = 1
    } else {
        resolutions++
    }
}
END { finish_entry(); exit bad ? 1 : 0 }
' "$@" >"$WORK/ledger.unsorted" || exit 1

if duplicates=$(cut -f1 "$WORK/ledger.unsorted" | sort | uniq -d) &&
   [ -n "$duplicates" ]; then
    fail "duplicate front-ledger finding IDs: $duplicates"
fi
sort "$WORK/ledger.unsorted" >"$WORK/ledger"

awk -F '\t' '
!/^#/ && NF {
    if (NF != 4) {
        printf "burndown check: manifest row %d must have exactly 4 fields\n", NR > "/dev/stderr"
        bad = 1
        next
    }
    if ($1 !~ /^[A-Z0-9]+-[CHML]-[0-9][0-9]$/) {
        printf "burndown check: malformed manifest finding ID %s\n", $1 > "/dev/stderr"
        bad = 1
    }
    if ($3 != "OPEN" && $3 != "PASS") {
        printf "burndown check: malformed lifecycle state for %s: %s\n", $1, $3 > "/dev/stderr"
        bad = 1
    }
    severity = $1
    sub(/-[0-9][0-9]$/, "", severity)
    sub(/^.*-/, "", severity)
    print $1 "\t" $3 "\t" severity
}
END { exit bad ? 1 : 0 }
' "$MANIFEST" >"$WORK/manifest.unsorted" || exit 1

if duplicates=$(cut -f1 "$WORK/manifest.unsorted" | sort | uniq -d) &&
   [ -n "$duplicates" ]; then
    fail "duplicate lifecycle-manifest finding IDs: $duplicates"
fi
sort "$WORK/manifest.unsorted" >"$WORK/manifest"

if ! cmp -s "$WORK/ledger" "$WORK/manifest"; then
    diff -u "$WORK/ledger" "$WORK/manifest" >&2 || true
    fail "front-ledger resolution state disagrees with lifecycle manifest"
fi

ledger_counts=$(awk -F '\t' '
$2 == "OPEN" {
    count[$3]++
    total++
}
END {
    printf "%d %d %d %d %d", count["C"], count["H"], count["M"], count["L"], total
}
' "$WORK/ledger")

burndown_counts=$(awk -F '|' '
function trim(s) {
    sub(/^[[:space:]]+/, "", s)
    sub(/[[:space:]]+$/, "", s)
    return s
}
NF >= 10 {
    c = trim($5); h = trim($6); m = trim($7); l = trim($8); total = trim($9)
    if (c ~ /^[0-9]+$/ && h ~ /^[0-9]+$/ && m ~ /^[0-9]+$/ &&
        l ~ /^[0-9]+$/ && total ~ /^[0-9]+$/) {
        if (c + h + m + l != total) {
            printf "burndown check: row %d total %d does not equal C/H/M/L sum %d\n", NR, total, c + h + m + l > "/dev/stderr"
            bad = 1
        }
        last = c " " h " " m " " l " " total
    }
}
END {
    if (last == "") {
        print "burndown check: no numeric burndown rows found" > "/dev/stderr"
        exit 1
    }
    if (bad)
        exit 1
    print last
}
' "$BURNDOWN") || exit 1

if [ "$burndown_counts" != "$ledger_counts" ]; then
    fail "final row is C/H/M/L/total $burndown_counts; current open findings are $ledger_counts"
fi

set -- $ledger_counts
echo "burndown check: PASS ($5 open: C=$1 H=$2 M=$3 L=$4; ledger and lifecycle agree)"
