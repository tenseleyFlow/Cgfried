#!/bin/sh

set -eu
LC_ALL=C
export LC_ALL

CTESTSUITE_PIN=5c7275656d751de0e68b2d340a95b5681858ed07

script_dir=$(dirname -- "$0")
repo=$(cd "$script_dir/.." && pwd)
ref=${CGF_CTESTSUITE_REF:-$repo/.docs/refs/c-testsuite}
out=${CGF_CTESTSUITE_OUT:-$repo/tests/ctestsuite}
policy=${CGF_CTESTSUITE_POLICY:-$repo/tests/ctestsuite-policy.tsv}
licenses=${CGF_CTESTSUITE_LICENSES:-$repo/tests/ctestsuite-licenses}

SCC_COMMIT=355356a9836e487939cf5e98b5332a63e5264e27
TINYCC_COMMIT=61ba9f229955f105af9d7dcdbfcc9c9effbe8af3
SCC_CASES=150
TINYCC_CASES=69

fail()
{
    echo "import-c-testsuite: $*" >&2
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

check_license_bundle()
{
    test -d "$licenses" || fail "c-testsuite license bundle is absent: $licenses"
    test ! -L "$licenses" || fail "c-testsuite license bundle must not be a symlink"
    expected_origins=$(mktemp "${TMPDIR:-/tmp}/cgf-ctest-origins.XXXXXX")
    expected_exclusions=$(mktemp "${TMPDIR:-/tmp}/cgf-ctest-exclusions.XXXXXX")
    expected_bundle=$(mktemp "${TMPDIR:-/tmp}/cgf-ctest-bundle.XXXXXX")
    actual_bundle=$(mktemp "${TMPDIR:-/tmp}/cgf-ctest-bundle-actual.XXXXXX")
    trap 'rm -f "$expected_origins" "$expected_exclusions" "$expected_bundle" "$actual_bundle"' EXIT HUP INT TERM
    {
        echo '# cgf-ctestsuite-origins-v1'
        echo '# origin<TAB>org<TAB>repository<TAB>commit<TAB>path-prefix<TAB>license-files'
        printf 'scc\twww.simple-cc.org\tgit://git.simple-cc.org/scc\t%s\ttests/scc/execute/\tscc-LICENSE\n' "$SCC_COMMIT"
        printf 'tinycc\thttps://bellard.org/tcc/\tgit://repo.or.cz/tinycc.git\t%s\ttests/tests2/\ttinycc-COPYING,tinycc-tests2-LICENSE\n' "$TINYCC_COMMIT"
    } >"$expected_origins"
    {
        echo '# cgf-ctestsuite-exclusions-v1'
        echo '# source<TAB>excluded-assets<TAB>reason'
        printf '00001.c\t00001.c,00001.c.expected,00001.c.tags\tmissing .otags; c-testsuite tests/LICENSE says the root MIT license excludes individual cases\n'
    } >"$expected_exclusions"
    cmp -s "$expected_origins" "$licenses/ORIGINS.tsv" ||
        fail "c-testsuite origin provenance is invalid"
    cmp -s "$expected_exclusions" "$licenses/EXCLUSIONS.tsv" ||
        fail "c-testsuite exclusion provenance is invalid"
    if find "$licenses" -mindepth 1 ! -type f -print | grep -q .; then
        fail "c-testsuite license bundle contains an unexpected directory, symlink, or special entry"
    fi
    printf '%s\n' ARTIFACTS.sha256 EXCLUSIONS.tsv ORIGINS.tsv README.md \
        ctestsuite-LICENSE ctestsuite-tests-LICENSE scc-LICENSE \
        tinycc-COPYING tinycc-tests2-LICENSE | LC_ALL=C sort >"$expected_bundle"
    find "$licenses" -mindepth 1 -maxdepth 1 -type f -print |
        sed "s|^$licenses/||" | LC_ALL=C sort >"$actual_bundle"
    cmp -s "$expected_bundle" "$actual_bundle" ||
        fail "c-testsuite license bundle coverage is invalid"
    test "$(sha256_file "$licenses/ctestsuite-LICENSE")" = 6d54349911a6735bfb4e8063659c99fb3bf8f14602218b423c77259d1473b99b ||
        fail "c-testsuite root license hash is invalid"
    test "$(sha256_file "$licenses/ctestsuite-tests-LICENSE")" = 724cbc4ed35a170680c54283fff9f91a3d4bae8fa72ecfe0c65e1c675208e8f8 ||
        fail "c-testsuite case license notice hash is invalid"
    test "$(sha256_file "$licenses/scc-LICENSE")" = d3b15200077f5f6d8933257c6cac09b7f9e6a8279bb2c7536ea305a4a777f007 ||
        fail "c-testsuite SCC license hash is invalid"
    test "$(sha256_file "$licenses/tinycc-COPYING")" = 512d2d21b6b3384ba64781abb0208a1b87740bc31e2df48e2b206ddb7e4d5779 ||
        fail "c-testsuite TinyCC COPYING hash is invalid"
    test "$(sha256_file "$licenses/tinycc-tests2-LICENSE")" = 9aedc94c459a3774eacbb88452c7f7816010a06d8bef776c93d2e4fac91fd3ca ||
        fail "c-testsuite TinyCC tests2 license hash is invalid"
    test "$(sha256_file "$licenses/README.md")" = 2683c10aea9b7b51c6f8b8af5857c06e4454d0ed34f865c9eebf2bc638d4c3a6 ||
        fail "c-testsuite license README hash is invalid"
    expected_hashes='0417945cafc2a8ebb490c107a4e3c87b252392be9cba4aa122474a409f299475  EXCLUSIONS.tsv
350a28c901efff8e9d27dfc63bf02bd4b77ec4bce6157c7544973c66af8b4c70  ORIGINS.tsv
2683c10aea9b7b51c6f8b8af5857c06e4454d0ed34f865c9eebf2bc638d4c3a6  README.md
6d54349911a6735bfb4e8063659c99fb3bf8f14602218b423c77259d1473b99b  ctestsuite-LICENSE
724cbc4ed35a170680c54283fff9f91a3d4bae8fa72ecfe0c65e1c675208e8f8  ctestsuite-tests-LICENSE
d3b15200077f5f6d8933257c6cac09b7f9e6a8279bb2c7536ea305a4a777f007  scc-LICENSE
512d2d21b6b3384ba64781abb0208a1b87740bc31e2df48e2b206ddb7e4d5779  tinycc-COPYING
9aedc94c459a3774eacbb88452c7f7816010a06d8bef776c93d2e4fac91fd3ca  tinycc-tests2-LICENSE'
    test "$(cat "$licenses/ARTIFACTS.sha256")" = "$expected_hashes" ||
        fail "c-testsuite license artifact manifest is invalid"
    rm -f "$expected_origins" "$expected_exclusions" "$expected_bundle" "$actual_bundle"
    trap - EXIT HUP INT TERM
}

origin_for_otags()
{
    source=$1
    otags=$2
    test -f "$otags" || fail "c-testsuite case provenance is absent: $source.otags"
    if ! {
        IFS= read -r org_line && IFS= read -r repository_line &&
            IFS= read -r version_line && IFS= read -r path_line &&
            ! IFS= read -r _
    } <"$otags"; then
        fail "malformed c-testsuite case provenance: $source.otags"
    fi
    case $org_line in org=*) org=${org_line#org=} ;; *) fail "malformed c-testsuite case provenance: $source.otags" ;; esac
    case $repository_line in repository=*) repository=${repository_line#repository=} ;; *) fail "malformed c-testsuite case provenance: $source.otags" ;; esac
    case $version_line in version=*) version=${version_line#version=} ;; *) fail "malformed c-testsuite case provenance: $source.otags" ;; esac
    case $path_line in path=*) upstream_path=${path_line#path=} ;; *) fail "malformed c-testsuite case provenance: $source.otags" ;; esac
    test -n "$org" && test -n "$repository" && test -n "$version" &&
        test -n "$upstream_path" || fail "malformed c-testsuite case provenance: $source.otags"
    check_path "$upstream_path"
    case "$org|$repository|$version|$upstream_path" in
        "www.simple-cc.org|git://git.simple-cc.org/scc|$SCC_COMMIT|tests/scc/execute/"*.c)
            echo scc
            ;;
        "https://bellard.org/tcc/|git://repo.or.cz/tinycc.git|$TINYCC_COMMIT|tests/tests2/"*.c)
            echo tinycc
            ;;
        *) fail "unknown c-testsuite case provenance: $source.otags" ;;
    esac
}

