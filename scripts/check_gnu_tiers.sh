#!/bin/sh
# docs/gnu-extensions.md is executable documentation: every GNU extension sits
# in exactly one tier, and this gate keeps the table honest about the code.
#
# What it enforces:
#   - every row of the IMPLEMENTED table names a fixture file that exists
#   - every row of the REFUSED table still has a message in src/ that refuses
#     it, so a tier cannot silently become "accepted and ignored"
#   - no extension appears in two tables
#
# What it deliberately does NOT enforce: that the fixture actually exercises
# the extension. A gate cannot tell a real test from a stub, and pretending
# otherwise would be its own vacuous pass -- that is what review is for.
set -u
LC_ALL=C
export LC_ALL

DOC=docs/gnu-extensions.md
fails=0

fail()
{
    echo "check_gnu_tiers: $*" >&2
    fails=$((fails + 1))
}

[ -f "$DOC" ] || {
    echo "check_gnu_tiers: $DOC is missing" >&2
    exit 1
}

# Rows are "| name | b | c |" under a "## Section" heading. awk tracks which
# section it is in; a row is any line starting with '|' that is not the header
# or the |---|---| separator.
rows_of()
{
    awk -v want="$1" '
        /^## / { sec = substr($0, 4); next }
        sec != want { next }
        /^\|/ {
            if ($0 ~ /^\|[- |]*\|$/) next
            line = $0
            sub(/^\|[ \t]*/, "", line)
            sub(/[ \t]*\|.*$/, "", line)
            gsub(/`/, "", line)
            gsub(/^[ \t]+|[ \t]+$/, "", line)
            if (line == "extension") next
            if (line == "") next
            print line
        }
    ' "$DOC"
}

fixture_of()
{
    awk -v want="$1" '
        /^## / { sec = substr($0, 4); next }
        sec != "Implemented" { next }
        /^\|/ {
            if ($0 ~ /^\|[- |]*\|$/) next
            n = split($0, f, "|")
            name = f[2]; fix = f[3]
            # Strip ALL backticks, not just the outer ones: rows_of does, and
            # a name with an INTERIOR pair -- `__asm__("name")` labels -- then
            # read differently here and reported a missing fixture for a row
            # whose fixture exists. The two must normalize identically.
            gsub(/`/, "", name)
            gsub(/`/, "", fix)
            gsub(/^[ \t]+|[ \t]+$/, "", name)
            gsub(/^[ \t]+|[ \t]+$/, "", fix)
            if (name == want) print fix
        }
    ' "$DOC"
}

implemented=$(rows_of Implemented)
ignored=$(rows_of "Parsed and ignored")
refused=$(rows_of Refused)

nimpl=0
for row in $implemented; do :; done
# Iterate line-wise: an extension name can contain spaces.
printf '%s\n' "$implemented" | while IFS= read -r name; do
    [ -n "$name" ] || continue
    fix=$(fixture_of "$name")
    if [ -z "$fix" ]; then
        fail "implemented row '$name' names no fixture"
    elif [ ! -f "$fix" ]; then
        fail "implemented row '$name' names a missing fixture: $fix"
    fi
done >"${TMPDIR:-/tmp}/gnu_tiers_impl.$$" 2>&1
if [ -s "${TMPDIR:-/tmp}/gnu_tiers_impl.$$" ]; then
    cat "${TMPDIR:-/tmp}/gnu_tiers_impl.$$" >&2
    fails=$((fails + 1))
fi
rm -f "${TMPDIR:-/tmp}/gnu_tiers_impl.$$"

# A refused extension must still be refused SOMEWHERE. The check is
# deliberately loose about wording -- it looks for the distinguishing token in
# a diagnostic string, not for an exact sentence, because tightening the
# message should not be a gate failure.
check_refused()
{
    token=$1
    label=$2

    # The token must appear in a STRING LITERAL, not merely somewhere in
    # src/. A comment explaining the refusal contains the same words, so a
    # bare grep passes whether or not the diagnostic still exists -- which
    # made the first version of the __label__ and empty-struct rows
    # VACUOUS: mutating the message away left the gate green.
    grep -rq -- "\"[^\"]*$token" src/ 2>/dev/null ||
        grep -rq -- "$token[^\"]*\"" src/ 2>/dev/null ||
        fail "refused row '$label' has no refusal left in src/ (token: $token)"
}

printf '%s\n' "$refused" | grep -q 'asm goto' &&
    check_refused 'asm goto' 'asm goto'
printf '%s\n' "$refused" | grep -q 'mode' &&
    check_refused "'mode' attribute is not supported" 'mode attribute'
printf '%s\n' "$refused" | grep -q 'vector_size' &&
    check_refused 'vector_size' 'vector_size'
printf '%s\n' "$refused" | grep -q 'nested functions' &&
    check_refused 'nested function' 'nested functions'
printf '%s\n' "$refused" | grep -q 'computed goto' &&
    check_refused 'computed goto' 'computed goto'
printf '%s\n' "$refused" | grep -q '__label__' &&
    check_refused 'block-scoped labels' '__label__'
printf '%s\n' "$refused" | grep -q 'empty struct' &&
    check_refused 'no-named-member' 'empty struct / union'

# One extension, one tier.
dup=$(printf '%s\n%s\n%s\n' "$implemented" "$ignored" "$refused" |
    grep -v '^$' | sort | uniq -d)
[ -z "$dup" ] || fail "extensions listed in more than one tier: $dup"

if [ "$fails" -ne 0 ]; then
    echo "check_gnu_tiers: $fails problem(s) in $DOC" >&2
    exit 1
fi
nimpl=$(printf '%s\n' "$implemented" | grep -c . || true)
nign=$(printf '%s\n' "$ignored" | grep -c . || true)
nref=$(printf '%s\n' "$refused" | grep -c . || true)
echo "check_gnu_tiers: $nimpl implemented, $nign parsed-ignored, $nref refused"
