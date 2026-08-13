#!/bin/sh
# Aggregate fresh serial musl builds into one controlled fleet receipt.
set -eu

LC_ALL=C
export LC_ALL
prog=musl-full-build-bench

die()
{
    echo "$prog: $*" >&2
    exit 3
}

[ "$#" -eq 3 ] || die "usage: $0 SOURCE WORK RECEIPT"
source=$1
work=$2
receipt=$3
root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
once=${CGF_MUSL_BUILD_ONCE:-$root/scripts/musl-full-build-once.sh}
classifier=${CGF_MUSL_BUILD_CLASSIFIER:-$root/scripts/bench-control.sh}
control=${CGF_MUSL_BUILD_CONTROL_RECEIPT:-}
host=${CGF_MUSL_BUILD_HOST:-}
runs=${CGF_MUSL_BUILD_RUNS:-10}
warmup=${CGF_MUSL_BUILD_WARMUP:-1}
date_utc=${CGF_MUSL_BUILD_DATE_UTC:-$(date -u '+%Y-%m-%dT%H:%M:%SZ')}
cgf_rev=${CGF_MUSL_BUILD_CGF_REVISION:-$(git -C "$root" rev-parse HEAD 2>/dev/null || true)}
cgf_tree=${CGF_MUSL_BUILD_CGF_TREE:-}

case $host in kasumi | hasu) ;; *) die 'host must explicitly name kasumi or hasu' ;; esac
case $runs in '' | *[!0-9]* | 0) die 'runs must be a positive integer' ;; esac
[ "$runs" -eq 10 ] || die 'runs must be exactly 10'
[ "$warmup" -eq 1 ] 2>/dev/null || die 'warmup must be exactly 1'
case $date_utc in ????-??-??T??:??:??Z) ;; *) die 'date_utc must be an ISO UTC timestamp' ;; esac
case $cgf_rev in ????????????????????????????????????????) ;; *) die 'Cgfried revision must be an exact commit id' ;; esac
if [ -z "$cgf_tree" ]; then
    if [ -z "$(git -C "$root" status --porcelain --untracked-files=normal 2>/dev/null)" ]; then
        cgf_tree=clean
    else
        cgf_tree=dirty
    fi
fi
case $cgf_tree in clean | dirty) ;; *) die 'Cgfried tree provenance must be clean or dirty' ;; esac
[ "$cgf_tree" = clean ] || die 'Cgfried tree must be clean before measurement'
[ -x "$once" ] || die "single-build helper is not executable: $once"
[ -x "$classifier" ] || die "control classifier is not executable: $classifier"
[ -r "$control" ] || die 'a readable control receipt is required'
class_status=0
class=$($classifier classify --require-v2 "$control") || class_status=$?
[ "$class_status:$class" = 0:controlled ] || die 'control receipt is not controlled fleet-control-v2 evidence'
[ ! -e "$work" ] || die "fresh benchmark work directory already exists: $work"
[ ! -e "$receipt" ] || die "refusing to overwrite receipt: $receipt"
mkdir -p "$work/samples" "$(dirname "$receipt")"

discard_build_tree()
{
    tree=$1
    case $tree in
    "$work"/warmup | "$work"/run-[0-9][0-9]) ;;
    *) die "refusing to remove unexpected build tree: $tree" ;;
    esac
    [ ! -L "$tree" ] || die "refusing to remove symlinked build tree: $tree"
    [ ! -e "$tree" ] || rm -rf "$tree" || die "cannot remove completed build tree: $tree"
}

"$once" "$source" "$work/warmup" "$work/samples/warmup.txt" ||
    die 'fresh warmup build failed'
discard_build_tree "$work/warmup"

i=1
while [ "$i" -le "$runs" ]; do
    sample_id=$(printf '%02d' "$i")
    "$once" "$source" "$work/run-$sample_id" "$work/samples/sample-$sample_id.txt" ||
        die "fresh build sample $i failed"
    discard_build_tree "$work/run-$sample_id"
    i=$((i + 1))
done

for key in schema musl_commit target compiler_wrapper source_date_epoch jobs \
    route.cgf_c route.host_complex route.host_assembler route.total \
    musl.stat.arena.ast.peak_kb_max musl.stat.arena.ast.blocks_max \
    musl.stat.arena.ast.waste_pct_max musl.stat.arena.ir.peak_kb_max \
    musl.stat.arena.ir.blocks_max musl.stat.arena.ir.waste_pct_max \
    musl.stat.intern.lookups_sum musl.stat.intern.hits_sum \
    musl.stat.intern.hit_pct musl.stat.pp.includes_sum \
    musl.stat.pp.guard_skips_sum musl.stat.pp.tokens_sum; do
    awk -F= -v key="$key" -v runs="$runs" '
        $1 == key {
            count++
            value = substr($0, length(key)+2)
            if (!(value in values)) { values[value]=1; distinct++ }
        }
        END { if (count != runs || distinct != 1) exit 1 }
    ' "$work"/samples/sample-*.txt || die "sample provenance disagrees for $key"
