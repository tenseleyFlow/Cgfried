#!/bin/sh
# Pin the ordered note chain rendered for Sprint 42 memory warnings.  The
# ordinary corpus runner owns warning location/flag checks; this harness owns
# the exact MsTrace note text and order.
set -eu
LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

cc=${1:-build/cgfried}
fixtures=${2:-tests/memsafe/wmem}
work=${CGF_MEM_WARN_WORK:-build/mem-warnings}

if [ ! -x "$cc" ]; then
    echo "memsafe_warn: compiler not executable: $cc" >&2
    exit 1
fi
if [ ! -d "$fixtures" ]; then
    echo "memsafe_warn: fixture directory not found: $fixtures" >&2
    exit 1
fi

mkdir -p "$work"
sources="$work/trace-sources.txt"
find "$fixtures" -type f -name '*.c' -print | sort > "$sources"

trace_count=0
while IFS= read -r source; do
    if ! grep -q '^// mem-trace: ' "$source"; then
        continue
    fi

    trace_count=$((trace_count + 1))
    name=$(printf '%s' "$source" | sed 's|[^A-Za-z0-9_.-]|_|g')
    expected="$work/$name.expected"
    actual="$work/$name.actual"
    err="$work/$name.err"
    out="$work/$name.out"
    flags=$(sed -n 's|^// mem-flags: ||p' "$source")

    if [ "$(printf '%s\n' "$flags" | wc -l | tr -d ' ')" -gt 1 ]; then
        echo "memsafe_warn: multiple mem-flags directives in $source" >&2
        exit 1
    fi
    if [ -n "$flags" ] && printf '%s\n' "$flags" | grep -q '[^A-Za-z0-9_+.,=/ -]'; then
        echo "memsafe_warn: unsafe mem-flags text in $source" >&2
        exit 1
    fi

    sed -n 's|^// mem-trace: ||p' "$source" > "$expected"
    # mem-flags is deliberately a whitespace-separated compiler flag list.
    # The character whitelist above prevents shell metacharacters.
    if "$cc" -fsyntax-only -Wmem $flags "$source" >"$out" 2>"$err"; then
        :
    else
        echo "memsafe_warn: compile failed for $source" >&2
        sed -n '1,160p' "$err" >&2
        exit 1
    fi
    if [ -s "$out" ]; then
        echo "memsafe_warn: unexpected stdout for $source" >&2
        exit 1
    fi
    awk '/: note: / {
        line = $0
        sub(/^.*: note: /, "", line)
        print line
    }' "$err" > "$actual"
    if ! cmp -s "$expected" "$actual"; then
        echo "memsafe_warn: trace changed for $source" >&2
        diff -u "$expected" "$actual" >&2 || true
        exit 1
    fi
done < "$sources"

if [ "$trace_count" -lt 10 ]; then
    echo "memsafe_warn: expected at least 10 trace fixtures, found $trace_count" >&2
    exit 1
fi

echo "memsafe_warn: $trace_count exact ordered note sequences passed"
