#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

GIT_VERSION=2.56.0
GIT_COMMIT=a018953688f1b10bddf91bff8747068f5f4746a4
GIT_SHA256=26c56c296b38c0695b26fa95f475f1d01704d2d38e73465ca30b0b2f5dc789d3
UNIT_TEST_COUNT=261
LINKED_PRODUCT_COUNT=10

fail() {
    echo "campaign-git: $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "usage: $0 configure|build|validate"
stage=$1
case $stage in configure | build | validate) ;; *) fail "unknown stage: $stage" ;; esac

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_GIT_ARCHIVE:-$root/build/campaigns/dl/git-$GIT_VERSION.tar.xz}
work=${CGF_CAMPAIGN_GIT_WORK:-$root/build/campaigns/git}
cgf=${CGF_CAMPAIGN_GIT_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_GIT_HOSTCC:-cc}
sole=${CGF_CAMPAIGN_GIT_SOLE_C:-$root/scripts/campaigns/sole-c.sh}
cc_wrapper=${CGF_CAMPAIGN_GIT_CC_WRAPPER:-$root/scripts/campaigns/git-cc.sh}
jobs=${CGF_CAMPAIGN_JOBS:-}

[ -x "$cgf" ] || fail "Cgfried compiler is missing or not executable: $cgf"
[ -x "$sole" ] || fail "sole-C wrapper is missing or not executable: $sole"
[ -x "$cc_wrapper" ] || fail "compiler adapter is missing or not executable: $cc_wrapper"
[ -f "$archive" ] || fail "verified source archive is missing: $archive"
got=$(sha256sum "$archive" | awk '{print $1}')
[ "$got" = "$GIT_SHA256" ] ||
    fail "source checksum mismatch: expected $GIT_SHA256, got $got"

case $work in /*) ;; *) work=$root/$work ;; esac
campaign_root=$root/build/campaigns
mkdir -p "$campaign_root"
campaign_root_real=$(CDPATH='' cd "$campaign_root" && pwd -P)
[ "$campaign_root_real" = "$campaign_root" ] ||
    fail "campaign root must not traverse symlinks: $campaign_root"
case $work in
    "$campaign_root"/*)
        work_name=${work#"$campaign_root"/}
        case $work_name in '' | . | .. | */*) fail "unsafe work directory: $work" ;; esac
        ;;
    *) fail "work directory must be a direct child of $campaign_root: $work" ;;
esac
[ ! -L "$work" ] || fail "work directory must not be a symlink: $work"

if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')
fi
case $jobs in '' | *[!0-9]* | 0) fail "CGF_CAMPAIGN_JOBS must be positive: $jobs" ;; esac

for tool in ar awk basename cmp find grep make paste perl readlink sed sha256sum sort tar; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool is unavailable: $tool"
done
command -v "$hostcc" >/dev/null 2>&1 || fail "host compiler is unavailable: $hostcc"

compiler_target=$("$cgf" -dumpmachine) || fail "cannot query Cgfried's target"
case $compiler_target in
    x86_64-linux-gnu | arm64-linux)
        platform=linux
        cflags='-std=gnu17 -O2 -Wno-attributes'
        object_count=563
        archive_member_count=303
        shell_test_count=33235
        shell_success_count=32180
        shell_broken_count=358
        OBJECT_CLOSURE_SHA256=d9b0880c5429ae58bddd45d23657107d196d5695a5a80d49a803e8c819e8402a
        LINK_CLOSURE_SHA256=63d4eea89da713655eea0f566411a80d948ca690ecf9c1159131197a45f29810
        ARCHIVE_CLOSURE_SHA256=dd9aecc20b76c312262bd76c2b3883bf2ad5d3605fa7f683338eb6b10ad8ccf0
        ;;
    arm64-macos)
        platform=darwin
        cflags='-std=gnu17 -O2 -Wno-attributes -DTARGET_CPU_ARM64=1 -DTARGET_OS_MAC=1 -DTARGET_OS_OSX=1'
        object_count=565
        archive_member_count=305
        shell_test_count=33203
        shell_success_count=31846
        shell_broken_count=361
        OBJECT_CLOSURE_SHA256=aacbf0461c958cdde7e1ff71a5d45778c78ea604fed98f06b92ffd9a316e143f
        LINK_CLOSURE_SHA256=63d4eea89da713655eea0f566411a80d948ca690ecf9c1159131197a45f29810
        ARCHIVE_CLOSURE_SHA256=ea88eaa097cd20fa972f62a5a05c770b53ea018694a93f94a73fcbb9354da7cc
        ;;
    *) fail "unsupported native campaign target: $compiler_target" ;;
