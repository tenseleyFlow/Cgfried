#!/bin/sh
set -eu

cc=${1:-build/cgfried}
fixtures=${2:-tests/memsafe/foundation}
work=${CGF_MEMSAFE_WORK:-build/memsafe-foundation}

if [ ! -x "$cc" ]; then
    echo "memsafe_foundation: compiler not executable: $cc" >&2
    exit 1
fi
if ! command -v timeout >/dev/null 2>&1; then
    echo "memsafe_foundation: GNU timeout is required for the <2s cap gate" >&2
    exit 1
fi

mkdir -p "$work"
count=0
trace_count=0
for source in "$fixtures"/*.c; do
    name=$(basename "$source" .c)
    out="$work/$name.out"
    err="$work/$name.err"
    second="$work/$name.second.err"
    silent_out="$work/$name.silent.out"
    silent_err="$work/$name.silent.err"
    limit=10

    if [ "$name" = branch_torture ]; then
        limit=2
    fi
    if (unset CGF_MEMSAFE_DUMP; timeout "$limit" "$cc" -fsyntax-only \
        "$source" >"$silent_out" 2>"$silent_err"); then
        :
    else
        echo "memsafe_foundation: inert compile failed for $name" >&2
        exit 1
    fi
    if [ -s "$silent_out" ] || [ -s "$silent_err" ]; then
        echo "memsafe_foundation: $name was not inert without the dump gate" >&2
        sed -n '1,80p' "$silent_out" >&2
        sed -n '1,80p' "$silent_err" >&2
        exit 1
    fi
    if timeout "$limit" env CGF_MEMSAFE_DUMP=1 CGF_VERIFY_AFTER_EACH=1 \
        "$cc" -fsyntax-only "$source" >"$out" 2>"$err"; then
        :
    else
        status=$?
        if [ "$status" -eq 124 ]; then
            echo "memsafe_foundation: $name exceeded ${limit}s" >&2
        else
            echo "memsafe_foundation: $name failed with status $status" >&2
        fi
        exit 1
    fi
    if [ -s "$out" ]; then
        echo "memsafe_foundation: $name wrote unexpected stdout" >&2
        exit 1
    fi
    if grep -E '(^|[[:space:]])(warning|error|note):' "$err" >/dev/null; then
        echo "memsafe_foundation: $name emitted a user diagnostic" >&2
        sed -n '1,160p' "$err" >&2
        exit 1
    fi
    cursor=0
    while IFS= read -r expected; do
        line=$(awk -v start="$cursor" -v needle="$expected" \
            'NR > start && index($0, needle) { print NR; exit }' "$err")
        if [ -z "$line" ]; then
            echo "memsafe_foundation: $name missing after line $cursor: $expected" >&2
            sed -n '1,160p' "$err" >&2
            exit 1
        fi
        cursor=$line
    done <<EOF
$(sed -n 's|^// MS_CHECK: ||p' "$source")
EOF
    timeout "$limit" env CGF_MEMSAFE_DUMP=1 CGF_VERIFY_AFTER_EACH=1 "$cc" \
        -fsyntax-only "$source" >"$out" 2>"$second"
    if ! cmp -s "$err" "$second"; then
        echo "memsafe_foundation: nondeterministic dump for $name" >&2
        diff -u "$err" "$second" >&2 || true
        exit 1
    fi
    count=$((count + 1))
    if grep -q '^// MS_CHECK: trace ' "$source"; then
        trace_count=$((trace_count + 1))
    fi
done

if [ "$count" -lt 10 ]; then
    echo "memsafe_foundation: expected at least 10 fixtures, found $count" >&2
    exit 1
fi
if [ "$trace_count" -lt 10 ]; then
    echo "memsafe_foundation: expected trace chains in at least 10 fixtures; "\
        "found $trace_count" >&2
    exit 1
fi

# The dump runs on a dedicated analysis module.  Prove that enabling it cannot
# perturb the separately lowered emission module.
asm_source="$fixtures/alloc_free.c"
asm_plain="$work/alloc_free.plain.s"
asm_dump="$work/alloc_free.dump.s"
asm_plain_err="$work/alloc_free.plain-asm.err"
asm_dump_err="$work/alloc_free.dump-asm.err"
unset CGF_MEMSAFE_DUMP
"$cc" -S "$asm_source" -o "$asm_plain" 2>"$asm_plain_err"
env CGF_MEMSAFE_DUMP=1 CGF_VERIFY_AFTER_EACH=1 "$cc" -S "$asm_source" \
    -o "$asm_dump" 2>"$asm_dump_err"
if [ -s "$asm_plain_err" ] || [ ! -s "$asm_dump_err" ] || \
    ! cmp -s "$asm_plain" "$asm_dump"; then
    echo "memsafe_foundation: dump gate changed assembly or stage output" >&2
    diff -u "$asm_plain" "$asm_dump" >&2 || true
    exit 1
fi

probe="$work/wmem.err"
if ! "$cc" -Wmem -fsyntax-only "$fixtures/alloc_free.c" \
    >"$work/wmem.out" 2>"$probe"; then
    echo "memsafe_foundation: the standard unknown-warning probe failed" >&2
    exit 1
fi
if ! grep -F '[-Wunknown-warning-option]' "$probe" >/dev/null; then
    echo "memsafe_foundation: -Wmem became visible before Sprint 42" >&2
    exit 1
fi

echo "memsafe_foundation: $count fixtures passed; inert gate, assembly, and "\
"verifier clean; dumps deterministic; branch cap <2s; -Wmem still deferred"
