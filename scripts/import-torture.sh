#!/bin/sh

set -eu
LC_ALL=C
export LC_ALL

GCC_PIN=7c38c56214bf2809399a2198441bb48ee1b00512
GCC_COPYING_SHA256=231f7edcc7352d7734a96eef0b8030f77982678c516876fcb81e25b32d68564c
GCC_COPYING3_SHA256=8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903
GCC_README_SHA256=49306c701a64d02dc25de7c89eac5643a3e73c159b4aa9438b47f6b9d86ba0df

script_dir=$(dirname -- "$0")
repo=$(cd "$script_dir/.." && pwd)
ref=${CGF_GCC_REF:-$repo/.docs/refs/gcc}
out=${CGF_TORTURE_OUT:-$repo/tests/torture}
policy=${CGF_TORTURE_POLICY:-$repo/tests/torture-policy.tsv}

fail()
{
    echo "import-torture: $*" >&2
    exit 1
}

sha256_file()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        fail "sha256sum or shasum -a 256 is required"
    fi
}

check_path()
{
    case $1 in
        ''|/*|*..*) fail "unsafe imported path: $1" ;;
        *[!A-Za-z0-9_./+~@-]* ) fail "unsafe imported path: $1" ;;
    esac
}

check_ref()
{
    test -d "$ref" || fail "GCC reference is absent: $ref"
    actual=$(git -C "$ref" rev-parse HEAD 2>/dev/null) ||
        fail "GCC reference is not a git checkout: $ref"
    test "$actual" = "$GCC_PIN" ||
        fail "GCC revision mismatch: expected $GCC_PIN, got $actual"
    source_root=$ref/gcc/testsuite/gcc.c-torture
    test -d "$source_root/compile" && test -d "$source_root/execute" ||
        fail "GCC c-torture trees are absent under $source_root"
    test -f "$ref/COPYING" && test -f "$ref/COPYING3" &&
        test -f "$ref/README" || fail "GCC license/notice set is incomplete"
    dirty=$(git -C "$ref" status --porcelain=v1 --untracked-files=all \
        --ignored -- COPYING COPYING3 README gcc/testsuite/gcc.c-torture \
        2>/dev/null) ||
        fail "cannot inspect GCC reference cleanliness"
    test -z "$dirty" || fail "GCC import paths contain local modifications"
}

check_policy()
{
    test -f "$policy" || fail "torture policy is absent: $policy"
    test "$(sed -n '1p' "$policy")" = '# cgf-torture-policy-v1' ||
        fail "torture policy schema header is invalid"
    while IFS="$(printf '\t')" read -r path disposition reason extra; do
        case $path in ''|'#'*) continue ;; esac
        test -z "${extra:-}" || fail "malformed torture policy row: $path"
        check_path "$path"
        case $disposition in
            run)
                test "$reason" = - || fail "run policy reason must be -: $path"
                ;;
            skip|xfail:TORT-[0-9][0-9][0-9])
                test "$reason" != - || fail "non-run policy needs a reason: $path"
                ;;
            *) fail "invalid torture policy disposition for $path: $disposition" ;;
        esac
        test -n "$reason" || fail "empty torture policy reason: $path"
    done <"$policy"
    awk -F '\t' '!/^#/ && NF { if (previous != "" && $1 <= previous) exit 1;
        previous=$1 }' "$policy" ||
        fail "torture policy paths must be sorted and unique"
}

validate_manifest_schema()
{
    awk -F '\t' '
    BEGIN {
        closed=" arm_arch_v5t_thumb_ok bfloat16_runtime c99_runtime dfp dfprt double64 double64plus exceptions fgraphite fileio float128_runtime float128x_runtime float16_runtime float32_runtime float32x_runtime float64_runtime float64x_runtime fopenmp fpic gnu_retain indirect_calls indirect_jumps int128 int32 int32plus label_values longlong64 mmap named_sections non_strict_prototype nonlocal_goto nonpic pthread ptr32plus return_address run_expensive_tests scheduling size20plus size32plus stdint_types trampolines untyped_assembly unwrapped "
    }
    /^#/ { if (data) exit 1; next }
    {
        data=1
        rows++
        if (NF != 7 || $1 == "" || length($2) != 64 || $2 !~ /^[0-9a-f]+$/)
            exit 1
        if (previous != "" && $1 <= previous) exit 1
        previous=$1
        expected="asset"
        if ($1 ~ /^compile\/.*\.c$/) expected="compile"
        else if ($1 ~ /^execute-ieee\/.*\.c$/) expected="run-ieee"
        else if ($1 ~ /^execute\/.*\.c$/ && $1 !~ /\/lib\// &&
                 $1 !~ /-lib\.c$/) expected="run"
        if ($3 != expected) exit 1
        if ($3 == "asset") {
            if ($4 != "-" || $5 != "-" || $6 != "-" || $7 != "-") exit 1
            next
        }
        if ($4 != "-") {
            count=split($4, requirement, ",")
            prior=""
            for (i=1; i<=count; i++) {
                if (index(closed, " " requirement[i] " ") == 0 ||
                    (prior != "" && requirement[i] <= prior)) exit 1
                prior=requirement[i]
            }
        }
        if ($5 != "-") {
            count=split($5, flag, / +/)
            for (i=1; i<=count; i++) {
                valid = flag[i] == "-fcommon" || flag[i] == "-fno-common" ||
                    flag[i] == "-fwrapv" || flag[i] == "-fno-strict-aliasing" ||
                    flag[i] == "-fomit-frame-pointer" ||
                    flag[i] == "-fno-omit-frame-pointer" ||
                    flag[i] ~ /^-std=(c89|c90|c99|c11|c17|c18|gnu89|gnu90|gnu99|gnu11|gnu17|gnu18)$/
                if (!valid) exit 1
            }
        }
        if ($6 == "run") {
            if ($7 != "-") exit 1
        } else if ($6 == "skip" || $6 ~ /^xfail:TORT-[0-9][0-9][0-9]$/) {
            if ($7 == "-" || $7 == "") exit 1
        } else exit 1
    }
    END { if (rows == 0) exit 1 }' "$out/MANIFEST" ||
        fail "manifest behavioral schema is invalid"
}

policy_for()
{
    awk -F '\t' -v wanted="$1" '$1 == wanted { print $2 "\t" $3; found=1 }
        END { if (!found) exit 1 }' "$policy"
}

quoted_include_problem()
{
    source=$1
    tree=$2
    rel=$3
    source_dir=$(dirname -- "$rel")
    awk 'match($0, /^[ \t]*#[ \t]*include[ \t]*"[^"]+"/) {
        text=substr($0, RSTART, RLENGTH)
        sub(/^[^"]*"/, "", text)
        sub(/"$/, "", text)
        print text
    }' "$source" | while IFS= read -r include; do
        case $include in
            /*) normalized=ESCAPED ;;
            *)
                normalized=$(printf '%s/%s\n' "$source_dir" "$include" | awk -F / '
                    {
                        depth=0
                        for (i=1; i<=NF; i++) {
                            if ($i == "" || $i == ".") continue
                            if ($i == "..") {
                                if (depth == 0) { print "ESCAPED"; exit }
                                depth--
                            } else component[++depth]=$i
                        }
                        for (i=1; i<=depth; i++)
                            printf "%s%s", (i == 1 ? "" : "/"), component[i]
                        printf "\n"
                    }')
                ;;
        esac
        if test "$normalized" = ESCAPED || test ! -f "$tree/$normalized"; then
            printf 'quoted include unavailable: %s\n' "$include"
            exit 0
        fi
    done
}

# Output: requirement<TAB>flags<TAB>disposition<TAB>reason.  This parser
# deliberately understands a small closed subset.  Anything conditional or
# target-specific outside that subset becomes an explicit skip.
metadata_for()
{
    file=$1
    expected_mode=$2
    awk -v expected_mode="$expected_mode" '
    BEGIN { disposition="run"; reason="-" }
    {
        line=$0
        scan=line
        while (match(scan, /\{[ \t]*dg-[a-z-]+/)) {
            name=substr(scan, RSTART, RLENGTH)
            sub(/^\{[ \t]*/, "", name)
            if (name != "dg-do" && name != "dg-options" &&
                name != "dg-additional-options" && name != "dg-skip-if" &&
                name != "dg-require-effective-target" && disposition != "skip") {
                disposition="skip"
                reason="unsupported directive " name
            }
            scan=substr(scan, RSTART + RLENGTH)
        }
        if (line ~ /\{[ \t]*dg-skip-if[ \t]/ && disposition != "skip") {
            disposition="skip"
            reason="upstream dg-skip-if condition"
        }
        if (line ~ /\{[ \t]*dg-do[ \t]/) {
            text=line
            sub(/^.*\{[ \t]*dg-do[ \t]+/, "", text)
            action=text
            sub(/[ \t}].*$/, "", action)
            tail=text
            sub(/^[^ \t}]*/, "", tail)
            if (tail !~ /^[ \t]*}/ && disposition != "skip") {
                disposition="skip"; reason="conditional dg-do"
            } else if (action != "compile" && action != "run" && disposition != "skip") {
                disposition="skip"; reason="unsupported dg-do " action
            }
            else if ((action == "compile" && expected_mode != "compile") ||
                     (action == "run" && expected_mode == "compile")) {
                if (disposition != "skip") {
                    disposition="skip"; reason="dg-do mode mismatch"
                }
            }
        }
        if (line ~ /\{[ \t]*dg-require-effective-target[ \t]/) {
            text=line
            sub(/^.*\{[ \t]*dg-require-effective-target[ \t]+/, "", text)
            target=text
            sub(/[ \t}].*$/, "", target)
            tail=text
            sub(/^[^ \t}]*/, "", tail)
            if (tail !~ /^[ \t]*}/ && disposition != "skip") {
                disposition="skip"; reason="conditional effective-target " target
            }
            else if (index(" arm_arch_v5t_thumb_ok bfloat16_runtime c99_runtime dfp dfprt double64 double64plus exceptions fgraphite fileio float128_runtime float128x_runtime float16_runtime float32_runtime float32x_runtime float64_runtime float64x_runtime fopenmp fpic gnu_retain indirect_calls indirect_jumps int128 int32 int32plus label_values longlong64 mmap named_sections non_strict_prototype nonlocal_goto nonpic pthread ptr32plus return_address run_expensive_tests scheduling size20plus size32plus stdint_types trampolines untyped_assembly unwrapped ", " " target " ") == 0) {
                if (disposition != "skip") {
                    disposition="skip"; reason="unsupported effective-target " target
                }
            }
            else req[target]=1
        }
        if (line ~ /\{[ \t]*dg-(options|additional-options)[ \t]/) {
            text=line
            q1=index(text, "\"")
            if (!q1) {
                if (disposition != "skip") {
                    disposition="skip"; reason="malformed options directive"
                }
                next
            }
            text=substr(text, q1+1)
            q2=index(text, "\"")
            if (!q2) {
                if (disposition != "skip") {
                    disposition="skip"; reason="malformed options directive"
                }
                next
            }
            opts=substr(text, 1, q2-1)
            tail=substr(text, q2+1)
            if (tail !~ /^[ \t]*}/) {
                if (disposition != "skip") {
                    disposition="skip"; reason="conditional options directive"
                }
            } else {
                gsub(/^[ \t]+|[ \t]+$/, "", opts)
                if (opts != "") {
                    count=split(opts, option, /[ \t]+/)
                    for (i=1; i<=count; i++) {
                        valid = option[i] == "-fcommon" || option[i] == "-fno-common" ||
                            option[i] == "-fwrapv" || option[i] == "-fno-strict-aliasing" ||
                            option[i] == "-fomit-frame-pointer" ||
                            option[i] == "-fno-omit-frame-pointer" ||
                            option[i] ~ /^-std=(c89|c90|c99|c11|c17|c18|gnu89|gnu90|gnu99|gnu11|gnu17|gnu18)$/
                        if (!valid) {
                            if (disposition != "skip") {
                                disposition="skip"; reason="unsupported flag " option[i]
                            }
                        } else
                            flags=flags (flags == "" ? "" : " ") option[i]
                    }
                }
            }
        }
    }
    END {
        order="arm_arch_v5t_thumb_ok bfloat16_runtime c99_runtime dfp dfprt double64 double64plus exceptions fgraphite fileio float128_runtime float128x_runtime float16_runtime float32_runtime float32x_runtime float64_runtime float64x_runtime fopenmp fpic gnu_retain indirect_calls indirect_jumps int128 int32 int32plus label_values longlong64 mmap named_sections non_strict_prototype nonlocal_goto nonpic pthread ptr32plus return_address run_expensive_tests scheduling size20plus size32plus stdint_types trampolines untyped_assembly unwrapped"
        n=split(order, names, " ")
        requirements=""
        for (i=1; i<=n; i++) if (req[names[i]])
            requirements=requirements (requirements == "" ? "" : ",") names[i]
        if (requirements == "") requirements="-"
        if (flags == "") flags="-"
        print requirements "\t" flags "\t" disposition "\t" reason
    }' "$file"
}

