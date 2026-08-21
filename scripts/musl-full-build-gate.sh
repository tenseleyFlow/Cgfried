#!/bin/sh
# Compare compatible host-specific full-musl build receipts.
set -eu

prog=musl-full-build-gate
[ "$#" -eq 2 ] || { echo "usage: $0 BASELINE RESULT" >&2; exit 3; }
baseline=$1
result=$2
root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
classifier=${CGF_MUSL_BUILD_CLASSIFIER:-$root/scripts/bench-control.sh}
for file in "$baseline" "$result"; do
    [ -r "$file" ] || { echo "$prog: cannot read $file" >&2; exit 3; }
done
[ -x "$classifier" ] || { echo "$prog: control classifier is not executable" >&2; exit 3; }
for file in "$baseline" "$result"; do
    control_status=0
    control_class=$($classifier classify --require-v2 "$file") || control_status=$?
    [ "$control_status:$control_class" = 0:controlled ] || {
        echo "$prog: uncontrolled evidence: $file" >&2
        exit 3
    }
done

set +e
awk -F= -v baseline_file="$baseline" '
function'" "'fail(message) { print "musl-full-build-gate: " message > "/dev/stderr"; bad=1 }
function'" "'regression(message) { print "musl-full-build-gate: " message > "/dev/stderr"; regressed=1 }
function'" "'read_line(file, key, value, equals) {
    equals=index($0,"=")
    key=substr($0,1,equals-1); value=substr($0,equals+1)
    if (!equals || key !~ /^[A-Za-z0-9_.-]+$/ || value == "") { fail(file ": malformed key=value receipt"); return }
    if (file == baseline_file) { if (key in base) fail(file ": duplicate " key); base[key]=value }
    else { if (key in cur) fail(file ": duplicate " key); cur[key]=value }
}
function'" "'require(values, key, label) { if (!(key in values)) { fail(label " missing " key); return 0 } return 1 }
function'" "'same(key) { if (require(base,key,"baseline") && require(cur,key,"result") && base[key] != cur[key]) fail(key " does not match baseline") }
function'" "'numeric(values,key,label) { if (!require(values,key,label)) return 0; if (values[key] !~ /^[0-9]+([.][0-9]+)?$/) { fail(label " has nonnumeric " key); return 0 } return 1 }
function'" "'timestamp(values,key,label,value) {
    if (!require(values,key,label)) return 0
    value=values[key]
    if (length(value) != 20 || value !~ /^[0-9-]+T[0-9:]+Z$/) { fail(label " has malformed " key); return 0 }
    return 1
}
function'" "'raw_vector(values,key,label,parts,n,i) {
    if (!require(values,key,label)) return 0
    n=split(values[key],parts,",")
    if(n != 10) { fail(label " " key " must contain 10 samples"); return 0 }
    for(i=1;i<=n;i++) if(parts[i] !~ /^[0-9]+([.][0-9]+)?$/) { fail(label " has malformed " key); return 0 }
    return 1
}
function exceeds_noise_floor(base_value,result_value,percent,base_mad,result_mad,
                             nominal,noise,allowance) {
    nominal=(base_value+0)*percent/100
    noise=4*((base_mad+0)>(result_mad+0)?(base_mad+0):(result_mad+0))
    allowance=nominal>noise?nominal:noise
    return (result_value+0)>(base_value+0)+allowance
}
{ read_line(FILENAME) }
END {
    fixed[1]="schema"; fixed[2]="host"; fixed[3]="target"; fixed[4]="workload"
    fixed[5]="musl_commit"; fixed[6]="compiler_wrapper"; fixed[7]="source_date_epoch"
    fixed[8]="jobs"; fixed[9]="runs"; fixed[10]="warmup"; fixed[11]="route.cgf_c"
    fixed[12]="route.host_complex"; fixed[13]="route.host_assembler"; fixed[14]="route.total"
    fixed[15]="control_protocol"; fixed[16]="logical_cpus"; fixed[17]="governor"
    fixed[18]="power_profile"; fixed[19]="scaling_driver"; fixed[20]="energy_performance_preference"
    fixed[21]="timeit_protocol"; fixed[22]="cgf_tree"
    for (i=1;i<=22;i++) same(fixed[i])
    if (base["schema"] != "cgfried.musl-full-build.v1") fail("unsupported baseline schema")
    if (cur["schema"] != "cgfried.musl-full-build.v1") fail("unsupported result schema")
    if (cur["host"] != "kasumi" && cur["host"] != "hasu") fail("wrong host")
    if (cur["target"] != "x86_64-linux-musl") fail("wrong host or target topology")
    if (cur["musl_commit"] != "b306b16af15c89a04d8e0c55cac2dadbeb39c083") fail("wrong musl pin")
    if (base["cgf_tree"] != "clean" || cur["cgf_tree"] != "clean") fail("controlled evidence requires cgf_tree=clean")
    if (!require(base,"cgf_rev","baseline") || length(base["cgf_rev"]) != 40 || base["cgf_rev"] !~ /^[0-9a-f]+$/) fail("baseline has malformed Cgfried revision")
    if (!require(cur,"cgf_rev","result") || length(cur["cgf_rev"]) != 40 || cur["cgf_rev"] !~ /^[0-9a-f]+$/) fail("result has malformed Cgfried revision")
    timestamp(base,"date","baseline"); timestamp(cur,"date","result")
    if (cur["compiler_wrapper"] != "scripts/campaigns/musl-cc.sh" || cur["source_date_epoch"] != 0 || cur["jobs"] != 1) fail("wrong build protocol")
    if (cur["runs"] != 10 || cur["warmup"] != 1 || cur["timeit_protocol"] != "runs=10,warmup=1;fresh-tree-per-sample;source-date-epoch=0;jobs=1") fail("wrong sample protocol")
    if (cur["route.cgf_c"] != 1254 || cur["route.host_complex"] != 68 || cur["route.host_assembler"] != 32 || cur["route.total"] != 1354) fail("wrong route counts")
    if (cur["control_protocol"] != "fleet-control-v2") fail("uncontrolled evidence")
    controls[1]="cpu_idle_pct"; controls[2]="load1"
    metrics[1]="wall_ms_median"; metrics[2]="wall_ms_mad"; metrics[3]="user_ms_median"; metrics[4]="user_ms_mad"
    metrics[5]="sys_ms_median"; metrics[6]="sys_ms_mad"; metrics[7]="maxrss_kb_max"
    for(i=1;i<=2;i++){numeric(base,controls[i],"baseline");numeric(cur,controls[i],"result")}
    for(i=1;i<=7;i++){numeric(base,metrics[i],"baseline");numeric(cur,metrics[i],"result")}
    modern[1]="cpu_ms_median"; modern[2]="cpu_ms_mad"
    modern[3]="maxrss_kb_median"; modern[4]="maxrss_kb_mad"
    for(i=1;i<=4;i++) {
        if (modern[i] in base) baseline_modern_fields++
        numeric(cur,modern[i],"result")
    }
    if ("raw.cpu_ms" in base) baseline_modern_fields++
    if (baseline_modern_fields == 0) baseline_legacy=1
    else if (baseline_modern_fields != 5) fail("baseline has incomplete modern dispersion metrics")
    if (!baseline_legacy)
        for(i=1;i<=4;i++) numeric(base,modern[i],"baseline")
    raw[1]="raw.wall_ms"; raw[2]="raw.user_ms"; raw[3]="raw.sys_ms"; raw[4]="raw.maxrss_kb"
    for(i=1;i<=4;i++){raw_vector(base,raw[i],"baseline");raw_vector(cur,raw[i],"result")}
    raw_vector(cur,"raw.cpu_ms","result")
    if (!baseline_legacy) raw_vector(base,"raw.cpu_ms","baseline")
    stats[1]="musl.stat.arena.ast.peak_kb_max"; stats[2]="musl.stat.arena.ast.blocks_max"
    stats[3]="musl.stat.arena.ast.waste_pct_max"; stats[4]="musl.stat.arena.ir.peak_kb_max"
    stats[5]="musl.stat.arena.ir.blocks_max"; stats[6]="musl.stat.arena.ir.waste_pct_max"
    stats[7]="musl.stat.intern.lookups_sum"; stats[8]="musl.stat.intern.hits_sum"
    stats[9]="musl.stat.intern.hit_pct"; stats[10]="musl.stat.pp.includes_sum"
    stats[11]="musl.stat.pp.guard_skips_sum"; stats[12]="musl.stat.pp.tokens_sum"
    for(i=1;i<=12;i++){numeric(base,stats[i],"baseline");numeric(cur,stats[i],"result")}
    if (!bad) {
        if (exceeds_noise_floor(base["wall_ms_median"],cur["wall_ms_median"],30,
                                base["wall_ms_mad"],cur["wall_ms_mad"]))
            regression("wall time regressed beyond max(+30%,4*MAD)")
        if (!baseline_legacy) {
            if (exceeds_noise_floor(base["cpu_ms_median"],cur["cpu_ms_median"],30,
                                    base["cpu_ms_mad"],cur["cpu_ms_mad"]))
                regression("paired CPU time regressed beyond max(+30%,4*MAD)")
            if (exceeds_noise_floor(base["maxrss_kb_max"],cur["maxrss_kb_max"],20,
                                    base["maxrss_kb_mad"],cur["maxrss_kb_mad"]))
                regression("maximum RSS regressed beyond max(+20%,4*MAD)")
        }
    }
    if (bad) exit 3
    if (regressed) exit 1
    if (baseline_legacy)
        print "musl-full-build-gate: evidence-only (legacy baseline lacks paired CPU/RSS dispersion)"
    else
        print "musl-full-build-gate: pass (noise-aware time and RSS)"
}
' "$baseline" "$result"
status=$?
set -e
case $status in 0 | 1 | 3) exit "$status" ;; *) exit 3 ;; esac
