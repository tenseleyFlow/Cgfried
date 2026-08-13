#!/bin/sh
# Measure one fresh, serial build of the pinned Sprint 57 musl hybrid lane.
set -eu

LC_ALL=C
export LC_ALL

prog=musl-full-build-once
pin=b306b16af15c89a04d8e0c55cac2dadbeb39c083
git_cmd=${CGF_MUSL_BUILD_GIT_CMD:-git}

die()
{
    echo "$prog: $*" >&2
    exit 3
}

numeric_receipt()
{
    file=$1
    for key in wall_ms_median user_ms_median sys_ms_median maxrss_kb_max; do
        awk -F= -v key="$key" '
            $1 == key { count++; value = substr($0, length(key) + 2) }
            END {
                if (count != 1 || value !~ /^[0-9]+([.][0-9]+)?$/) exit 1
            }
        ' "$file" || die "timer receipt has no unique numeric $key"
    done
}

if [ "${1:-}" = --build ]; then
    [ "$#" -eq 3 ] || die 'internal build mode requires SOURCE WORK'
    source=$2
    work=$3
    wrapper=${CGF_MUSL_BUILD_WRAPPER:?CGF_MUSL_BUILD_WRAPPER is required}
    cgf=${CGF_MUSL_BUILD_CGF:?CGF_MUSL_BUILD_CGF is required}
    hostcc=${CGF_MUSL_BUILD_HOSTCC:-gcc}
    as_path=${CGF_AS_PATH:-$(command -v as 2>/dev/null || true)}
    ld_path=${CGF_LD_PATH:-$(command -v ld 2>/dev/null || true)}
    [ -n "$as_path" ] || die 'assembler is unavailable; set CGF_AS_PATH'
    [ -n "$ld_path" ] || die 'linker is unavailable; set CGF_LD_PATH'
    CGF_AS_PATH=$as_path
    CGF_LD_PATH=$ld_path
    export CGF_AS_PATH CGF_LD_PATH
    mkdir -p "$work" || die 'cannot create the fresh musl work directory'
    work=$(CDPATH='' cd "$work" && pwd -P) ||
        die 'cannot resolve the fresh musl work directory'
    mkdir -p "$work/src" "$work/routes" "$work/logs"
    "$git_cmd" -C "$source" archive "$pin" | tar -x -C "$work/src" ||
        die 'cannot archive the pinned musl source'
    export CGF_MUSL_CGF="$cgf" CGF_MUSL_HOSTCC="$hostcc"
    export CGF_MUSL_ROUTE_DIR="$work/routes" SOURCE_DATE_EPOCH=0 CGF_STATS=1
    (
        cd "$work/src"
        CC="$wrapper" ./configure --target=x86_64 --disable-shared --prefix=/usr
    ) >"$work/logs/configure.log" 2>&1 || die 'musl configure failed'
    SOURCE_DATE_EPOCH=0 make -C "$work/src" -j1 AR=ar RANLIB=ranlib \
        >"$work/logs/build.log" 2>&1 || die 'musl build failed'
    [ -f "$work/src/lib/libc.a" ] || die 'musl build produced no libc.a'
    exit 0
fi

[ "$#" -eq 3 ] || die "usage: $0 SOURCE WORK RECEIPT"
source=$1
work=$2
receipt=$3
root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
wrapper=${CGF_MUSL_BUILD_WRAPPER:-$root/scripts/campaigns/musl-cc.sh}
cgf=${CGF_MUSL_BUILD_CGF:-$root/build/cgfried}
timeit=${CGF_MUSL_BUILD_TIMEIT:-$root/build/timeit}
hostcc=${CGF_MUSL_BUILD_HOSTCC:-gcc}
timeout_seconds=${CGF_MUSL_BUILD_TIMEOUT:-1800}

command -v "$git_cmd" >/dev/null 2>&1 || die "Git command is unavailable: $git_cmd"
actual=$("$git_cmd" -C "$source" rev-parse --verify HEAD 2>/dev/null) ||
    die 'cannot resolve the musl source revision'
[ "$actual" = "$pin" ] || die "musl pin mismatch: expected $pin got $actual"
[ -x "$wrapper" ] || die "hybrid compiler wrapper is not executable: $wrapper"
wrapper_dir=$(CDPATH='' cd "$(dirname "$wrapper")" && pwd -P) ||
    die 'cannot resolve the hybrid compiler wrapper directory'
[ "$wrapper_dir/$(basename "$wrapper")" = "$root/scripts/campaigns/musl-cc.sh" ] ||
    die 'hybrid compiler wrapper must be scripts/campaigns/musl-cc.sh'
[ -x "$cgf" ] || die "Cgfried compiler is not executable: $cgf"
[ -x "$timeit" ] || die "timer is not executable: $timeit"
command -v "$hostcc" >/dev/null 2>&1 || die "host compiler is unavailable: $hostcc"
case $timeout_seconds in '' | *[!0-9]* | 0) die 'timeout must be a positive integer' ;; esac
[ ! -e "$work" ] || die "fresh work directory already exists: $work"
[ ! -e "$receipt" ] || die "refusing to overwrite receipt: $receipt"
mkdir -p "$(dirname "$work")" "$(dirname "$receipt")"

