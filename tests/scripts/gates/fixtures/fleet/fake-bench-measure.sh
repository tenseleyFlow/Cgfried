#!/bin/sh
set -eu
: "${CGF_BENCH_RESULTS:?}"
{
    echo "host=${CGF_FLEET_HOST:-kasumi}"
    echo 'target=x86_64-linux-gnu'
    echo 'governor=performance'
    echo 'load1=0.10'
    echo 'power_profile=performance'
    echo 'scaling_driver=acpi-cpufreq'
    echo 'energy_performance_preference=unavailable'
    echo 'control_protocol=fleet-control-v2'
    echo 'logical_cpus=16'
    echo 'cpu_idle_pct=90'
    echo 'self.corpus=cgfried-src-fixture:1-files'
    echo 'sqlite3.wall_ms_median=1.000000'
} >"$CGF_BENCH_RESULTS"
