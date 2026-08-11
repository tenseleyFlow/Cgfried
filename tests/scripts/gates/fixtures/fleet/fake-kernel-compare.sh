#!/bin/sh
set -eu
: "${CGF_KERNEL_RUNTIME_OUTPUT:?}"
: "${FIXTURE_COMPARE_LOG:?}"

emit_field()
{
    field_name=$1
    field_override=$2
    field_default=$3
    case $field_override in
    __missing__) ;;
    __duplicate__)
        echo "$field_name=$field_default"
        echo "$field_name=$field_default"
        ;;
    '') echo "$field_name=$field_default" ;;
    *) echo "$field_name=$field_override" ;;
    esac
}
{
    echo "runtime_only=${CGF_KERNEL_RUNTIME_ONLY:-}"
    echo "host=${CGF_KERNEL_RUNTIME_HOST:-}"
    echo "targets=${CGF_KERNEL_TARGETS:-}"
} >"$FIXTURE_COMPARE_LOG"
case ${FIXTURE_COMPARE_STATUS:-0} in
0) ;;
*) exit "$FIXTURE_COMPARE_STATUS" ;;
esac
{
    echo '# cgfried kernel runtime metrics v1'
    echo 'date=2026-08-10T12:00:00Z'
    echo "host=${FIXTURE_ARTIFACT_HOST:-${CGF_KERNEL_RUNTIME_HOST:-}}"
    echo "target=${CGF_KERNEL_TARGETS:-}"
    if [ -n "${FIXTURE_GOVERNOR:-}" ]; then
        echo "governor=$FIXTURE_GOVERNOR"
    elif [ "${CGF_KERNEL_TARGETS:-}" = arm64-macos ]; then
        echo 'governor=unavailable'
    else
        echo 'governor=performance'
    fi
    if [ -n "${FIXTURE_LOAD1:-}" ]; then
        echo "load1=$FIXTURE_LOAD1"
    else
        echo 'load1=0.10'
    fi
    if [ "${CGF_KERNEL_TARGETS:-}" = arm64-macos ]; then
        fixture_power_profile=unavailable
        fixture_scaling_driver=unavailable
        fixture_epp=unavailable
    else
        fixture_power_profile=performance
        fixture_scaling_driver=acpi-cpufreq
        fixture_epp=performance
    fi
    emit_field power_profile "${FIXTURE_POWER_PROFILE:-}" "$fixture_power_profile"
    emit_field scaling_driver "${FIXTURE_SCALING_DRIVER:-}" "$fixture_scaling_driver"
    emit_field energy_performance_preference "${FIXTURE_EPP:-}" "$fixture_epp"
    if [ "${CGF_KERNEL_TARGETS:-}" = arm64-macos ]; then
        fixture_logical_cpus=10
    else
        fixture_logical_cpus=18
    fi
    emit_field control_protocol "${FIXTURE_CONTROL_PROTOCOL:-}" fleet-control-v2
    emit_field logical_cpus "${FIXTURE_LOGICAL_CPUS:-}" "$fixture_logical_cpus"
    emit_field cpu_idle_pct "${FIXTURE_CPU_IDLE_PCT:-}" 90
    echo "${CGF_KERNEL_TARGETS:-}.tiny.O2.cgf.wall_ms_median=1.000000"
    echo "${CGF_KERNEL_TARGETS:-}.tiny.O2.cgf.wall_ms_mad=0.010000"
} >"$CGF_KERNEL_RUNTIME_OUTPUT"