done
awk -F= -v runs="$runs" '
    $1 ~ /^(wall_ms_median|user_ms_median|sys_ms_median|maxrss_kb_max)$/ {
        count[$1]++
        value=substr($0, length($1)+2)
        if (value !~ /^[0-9]+([.][0-9]+)?$/) bad=1
    }
    END {
        required[1]="wall_ms_median"; required[2]="user_ms_median"
        required[3]="sys_ms_median"; required[4]="maxrss_kb_max"
        for(i=1;i<=4;i++) if(count[required[i]] != runs) bad=1
        exit bad
    }
' "$work"/samples/sample-*.txt || die 'sample metrics are malformed or incomplete'
[ "$(sed -n 's/^schema=//p' "$work/samples/sample-01.txt")" = \
    cgfried.musl-full-build-sample.v1 ] || die 'unsupported sample schema'

metric()
{
    key=$1
    mode=$2
    awk -F= -v key="$key" -v mode="$mode" '
        $1 == key { value[++n] = substr($0, length(key)+2) + 0 }
        END {
            if (!n) exit 1
            for (i=2; i<=n; i++) {
                x=value[i]; j=i-1
                while (j && value[j] > x) { value[j+1]=value[j]; j-- }
                value[j+1]=x
            }
            if (mode == "max") print value[n]
            else if (n % 2) print value[(n+1)/2]
            else printf "%.3f\n", (value[n/2]+value[n/2+1])/2
        }
    ' "$work"/samples/sample-*.txt
}

mad()
{
    key=$1
    median_value=$(metric "$key" median)
    awk -F= -v key="$key" -v median="$median_value" '
        $1 == key {
            x=substr($0,length(key)+2)+0-median
            if(x<0)x=-x
            value[++n]=x
        }
        END {
            for(i=2;i<=n;i++){x=value[i];j=i-1;while(j&&value[j]>x){value[j+1]=value[j];j--}value[j+1]=x}
            if(n%2) print value[(n+1)/2]
            else printf "%.3f\n",(value[n/2]+value[n/2+1])/2
        }
    ' "$work"/samples/sample-*.txt
}

raw_vector()
{
    key=$1
    awk -F= -v key="$key" '$1==key { if(seen++) printf ","; printf "%s",substr($0,length(key)+2) } END { print "" }' \
        "$work"/samples/sample-*.txt
}

{
    echo 'schema=cgfried.musl-full-build.v1'
    echo "host=$host"
    echo 'target=x86_64-linux-musl'
    echo 'workload=musl-full-static-hybrid'
    echo 'musl_commit=b306b16af15c89a04d8e0c55cac2dadbeb39c083'
    echo "date=$date_utc"
    echo "cgf_rev=$cgf_rev"
    echo "cgf_tree=$cgf_tree"
    echo 'compiler_wrapper=scripts/campaigns/musl-cc.sh'
    echo 'source_date_epoch=0'
    echo 'jobs=1'
    echo "runs=$runs"
    echo 'warmup=1'
    echo "timeit_protocol=runs=$runs,warmup=1;fresh-tree-per-sample;source-date-epoch=0;jobs=1"
    echo 'route.cgf_c=1254'
    echo 'route.host_complex=68'
    echo 'route.host_assembler=32'
    echo 'route.total=1354'
    sed -n '/^control_protocol=/p;/^logical_cpus=/p;/^cpu_idle_pct=/p;/^load1=/p;/^governor=/p;/^power_profile=/p;/^scaling_driver=/p;/^energy_performance_preference=/p' "$control"
    echo "wall_ms_median=$(metric wall_ms_median median)"
    echo "wall_ms_mad=$(mad wall_ms_median)"
    echo "user_ms_median=$(metric user_ms_median median)"
    echo "user_ms_mad=$(mad user_ms_median)"
    echo "sys_ms_median=$(metric sys_ms_median median)"
    echo "sys_ms_mad=$(mad sys_ms_median)"
    echo "maxrss_kb_max=$(metric maxrss_kb_max max)"
    echo "raw.wall_ms=$(raw_vector wall_ms_median)"
    echo "raw.user_ms=$(raw_vector user_ms_median)"
    echo "raw.sys_ms=$(raw_vector sys_ms_median)"
    echo "raw.maxrss_kb=$(raw_vector maxrss_kb_max)"
    for key in musl.stat.arena.ast.peak_kb_max \
        musl.stat.arena.ast.blocks_max musl.stat.arena.ast.waste_pct_max \
        musl.stat.arena.ir.peak_kb_max musl.stat.arena.ir.blocks_max \
        musl.stat.arena.ir.waste_pct_max musl.stat.intern.lookups_sum \
        musl.stat.intern.hits_sum musl.stat.intern.hit_pct \
        musl.stat.pp.includes_sum musl.stat.pp.guard_skips_sum \
        musl.stat.pp.tokens_sum; do
        sed -n "s/^$key=/$key=/p" "$work/samples/sample-01.txt"
    done
} >"$receipt"
echo "$prog: wrote $receipt"