validate_case_provenance()
(
    tree=$1
    test ! -e "$tree/00001.c" && test ! -e "$tree/00001.c.expected" &&
        test ! -e "$tree/00001.c.tags" && test ! -e "$tree/00001.c.otags" ||
        fail "unprovenanced c-testsuite case 00001 must be excluded"
    origins=$(mktemp "${TMPDIR:-/tmp}/cgf-ctest-case-origins.XXXXXX")
    trap 'rm -f "$origins"' EXIT HUP INT TERM
    : >"$origins"
    find "$tree" -maxdepth 1 -type f -name '*.c' -print | LC_ALL=C sort |
    while IFS= read -r source_path; do
        source=$(basename -- "$source_path")
        origin_for_otags "$source" "$source_path.otags" >>"$origins"
    done
    find "$tree" -maxdepth 1 -type f -name '*.c.otags' -print | LC_ALL=C sort |
    while IFS= read -r otags; do
        source=${otags%.otags}
        test -f "$source" || fail "orphan c-testsuite case provenance: $(basename -- "$otags")"
    done
    scc_count=$(awk '$0 == "scc" { n++ } END { print n+0 }' "$origins")
    tinycc_count=$(awk '$0 == "tinycc" { n++ } END { print n+0 }' "$origins")
    test "$scc_count" = "$SCC_CASES" ||
        fail "c-testsuite SCC origin count mismatch: expected $SCC_CASES, got $scc_count"
    test "$tinycc_count" = "$TINYCC_CASES" ||
        fail "c-testsuite TinyCC origin count mismatch: expected $TINYCC_CASES, got $tinycc_count"
    rm -f "$origins"
    trap - EXIT HUP INT TERM
)

