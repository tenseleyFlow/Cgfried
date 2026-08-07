#!/bin/sh
# Sprint 26: the driver flag-soup differential. Every row of
# tests/driver/matrix.txt runs against BOTH gcc and cgf with identical
# argv in fresh scratch dirs; agreement is judged per the row's tags
# (see matrix.txt). Depfiles compare after normalization: continuations
# unfolded, spaces squeezed, gcc's stdc-predef.h and both compilers'
# OWN header dirs dropped (each lists its own stddef.h by design).
# Ends with the DoD-4 configure-fragment probe of the introspection
# surface. HARNESS_SKIP (loudly) when gcc is absent.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_DRIVER_MATRIX_WORK:-build/driver-matrix}
MATRIX=tests/driver/matrix.txt

if ! command -v gcc >/dev/null 2>&1; then
    echo "HARNESS_SKIP suite=drivermatrix test=all count=1" \
        "reason=\"no gcc on this host\""
    exit 0
fi

CGF_ABS=$(cd "$(dirname "$CGF")" && pwd)/$(basename "$CGF")
ROOT=$(pwd)
rm -rf "$WORK"
mkdir -p "$WORK"

fails=0
rows=0

# One scratch dir per (row, compiler): identical inputs, no cross-talk.
make_srcdir() {
    dir=$1
    mkdir -p "$dir"
    printf '#include "hdr.h"\nint main(void) { return HDRV; }\n' \
        > "$dir/main.c"
    printf '#define HDRV 0\n' > "$dir/hdr.h"
    printf 'int aux_fn(void) { return 1; }\n' > "$dir/aux.c"
    printf '#include "hdr.h"\n#include "sp ace.h"\nint d;\n' > "$dir/dep.c"
    printf 'int spmark;\n' > "$dir/sp ace.h"
    printf '#define PREV 13\n' > "$dir/pre.h"
    printf 'int main(void) { return PREV; }\n' > "$dir/imain.c"
    printf 'int main(void) { return X; }\n' > "$dir/xmain.c"
    printf 'int main(void) { return 11; }\n' > "$dir/weird.txt"
    printf -- '-c main.c\n' > "$dir/rsp_c.txt"
    printf -- '@rsp_inner.txt -o prog\n' > "$dir/rsp_outer.txt"
    printf -- "'-DX=42' xmain.c\n" > "$dir/rsp_inner.txt"
    # Sprint 27 archive-order fixtures: a STATIC archive (shared libs
    # resolve regardless of position; only .a extraction is positional).
    printf 'int zzfn(void) { return 7; }\n' > "$dir/zz.c"
    printf 'int zzfn(void);\nint main(void) { return zzfn(); }\n' \
        > "$dir/zmain.c"
    # NSS-class symbol for the static-glibc warning row (manual proto:
    # pulling in netdb.h is a Sprint 28+ header concern, not a link
    # one). A static function-pointer initializer keeps the reference
    # unfoldable — gcc folds `&f ? a : b` away and never emits it.
    printf 'extern int getaddrinfo();\nint (*volatile nss_ref)() = getaddrinfo;\nint main(void) { return 0; }\n' \
        > "$dir/nss.c"
    # Pre-built objects/archives via gcc (plain ELF inputs; the LINK is
    # what each row exercises).
    printf 'int main(void) { return 9; }\n' > "$dir/dup.c"
    (cd "$dir" && gcc -c aux.c -o aux.o 2>/dev/null &&
        gcc -c zz.c -o zz.o && ar rcs libzz.a zz.o &&
        ar rcS libnoidx.a zz.o && rm zz.o &&
        ar rcs libempty.a &&
        gcc -c zmain.c -o zmain.o && gcc -c dup.c -o dupmain.o)
}

# Unfold continuations, squeeze spaces, drop stdc-predef and each
# compiler's own header dir (absolute prereqs under /usr/lib/gcc or the
# cgf exe tree).
norm_dep() {
    awk '{ if (sub(/ *\\$/, "")) printf "%s", $0; else print }' |
        sed -e 's| /usr/include/stdc-predef\.h||g' \
            -e 's| /[^ ]*/include/[^ ]*\.h||g' \
            -e 's|  *| |g' -e 's| $||'
}

