#!/bin/sh
# No deferral in src/ may name a sprint that has already closed.
#
# A deferral naming a CLOSED sprint is worse than one naming an open sprint:
# it reads as tracked when nothing tracks it, and nobody reading the roadmap
# will ever find it. Nine such sites had accumulated by Sprint 51, and one of
# them -- "dynamic stack allocation lands in Sprint 49" -- was hiding a whole
# missing C construct (VLAs on arm64) two sprints after that sprint closed.
#
# The check is deliberately dumb: match the sprint-naming phrasings the
# codebase actually uses and compare the number against ci/closed_sprints.txt.
# It cannot tell a reachable deferral from a dead one -- that is what an audit
# by reachability is for -- but it does stop the number itself from rotting.
set -u
LC_ALL=C
export LC_ALL

closed=$(grep -v '^[[:space:]]*#' ci/closed_sprints.txt | grep -o '[0-9][0-9]*' |
    head -1)
if [ -z "$closed" ]; then
    echo "check_deferrals: ci/closed_sprints.txt names no sprint number" >&2
    exit 1
fi

# The macro DEFINITIONS and format strings are the mechanism, not instances:
# they carry a %d or a #n rather than a literal sprint number, so a pattern
# anchored on digits skips them without needing a path exclusion list.
bad=$(grep -rnE 'lands? in Sprint [0-9]+|land in Sprint [0-9]+' src/ |
    while IFS= read -r hit; do
        n=$(printf '%s\n' "$hit" | grep -oE 'Sprint [0-9]+' | grep -oE '[0-9]+' |
            head -1)
        [ -n "$n" ] || continue
        [ "$n" -le "$closed" ] && printf '%s\n' "$hit"
    done)

if [ -n "$bad" ]; then
    echo "check_deferrals: these defer work to a sprint that already closed" >&2
    echo "  (highest closed sprint is $closed, per ci/closed_sprints.txt)" >&2
    printf '%s\n' "$bad" | sed 's/^/  /' >&2
    echo "" >&2
    echo "Fix the gap, or reword the message to say what is actually true." >&2
    echo "Audit by REACHABILITY -- compile a program that should hit it --" >&2
    echo "never by reading: most such messages are dead defensive text, and" >&2
    echo "the one that is not looks exactly the same." >&2
    exit 1
fi

echo "check_deferrals: no deferral names a closed sprint (<= $closed)"