timer_receipt=$work.timer.txt
raw=$work.raw.txt
CGF_MUSL_BUILD_WRAPPER=$wrapper CGF_MUSL_BUILD_CGF=$cgf \
CGF_MUSL_BUILD_HOSTCC=$hostcc \
    "$timeit" -n 1 -w 0 -t "$timeout_seconds" -o "$raw" -- \
    "$0" --build "$source" "$work" >"$timer_receipt" ||
    die 'timed musl build failed'
numeric_receipt "$timer_receipt"

routes=$work/routes.tsv
find "$work/routes" -type f -name '*.route' -exec cat {} + |
    LC_ALL=C sort >"$routes"
cgf_c=$(awk -F '\t' '$1=="cgf" && $2 ~ /[.]c$/ {n++} END {print n+0}' "$routes")
host_complex=$(awk -F '\t' '$1=="host" && $2 ~ /src\/complex\/.*[.]c$/ {n++} END {print n+0}' "$routes")
host_asm=$(awk -F '\t' '$1=="host" && $2 ~ /[.][sS]$/ {n++} END {print n+0}' "$routes")
total=$(wc -l <"$routes" | tr -d ' ')
[ "$cgf_c:$host_complex:$host_asm:$total" = 1254:68:32:1354 ] ||
    die "wrong route counts: cgf=$cgf_c complex=$host_complex assembler=$host_asm total=$total"

stats=$work/stats.txt
awk '
    /^stat: arena[.]ast / {
        ast_count++
        for(i=3;i<=NF;i++){split($i,a,"=");if(a[1]=="peak_kb"&&a[2]>ast_peak)ast_peak=a[2];if(a[1]=="blocks"&&a[2]>ast_blocks)ast_blocks=a[2];if(a[1]=="waste_pct"&&a[2]>ast_waste)ast_waste=a[2]}
    }
    /^stat: arena[.]ir / {
        ir_count++
        for(i=3;i<=NF;i++){split($i,a,"=");if(a[1]=="peak_kb"&&a[2]>ir_peak)ir_peak=a[2];if(a[1]=="blocks"&&a[2]>ir_blocks)ir_blocks=a[2];if(a[1]=="waste_pct"&&a[2]>ir_waste)ir_waste=a[2]}
    }
    /^stat: intern / {
        intern_count++
        for(i=3;i<=NF;i++){split($i,a,"=");if(a[1]=="lookups")lookups+=a[2];if(a[1]=="hits")hits+=a[2]}
    }
    /^stat: pp / {
        pp_count++
        for(i=3;i<=NF;i++){split($i,a,"=");if(a[1]=="includes")includes+=a[2];if(a[1]=="guard_skips")guards+=a[2];if(a[1]=="tokens")tokens+=a[2]}
    }
    END {
        if(ast_count!=1254||ir_count!=1254||intern_count!=1254||pp_count!=1254)exit 1
        printf "musl.stat.arena.ast.peak_kb_max=%d\n",ast_peak
        printf "musl.stat.arena.ast.blocks_max=%d\n",ast_blocks
        printf "musl.stat.arena.ast.waste_pct_max=%d\n",ast_waste
        printf "musl.stat.arena.ir.peak_kb_max=%d\n",ir_peak
        printf "musl.stat.arena.ir.blocks_max=%d\n",ir_blocks
        printf "musl.stat.arena.ir.waste_pct_max=%d\n",ir_waste
        printf "musl.stat.intern.lookups_sum=%.0f\n",lookups
        printf "musl.stat.intern.hits_sum=%.0f\n",hits
        printf "musl.stat.intern.hit_pct=%d\n",lookups ? int(hits*100/lookups) : 0
        printf "musl.stat.pp.includes_sum=%.0f\n",includes
        printf "musl.stat.pp.guard_skips_sum=%.0f\n",guards
        printf "musl.stat.pp.tokens_sum=%.0f\n",tokens
    }
' "$work/logs/build.log" >"$stats" || die 'musl build did not produce the exact Cgfried stats inventory'

{
    echo 'schema=cgfried.musl-full-build-sample.v1'
    echo "musl_commit=$pin"
    echo 'target=x86_64-linux-musl'
    echo 'compiler_wrapper=scripts/campaigns/musl-cc.sh'
    echo 'source_date_epoch=0'
    echo 'jobs=1'
    echo 'route.cgf_c=1254'
    echo 'route.host_complex=68'
    echo 'route.host_assembler=32'
    echo 'route.total=1354'
    sed -n '/^wall_ms_median=/p;/^user_ms_median=/p;/^sys_ms_median=/p;/^maxrss_kb_max=/p' \
        "$timer_receipt"
    cat "$stats"
} >"$receipt"
rm -f "$timer_receipt" "$raw"
echo "$prog: wrote $receipt"