esac

as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "native assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "native linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

tree=$work/cgfried-src
host_tree=$work/host-src
logs=$work/logs
receipts=$work/sole-c
manifest=$work/sole-c-closure.tsv
report=$work/sole-c-report.txt
object_inventory=$work/object-closure.tsv
link_inventory=$work/link-closure.tsv
archive_inventory=$work/archive-closure.tsv
program_inventory=$work/program-closure.txt
make_database=$work/make-database.txt

export CGF_CAMPAIGN_GIT_CGF=$cgf
export CGF_CAMPAIGN_GIT_SOLE=$sole
export CGF_CAMPAIGN_GIT_RECEIPTS=$receipts

git_make() {
    destination=$1
    compiler=$2
    shift 2
    make -C "$destination" \
        CC="$compiler" \
        CFLAGS="$cflags" \
        COMPUTE_HEADER_DEPENDENCIES=yes \
        GENERATE_COMPILATION_DATABASE=no \
        NO_APPLE_COMMON_CRYPTO=YesPlease \
        NO_CURL=YesPlease \
        NO_EXPAT=YesPlease \
        NO_GETTEXT=YesPlease \
        NO_OPENSSL=YesPlease \
        NO_PERL=YesPlease \
        NO_PYTHON=YesPlease \
        NO_RUST=YesPlease \
        NO_TCLTK=YesPlease \
        V=1 "$@"
}

read_one() {
    key=$1
    receipt=$2
    awk -v key="$key" '
        index($0, key "=") == 1 {
            count++
            value = substr($0, length(key) + 2)
        }
        END {
            if (count != 1) exit 1
            print value
        }
    ' "$receipt"
}

archive_members() {
    ar t "$1" | awk '$0 !~ /^__.SYMDEF( SORTED)?$/ { count++ } END { print count + 0 }'
}

