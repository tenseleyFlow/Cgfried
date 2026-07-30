#!/bin/sh
# The static-initializer differential: our byte image vs gcc's.
#
# Rather than parsing an object file, the oracle simply COMPILES the same
# initializer and prints the object's bytes at runtime. That captures
# everything an emitted image must match — padding, bitfield packing,
# union overlay, float encodings — with no dependence on section layout
# or on objcopy's flags.
#
# Padding is the interesting part: it must be ZERO on both sides, which
# is a real constraint rather than an accident, and the only choice
# compatible with a byte-identical bootstrap.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:?usage: init_diff.sh path/to/cgfried [cases]}
CASES=${2:-tests/fixtures/init/cases.txt}
GCC=${CGF_DIFF_GCC:-gcc}
WORK=${CGF_INIT_WORK:-build/init-diff}

command -v "$GCC" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=initdiff test=gcc-oracle count=1 reason="gcc not found"'
    exit 0
}
[ -f "$CASES" ] || { echo "init_diff: missing $CASES" >&2; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK"

agree=0
disagree=0
n=0

while IFS= read -r line; do
    case $line in ''|\#*) continue ;; esac
    # `|` separates the prelude from the declarator: `;;` would have
    # swallowed the prelude's own terminating semicolon.
    prelude=${line%%|*}
    decl=${line#*|}
    n=$((n + 1))
    id=$(printf 'c%04d' "$n")

    # The name is the last identifier before '=' or '['.
    name=$(printf '%s' "$decl" | sed 's/ *=.*//' | sed 's/\[.*//' |
        awk '{print $NF}' | sed 's/^\*//')

    printf '%s\n%s;\n' "$prelude" "$decl" > "$WORK/$id.c"
    # The cursor is named distinctly so it cannot shadow the object under
    # test — `p` collided with a struct called `p`.
    printf '%s\n%s;\n#include <stdio.h>\nint main(void){const unsigned char *cgf_b=(const unsigned char*)&%s;unsigned long cgf_i;for(cgf_i=0;cgf_i<sizeof %s;cgf_i++)printf("%%02X",cgf_b[cgf_i]);printf("\\n");return 0;}\n' \
        "$prelude" "$decl" "$name" "$name" > "$WORK/$id.oracle.c"

    if ! "$GCC" -std=c17 -w -o "$WORK/$id.oracle" "$WORK/$id.oracle.c" \
        2>"$WORK/$id.gccerr"; then
        echo "init_diff: SKIP $id (gcc rejected): $decl" >&2
        continue
    fi
    theirs=$("$WORK/$id.oracle")
    ours=$("$CGF" -fdump-init "$WORK/$id.c" 2>"$WORK/$id.err" |
        awk -v want="$name:" '$1 == want { print $3 }' | sed 's/^bytes=//')

    if [ -z "$ours" ]; then
        echo "init_diff: NO IMAGE for $id: $decl" >&2
        sed 's/^/    /' "$WORK/$id.err" >&2
        disagree=$((disagree + 1))
        continue
    fi
    if [ "$ours" = "$theirs" ]; then
        agree=$((agree + 1))
    else
        echo "init_diff: DISAGREE $id: $decl" >&2
        echo "    ours: $ours" >&2
        echo "    gcc : $theirs" >&2
        disagree=$((disagree + 1))
    fi
done < "$CASES"

total=$((agree + disagree))
[ "$total" -gt 0 ] || { echo "init_diff: no cases" >&2; exit 1; }
echo "init_diff: $agree/$total initializer images byte-identical to gcc"
[ "$disagree" -eq 0 ]
