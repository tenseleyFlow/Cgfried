#!/bin/sh
# The layout differential — this sprint's crown jewel.
#
# We cannot ask gcc "what is your layout?" in any portable, parseable
# form. But we CAN ask it to AGREE with ours: for each generated struct,
# take OUR `-fdump-layout` numbers, emit `_Static_assert` lines built from
# them, and hand the result to gcc. gcc accepting the file is a proof that
# gcc's layout equals ours — checked by the oracle's own constraint
# checker, with no dump format to parse and nothing version-specific to
# track.
#
# A disagreement prints the struct, our numbers, and gcc's complaint,
# which names the exact assertion that failed.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:?usage: layout_diff.sh path/to/cgfried [count] [seed]}
COUNT=${2:-500}
SEED=${3:-1}
GEN=${CGF_LAYOUT_GEN:-build/gen_layout}
GCC=${CGF_DIFF_GCC:-gcc}
WORK=${CGF_LAYOUT_WORK:-build/layout-diff}

command -v "$GCC" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=layout test=gcc-oracle count=1 reason="gcc not found"'
    exit 0
}
[ -x "$GEN" ] || {
    echo 'HARNESS_SKIP suite=layout test=generator count=1 reason="gen_layout not built"'
    exit 0
}

rm -rf "$WORK"
mkdir -p "$WORK/src"
"$GEN" --count "$COUNT" --seed "$SEED" --out "$WORK/src" >/dev/null

agree=0
disagree=0

for f in "$WORK"/src/*.c; do
    base=$(basename "$f" .c)
    asserts="$WORK/$base.check.c"

    # OUR layout, as assertions. offsetof is spelled by hand so the file
    # needs no headers at all — the differential must not depend on the
    # host's <stddef.h> being parseable by us.
    if ! "$CGF" -fdump-layout "$f" >"$WORK/$base.dump" 2>"$WORK/$base.err"; then
        echo "layout_diff: cgfried rejected $base" >&2
        sed 's/^/    /' "$WORK/$base.err" >&2
        disagree=$((disagree + 1))
        continue
    fi

    {
        cat "$f"
        echo '#define CGF_OFFSETOF(T, m) ((unsigned long)&(((T *)0)->m))'
        awk '
        /^(struct|union) / {
            # "struct S: size=N align=M"
            match($0, /size=[0-9]+/);  sz = substr($0, RSTART+5, RLENGTH-5)
            match($0, /align=[0-9]+/); al = substr($0, RSTART+6, RLENGTH-6)
            kind = $1
            printf "_Static_assert(sizeof(%s S) == %s, \"size\");\n", kind, sz
            printf "_Static_assert(_Alignof(%s S) == %s, \"align\");\n", kind, al
            next
        }
        # "  name: offset=N size=.. align=.." — bitfields have no address,
        # so only non-bitfield, named members get an offsetof assertion.
        /^  [A-Za-z_][A-Za-z_0-9]*: offset=[0-9]+ size=/ {
            name = $1; sub(/:$/, "", name)
            match($0, /offset=[0-9]+/); off = substr($0, RSTART+7, RLENGTH-7)
            printf "_Static_assert(CGF_OFFSETOF(%s S, %s) == %s, \"offset %s\");\n", kind, name, off, name
        }
        ' "$WORK/$base.dump"
    } >"$asserts"

    if "$GCC" -std=c17 -w -fsyntax-only "$asserts" 2>"$WORK/$base.gcc.err"; then
        agree=$((agree + 1))
    else
        echo "layout_diff: DISAGREEMENT on $base" >&2
        sed 's/^/    /' "$f" >&2
        echo "    --- our layout:" >&2
        sed 's/^/    /' "$WORK/$base.dump" >&2
        echo "    --- gcc:" >&2
        head -5 "$WORK/$base.gcc.err" | sed 's/^/    /' >&2
        disagree=$((disagree + 1))
    fi
done

total=$((agree + disagree))
[ "$total" -gt 0 ] || { echo "layout_diff: no files generated" >&2; exit 1; }
echo "layout_diff: $agree/$total structs agree with gcc on size, alignment and offsets"
[ "$disagree" -eq 0 ]