audit_archive() {
    if ! tar -tf "$archive" | awk -v root_name="git-$GIT_VERSION" '
        {
            name = $0
            if (substr(name, 1, 1) == "/" || index(name, "\\") != 0 ||
                (name != root_name && name != root_name "/" &&
                 index(name, root_name "/") != 1)) {
                bad = 1
                next
            }
            sub(/\/$/, "", name)
            count = split(name, component, "/")
            for (i = 1; i <= count; i++)
                if (component[i] == "" || component[i] == "." ||
                    component[i] == "..") bad = 1
        }
        END { exit bad }
    '; then
        fail "source archive contains an unsafe or unexpected path"
    fi
    if ! tar -tvf "$archive" | awk '
        substr($1, 1, 1) == "-" || substr($1, 1, 1) == "d" ||
        substr($1, 1, 1) == "l" { next }
        { bad = 1 }
        END { exit bad }
    '; then
        fail "source archive contains an unsupported special-file member"
    fi
    actual_links=$(tar -tvf "$archive" | awk '
        substr($1, 1, 1) == "l" { print $(NF - 2) "\t" $NF }
    ' | sort)
    expected_links=$(printf '%s\t%s\n' \
        "git-$GIT_VERSION/RelNotes" 'Documentation/RelNotes/2.56.0.adoc' \
        "git-$GIT_VERSION/subprojects/git-gui" '../git-gui' \
        "git-$GIT_VERSION/subprojects/gitk" '../gitk-git')
    [ "$actual_links" = "$expected_links" ] ||
        fail "source archive symbolic-link inventory changed"
}

extract_tree() {
    destination=$1
    mkdir -p "$destination"
    tar --no-same-owner --no-same-permissions -xf "$archive" \
        -C "$destination" --strip-components=1
    [ "$(readlink "$destination/RelNotes")" = 'Documentation/RelNotes/2.56.0.adoc' ] ||
        fail "RelNotes symbolic link changed during extraction"
    [ "$(readlink "$destination/subprojects/git-gui")" = '../git-gui' ] ||
        fail "git-gui symbolic link changed during extraction"
    [ "$(readlink "$destination/subprojects/gitk")" = '../gitk-git' ] ||
        fail "gitk symbolic link changed during extraction"
}

configure_tree() {
    label=$1
    destination=$2
    compiler=$3
    result_code=0
    git_make "$destination" "$compiler" GIT-BUILD-OPTIONS \
        >"$logs/$label/configure.log" 2>&1 || result_code=$?
    if [ "$result_code" -ne 0 ]; then
        tail -200 "$logs/$label/configure.log" >&2
        fail "$label configure failed"
    fi
    [ -f "$destination/GIT-BUILD-OPTIONS" ] ||
        fail "$label configure produced no GIT-BUILD-OPTIONS"
}

validate_configuration() {
    label=$1
    destination=$2
    options=$destination/GIT-BUILD-OPTIONS
    for setting in \
        "NO_CURL='YesPlease'" \
        "NO_EXPAT='YesPlease'" \
        "NO_GETTEXT='YesPlease'" \
        "NO_PERL='YesPlease'" \
        "NO_PYTHON='YesPlease'"; do
        grep -Fqx "$setting" "$options" ||
            fail "$label configuration changed: $setting"
    done
    grep -Fqx "GIT_VERSION=$GIT_VERSION" "$destination/GIT-VERSION-FILE" ||
        fail "$label source version changed"
}

configure_stage() {
    rm -rf "$work"
    mkdir -p "$logs/cgfried" "$logs/host"
    audit_archive
    extract_tree "$tree"
    extract_tree "$host_tree"
    grep -Fqx "DEF_VER=v$GIT_VERSION" "$tree/GIT-VERSION-GEN" ||
        fail "release archive declares the wrong version"
    cmp "$tree/GIT-VERSION-GEN" "$host_tree/GIT-VERSION-GEN" >/dev/null ||
        fail "compiler lanes did not receive identical version generators"
    {
        printf 'version=%s\n' "$GIT_VERSION"
        printf 'commit=%s\n' "$GIT_COMMIT"
        printf 'sha256=%s\n' "$GIT_SHA256"
        printf 'target=%s\n' "$compiler_target"
        printf 'platform=%s\n' "$platform"
        printf 'compiler=%s\n' "$cgf"
        printf 'compiler_sha256=%s\n' "$(sha256sum "$cgf" | awk '{print $1}')"
        printf 'host_compiler=%s\n' "$hostcc"
        printf 'language=gnu17\n'
        printf 'optimization=O2\n'
        printf 'project_linkage=static\n'
        printf 'curl=off\n'
        printf 'expat=off\n'
        printf 'gettext=off\n'
        printf 'openssl=off\n'
        printf 'perl=off\n'
        printf 'python=off\n'
        printf 'rust=off\n'
        printf 'tcltk=off\n'
    } >"$work/provenance.txt"
    configure_tree cgfried "$tree" "$cgf"
    configure_tree host "$host_tree" "$hostcc"
    validate_configuration cgfried "$tree"
    validate_configuration host "$host_tree"
    cmp "$tree/GIT-VERSION-FILE" "$host_tree/GIT-VERSION-FILE" >/dev/null ||
        fail "compiler lanes generated different source versions"
}

build_tree() {
    label=$1
    destination=$2
    compiler=$3
    mode=$4
    result_code=0
    if [ "$mode" = cgfried ]; then
        CGF_CAMPAIGN_GIT_CC_MODE=build \
            git_make "$destination" "$compiler" -j "$jobs" all \
                >"$logs/$label/build.log" 2>&1 || result_code=$?
    else
        git_make "$destination" "$compiler" -j "$jobs" all \
            >"$logs/$label/build.log" 2>&1 || result_code=$?
    fi
    if [ "$result_code" -ne 0 ]; then
        tail -320 "$logs/$label/build.log" >&2
        fail "$label build failed"
    fi
}

write_expected_products() {
    cat <<'EOF'
git
git-daemon
git-http-backend
git-imap-send
git-sh-i18n--envsubst
git-shell
scalar
t/helper/test-fake-ssh
t/helper/test-tool
t/unit-tests/bin/unit-tests
EOF
}

verify_products() {
    label=$1
    destination=$2
    [ -f "$destination/libgit.a" ] || fail "$label omitted libgit.a"
    [ "$(archive_members "$destination/libgit.a")" -eq "$archive_member_count" ] ||
        fail "$label libgit.a member count changed"
    write_expected_products | while IFS= read -r product; do
        [ -x "$destination/$product" ] || fail "$label omitted linked product $product"
    done
    retained_objects=$(find "$destination" -type f -name '*.o' | wc -l | tr -d ' ')
    [ "$retained_objects" -eq "$object_count" ] ||
        fail "$label retained object inventory is not exactly $object_count"
    [ "$("$destination/git" version)" = "git version $GIT_VERSION" ] ||
        fail "$label executable reports the wrong version"
    [ -z "$(find "$destination" -type f \( -name '*.so' -o -name '*.dylib' \) -print -quit)" ] ||
        fail "$label produced a shared project library"
}

build_stage() {
    [ -f "$tree/GIT-BUILD-OPTIONS" ] || fail "configure stage has not completed"
    [ ! -e "$receipts" ] || fail "receipt root already exists: $receipts"
    "$sole" init "$receipts" "$cgf"
    build_tree cgfried "$tree" "$cc_wrapper" cgfried
    build_tree host "$host_tree" "$hostcc" host
    verify_products cgfried "$tree"
    verify_products host "$host_tree"
}

metric_from_log() {
    metric=$1
    input=$2
    awk -v metric="$metric" '
        $1 == metric && NF == 2 && $2 ~ /^[0-9]+$/ {
            count++
            value = $2
        }
        END {
            if (count != 1) exit 1
            print value
        }
    ' "$input"
}

test_unit_tree() {
    label=$1
    destination=$2
    result_code=0
    (
        cd "$destination"
        ./t/unit-tests/bin/unit-tests
    ) >"$logs/$label/unit.log" 2>&1 || result_code=$?
    if [ "$result_code" -ne 0 ]; then
        tail -200 "$logs/$label/unit.log" >&2
        fail "$label C unit suite failed"
    fi
    grep -Fqx "1..$UNIT_TEST_COUNT" "$logs/$label/unit.log" ||
        fail "$label C unit suite did not run exactly $UNIT_TEST_COUNT tests"
    if grep -F '^not ok ' "$logs/$label/unit.log" >/dev/null; then
        fail "$label C unit suite reported a failure"
    fi
}

test_shell_tree() {
    label=$1
    destination=$2
    compiler=$3
    mode=$4
    result_code=0
    if [ "$mode" = cgfried ]; then
        CGF_CAMPAIGN_GIT_CC_MODE=build \
            git_make "$destination" "$compiler" -j "$jobs" test \
                >"$logs/$label/test.log" 2>&1 || result_code=$?
    else
        git_make "$destination" "$compiler" -j "$jobs" test \
            >"$logs/$label/test.log" 2>&1 || result_code=$?
    fi
    if [ "$result_code" -ne 0 ]; then
        tail -400 "$logs/$label/test.log" >&2
        fail "$label shell test suite failed"
    fi
    fixed=$(metric_from_log fixed "$logs/$label/test.log") ||
        fail "$label shell summary omitted fixed count"
    success=$(metric_from_log success "$logs/$label/test.log") ||
        fail "$label shell summary omitted success count"
    failed=$(metric_from_log failed "$logs/$label/test.log") ||
        fail "$label shell summary omitted failed count"
    broken=$(metric_from_log broken "$logs/$label/test.log") ||
        fail "$label shell summary omitted broken count"
    total=$(metric_from_log total "$logs/$label/test.log") ||
        fail "$label shell summary omitted total count"
    [ "$fixed" -eq 0 ] || fail "$label shell suite fixed a declared breakage"
    [ "$failed" -eq 0 ] || fail "$label shell suite reported failures"
    [ "$success" -eq "$shell_success_count" ] ||
        fail "$label shell suite passed $success tests, expected $shell_success_count"
    [ "$broken" -eq "$shell_broken_count" ] ||
        fail "$label shell suite reported $broken known breakages, expected $shell_broken_count"
    [ "$total" -eq "$shell_test_count" ] ||
        fail "$label shell suite ran $total tests, expected $shell_test_count"
    {
        printf 'broken=%s\n' "$broken"
        printf 'failed=%s\n' "$failed"
        printf 'fixed=%s\n' "$fixed"
        printf 'success=%s\n' "$success"
        printf 'total=%s\n' "$total"
    } >"$work/$label-shell-summary.txt"
}

run_smoke() {
    label=$1
    destination=$2
    smoke=$work/parity/$label
    home=$smoke/home
    repository=$smoke/repository
    mkdir -p "$home"
    env HOME="$home" XDG_CONFIG_HOME="$home/xdg" GIT_CONFIG_NOSYSTEM=1 \
        "$destination/git" init -q --template="$destination/templates/blt" \
            --initial-branch=main "$repository"
    env HOME="$home" XDG_CONFIG_HOME="$home/xdg" GIT_CONFIG_NOSYSTEM=1 \
        GIT_AUTHOR_DATE='2001-02-03T04:05:06 +0000' \
        GIT_COMMITTER_DATE='2001-02-03T04:05:06 +0000' \
        "$destination/git" -C "$repository" \
            -c user.name='Cgfried Campaign' -c user.email='campaign@example.invalid' \
            commit -q --allow-empty -m campaign
    {
        HOME="$home" XDG_CONFIG_HOME="$home/xdg" GIT_CONFIG_NOSYSTEM=1 \
            "$destination/git" version
        printf 'count='
        HOME="$home" XDG_CONFIG_HOME="$home/xdg" GIT_CONFIG_NOSYSTEM=1 \
            "$destination/git" -C "$repository" rev-list --count HEAD
        printf 'oid='
        HOME="$home" XDG_CONFIG_HOME="$home/xdg" GIT_CONFIG_NOSYSTEM=1 \
            "$destination/git" -C "$repository" rev-parse HEAD
        printf 'subject='
        HOME="$home" XDG_CONFIG_HOME="$home/xdg" GIT_CONFIG_NOSYSTEM=1 \
            "$destination/git" -C "$repository" log -1 --format=%s
    } >"$work/parity/$label.txt"
}

normalize_tree_path() {
    artifact=$1
    case $artifact in
        "$tree"/*) printf '%s\n' "${artifact#"$tree"/}" ;;
        *) fail "build artifact escaped Git tree: $artifact" ;;
    esac
}

normalize_source_path() {
    source=$1
    case $source in
        "$tree"/*) printf '@src/%s\n' "${source#"$tree"/}" ;;
        *) fail "object source escaped pinned Git tree: $source" ;;
    esac
}

materialize_tree_path() {
    relative=$1
    printf '%s/%s\n' "$tree" "$relative"
}

materialize_source_path() {
    identity=$1
    case $identity in
        @src/*) printf '%s/%s\n' "$tree" "${identity#@src/}" ;;
        *) fail "invalid normalized source identity: $identity" ;;
    esac
}

write_closure_inventories() {
    : >"$object_inventory"
    : >"$link_inventory"
    for receipt in "$receipts"/invocations/*/receipt.txt; do
        [ -f "$receipt" ] || fail "no compiler receipts were recorded"
        kind=$(read_one kind "$receipt") || fail "receipt has no unique kind: $receipt"
        output=$(read_one output "$receipt") || fail "receipt has no unique output: $receipt"
        output_relative=$(normalize_tree_path "$output")
        case $kind in
            object)
                source=$(read_one source "$receipt") ||
                    fail "receipt has no unique source: $receipt"
                source_identity=$(normalize_source_path "$source")
                printf '%s\t%s\n' "$source_identity" "$output_relative" \
                    >>"$object_inventory"
                ;;
            link)
                awk 'index($0, "input=") == 1 { print substr($0, 7) }' \
                    "$receipt" | while IFS= read -r input; do
                        input_relative=$(normalize_tree_path "$input")
                        printf '%s\t%s\n' "$output_relative" "$input_relative"
                    done >>"$link_inventory"
                ;;
            *) fail "unexpected retained compiler invocation kind: $kind" ;;
        esac
    done
    sort -o "$object_inventory" "$object_inventory"
    sort -o "$link_inventory" "$link_inventory"
}