check_path()
{
    case $1 in
        ''|/*|*..*) fail "unsafe imported path: $1" ;;
        *[!A-Za-z0-9_./+~@-]* ) fail "unsafe imported path: $1" ;;
    esac
}

check_ref()
{
    test -d "$ref" || fail "c-testsuite reference is absent: $ref"
    actual=$(git -C "$ref" rev-parse HEAD 2>/dev/null) ||
        fail "c-testsuite reference is not a git checkout: $ref"
    test "$actual" = "$CTESTSUITE_PIN" ||
        fail "c-testsuite revision mismatch: expected $CTESTSUITE_PIN, got $actual"
    source_root=$ref/tests/single-exec
    test -d "$source_root" || fail "single-exec tree is absent: $source_root"
    dirty=$(git -C "$ref" status --porcelain=v1 --untracked-files=all \
        --ignored -- tests/single-exec 2>/dev/null) ||
        fail "cannot inspect c-testsuite reference cleanliness"
    test -z "$dirty" || fail "c-testsuite import paths contain local modifications"
    test -f "$source_root/00001.c" || fail "expected excluded c-testsuite case is absent: 00001.c"
    test ! -e "$source_root/00001.c.otags" || fail "00001.c unexpectedly has provenance"
}

check_policy()
{
    test -f "$policy" || fail "c-testsuite policy is absent: $policy"
    test "$(sed -n '1p' "$policy")" = '# cgf-ctestsuite-policy-v1' ||
        fail "c-testsuite policy schema header is invalid"
    while IFS="$(printf '\t')" read -r source disposition reason extra; do
        case $source in ''|'#'*) continue ;; esac
        check_path "$source"
        case $source in *.c) ;; *) fail "c-testsuite policy source is not .c: $source" ;; esac
        test -z "${extra:-}" || fail "malformed c-testsuite policy row: $source"
        case $disposition in
            run)
                test "$reason" = - || fail "run policy reason must be -: $source"
                ;;
            skip|xfail:TORT-[0-9][0-9][0-9])
                test "$reason" != - || fail "non-run policy needs a reason: $source"
                ;;
            *) fail "invalid c-testsuite policy disposition for $source: $disposition" ;;
        esac
        test -n "$reason" || fail "empty c-testsuite policy reason: $source"
    done <"$policy"
    awk -F '\t' '!/^#/ && NF { if (previous != "" && $1 <= previous) exit 1;
        previous=$1 }' "$policy" ||
        fail "c-testsuite policy sources must be sorted and unique"
}

validate_manifest_schema()
{
    awk -F '\t' '
    /^#/ { if (data) exit 1; next }
    $1 == "file" {
        data=1
        if (section == "case" || NF != 3 || $2 == "" ||
            length($3) != 64 || $3 !~ /^[0-9a-f]+$/) exit 1
        if (file_previous != "" && $2 <= file_previous) exit 1
        file_previous=$2
        files[$2]=1
        next
    }
    $1 == "case" {
        data=1
        section="case"
        if (NF != 6 || $2 !~ /^[A-Za-z0-9_+@.-]+\.c$/) exit 1
        if (case_previous != "" && $2 <= case_previous) exit 1
        case_previous=$2
        if (!files[$2]) exit 1
        if ($3 != "-" && $3 != $2 ".expected") exit 1
        if ($3 != "-" && !files[$3]) exit 1
        if ($4 != "-") {
            count=split($4, tag, ",")
            prior=""
            for (i=1; i<=count; i++) {
                if (tag[i] !~ /^[A-Za-z0-9_+@.:=\/-]+$/ ||
                    (prior != "" && tag[i] <= prior)) exit 1
                prior=tag[i]
            }
            if (!files[$2 ".tags"]) exit 1
        }
        if ($5 == "run") {
            if ($6 != "-") exit 1
        } else if ($5 == "skip" || $5 ~ /^xfail:TORT-[0-9][0-9][0-9]$/) {
            if ($6 == "-" || $6 == "") exit 1
        } else exit 1
        cases++
        next
    }
    { exit 1 }
    END { if (cases == 0) exit 1 }' "$out/MANIFEST" ||
        fail "manifest behavioral schema is invalid"

    schema_scratch=$(mktemp -d "${TMPDIR:-/tmp}/cgf-ctest-schema.XXXXXX")
    schema_sources=$schema_scratch/sources
    schema_cases=$schema_scratch/cases
    trap 'rm -rf "$schema_scratch"' EXIT HUP INT TERM
    find "$out" -maxdepth 1 -type f -name '*.c' -print |
        sed "s|^$out/||" | LC_ALL=C sort >"$schema_sources"
    awk -F '\t' '$1 == "case" { print $2 }' "$out/MANIFEST" >"$schema_cases"
    cmp -s "$schema_sources" "$schema_cases" ||
        fail "manifest must contain exactly one case per imported C source"

    awk -F '\t' '$1 == "case" { print $2 "\t" $3 "\t" $4 }' \
        "$out/MANIFEST" | while IFS="$(printf '\t')" read -r source expected tags; do
        actual_expected=-
        test -f "$out/$source.expected" && actual_expected=$source.expected
        test "$expected" = "$actual_expected" ||
            fail "manifest expected-output mapping is invalid: $source"
        actual_tags=-
        if test -f "$out/$source.tags"; then
            actual_tags=$(tr ' \t' '\n' <"$out/$source.tags" | sed '/^$/d' |
                LC_ALL=C sort -u | awk 'BEGIN { first=1 }
                    { if (!first) printf ","; printf "%s", $0; first=0 }
                    END { if (!first) printf "\n" }')
            test -n "$actual_tags" || actual_tags=-
        fi
        test "$tags" = "$actual_tags" ||
            fail "manifest tag mapping is invalid: $source"
    done
    rm -rf "$schema_scratch"
    trap - EXIT HUP INT TERM
}

policy_for()
{
    awk -F '\t' -v wanted="$1" '$1 == wanted { print $2 "\t" $3; found=1 }
        END { if (!found) exit 1 }' "$policy"
}

write_manifest()
{
    tree=$1
    validate_case_provenance "$tree"
    files=$tree/.manifest.files
    cases=$tree/.manifest.cases
    : >"$files"
    : >"$cases"
    find "$tree" -type f ! -name MANIFEST ! -name '.manifest.*' -print |
        LC_ALL=C sort | while IFS= read -r file; do
        rel=${file#"$tree"/}
        check_path "$rel"
        printf 'file\t%s\t%s\n' "$rel" "$(sha256_file "$file")" >>"$files"
    done
    find "$tree" -maxdepth 1 -type f -name '*.c' -print |
        LC_ALL=C sort | while IFS= read -r source; do
        rel=${source#"$tree"/}
        expected=-
        test -f "$source.expected" && expected=$rel.expected
        tags=-
        if test -f "$source.tags"; then
            tags=$(tr ' \t' '\n' <"$source.tags" | sed '/^$/d' |
                LC_ALL=C sort -u | awk 'BEGIN { first=1 } { if (!first) printf ","; printf "%s", $0; first=0 } END { if (!first) printf "\n" }')
            test -n "$tags" || tags=-
        fi
        disposition=run
        reason=-
        if override=$(policy_for "$rel"); then
            disposition=$(printf '%s\n' "$override" | cut -f1)
            reason=$(printf '%s\n' "$override" | cut -f2-)
        fi
        printf 'case\t%s\t%s\t%s\t%s\t%s\n' \
            "$rel" "$expected" "$tags" "$disposition" "$reason" >>"$cases"
    done
    {
        echo '# cgf-ctestsuite-manifest-v1'
        echo "# c-testsuite-revision: $CTESTSUITE_PIN"
        echo '# pristine-policy: no local modifications'
        echo '# license-bundle: tests/ctestsuite-licenses (externally tracked and hash-pinned)'
        echo "# origin-count: scc $SCC_CASES"
        echo "# origin-count: tinycc $TINYCC_CASES"
        echo '# excluded-case: 00001.c and sidecars (missing .otags provenance)'
        echo '# file schema: file<TAB>relative-path<TAB>sha256'
        echo '# case schema: case<TAB>source.c<TAB>source.c.expected-or--<TAB>comma-sorted-tags-or--<TAB>run|skip|xfail:TORT-NNN<TAB>reason-or--'
        LC_ALL=C sort -t "$(printf '\t')" -k2,2 "$files"
        LC_ALL=C sort -t "$(printf '\t')" -k2,2 "$cases"
    } >"$tree/MANIFEST"
    while IFS="$(printf '\t')" read -r source disposition reason extra; do
        case $source in ''|'#'*) continue ;; esac
        awk -F '\t' -v wanted="$source" '$1 == "case" && $2 == wanted { found=1 }
            END { exit !found }' "$cases" ||
            fail "c-testsuite policy source is not imported: $source"
    done <"$policy"
    rm -f "$files" "$cases"
}

verify_manifest()
{
    test ! -L "$out" || fail "output must not be a symlink: $out"
    test -d "$out" || fail "output is not an importer-owned directory: $out"
    test -f "$out/MANIFEST" || fail "manifest is absent: $out/MANIFEST"
    grep -Fxq '# cgf-ctestsuite-manifest-v1' "$out/MANIFEST" ||
        fail "manifest schema header is invalid"
    grep -Fxq "# c-testsuite-revision: $CTESTSUITE_PIN" "$out/MANIFEST" ||
        fail "manifest c-testsuite revision is not pinned"
    grep -Fxq '# license-bundle: tests/ctestsuite-licenses (externally tracked and hash-pinned)' "$out/MANIFEST" ||
        fail "manifest license bundle reference is absent"
    grep -Fxq "# origin-count: scc $SCC_CASES" "$out/MANIFEST" ||
        fail "manifest SCC origin count is invalid"
    grep -Fxq "# origin-count: tinycc $TINYCC_CASES" "$out/MANIFEST" ||
        fail "manifest TinyCC origin count is invalid"
    grep -Fxq '# excluded-case: 00001.c and sidecars (missing .otags provenance)' "$out/MANIFEST" ||
        fail "manifest exclusion provenance is absent"
    if find "$out" ! -type d ! -type f -print | grep -q .; then
        fail "imported c-testsuite tree contains a symlink or special entry"
    fi
    for bundle_file in ARTIFACTS.sha256 EXCLUSIONS.tsv ORIGINS.tsv README.md \
        ctestsuite-LICENSE ctestsuite-tests-LICENSE scc-LICENSE \
        tinycc-COPYING tinycc-tests2-LICENSE; do
        test -f "$out/LICENSES/$bundle_file" ||
            fail "imported c-testsuite license artifact is absent: $bundle_file"
        cmp -s "$licenses/$bundle_file" "$out/LICENSES/$bundle_file" ||
            fail "imported c-testsuite license artifact does not match pinned bundle: $bundle_file"
    done
    validate_manifest_schema
    validate_case_provenance "$out"
    verify_scratch=$(mktemp -d "${TMPDIR:-/tmp}/cgf-ctest-verify.XXXXXX")
    listed=$verify_scratch/listed
    actual=$verify_scratch/actual
    trap 'rm -rf "$verify_scratch"' EXIT HUP INT TERM
    : >"$listed"
    awk -F '\t' '$1 == "file" { print $2 "\t" $3 }' "$out/MANIFEST" |
    while IFS="$(printf '\t')" read -r rel expected; do
        check_path "$rel"
        test -f "$out/$rel" || fail "manifest file is absent: $rel"
        got=$(sha256_file "$out/$rel")
        test "$got" = "$expected" || fail "sha256 mismatch: $rel"
        printf '%s\n' "$rel" >>"$listed"
    done
    find "$out" -type f ! -name MANIFEST ! -name '.verify.*' -print |
        sed "s|^$out/||" | LC_ALL=C sort >"$actual"
    LC_ALL=C sort "$listed" -o "$listed"
    cmp -s "$listed" "$actual" || fail "manifest does not cover the imported tree exactly"
    while IFS="$(printf '\t')" read -r source disposition reason extra; do
        case $source in ''|'#'*) continue ;; esac
        awk -F '\t' -v wanted="$source" -v disposition="$disposition" \
            -v reason="$reason" '$1 == "case" && $2 == wanted &&
                $5 == disposition && $6 == reason { found=1 }
                END { exit !found }' "$out/MANIFEST" ||
            fail "manifest does not reflect c-testsuite policy for $source"
    done <"$policy"
    rm -rf "$verify_scratch"
    trap - EXIT HUP INT TERM
}

case ${1:-} in
    '') ;;
    --verify)
        check_policy
        check_license_bundle
        verify_manifest
        echo "c-testsuite import verified: $out"
        exit 0
        ;;
    *) fail "usage: $0 [--verify]" ;;
esac

check_ref
check_policy
check_license_bundle
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
    test "$(sed -n '1p' "$out/MANIFEST")" = '# cgf-ctestsuite-manifest-v1' ||
        fail "refusing to replace directory with foreign manifest: $out"
    grep -Fxq "# c-testsuite-revision: $CTESTSUITE_PIN" "$out/MANIFEST" ||
        fail "refusing to replace directory with unpinned manifest: $out"
fi
stage=$(mktemp -d "$parent/.ctestsuite-import.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
find "$source_root" -maxdepth 1 -type f \
    \( -name '*.c' -o -name '*.c.expected' -o -name '*.c.tags' -o -name '*.c.otags' \) \
    -print | LC_ALL=C sort | while IFS= read -r file; do
    case $(basename -- "$file") in 00001.c|00001.c.expected|00001.c.tags|00001.c.otags) continue ;; esac
    cp "$file" "$stage/$(basename -- "$file")"
done
mkdir "$stage/LICENSES"
for license_file in ARTIFACTS.sha256 EXCLUSIONS.tsv ORIGINS.tsv README.md \
    ctestsuite-LICENSE ctestsuite-tests-LICENSE scc-LICENSE \
    tinycc-COPYING tinycc-tests2-LICENSE; do
    cp "$licenses/$license_file" "$stage/LICENSES/$license_file"
done
write_manifest "$stage"
rm -rf "$out"
mv "$stage" "$out"
trap - EXIT HUP INT TERM
verify_manifest
echo "c-testsuite import complete: $out"