write_manifest()
{
    tree=$1
    manifest=$tree/MANIFEST
    rows=$tree/.manifest.rows
    : >"$rows"
    find "$tree" -type f ! -name MANIFEST ! -name passing.txt \
        ! -name .manifest.rows -print |
        LC_ALL=C sort | while IFS= read -r file; do
        rel=${file#"$tree"/}
        check_path "$rel"
        hash=$(sha256_file "$file")
        mode=asset
        requirement=-
        flags=-
        disposition=-
        reason=-
        case $rel in
            compile/*.c) mode=compile ;;
            execute-ieee/*.c) mode=run-ieee ;;
            execute/*.c)
                case $rel in
                    */lib/*|*-lib.c) mode=asset ;;
                    *) mode=run ;;
                esac
                ;;
        esac
        if test "$mode" != asset; then
            meta=$(metadata_for "$file" "$mode")
            requirement=$(printf '%s\n' "$meta" | cut -f1)
            flags=$(printf '%s\n' "$meta" | cut -f2)
            disposition=$(printf '%s\n' "$meta" | cut -f3)
            reason=$(printf '%s\n' "$meta" | cut -f4-)
            stem=${file%.c}
            case $rel in
                execute/builtins/*.c)
                    disposition=skip
                    reason='requires DejaGNU builtins multi-source harness'
                    ;;
            esac
            if test -f "$stem.x"; then
                disposition=skip
                reason='unsupported DejaGNU .x control'
            fi
            include_problem=$(quoted_include_problem "$file" "$tree" "$rel")
            if test -n "$include_problem"; then
                disposition=skip
                reason=$include_problem
            fi
            if override=$(policy_for "$rel"); then
                disposition=$(printf '%s\n' "$override" | cut -f1)
                reason=$(printf '%s\n' "$override" | cut -f2-)
            fi
        fi
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$rel" "$hash" "$mode" "$requirement" "$flags" \
            "$disposition" "$reason" >>"$rows"
    done
    {
        echo '# cgf-torture-manifest-v1'
        echo "# gcc-revision: $GCC_PIN"
        echo '# pristine-policy: no local modifications; adaptations are manifest policy only'
        echo '# schema: path<TAB>sha256<TAB>mode<TAB>requirement<TAB>flags<TAB>disposition<TAB>reason'
        echo '# mode: compile | run | run-ieee | asset'
        echo '# requirement: - or comma-separated closed-enum effective-target names'
        echo '# ieee-policy: run-ieee excludes fast-math optimization lanes'
        LC_ALL=C sort -t "$(printf '\t')" -k1,1 "$rows"
    } >"$manifest"
    while IFS="$(printf '\t')" read -r path disposition reason extra; do
        case $path in ''|'#'*) continue ;; esac
        awk -F '\t' -v wanted="$path" '$1 == wanted && $3 != "asset" { found=1 }
            END { exit !found }' "$rows" ||
            fail "torture policy path is not an imported test: $path"
    done <"$policy"
    rm -f "$rows"
}

verify_manifest()
{
    test ! -L "$out" || fail "output must not be a symlink: $out"
    test -d "$out" || fail "output is not an importer-owned directory: $out"
    bad_entry=$(find "$out" ! -type f ! -type d -print -quit) ||
        fail "cannot inspect imported tree entry types"
    test -z "$bad_entry" || fail "imported tree contains a non-regular entry: $bad_entry"
    test -f "$out/MANIFEST" || fail "manifest is absent: $out/MANIFEST"
    test "$(sed -n '1p' "$out/MANIFEST")" = '# cgf-torture-manifest-v1' ||
        fail "manifest schema header is invalid"
    grep -Fxq "# gcc-revision: $GCC_PIN" "$out/MANIFEST" ||
        fail "manifest GCC revision is not pinned"
    validate_manifest_schema
    tab=$(printf '\t')
    grep -Fxq "COPYING${tab}${GCC_COPYING_SHA256}${tab}asset${tab}-${tab}-${tab}-${tab}-" \
        "$out/MANIFEST" || fail "manifest GCC COPYING provenance is invalid"
    grep -Fxq "COPYING3${tab}${GCC_COPYING3_SHA256}${tab}asset${tab}-${tab}-${tab}-${tab}-" \
        "$out/MANIFEST" || fail "manifest GCC COPYING3 provenance is invalid"
    grep -Fxq "README${tab}${GCC_README_SHA256}${tab}asset${tab}-${tab}-${tab}-${tab}-" \
        "$out/MANIFEST" || fail "manifest GCC README provenance is invalid"
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/cgf-torture-verify.XXXXXX")
    listed=$scratch/listed
    actual=$scratch/actual
    trap 'rm -rf "$scratch"' EXIT HUP INT TERM
    : >"$listed"
    awk -F '\t' '!/^#/ { print $1 "\t" $2 }' "$out/MANIFEST" |
    while IFS="$(printf '\t')" read -r rel expected; do
        check_path "$rel"
        test -f "$out/$rel" || fail "manifest file is absent: $rel"
        got=$(sha256_file "$out/$rel")
        test "$got" = "$expected" || fail "sha256 mismatch: $rel"
        printf '%s\n' "$rel" >>"$listed"
    done
    find "$out" -type f ! -name MANIFEST ! -name passing.txt -print |
        sed "s|^$out/||" | LC_ALL=C sort >"$actual"
    LC_ALL=C sort "$listed" -o "$listed"
    cmp -s "$listed" "$actual" || fail "manifest does not cover the imported tree exactly"
    while IFS="$(printf '\t')" read -r path disposition reason extra; do
        case $path in ''|'#'*) continue ;; esac
        awk -F '\t' -v wanted="$path" -v disposition="$disposition" \
            -v reason="$reason" '$1 == wanted && $6 == disposition && $7 == reason { found=1 }
            END { exit !found }' "$out/MANIFEST" ||
            fail "manifest does not reflect torture policy for $path"
    done <"$policy"
    rm -rf "$scratch"
    trap - EXIT HUP INT TERM
}

case ${1:-} in
    '') ;;
    --verify)
        check_policy
        verify_manifest
        echo "torture import verified: $out"
        exit 0
        ;;
    *) fail "usage: $0 [--verify]" ;;
esac

check_ref
check_policy
parent=$(dirname -- "$out")
mkdir -p "$parent"
parent=$(cd "$parent" && pwd -P)
base=$(basename -- "$out")
case $base in ''|.|..) fail "unsafe output directory: $out" ;; esac
out=$parent/$base
repo=$(cd "$repo" && pwd -P)
repo_parent=$(dirname -- "$repo")
case $out in
    /|"$repo"|"$repo_parent") fail "unsafe output directory: $out" ;;
esac
if test -L "$out"; then
    fail "refusing to replace symlink output: $out"
elif test -e "$out"; then
    test -d "$out" || fail "refusing to replace non-directory output: $out"
    test -f "$out/MANIFEST" || fail "refusing to replace unowned directory: $out"
    test "$(sed -n '1p' "$out/MANIFEST")" = '# cgf-torture-manifest-v1' ||
        fail "refusing to replace directory with foreign manifest: $out"
    grep -Fxq "# gcc-revision: $GCC_PIN" "$out/MANIFEST" ||
        fail "refusing to replace directory with unpinned manifest: $out"
fi
stage=$(mktemp -d "$parent/.torture-import.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
mkdir -p "$stage/compile" "$stage/execute" "$stage/execute-ieee"
cp -R "$source_root/compile/." "$stage/compile/"
cp -R "$source_root/execute/." "$stage/execute/"
if test -d "$stage/execute/ieee"; then
    cp -R "$stage/execute/ieee/." "$stage/execute-ieee/"
    rm -rf "$stage/execute/ieee"
fi
cp "$ref/COPYING" "$stage/COPYING"
cp "$ref/COPYING3" "$stage/COPYING3"
cp "$ref/README" "$stage/README"
if test -f "$out/passing.txt"; then
    cp "$out/passing.txt" "$stage/passing.txt"
fi
write_manifest "$stage"
rm -rf "$out"
mv "$stage" "$out"
trap - EXIT HUP INT TERM
verify_manifest
echo "torture import complete: $out"