write_archive_inventory() {
    CGF_CAMPAIGN_GIT_CC_MODE=build \
        git_make "$tree" "$cc_wrapper" -pn >"$make_database" \
            2>"$logs/cgfried/make-database.log" ||
        fail "cannot inspect Git's make database"
    awk '
        /^libgit[.]a:/ {
            targets++
            for (i = 2; i <= NF; i++)
                if ($i ~ /[.]o$/) print $i
        }
        END { if (targets != 1) exit 1 }
    ' "$make_database" >"$work/libgit-prerequisites.txt" ||
        fail "make database has no unique libgit.a prerequisite list"
    [ "$(wc -l <"$work/libgit-prerequisites.txt" | tr -d ' ')" -eq "$archive_member_count" ] ||
        fail "make database libgit.a member count changed"
    ar t "$tree/libgit.a" | awk '$0 !~ /^__.SYMDEF( SORTED)?$/' \
        >"$work/libgit-members.txt"
    awk '{ count[$0]++ } END { if (count["fsck.o"] != 2 || count["tree.o"] != 2) exit 1 }' \
        "$work/libgit-members.txt" ||
        fail "libgit.a duplicate-member contract changed"
    paste "$work/libgit-members.txt" "$work/libgit-prerequisites.txt" |
        while IFS="$(printf '\t')" read -r member object_relative; do
            [ "$member" = "$(basename "$object_relative")" ] ||
                fail "libgit.a order differs from Git's prerequisite graph"
            awk -F '\t' -v object="$object_relative" \
                '$2 == object { found++ } END { exit found != 1 }' \
                "$object_inventory" ||
                fail "archive prerequisite is not a unique certified object: $object_relative"
            printf 'libgit.a\t%s\t%s\n' "$member" "$object_relative"
        done >"$archive_inventory"
    ar t "$host_tree/libgit.a" | awk '$0 !~ /^__.SYMDEF( SORTED)?$/' \
        >"$work/host-libgit-members.txt"
    cmp "$work/libgit-members.txt" "$work/host-libgit-members.txt" >/dev/null ||
        fail "host and Cgfried libgit.a member inventories differ"
}

