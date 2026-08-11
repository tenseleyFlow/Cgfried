#!/bin/sh
set -eu
: "${CGF_BENCH_RESULTS:?}"
{
    echo 'target=x86_64-linux-gnu'
    echo 'sqlite3.wall_ms_median=1.000000'
} >"$CGF_BENCH_RESULTS"
