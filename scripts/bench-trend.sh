#!/bin/sh
# Render zero-dependency 90-day benchmark trends from dated run artifacts.
set -u
LC_ALL=C
export LC_ALL

die()
{
    echo "bench-trend: $*" >&2
    exit 3
}

days=90
files=
while [ "$#" -gt 0 ]; do
    case $1 in
        --days) [ "$#" -ge 2 ] || die "--days requires a value"; days=$2; shift 2 ;;
        --) shift; break ;;
        -*) die "unknown option $1" ;;
        *) files="$files
$1"; shift ;;
    esac
done
case $days in *[!0-9]* | '') die "days must be a positive integer" ;; esac
[ "$days" -gt 0 ] || die "days must be a positive integer"
[ -n "$files" ] || die "at least one run file or directory is required"

tmp=${TMPDIR:-/tmp}/cgf-bench-trend.$$
mkdir "$tmp" 2>/dev/null || die "cannot create temporary directory"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
: >"$tmp/records"

old_ifs=$IFS
IFS='
'
for input in $files; do
    [ -n "$input" ] || continue
    if [ -d "$input" ]; then
        found=$(find "$input" -type f -name '*.txt' -print | sort)
        [ -n "$found" ] || die "no run files in $input"
    else
        [ -r "$input" ] || die "cannot read $input"
        found=$input
    fi
    for file in $found; do
        awk -v file="$file" -v out="$tmp/records" '
            function'" "'trim(s) { sub(/^[[:space:]]+/,"",s); sub(/[[:space:]]+$/, "",s); return s }
            function'" "'fail(s) { print "bench-trend: " file ":" FNR ": " s > "/dev/stderr"; bad=1 }
            function'" "'valid_date(s,    p,y,m,d,limit) {
                if (s !~ /^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$/) return 0
                split(s,p,"-"); y=p[1]+0; m=p[2]+0; d=p[3]+0
                if (m < 1 || m > 12) return 0
                limit = (m==2 ? ((y%4==0 && (y%100!=0 || y%400==0)) ? 29 : 28) :
                         (m==4 || m==6 || m==9 || m==11) ? 30 : 31)
                return d >= 1 && d <= limit
            }
            {
                raw=$0
                if (raw ~ /^[[:space:]]*#/) {
                    line=raw
                    sub(/^[[:space:]]*#[[:space:]]*/,"",line)
                    line=trim(line)
                    eq=index(line,"=")
                    if (!eq) next
                    key=trim(substr(line,1,eq-1)); value=trim(substr(line,eq+1))
                    if (key=="target") target=value
                    else if (key=="host") host=value
                    else if (key=="host_class") host_class=value
                    else if (key=="date" || key=="date_utc") date=value
                    next
                }
                line=raw; sub(/[[:space:]]*#.*/,"",line); line=trim(line)
                if (line=="") next
                eq=index(line,"=")
                if (!eq) { fail("expected metric=value"); next }
                key=trim(substr(line,1,eq-1)); value=trim(substr(line,eq+1))
                if (key !~ /^[A-Za-z0-9_.:-]+$/) { fail("invalid metric name " key); next }
                if (key in seen) { fail("duplicate metric " key); next }
                seen[key]=1; val[key]=value
                if (key=="target") target=value
                else if (key=="host") host=value
                else if (key=="host_class") host_class=value
                else if (key=="date" || key=="date_utc") date=value
            }
            END {
                if (target=="") { print "bench-trend: " file ": missing target" > "/dev/stderr"; bad=1 }
                if (host=="") host=(host_class=="" ? "unknown" : host_class)
                sub(/T.*/,"",date)
                if (!valid_date(date)) {
                    print "bench-trend: " file ": missing or malformed date" > "/dev/stderr"; bad=1
                }
                if (bad) exit 3
                for (key in val) {
                    if (val[key] !~ /^[0-9]+([.][0-9]+)?$/) continue
                    if (key ~ /user_ms_median$/) {
                        prefix=key; sub(/user_ms_median$/,"",prefix); sys=prefix "sys_ms_median"
                        if (!(sys in val) || val[sys] !~ /^[0-9]+([.][0-9]+)?$/) {
                            print "bench-trend: " file ": " key " requires numeric " sys > "/dev/stderr"; bad=1
                        } else print target "\t" host "\t" date "\t" prefix "user+sys_ms_median\t" val[key]+val[sys] >> out
                    } else if (key !~ /sys_ms_median$/ &&
                               (key ~ /(wall_ms_median|maxrss_kb_max|size|size_stripped|size_unstripped|icount|text)$/ || key ~ /[.]stat[.](arena|intern)[.]/))
                        print target "\t" host "\t" date "\t" key "\t" val[key] >> out
                }
                if (bad) exit 3
            }
        ' "$file" || exit $?
    done
done
IFS=$old_ifs
[ -s "$tmp/records" ] || die "inputs contain no trend metrics"

awk -F '	' -v days="$days" '
    function'" "'civil(y,m,d,    era,yoe,doy,mp) {
        if (m <= 2) y--
        era=int(y/400); yoe=y-era*400; mp=m+(m>2?-3:9)
        doy=int((153*mp+2)/5)+d-1
        return era*146097+yoe*365+int(yoe/4)-int(yoe/100)+doy
    }
    {
        split($3,p,"-"); stamp=civil(p[1]+0,p[2]+0,p[3]+0); when[NR]=stamp; line[NR]=$0
        if (NR==1 || stamp>latest) latest=stamp
    }
    END { for(i=1;i<=NR;i++) if (when[i] >= latest-days+1) print line[i] }
' "$tmp/records" >"$tmp/window"

awk -F '	' '
    function'" "'rank(k) {
        if(k~/wall_ms_median$/) return 10; if(k~/user[+]sys_ms_median$/) return 20
        if(k~/maxrss_kb_max$/) return 30; if(k~/size/) return 40
        if(k~/icount$/) return 50; if(k~/text$/) return 60; return 70
    }
    { printf "%03d\t%s\t%s\t%s\t%s\t%s\n",rank($4),$1,$2,$4,$3,$5 }
' "$tmp/window" | sort -t '	' -k1,1n -k2,2 -k3,3 -k4,4 -k5,5 >"$tmp/sorted"

echo "# Performance trend — last $days days"
echo
echo '| target / host | metric | first | latest | delta | threshold | trend | status |'
echo '|---|---|---:|---:|---:|---|---|---|'
awk -F '	' '
    function'" "'pct(a,b) { return a+0==0 ? (b+0==0 ? "+0.0%" : "+inf") : sprintf("%+.1f%%",(b-a)*100/a) }
    function'" "'flush(    i,min,max,pos,spark,threshold,bad,diff) {
        if (!count) return
        min=max=value[1]
        for(i=2;i<=count;i++){if(value[i]<min)min=value[i];if(value[i]>max)max=value[i]}
        spark=""
        for(i=1;i<=count;i++){
            pos=(max==min?0:int((value[i]-min)*3/(max-min)+0.5)); spark=spark substr("._-^",pos+1,1)
        }
        diff=value[count]-value[1]
        if(metric~/[.]cgf[.]wall_ms_median$/){threshold="+10% (runtime; per-run gate also requires >4 MAD)";bad=value[count]*100>value[1]*110}
        else if(metric~/[.]gcc[.]wall_ms_median$/){threshold="report-only (gcc runtime reference)";bad=0}
        else if(metric~/wall_ms_median$/ || metric~/user[+]sys_ms_median$/){threshold="+30%";bad=value[count]*100>value[1]*130}
        else if(metric~/maxrss_kb_max$/){threshold="+20%";bad=value[count]*100>value[1]*120}
        else if(metric~/size_unstripped$/){threshold="report-only (unstripped size)";bad=0}
        else if(metric~/size/){threshold="+15%";bad=value[count]*100>value[1]*115}
        else if(metric~/icount$/){threshold="max(+2%, +2 instr)";bad=(diff>2 && value[count]*100>value[1]*102)}
        else if(metric~/([.]O2[.]text|[.]Os[.]text)$/ || metric=="cgf.text"){threshold="report-only (size section)";bad=0}
        else if(metric~/text$/){threshold="+5%";bad=value[count]*100>value[1]*105}
        else {threshold="report-only";bad=0}
        printf "| %s / %s | %s | %s | %s | %s | %s | `%s` | %s |\n",target,host,metric,value[1],value[count],pct(value[1],value[count]),threshold,spark,(bad?"FLAG":"OK")
        count=0
    }
    {
        id=$2 SUBSEP $3 SUBSEP $4
        if(last!="" && id!=last) flush()
        last=id; target=$2; host=$3; metric=$4; value[++count]=$6
    }
    END { flush() }
' "$tmp/sorted"