verify_inventory_contract() {
    object_got=$(sha256sum "$object_inventory" | awk '{print $1}')
    link_got=$(sha256sum "$link_inventory" | awk '{print $1}')
    archive_got=$(sha256sum "$archive_inventory" | awk '{print $1}')
    [ "$(wc -l <"$object_inventory" | tr -d ' ')" -eq "$object_count" ] ||
        fail "sole-C object closure is not exactly $object_count translations"
    [ "$(sort -u "$object_inventory" | wc -l | tr -d ' ')" -eq "$object_count" ] ||
        fail "sole-C object closure contains a duplicate identity"
    [ "$(wc -l <"$archive_inventory" | tr -d ' ')" -eq "$archive_member_count" ] ||
        fail "sole-C archive closure is not exactly $archive_member_count members"
    awk -F '\t' '!seen[$1]++ { print $1 }' "$link_inventory" | sort \
        >"$program_inventory"
    write_expected_products | sort >"$work/expected-products.txt"
    cmp "$work/expected-products.txt" "$program_inventory" >/dev/null ||
        fail "linked-product inventory differs from Git's pinned products"
    [ "$(wc -l <"$program_inventory" | tr -d ' ')" -eq "$LINKED_PRODUCT_COUNT" ] ||
        fail "sole-C linked-product closure is not exactly $LINKED_PRODUCT_COUNT products"
    for tuple in \
        "$OBJECT_CLOSURE_SHA256:$object_got:object" \
        "$LINK_CLOSURE_SHA256:$link_got:link" \
        "$ARCHIVE_CLOSURE_SHA256:$archive_got:archive"; do
        expected=${tuple%%:*}
        remainder=${tuple#*:}
        measured=${remainder%%:*}
        label=${tuple##*:}
        printf 'campaign-git: measured %s closure %s\n' "$label" "$measured" >&2
        [ "$expected" = MEASURE ] || [ "$expected" = "$measured" ] ||
            fail "$label closure digest changed"
    done
}

write_manifest() {
    write_closure_inventories
    write_archive_inventory
    verify_inventory_contract
    {
        echo '# cgf-sole-c-closure-v1'
        while IFS="$(printf '\t')" read -r source_identity output_relative; do
            source=$(materialize_source_path "$source_identity")
            output=$(materialize_tree_path "$output_relative")
            printf 'object\t%s\t%s\n' "$source" "$output"
        done <"$object_inventory"
        while IFS="$(printf '\t')" read -r archive_relative member object_relative; do
            object=$(materialize_tree_path "$object_relative")
            printf 'archive\t%s/%s\t%s\t%s\n' \
                "$tree" "$archive_relative" "$member" "$object"
        done <"$archive_inventory"
        while IFS="$(printf '\t')" read -r product_relative input_relative; do
            product=$(materialize_tree_path "$product_relative")
            input=$(materialize_tree_path "$input_relative")
            printf 'link-input\t%s\t%s\n' "$product" "$input"
        done <"$link_inventory"
    } >"$manifest"
}

validate_stage() {
    verify_products cgfried "$tree"
    verify_products host "$host_tree"
    test_unit_tree cgfried "$tree"
    test_unit_tree host "$host_tree"
    test_shell_tree cgfried "$tree" "$cc_wrapper" cgfried
    test_shell_tree host "$host_tree" "$hostcc" host
    cmp "$work/cgfried-shell-summary.txt" "$work/host-shell-summary.txt" >/dev/null ||
        fail "host and Cgfried shell-test summaries differ"
    mkdir -p "$work/parity"
    run_smoke cgfried "$tree"
    run_smoke host "$host_tree"
    cmp "$work/parity/cgfried.txt" "$work/parity/host.txt" >/dev/null ||
        fail "host and Cgfried smoke outputs differ"
    grep -Fqx "git version $GIT_VERSION" "$work/parity/cgfried.txt" ||
        fail "smoke version output changed"
    grep -Fqx 'count=1' "$work/parity/cgfried.txt" ||
        fail "smoke revision walk changed"
    grep -Fqx 'subject=campaign' "$work/parity/cgfried.txt" ||
        fail "smoke log output changed"
    write_manifest
    "$sole" verify "$receipts" "$cgf" "$manifest" "$report"
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'baseline.build\tPASS\tcompiler=host-cc,opt=O2\n'
        printf 'baseline.test.shell\tPASS\ttests=darwin:33203|linux:33235,successes=darwin:31846|linux:32180,known-breakages=darwin:361|linux:358,failures=0\n'
        printf 'baseline.test.unit\tPASS\ttests=261\n'
        printf 'build\tPASS\tarchives=1,objects=darwin:565|linux:563\n'
        printf 'compiler.sole-c\tPASS\tproject-objects=darwin:565|linux:563,archive-members=darwin:305|linux:303,linked-products=10\n'
        printf 'configure\tPASS\tcurl=off,expat=off,gettext=off,mode=gnu17,perl=off,python=off,rust=off,tcltk=off\n'
        printf 'linkage\tPASS\tarchives=1,binaries=10,static-project-closure=yes\n'
        printf 'parity.smoke\tPASS\tinit=1,commit=1,log=matched\n'
        printf 'source.archive\tPASS\tsha256=%s\n' "$GIT_SHA256"
        printf 'source.pin\tPASS\tcommit=%s,version=%s\n' "$GIT_COMMIT" "$GIT_VERSION"
        printf 'test.shell\tPASS\ttests=darwin:33203|linux:33235,successes=darwin:31846|linux:32180,known-breakages=darwin:361|linux:358,failures=0,host=matched\n'
        printf 'test.unit\tPASS\ttests=261,host=matched\n'
    } >"$work/results.txt"
    printf 'campaign-git: PASS target=%s results=%s artifacts=%s\n' \
        "$compiler_target" "$work/results.txt" "$logs"
}

case $stage in
    configure) configure_stage ;;
    build) build_stage ;;
    validate) validate_stage ;;
esac
