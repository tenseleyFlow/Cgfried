#!/bin/sh
# Sprint 38 warning differential: compare every warning fixture's exact
# (source line, warning flag) set against the GCC 8 baseline.
set -eu
LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

CGF=${1:?usage: warn_diff.sh path/to/cgfried}
GCC8=${CGF_DIFF_GCC8:-gcc-8}
WORK=${CGF_WARN_DIFF_WORK:-build/warn-diff}

command -v "$GCC8" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=warndiff test=gcc8-oracle count=1 reason="gcc-8 not found (set CGF_DIFF_GCC8)"'
    exit 0
}

rm -rf "$WORK"
mkdir -p "$WORK"
agree=0
normalized=0
annotated=0
disagree=0
serial=0

warning_set() {
    awk '
    /:[0-9][0-9]*:[0-9][0-9]*: (warning|error):/ && /\[-W[^]]*\]/ {
        location = $0
        sub(/^[^:]*:/, "", location)
        sub(/:.*/, "", location)
        flag = $0
        sub(/^.*\[-W/, "", flag)
        sub(/\].*$/, "", flag)
        sub(/^error=/, "", flag)
        sub(/=$/, "", flag)
        print location, flag
    }
    ' "$1" | sort -u > "$2"
}

check() {
    file=$1
    flags=$(sed -n 's@^// FLAGS: @@p' "$file" | sed -n '1p')
    serial=$((serial + 1))
    cgf_err="$WORK/$serial.cgf.err"
    gcc_err="$WORK/$serial.gcc.err"
    cgf_set="$WORK/$serial.cgf.set"
    gcc_set="$WORK/$serial.gcc.set"
    gcc_file=$file
    cgf_mode=
    gcc_mode=
    gcc_flags=$flags
    cgf_only=$(sed -n \
        's@^// DIVERGES(gcc-8): cgf-only-warning=\([a-z0-9-][a-z0-9-]*\)$@\1@p' \
        "$file")

    if [ -n "$cgf_only" ]; then
        gcc_flags=
        for flag in $flags; do
            if [ "$flag" != "-W$cgf_only" ]; then
                gcc_flags="$gcc_flags $flag"
            fi
        done
    fi

    # GCC rejects the runner's // metadata in strict C89 before reaching the
    # program.  Blank only those harness lines in a line-preserving oracle
    # copy; the language flags and program tokens remain identical.
    case " $flags " in
    *' -std=c89 '*)
        gcc_file="$WORK/$serial.gcc.c"
        sed 's@^//.*$@@' "$file" > "$gcc_file"
        ;;
    esac
    # -S fixtures need distinct output files, but both compilers receive the
    # same compile mode and option set.
    case " $flags " in
    *' -S '*)
        cgf_mode="-o $WORK/$serial.cgf.s"
        gcc_mode="-o $WORK/$serial.gcc.s"
        ;;
    esac

    if "$CGF" $flags $cgf_mode "$file" >/dev/null 2>"$cgf_err"; then
        cgf_status=0
    else
        cgf_status=$?
    fi
    if "$GCC8" $gcc_flags $gcc_mode "$gcc_file" >/dev/null 2>"$gcc_err"; then
        gcc_status=0
    else
        gcc_status=$?
    fi
    warning_set "$cgf_err" "$cgf_set"
    warning_set "$gcc_err" "$gcc_set"

    if [ -n "$cgf_only" ]; then
        cgf_normalized="$WORK/$serial.cgf.normalized.set"
        cgf_extension="$WORK/$serial.cgf.extension.set"
        awk -v flag="$cgf_only" '$2 == flag { print }' "$cgf_set" \
            > "$cgf_extension"
        awk -v flag="$cgf_only" '$2 != flag { print }' "$cgf_set" \
            > "$cgf_normalized"
        extension_count=$(wc -l < "$cgf_extension" | tr -d ' ')
        if [ "$extension_count" -eq 1 ] &&
            [ "$cgf_status" -eq "$gcc_status" ] &&
            cmp -s "$cgf_normalized" "$gcc_set"; then
            normalized=$((normalized + 1))
            return
        fi
        echo "warn_diff: DISAGREE $file: cgf-only $cgf_only contract failed" >&2
        echo "warn_diff: ours-exit=$cgf_status gcc8-exit=$gcc_status extension-count=$extension_count" >&2
        diff -u "$gcc_set" "$cgf_normalized" >&2 || true
        disagree=$((disagree + 1))
        return
    fi

    if [ "$cgf_status" -eq "$gcc_status" ] && cmp -s "$cgf_set" "$gcc_set"; then
        agree=$((agree + 1))
        return
    fi
    if grep -Eq '^// DIVERGES\(gcc-8\): .+' "$file"; then
        annotated=$((annotated + 1))
        echo "warn_diff: ANNOTATED $file"
        return
    fi

    echo "warn_diff: DISAGREE $file: ours-exit=$cgf_status gcc8-exit=$gcc_status" >&2
    diff -u "$gcc_set" "$cgf_set" >&2 || true
    disagree=$((disagree + 1))
}

for file in $(find tests/warn -type f -name '*.c' | sort); do
    check "$file"
done

total=$((agree + normalized + annotated + disagree))
echo "warn_diff: $agree/$total exact warning sets match GCC 8; $normalized normalized CGF-only; $annotated annotated; $disagree unannotated"
[ "$disagree" -eq 0 ]
