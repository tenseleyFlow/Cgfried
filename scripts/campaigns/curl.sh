#!/bin/sh
set -eu

CURL_VERSION=8.9.1
CURL_SHA256=f292f6cc051d5bbabf725ef85d432dfeacc8711dd717ea97612ae590643801e5

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
archive=${CGF_CAMPAIGN_CURL_CACHE:-$root/build/campaigns/dl/curl-$CURL_VERSION.tar.xz}
work=${CGF_CAMPAIGN_CURL_WORK:-$root/build/campaigns/curl}
cgf=${CGF_CAMPAIGN_CURL_CGF:-$root/build/cgfried}
hostcc=${CGF_CAMPAIGN_CURL_HOSTCC:-gcc}
jobs=${CGF_CAMPAIGN_JOBS:-}
compat_header_rel=ci/campaigns/compat/arm64-linux-u128-storage.h
compat_header=$root/$compat_header_rel
compat_policy=opaque-u64x2-align16-v1
probe_ledger=$root/scripts/campaigns/curl-probe-ledger.sh

fail() {
    echo "campaign-curl: $*" >&2
    exit 1
}

[ -r "$archive" ] || fail "source cache is missing or unreadable: $archive"
[ -x "$cgf" ] || fail "cgfried compiler is missing or not executable: $cgf"
command -v "$hostcc" >/dev/null 2>&1 ||
    fail "host GCC is unavailable: $hostcc"
command -v sha256sum >/dev/null 2>&1 || fail "sha256sum is unavailable"
[ -r "$compat_header" ] ||
    fail "ARM64 hosted-header compatibility file is unreadable: $compat_header"
[ -x "$probe_ledger" ] ||
    fail "configure-deviation ledger helper is not executable: $probe_ledger"

compiler_target=$("$cgf" -dumpmachine) ||
    fail "cannot query Cgfried's target"
compat_active=no
compat_cppflags=
case $compiler_target in
    x86_64-linux-gnu) ;;
    arm64-linux)
        compat_active=yes
        compat_cppflags="-include $compat_header"
        ;;
    *) fail "unsupported native campaign target: $compiler_target" ;;
esac
compat_header_sha256=$(sha256sum "$compat_header" | awk '{ print $1 }')

printf '%s  %s\n' "$CURL_SHA256" "$archive" | sha256sum -c - >/dev/null ||
    fail "source checksum mismatch: $archive"

archive_members=$archive.members.$$
trap 'rm -f "$archive_members"' EXIT HUP INT TERM
tar -tJf "$archive" >"$archive_members" || fail "cannot list source archive: $archive"
awk -v root="curl-$CURL_VERSION" '
    BEGIN { bad = 0 }
    {
        name = $0
        original = name
        if (substr(name, 1, 1) == "/" || index(name, "\\") != 0 ||
            (name != root && name != root "/" && index(name, root "/") != 1)) {
            printf "campaign-curl: archive member escapes expected root: %s\n", name > "/dev/stderr"
            bad = 1
            next
        }
        sub(/\/$/, "", name)
        count = split(name, component, "/")
        for (i = 1; i <= count; i++) {
            if (component[i] == "" || component[i] == "." || component[i] == "..") {
                printf "campaign-curl: archive member contains an unsafe component: %s\n", original > "/dev/stderr"
                bad = 1
                break
            }
        }
    }
    END { exit bad }
' "$archive_members" || fail "source archive member audit failed"
tar -tvJf "$archive" | awk '
    substr($1, 1, 1) != "-" && substr($1, 1, 1) != "d" { bad = 1 }
    END { exit bad }
' || fail "source archive contains a link or special-file member"
rm -f "$archive_members"
trap - EXIT HUP INT TERM

