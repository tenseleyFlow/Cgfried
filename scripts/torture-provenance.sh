#!/bin/sh
# Emit the fixed provenance portion of a publishable torture-results-v2 header.
set -eu

LC_ALL=C
export LC_ALL

die()
{
    echo "torture-provenance: $*" >&2
    exit 3
}

usage()
{
    die "usage: torture-provenance.sh --write-receipt FILE --compiler PATH | --receipt FILE --driver PATH --compiler PATH --runner PATH --target TARGET --torture-manifest FILE --ctestsuite-manifest FILE"
}

driver=
compiler=
runner=
target=
torture_manifest=
ctestsuite_manifest=
receipt=
write_receipt=
while [ "$#" -gt 0 ]; do
    [ "$#" -ge 2 ] || usage
    case $1 in
    --driver) driver=$2 ;;
    --compiler) compiler=$2 ;;
    --runner) runner=$2 ;;
    --target) target=$2 ;;
    --torture-manifest) torture_manifest=$2 ;;
    --ctestsuite-manifest) ctestsuite_manifest=$2 ;;
    --receipt) receipt=$2 ;;
    --write-receipt) write_receipt=$2 ;;
    *) usage ;;
    esac
    shift 2
done

repo=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P) ||
    die "cannot resolve repository root"

if command -v sha256sum >/dev/null 2>&1; then
    sha_kind=sha256sum
elif command -v shasum >/dev/null 2>&1; then
    sha_kind=shasum
else
    die "sha256sum or shasum -a 256 is required"
fi

sha_file()
{
    case $sha_kind in
    sha256sum) sha256sum "$1" | awk '{ print $1 }' ;;
    shasum) shasum -a 256 "$1" | awk '{ print $1 }' ;;
    esac
}

sha_stdin()
{
    case $sha_kind in
    sha256sum) sha256sum | awk '{ print $1 }' ;;
    shasum) shasum -a 256 | awk '{ print $1 }' ;;
    esac
}

