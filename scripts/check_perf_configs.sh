#!/bin/sh
# Prove that the committed gate lattice and its public documentation agree.
set -eu

prog=check_perf_configs
repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
config_dir=${1:-$repo/ci/gates.d}
document=${2:-$repo/doc/perf-gates.md}
closed_sprints=${3:-$repo/ci/closed_sprints.txt}

die()
{
    echo "$prog: $*" >&2
    exit 3
}

[ -d "$config_dir" ] || die "config directory does not exist: $config_dir"
[ -r "$document" ] || die "cannot read document: $document"
[ -r "$closed_sprints" ] || die "cannot read closed-sprint ratchet: $closed_sprints"

closed=$(awk '
/^[[:space:]]*#/ || /^[[:space:]]*$/ {
    next
}
{
    entries++
    if ($0 !~ /^[0-9][0-9]*$/)
        malformed = 1
    value = $0
}
END {
    if (entries != 1 || malformed)
        exit 3
    print value
}
' "$closed_sprints") ||
    die "$closed_sprints: expected exactly one bare nonnegative integer line"

work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-perf-configs.XXXXXX") ||
    die "cannot create temporary directory"
trap 'rm -rf "$work"' EXIT HUP INT TERM
: >"$work/config-names"
count=0

for config in "$config_dir"/*.conf; do
    [ -f "$config" ] || continue
    values=$(awk -v closed="$closed" '
function'" "'fail(message) {
    print "check_perf_configs: " FILENAME ":" FNR ": " message > "/dev/stderr"
    bad = 1
}
function'" "'trim(value) {
    sub(/^[[:space:]]+/, "", value)
    sub(/[[:space:]]+$/, "", value)
    return value
}
{
    line = $0
    sub(/[[:space:]]*#.*/, "", line)
    line = trim(line)
    if (line == "")
        next
    equals = index(line, "=")
    if (!equals) {
        fail("expected key=value")
        next
    }
    key = trim(substr(line, 1, equals - 1))
    value = trim(substr(line, equals + 1))
    if (key !~ /^[a-z][a-z0-9_]*$/ || value == "") {
        fail("invalid configuration entry")
        next
    }
    if (key in seen) {
        fail("duplicate key " key)
        next
    }
    seen[key] = 1
    value_of[key] = value
}
END {
    required[1] = "name"
    required[2] = "state"
    required[3] = "where"
    required[4] = "when"
    required[5] = "threshold"
    required[6] = "rationale"
    required[7] = "owner_sprint"
    for (i = 1; i <= 7; i++)
        if (!(required[i] in value_of))
            fail("missing " required[i])
    state = value_of["state"]
    if (state !~ /^(blocking|trial|report-only|inactive)$/)
        fail("invalid state " state)
    if (state == "trial" &&
        (value_of["quiet_days"] != "14" ||
         value_of["false_trip_limit"] != "2" ||
         value_of["false_trip_window_days"] != "30"))
        fail("trial gate must encode the 14-day/2-in-30-day policy")
    if (state == "inactive" && !("defer_until" in value_of))
        fail("inactive gate lacks defer_until")
    if (state == "inactive" && ("defer_until" in value_of)) {
        deferred = value_of["defer_until"]
        if (deferred !~ /^Sprint [0-9][0-9]*$/)
            fail("inactive gate defer_until must be exactly Sprint N")
        else if (substr(deferred, 8) + 0 <= closed + 0)
            fail("inactive gate defers to closed " deferred)
    }
    if (state != "inactive" && ("defer_until" in value_of))
        fail("active gate must not set defer_until")
    if (bad)
        exit 3
    print value_of["name"]
    print state
}
' "$config") || exit $?
    name=$(printf '%s\n' "$values" | sed -n '1p')
    state=$(printf '%s\n' "$values" | sed -n '2p')
    case $name in
    ''|*[!a-z0-9-]*) die "$config: invalid name '$name'" ;;
    esac
    base=${config##*/}
    base=${base%.conf}
    [ "$base" = "$name" ] ||
        die "$config: filename must match name=$name"
    printf '%s\n' "$name" >>"$work/config-names"
    printf '%s\t%s\n' "$name" "$state" >>"$work/config-states"
    count=$((count + 1))
done

[ "$count" -gt 0 ] || die "no gate configs found in $config_dir"
if [ "$(sort "$work/config-names" | uniq -d | wc -l | tr -d ' ')" -ne 0 ]; then
    die "duplicate gate names"
fi

sed -n 's/.*<!-- perf-gate:\([a-z0-9][a-z0-9-]*\) -->.*/\1/p' "$document" |
    sort >"$work/doc-names"
sort "$work/config-names" >"$work/config-names.sorted"

if ! cmp -s "$work/config-names.sorted" "$work/doc-names"; then
    echo "$prog: gate configs and doc markers differ:" >&2
    diff -u "$work/config-names.sorted" "$work/doc-names" >&2 || true
    exit 3
fi

echo "$prog: $count gate configs match doc/perf-gates.md"