# -### stderr -> one subcommand class per line, plus the link line's
# input-order tokens (objects and -l in position). gcc prints its
# subcommand lines space-indented and mostly unquoted; cgf prints every
# arg quoted — strip quotes and classify by the first token's basename.
# Temp objects (gcc's /tmp/cc*.o, cgf's *.cgf.N.o) normalize to <obj>.
norm_plan() {
    awk '{
        line = $0
        gsub(/"/, "", line)
        sub(/^ +/, "", line)
        split(line, f, " ")
        n = split(f[1], parts, "/")
        base = parts[n]
        if (base ~ /^cc1/ || base ~ /^cgf/) { print "compile"; next }
        if (base == "as" || base ~ /afs-as/) { print "assemble"; next }
        if (base == "collect2" || base == "ld") {
            printf "link:"
            for (i = 2; i in f; i++) {
                tok = f[i]
                n2 = split(tok, p2, "/")
                b2 = p2[n2]
                if (b2 ~ /\.o$/ && b2 !~ /crt/) {
                    if (b2 ~ /^cc.*\.o$/ && tok ~ /^\/tmp\//) b2 = "<obj>"
                    if (b2 ~ /\.cgf\.[0-9]+\.o$/) b2 = "<obj>"
                    printf " %s", b2
                } else if (tok ~ /^-l/ && tok != "-lc" &&
                           tok !~ /^-lgcc/ && tok !~ /^-latomic/) {
                    printf " %s", tok
                }
            }
            print ""
            next
        }
    }'
}

# Files created by a run, relative names, sorted; scratch inputs and
# both drivers` intermediate temps excluded.
files_after() {
    dir=$1
    (cd "$dir" && ls -1 2>/dev/null) | grep -vE \
        '^(main\.c|hdr\.h|aux\.c|dep\.c|sp ace\.h|pre\.h|imain\.c|xmain\.c|weird\.txt|rsp_c\.txt|rsp_outer\.txt|rsp_inner\.txt|aux\.o|zz\.c|zmain\.c|zmain\.o|libzz\.a|libnoidx\.a|libempty\.a|nss\.c|dup\.c|dupmain\.o)$' |
        sort
}

row_fail() {
    fails=$((fails + 1))
    echo "driver_matrix FAIL row $rows [$tags]: $args ($1)" >&2
}

while IFS='|' read -r tags args; do
    case "$tags" in ''|'#'*) continue ;; esac
    rows=$((rows + 1))
    gdir="$WORK/r$rows-gcc"
    cdir="$WORK/r$rows-cgf"
    make_srcdir "$gdir"
    make_srcdir "$cdir"

    # split args on spaces, then map '~' -> ' ' inside each arg
    set --
    for aarg in $args; do
        set -- "$@" "$(printf '%s' "$aarg" | tr '~' ' ')"
    done

    case "$tags" in
    *C*)
        (cd "$cdir" && "$CGF_ABS" "$@" > out.log 2> err.log)
        cst=$?
        case "$tags" in
        *E*) [ "$cst" -ne 0 ] || row_fail "cgf unexpectedly succeeded" ;;
        *) [ "$cst" -eq 0 ] || row_fail "cgf exited $cst" ;;
        esac
        if [ "$cst" -eq 0 ]; then
            case "$tags" in
            *R*)
                (cd "$cdir" && ./prog)
                crun=$?
                # the cgf-only R rows pin the value in the SOURCE
                exp=$(grep -ho 'return [0-9]*' "$cdir/weird.txt" |
                    grep -o '[0-9]*')
                [ "$crun" = "${exp:-0}" ] ||
                    row_fail "cgf-only run exit $crun != ${exp:-0}"
                ;;
            esac
        fi
        continue
        ;;
    esac

    (cd "$gdir" && gcc "$@" > out.log 2> err.log)
    gst=$?
    (cd "$cdir" && "$CGF_ABS" "$@" > out.log 2> err.log)
    cst=$?

    case "$tags" in
    *E*)
        if [ "$gst" -eq 0 ] || [ "$cst" -eq 0 ]; then
            row_fail "expected both to fail (gcc=$gst cgf=$cst)"
        fi
        continue
        ;;
    *U*)
        # Archive-position failure (Sprint 27): both drivers fail AND
        # both name the undefined symbol.
        if [ "$gst" -eq 0 ] || [ "$cst" -eq 0 ]; then
            row_fail "expected archive-order failure (gcc=$gst cgf=$cst)"
        elif ! grep -q zzfn "$gdir/err.log" ||
            ! grep -q zzfn "$cdir/err.log"; then
            row_fail "undefined-symbol text missing from stderr"
        fi
        continue
        ;;
    *N*)
        # Static NSS-class link: both succeed AND both surface the
        # .gnu.warning text verbatim.
        if [ "$gst" -ne 0 ] || [ "$cst" -ne 0 ]; then
            row_fail "expected both static links ok (gcc=$gst cgf=$cst)"
        elif ! grep -qi getaddrinfo "$gdir/err.log" ||
            ! grep -qi getaddrinfo "$cdir/err.log"; then
            row_fail "NSS static-link warning missing from stderr"
        fi
        continue
        ;;
    esac
    if [ "$gst" -ne "$cst" ] && { [ "$gst" -eq 0 ] || [ "$cst" -eq 0 ]; }; then
        row_fail "exit status disagreement (gcc=$gst cgf=$cst)"
        continue
    fi

    case "$tags" in
    *D*)
        norm_dep < "$gdir/out.log" > "$gdir/out.norm"
        norm_dep < "$cdir/out.log" > "$cdir/out.norm"
        cmp -s "$gdir/out.norm" "$cdir/out.norm" || {
            row_fail "depfile stdout mismatch"
            diff -u "$gdir/out.norm" "$cdir/out.norm" >&2 || true
        }
        ;;
    esac
    case "$tags" in
    *G*)
        gdep=$(cd "$gdir" && ls *.d dep.out 2>/dev/null | head -1)
        cdep=$(cd "$cdir" && ls *.d dep.out 2>/dev/null | head -1)
        if [ -z "$gdep" ] || [ "$gdep" != "$cdep" ]; then
            row_fail "depfile name mismatch ('$gdep' vs '$cdep')"
        else
            norm_dep < "$gdir/$gdep" > "$gdir/d.norm"
            norm_dep < "$cdir/$cdep" > "$cdir/d.norm"
            cmp -s "$gdir/d.norm" "$cdir/d.norm" || {
                row_fail "depfile $gdep mismatch"
                diff -u "$gdir/d.norm" "$cdir/d.norm" >&2 || true
            }
        fi
        ;;
    esac
    case "$tags" in
    *A*)
        # Agreement-only row: assert the two drivers reach the SAME
        # outcome without claiming which. Used where the answer is
        # binutils-version-dependent (both drivers ride the same ld).
        gok=0; [ "$gst" -eq 0 ] || gok=1
        cok=0; [ "$cst" -eq 0 ] || cok=1
        [ "$gok" -eq "$cok" ] ||
            row_fail "outcome disagreement (gcc=$gst cgf=$cst)"
        ;;
    esac
    case "$tags" in
    *F*)
        # The listing must NOT be written inside the directory being
        # listed: the redirect creates the file concurrently with `ls`,
        # so whether it appears is a race (green on one host, red on
        # the next). Keep both listings outside the scratch dirs.
        files_after "$gdir" | grep -v '\.log$' > "$WORK/r$rows.gfiles"
        files_after "$cdir" | grep -v '\.log$' > "$WORK/r$rows.cfiles"
        cmp -s "$WORK/r$rows.gfiles" "$WORK/r$rows.cfiles" || {
            row_fail "produced-file sets differ"
            diff -u "$WORK/r$rows.gfiles" "$WORK/r$rows.cfiles" >&2 || true
        }
        ;;
    esac
    case "$tags" in
    *R*)
        # A missing binary on BOTH sides would compare 127 == 127 and
        # pass while proving nothing — assert existence first.
        if [ ! -x "$gdir/prog" ] || [ ! -x "$cdir/prog" ]; then
            row_fail "R row produced no runnable prog (gcc=$([ -x "$gdir/prog" ] && echo yes || echo no) cgf=$([ -x "$cdir/prog" ] && echo yes || echo no))"
        else
            (cd "$gdir" && ./prog)
            grun=$?
            (cd "$cdir" && ./prog)
            crun=$?
            [ "$grun" -eq "$crun" ] || row_fail "run exit gcc=$grun cgf=$crun"
        fi
        ;;
    esac
    case "$tags" in
    *P*)
        (cd "$gdir" && gcc -### "$@" > /dev/null 2> plan.log)
        (cd "$cdir" && "$CGF_ABS" -### "$@" > /dev/null 2> plan.log)
        norm_plan < "$gdir/plan.log" > "$gdir/plan.norm"
        norm_plan < "$cdir/plan.log" > "$cdir/plan.norm"
        cmp -s "$gdir/plan.norm" "$cdir/plan.norm" || {
            row_fail "subcommand plan mismatch"
            diff -u "$gdir/plan.norm" "$cdir/plan.norm" >&2 || true
        }
        ;;
    esac
done < "$MATRIX"

# --- DoD 4: the configure-fragment probe (autoconf-shaped checks) ------
probe_fail() {
    fails=$((fails + 1))
    echo "driver_matrix FAIL probe: $1" >&2
}
v=$("$CGF_ABS" --version) && printf '%s\n' "$v" |
    grep -q '^cgfried [0-9][0-9.]* (.*)$' || probe_fail "--version shape: $v"
dv=$("$CGF_ABS" -dumpversion) || probe_fail "-dumpversion exit"
printf '%s\n' "$dv" | grep -q '^[0-9][0-9.]*$' ||
    probe_fail "-dumpversion shape: $dv"
dm=$("$CGF_ABS" -dumpmachine) || probe_fail "-dumpmachine exit"
printf '%s\n' "$dm" | grep -q '^x86_64-.*linux' ||
    probe_fail "-dumpmachine shape: $dm"
pp=$("$CGF_ABS" -print-prog-name=ld) || probe_fail "-print-prog-name exit"
[ -n "$pp" ] || probe_fail "-print-prog-name empty"
pe=$("$CGF_ABS" -print-prog-name=no-such-tool-xyz) &&
    [ "$pe" = "no-such-tool-xyz" ] ||
    probe_fail "-print-prog-name echo-back: $pe"
pf=$("$CGF_ABS" -print-file-name=crt1.o) || probe_fail "-print-file-name exit"
pfe=$("$CGF_ABS" -print-file-name=no-such-file.o) &&
    [ "$pfe" = "no-such-file.o" ] || probe_fail "-print-file-name echo-back"
"$CGF_ABS" -print-search-dirs | grep -q '^install: ' ||
    probe_fail "-print-search-dirs shape"

# A .h on the command line goes to the COMPILER, never to the linker. gcc
# dispatches it as a header to precompile and hands only the .c objects to
# ld; with no `.h` row in the extension dispatch it reached the link stream
# instead and ld reported "file format not recognized; treating as linker
# script" on a header. Reported by a user whose identical gcc command line
# worked. The corpus half is tests/programs/driver/header_input_not_linked.c;
# what needs a raw command line is `-c header`, which the runner cannot
# express because it always appends its own input and -o.
printf 'int hp(void);\n' > "$WORK/probe_hdr.h"
hw=$("$CGF_ABS" -c "$WORK/probe_hdr.h" 2>&1)
case "$hw" in
*"precompiled headers are not implemented"*) ;;
*) probe_fail "-c on a header should say precompiled headers are not
implemented, got: $hw" ;;
esac
# ...and having said so, it must leave nothing behind pretending otherwise.
[ -e "$WORK/probe_hdr.h.gch" ] || [ -e "$WORK/probe_hdr.o" ] &&
    probe_fail "-c on a header produced an output file"
# A BROKEN header still has to fail, or the check above is satisfied by a
# driver that ignores headers entirely.
printf 'int hp(void)\n' > "$WORK/probe_bad.h"
if "$CGF_ABS" -c "$WORK/probe_bad.h" >/dev/null 2>&1; then
    probe_fail "a syntactically broken header was accepted"
fi

cd "$ROOT"
if [ "$fails" -ne 0 ]; then
    echo "driver_matrix: $fails failure(s) across $rows rows" >&2
    exit 1
fi
echo "driver_matrix: $rows/$rows rows agree with gcc; configure probe ok"