resolve_file()
{
    rf_value=$1
    case $rf_value in
    */*)
        rf_dir=$(CDPATH='' cd "$(dirname "$rf_value")" && pwd -P) || return 1
        rf_path=$rf_dir/$(basename "$rf_value")
        ;;
    *)
        rf_path=$(command -v "$rf_value" 2>/dev/null) || return 1
        case $rf_path in
        /*) ;;
        *)
            rf_dir=$(CDPATH='' cd "$(dirname "$rf_path")" && pwd -P) || return 1
            rf_path=$rf_dir/$(basename "$rf_path")
            ;;
        esac
        ;;
    esac
    [ -f "$rf_path" ] && [ -r "$rf_path" ] || return 1
    printf '%s\n' "$rf_path"
}

case ${write_receipt:+write}:${receipt:+read} in
write: | :read) ;;
*) usage ;;
esac
[ -n "$compiler" ] || usage
if [ -n "$write_receipt" ] &&
    [ -n "$driver$runner$target$torture_manifest$ctestsuite_manifest$receipt" ]; then
    usage
fi
compiler_path=$(resolve_file "$compiler") || die "cannot resolve compiler binary: $compiler"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-torture-provenance.XXXXXX") ||
    die "cannot create temporary directory"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Hash a path-labelled list of content hashes.  The labels make renames part
# of the source state while keeping the digest independent of worktree path.
(
    cd "$repo"
    {
        printf '%s\n' Makefile
        find src include -type f -print
    } | LC_ALL=C sort -u
) >"$tmp/source-files"
: >"$tmp/source-state"
while IFS= read -r source_file; do
    [ -f "$repo/$source_file" ] || die "compiler source file vanished: $source_file"
    printf '%s\t%s\n' "$source_file" "$(sha_file "$repo/$source_file")" \
        >>"$tmp/source-state"
done <"$tmp/source-files"
compiler_source_sha=$(sha_stdin <"$tmp/source-state")

revision=unversioned
if command -v git >/dev/null 2>&1; then
    candidate=$(git -C "$repo" rev-parse --verify HEAD 2>/dev/null || true)
    case $candidate in
    '' | *[!0-9a-fA-F]*) ;;
    *) revision=$(printf '%s' "$candidate" | tr 'A-F' 'a-f') ;;
    esac
fi

tree_state=unversioned
if [ "$revision" != unversioned ]; then
    tree_state=clean
    if [ -n "$(git -C "$repo" status --porcelain --untracked-files=all -- \
        Makefile src include 2>/dev/null || true)" ]; then
        tree_state=dirty
    fi
fi
compiler_sha=$(sha_file "$compiler_path")

{
    echo '# cgf-compiler-provenance-v1'
    printf 'source_revision=%s\n' "$revision"
    printf 'source_state_sha256=%s\n' "$compiler_source_sha"
    printf 'source_tree_state=%s\n' "$tree_state"
    printf 'compiler_sha256=%s\n' "$compiler_sha"
} >"$tmp/expected-receipt"

if [ -n "$write_receipt" ]; then
    receipt_dir=$(dirname "$write_receipt")
    mkdir -p "$receipt_dir" || die "cannot create receipt directory: $receipt_dir"
    receipt_dir=$(CDPATH='' cd "$receipt_dir" && pwd -P) ||
        die "cannot resolve receipt directory: $receipt_dir"
    receipt_path=$receipt_dir/$(basename "$write_receipt")
    receipt_tmp=$(mktemp "$receipt_dir/.$(basename "$write_receipt").XXXXXX") ||
        die "cannot stage compiler provenance receipt"
    trap 'rm -rf "$tmp"; rm -f "$receipt_tmp"' EXIT HUP INT TERM
    cp "$tmp/expected-receipt" "$receipt_tmp" || die "cannot write compiler provenance receipt"
    chmod 644 "$receipt_tmp" || die "cannot set compiler provenance receipt permissions"
    mv -f "$receipt_tmp" "$receipt_path" || die "cannot publish compiler provenance receipt"
    exit 0
fi

[ -n "$driver" ] && [ -n "$runner" ] && [ -n "$target" ] &&
    [ -n "$torture_manifest" ] && [ -n "$ctestsuite_manifest" ] || usage
driver_path=$(resolve_file "$driver") || die "cannot resolve compiler driver: $driver"
runner_path=$(resolve_file "$runner") || die "cannot resolve torture runner: $runner"
case $target in
x86_64-linux-gnu | arm64-linux) ;;
*) die "unsupported target: $target" ;;
esac
torture_manifest_path=$(resolve_file "$torture_manifest") ||
    die "cannot resolve torture manifest: $torture_manifest"
ctestsuite_manifest_path=$(resolve_file "$ctestsuite_manifest") ||
    die "cannot resolve c-testsuite manifest: $ctestsuite_manifest"
[ -f "$receipt" ] && [ -r "$receipt" ] ||
    die "compiler provenance receipt is missing or unreadable: $receipt"
cmp -s "$tmp/expected-receipt" "$receipt" ||
    die "compiler provenance receipt is stale or tampered: $receipt"

# Hash the complete result-generation contract by stable role names.  The
# native ARM execution wrapper is included even when the current lane does
# not invoke it, so both target streams share one common harness identity.
{
    printf '%s\t%s\n' matrix "$(sha_file "$repo/ci/torture.mk")"
    printf '%s\t%s\n' runner "$(sha_file "$runner_path")"
    printf '%s\t%s\n' triage "$(sha_file "$repo/scripts/triage-torture.sh")"
    printf '%s\t%s\n' provenance "$(sha_file "$repo/scripts/torture-provenance.sh")"
    printf '%s\t%s\n' qemu-run "$(sha_file "$repo/scripts/qemu-run.sh")"
} >"$tmp/harness-state"
harness_sha=$(sha_stdin <"$tmp/harness-state")

printf '# source-revision=%s\n' "$revision"
printf '# compiler-source-sha256=%s\n' "$compiler_source_sha"
printf '# harness-sha256=%s\n' "$harness_sha"
printf '# torture-manifest-sha256=%s\n' "$(sha_file "$torture_manifest_path")"
printf '# ctestsuite-manifest-sha256=%s\n' "$(sha_file "$ctestsuite_manifest_path")"
printf '# target=%s\n' "$target"
printf '# compiler-binary-sha256=%s\n' "$compiler_sha"
printf '# compiler-driver-sha256=%s\n' "$(sha_file "$driver_path")"
