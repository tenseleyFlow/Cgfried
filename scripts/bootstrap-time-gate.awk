function fail(message) {
    print "bootstrap-time-gate: " message > "/dev/stderr"
    bad = 1
}

function trim(value) {
    sub(/^[[:space:]]+/, "", value)
    sub(/[[:space:]]+$/, "", value)
    return value
}

function read_file(file, values,    line, equals, key, value) {
    while ((getline line < file) > 0) {
        sub(/[[:space:]]*#.*/, "", line)
        line = trim(line)
        if (line == "")
            continue
        equals = index(line, "=")
        if (!equals) {
            fail(file ": expected key=value")
            continue
        }
        key = trim(substr(line, 1, equals - 1))
        value = trim(substr(line, equals + 1))
        if (key !~ /^[A-Za-z0-9_.:-]+$/ || value == "") {
            fail(file ": malformed metric")
            continue
        }
        if (key in values) {
            fail(file ": duplicate " key)
            continue
        }
        values[key] = value
    }
    close(file)
}

function require(values, key, label) {
    if (!(key in values) || values[key] == "") {
        fail(label ": missing " key)
        return 0
    }
    return 1
}

function numeric(values, key, label) {
    if (!require(values, key, label))
        return 0
    if (values[key] !~ /^[0-9]+([.][0-9]+)?$/) {
        fail(label ": invalid " key)
        return 0
    }
    return 1
}

function same(key,    have_base, have_result) {
    have_base = require(base, key, "baseline")
    have_result = require(current, key, "result")
    if (have_base && have_result && base[key] != current[key])
        fail(key " provenance mismatch (baseline=" base[key] \
             " result=" current[key] ")")
}

function validate(values, label,    metric, user_metric, sys_metric) {
    require(values, "schema", label)
    require(values, "target", label)
    require(values, "host", label)
    require(values, "level", label)
    require(values, "jobs", label)
    require(values, "protocol", label)
    require(values, "normalization", label)
    require(values, "sysroot", label)
    require(values, "governor", label)
    require(values, "power_profile", label)
    require(values, "scaling_driver", label)
    require(values, "energy_performance_preference", label)
    require(values, "control_protocol", label)
    require(values, "logical_cpus", label)
    require(values, "date", label)
    require(values, "cgf_rev", label)
    require(values, "cgf_tree", label)
    require(values, "compiler", label)
    require(values, "compiler_sha256", label)

    if (values["schema"] != "cgfried.bootstrap-timing.v1")
        fail(label ": invalid schema")
    if (values["target"] != "x86_64-linux-gnu")
        fail(label ": invalid target " values["target"])
    if (values["host"] != "kasumi" && values["host"] != "hasu")
        fail(label ": unsupported timing host " values["host"])
    if ("host_class" in values)
        fail(label ": named-host timing must not also carry host_class")
    if (values["level"] != "O2" || values["jobs"] != "8")
        fail(label ": fleet timing requires level=O2 and jobs=8")
    if (values["protocol"] != "cgfried-bootstrap-v1")
        fail(label ": invalid protocol")
    if (values["normalization"] != "none")
        fail(label ": timing receipt applied normalization")
    if (values["cgf_tree"] != "clean")
        fail(label ": timing receipt requires a clean source tree")
    if (values["compiler_sha256"] !~ /^[0-9a-f]+$/ ||
        length(values["compiler_sha256"]) != 64)
        fail(label ": invalid compiler_sha256")

    metric = "stage1.O2.wall_ms_median"
    user_metric = "stage1.O2.user_ms_median"
    sys_metric = "stage1.O2.sys_ms_median"
    numeric(values, metric, label)
    numeric(values, "stage1.O2.wall_ms_mad", label)
    numeric(values, user_metric, label)
    numeric(values, sys_metric, label)
    numeric(values, "stage1.O2.maxrss_kb_max", label)
}

BEGIN {
    if (have_baseline)
        read_file(baseline_file, base)
    read_file(ARGV[1], current)
    ARGV[1] = ""
}

END {
    validate(current, "result")
    if (have_baseline)
        validate(base, "baseline")
    if (bad)
        exit 3
    if (!have_baseline) {
        print "bootstrap-time-gate: warmup; baseline missing: " baseline_file
        exit 0
    }
    same_keys[1] = "schema"
    same_keys[2] = "target"
    same_keys[3] = "host"
    same_keys[4] = "level"
    same_keys[5] = "jobs"
    same_keys[6] = "protocol"
    same_keys[7] = "normalization"
    same_keys[8] = "sysroot"
    same_keys[9] = "governor"
    same_keys[10] = "power_profile"
    same_keys[11] = "scaling_driver"
    same_keys[12] = "energy_performance_preference"
    same_keys[13] = "control_protocol"
    same_keys[14] = "logical_cpus"
    for (i = 1; i <= 14; ++i)
        same(same_keys[i])

    metric = "stage1.O2.wall_ms_median"
    user_metric = "stage1.O2.user_ms_median"
    sys_metric = "stage1.O2.sys_ms_median"
    if (bad)
        exit 3

    if (current[metric] + 0 > (base[metric] + 0) * 1.30)
        regressed = 1
    current_cpu = current[user_metric] + current[sys_metric]
    baseline_cpu = base[user_metric] + base[sys_metric]
    if (current_cpu > baseline_cpu * 1.30)
        regressed = 1
    if (regressed) {
        print "bootstrap-time-gate: regression (>30% wall or user+sys)" \
            > "/dev/stderr"
        exit 1
    }
    print "bootstrap-time-gate: pass"
}