case $archive in /*) ;; *) archive=$root/$archive ;; esac
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
case $archive in "$work"/*) fail "source cache must be outside the disposable work tree" ;; esac

rm -rf "$work"
mkdir -p "$work/cgfried-src" "$work/host-gcc-src" \
    "$work/logs/cgfried" "$work/logs/host-gcc"
tar --no-same-owner --no-same-permissions -xJf "$archive" \
    -C "$work/cgfried-src" --strip-components=1
tar --no-same-owner --no-same-permissions -xJf "$archive" \
    -C "$work/host-gcc-src" --strip-components=1
printf '%s\n' "$CURL_VERSION" >"$work/source-version.txt"
printf '%s\n' "$CURL_SHA256" >"$work/source-sha256.txt"
u128_matches=$work/logs/upstream-uint128-uses.txt
u128_scan_errors=$work/logs/upstream-uint128-scan.err
u128_scan_status=0
LC_ALL=C grep -r -I -n -F --include='*.c' --include='*.h' -- \
    '__uint128_t' "$work/cgfried-src" >"$u128_matches" \
    2>"$u128_scan_errors" || u128_scan_status=$?
case $u128_scan_status in
    0 | 1) ;;
    *)
        cat "$u128_scan_errors" >&2
        fail "cannot audit pinned Curl sources for integer-128 use"
        ;;
esac
upstream_u128_count=$(wc -l <"$u128_matches" | tr -d ' ')
[ "$upstream_u128_count" -eq 0 ] ||
    fail "pinned Curl sources use unsupported integer-128 semantics"

if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')
fi
case $jobs in '' | *[!0-9]* | 0) fail "CGF_CAMPAIGN_JOBS must be a positive integer" ;; esac

as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
[ -n "$as_path" ] || fail "assembler is unavailable; set CGF_AS_PATH"
[ -n "$ld_path" ] || fail "linker is unavailable; set CGF_LD_PATH"
export CGF_AS_PATH="$as_path" CGF_LD_PATH="$ld_path"

# Keep host-dependent optional libraries out of both lanes.  HTTP and FILE
# remain enabled, so the produced binary is useful while the campaign stays
# reproducible on minimal native runners.
configure_options='--disable-shared
--enable-static
--without-ssl
--without-libpsl
--disable-ldap
--disable-ldaps
--disable-ares
--without-zlib
--without-brotli
--without-zstd
--without-libidn2
--without-libgsasl
--without-librtmp
--without-nghttp2
--without-nghttp3
--without-ngtcp2
--without-quiche
--without-msh3
--without-hyper
--disable-manual
--disable-docs'
printf '%s\n' "$configure_options" >"$work/configure-options.txt"
{
    printf 'version=%s\n' "$CURL_VERSION"
    printf 'sha256=%s\n' "$CURL_SHA256"
    printf 'archive=%s\n' "$archive"
    printf 'cgfried=%s\n' "$cgf"
    printf 'host-gcc=%s\n' "$hostcc"
    printf 'assembler=%s\n' "$as_path"
    printf 'linker=%s\n' "$ld_path"
    printf 'jobs=%s\n' "$jobs"
    printf 'compiler-target=%s\n' "$compiler_target"
    printf 'hosted-header-compatibility=%s\n' "$compat_active"
    printf 'hosted-header-policy=%s\n' "$compat_policy"
    printf 'hosted-header-path=%s\n' "$compat_header_rel"
    printf 'hosted-header-sha256=%s\n' "$compat_header_sha256"
    printf 'upstream-uint128-occurrences=%s\n' "$upstream_u128_count"
} >"$work/logs/commands.txt"

configure_lane() {
    label=$1
    tree=$2
    compiler=$3
    cppflags=$4
    log=$work/logs/$label/configure.log
    status=0
    (
        cd "$tree"
        # Word splitting is intentional: configure_options is one option per
        # line and contains no shell metacharacters or values with spaces.
        # shellcheck disable=SC2086
        LC_ALL=C SOURCE_DATE_EPOCH=0 CC="$compiler" CPPFLAGS="$cppflags" \
            ./configure $configure_options --cache-file=config.cache
    ) >"$log" 2>&1 || status=$?
    if grep -F 'cgfried: internal compiler error' "$tree/config.log" "$log" \
        >"$work/logs/$label/ice.txt" 2>/dev/null; then
        fail "$label configure triggered a compiler ICE (see $work/logs/$label/ice.txt)"
    fi
    : >"$work/logs/$label/ice.txt"
    if [ "$status" -ne 0 ]; then
        tail -80 "$log" >&2
        fail "$label configure failed with status $status (config.log retained at $tree/config.log)"
    fi
    cp "$tree/config.log" "$work/logs/$label/config.log"
}

configure_lane host-gcc "$work/host-gcc-src" "$hostcc" ''
configure_lane cgfried "$work/cgfried-src" "$cgf" "$compat_cppflags"

# Preserve every cached probe result and a deterministic cross-compiler diff.
# Compiler identity/flag-capability probes are kept in the raw snapshots but
# filtered from the platform-feature parity gate.
normalize_cache() {
    input=$1
    output=$2
    sed -n '/^## Cache variables. ##$/,/^## Output variables. ##$/p' "$input" |
        sed -n '/^[a-zA-Z][a-zA-Z0-9_]*=/p' |
        LC_ALL=C sort >"$output"
}
platform_cache() {
    input=$1
    output=$2
    awk -F= '
        /^(ac_cv_env_|ac_cv_prog_|ac_cv_c_compiler_|lt_cv_prog_compiler_)/ { next }
        /^(curl_cv_|ac_cv_)/ { print }
    ' "$input" >"$output"
}
normalize_cache "$work/logs/cgfried/config.log" "$work/logs/cgfried/probes.txt"
normalize_cache "$work/logs/host-gcc/config.log" "$work/logs/host-gcc/probes.txt"
platform_cache "$work/logs/cgfried/probes.txt" "$work/logs/cgfried/platform-probes.txt"
platform_cache "$work/logs/host-gcc/probes.txt" "$work/logs/host-gcc/platform-probes.txt"
host_only=$work/logs/host-gcc/platform-only.txt
cgf_only=$work/logs/cgfried/platform-only.txt
comm -23 "$work/logs/host-gcc/platform-probes.txt" \
    "$work/logs/cgfried/platform-probes.txt" >"$host_only"
comm -13 "$work/logs/host-gcc/platform-probes.txt" \
    "$work/logs/cgfried/platform-probes.txt" >"$cgf_only"
"$probe_ledger" "$host_only" "$cgf_only" "$work/probe-deviations.txt"
probe_deviations=$(wc -l <"$work/probe-deviations.txt" | tr -d ' ')
probe_sha256=$(sha256sum "$work/probe-deviations.txt" | awk '{ print $1 }')
expected_probe_sha256=5b3997ad9bae1ceb6f1808c0d692f5e522139b4c38785d56a0cc1d91f047e0f6
[ "$probe_sha256" = "$expected_probe_sha256" ] || {
    cat "$work/probe-deviations.txt" >&2
    fail "configure deviation ledger changed: expected $expected_probe_sha256, got $probe_sha256"
}

build_lane() {
    label=$1
    tree=$2
    log=$work/logs/$label/build.log
    if ! LC_ALL=C SOURCE_DATE_EPOCH=0 make -C "$tree" -j"$jobs" V=1 \
        >"$log" 2>&1; then
        tail -100 "$log" >&2
        fail "$label build failed (see $log)"
    fi
    [ -x "$tree/src/curl" ] || fail "$label build produced no src/curl"
}

build_lane host-gcc "$work/host-gcc-src"
build_lane cgfried "$work/cgfried-src"

normalize_version() {
    input=$1
    output=$2
    awk -v version="$CURL_VERSION" '
        NR == 1 {
            if ($1 != "curl" || $2 != version || $4 != "libcurl/" version) exit 1
            print "curl=" $2
            print "libcurl=" substr($4, 9)
            next
        }
        $1 == "Protocols:" {
            have_file = have_http = 0
            protocols = ""
            for (i = 2; i <= NF; i++) {
                if ($i == "file") have_file = 1
                if ($i == "http") have_http = 1
                protocols = protocols (i == 2 ? "" : " ") $i
            }
            if (!have_file || !have_http) exit 1
            print "protocols=" protocols
            next
        }
        $1 == "Features:" {
            features = ""
            for (i = 2; i <= NF; i++) features = features (i == 2 ? "" : " ") $i
            print "features=" features
        }
    ' "$input" >"$output"
    [ "$(wc -l <"$output" | tr -d ' ')" = 4 ]
}

for label in host-gcc cgfried; do
    tree=$work/$label-src
    version_log=$work/logs/$label/version.log
    if ! "$tree/src/curl" --version >"$version_log" 2>&1; then
        cat "$version_log" >&2
        fail "$label curl --version failed"
    fi
    if ! normalize_version "$version_log" "$work/logs/$label/version-surface.txt"; then
        cat "$version_log" >&2
        fail "$label curl --version has an unexpected version or protocol surface"
    fi
done
if ! diff -u "$work/logs/host-gcc/version-surface.txt" \
    "$work/logs/cgfried/version-surface.txt" >"$work/version-surface.diff"; then
    cat "$work/version-surface.diff" >&2
    fail "Cgfried curl version/protocol surface differs from host GCC"
fi

# Building the test support is part of the rung; executing a curated offline
# runtests.pl allowlist remains an explicitly non-gating stretch objective.
if ! LC_ALL=C SOURCE_DATE_EPOCH=0 make -C "$work/cgfried-src/tests" -j"$jobs" \
    >"$work/logs/cgfried/tests-build.log" 2>&1; then
    tail -100 "$work/logs/cgfried/tests-build.log" >&2
    fail "cgfried curl test-support build failed"
fi

{
    echo '# cgf-campaign-results-v1'
    printf '# columns=key\toutcome\tdetail\n'
    printf 'baseline.build\tPASS\tcompiler=host-gcc\n'
    printf 'baseline.configure\tPASS\tcompiler=host-gcc\n'
    printf 'build\tPASS\tcompiler=cgfried\n'
    printf 'configure\tPASS\tcompiler=cgfried,ice=0\n'
    printf 'hosted-header.compat\tPASS\tCAMP-ALL-004;header-sha256=%s,policy=%s,scope=arm64-linux-system-headers,upstream-uses=%s\n' \
        "$compat_header_sha256" "$compat_policy" "$upstream_u128_count"
    printf 'probe.deviations\tPASS\tCAMP-CURL-001+CAMP-CURL-002;rows=%s,sha256=%s,artifact=probe-deviations.txt\n' "$probe_deviations" "$probe_sha256"
    printf 'source.cache\tPASS\tmode=offline,sha256=%s\n' "$CURL_SHA256"
    printf 'source.pin\tPASS\tversion=%s\n' "$CURL_VERSION"
    printf 'stretch.offline-subset\tSKIP\tCAMP-CURL-003;scope=stretch,network-harness-deferred\n'
    printf 'test.version\tPASS\tbaseline=host-gcc,version=curl-%s,protocols=file+http,surface=exact\n' "$CURL_VERSION"
    printf 'tests.build\tPASS\tscope=test-support\n'
} >"$work/results.txt"

printf 'campaign-curl: PASS version=%s probe-deviation-rows=%s results=%s\n' \
    "$CURL_VERSION" "$probe_deviations" "$work/results.txt"
