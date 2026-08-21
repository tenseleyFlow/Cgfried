#!/bin/sh
# Sprint 61 closeout gate: every roadmap phase has a structurally complete,
# evidence-backed closeout, and Sprint 62 stays blocked until all are READY.
set -u

LC_ALL=C
export LC_ALL

root=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
checker=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/check-closeouts.awk
audits=$root/.docs/audits
manifest=$root/ci/closeout-dod.tsv
expected_sprints=${CGF_CLOSEOUT_EXPECTED_SPRINTS:-62}
expected_items=${CGF_CLOSEOUT_EXPECTED_ITEMS:-429}
status=0
count=0
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-check-closeouts.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail()
{
    echo "check-closeouts: $*" >&2
    status=1
}

check_dod_coverage()
{
    phase=$1
    file=$2
    number=$3
    expected=$work/expected-$number
    actual=$work/actual-$number
    awk -F '\t' -v phase="$phase" '
    !/^#/ && NF && $1 == phase {
        for (item = 1; item <= $3; item++)
            print "S" $2 "." item
    }
    ' "$manifest" >"$expected"

    awk '
    $0 == "## DoD items (from the phase\047s sprint files, every numbered item)" {
        in_dod = 1
        next
    }
    in_dod && /^## / { in_dod = 0 }
    in_dod && /^- \[[x ]\] S[0-9]+\.[0-9]+ / {
        item = $0
        sub(/^- \[[x ]\] /, "", item)
        sub(/ .*/, "", item)
        print item
    }
    ' "$file" >"$actual"

    if duplicates=$(sort "$actual" | uniq -d) && [ -n "$duplicates" ]; then
        fail "duplicate DoD item(s) in $(basename -- "$file"): $duplicates"
        return
    fi
    sort -u "$expected" -o "$expected"
    sort -u "$actual" -o "$actual"
    if ! cmp -s "$expected" "$actual"; then
        diff -u "$expected" "$actual" >&2 || true
        fail "DoD coverage differs from tracked manifest in $(basename -- "$file")"
    fi
}

[ -r "$manifest" ] || {
    fail "missing DoD manifest: ci/closeout-dod.tsv"
    exit 1
}

awk -F '\t' -v expected_sprints="$expected_sprints" \
    -v expected_items="$expected_items" '
!/^#/ && NF {
    if (NF != 3 || $1 !~ /^(0[0-9]|1[0-3])-[a-z0-9_]+(-[a-z0-9_]+)*$/ ||
        $2 !~ /^[0-9]+$/ || $3 !~ /^[1-9][0-9]*$/) {
        printf "check-closeouts: malformed DoD manifest row %d\n", NR > "/dev/stderr"
        bad = 1
        next
    }
    key = $1 SUBSEP $2
    if (seen[key]++) {
        printf "check-closeouts: duplicate phase/sprint row %s S%s\n", $1, $2 > "/dev/stderr"
        bad = 1
    }
    sprint_count++
    item_count += $3
    phase[$1] = 1
    phase_number[substr($1, 1, 2)] = 1
}
END {
    for (name in phase)
        phase_count++
    if (sprint_count != expected_sprints) {
        printf "check-closeouts: expected %d sprint rows, found %d\n", expected_sprints, sprint_count > "/dev/stderr"
        bad = 1
    }
    if (item_count != expected_items) {
        printf "check-closeouts: expected %d DoD items, found %d\n", expected_items, item_count > "/dev/stderr"
        bad = 1
    }
    if (phase_count != 14) {
        printf "check-closeouts: expected 14 phases, found %d\n", phase_count > "/dev/stderr"
        bad = 1
    }
    for (i = 0; i < 14; i++) {
        number = sprintf("%02d", i)
        if (!phase_number[number]) {
            printf "check-closeouts: missing Phase %s manifest rows\n", number > "/dev/stderr"
            bad = 1
        }
    }
    exit bad ? 1 : 0
}
' "$manifest" || exit 1

for base in $(awk -F '\t' '!/^#/ && NF { print $1 }' "$manifest" | sort -u); do
    number=${base%%-*}
    file=$audits/closeout-$base.md
    if [ ! -f "$file" ] || [ -L "$file" ]; then
        fail "missing regular closeout file: .docs/audits/closeout-$base.md"
        continue
    fi
    count=$((count + 1))

    if ! awk -v expected="$number" -v shown=".docs/audits/closeout-$base.md" \
        -f "$checker" "$file"; then
        status=1
    fi
    check_dod_coverage "$base" "$file" "$number"
done

if [ -d "$audits" ]; then
    actual=$(find "$audits" -maxdepth 1 -type f -name 'closeout-*.md' | wc -l |
        tr -d '[:space:]')
    [ "$actual" -eq 14 ] || fail "expected exactly 14 closeout files, found $actual"
fi

if [ "$status" -eq 0 ] && [ "$count" -eq 14 ]; then
    echo "check-closeouts: 14 phase closeouts are conforming and READY"
    exit 0
fi
exit 1
