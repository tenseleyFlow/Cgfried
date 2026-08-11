# Performance gates

This document is the public contract for Cgfried's performance checks.  Each
row maps to exactly one file in `ci/gates.d/`; the configuration checker
keeps the two inventories identical.  A missing metric or malformed input is
an infrastructure error, never a skip.

## Gate lattice

| What | Where | When | Threshold | State and rationale |
|---|---|---|---|---|
| Compile wall and user+sys, per Sprint 52 lane <!-- perf-gate:compile-time --> | Fleet hosts `kasumi`, `hasu`, and `nomad-1`; shared CI: **never** (`BENCH_SKIP_TIME=1`) | Nightly and pre-release | More than +30% median | **blocking** — scheduler noise requires controlled hosts and the widest band |
| Maximum RSS, per compile lane <!-- perf-gate:max-rss --> | Shared x86 CI, native ARM CI, and fleet | Every PR and fleet run | More than +20% | **blocking** — peak RSS is stable enough for shared runners |
| Stripped corpus-program binaries <!-- perf-gate:corpus-binary-size --> | Shared x86 and native ARM CI | Every PR | More than +15% per program at either `-O2` or `-Os` | **trial** — deterministic, but every new gate earns a quiet streak |
| Stripped `cgf` binary <!-- perf-gate:cgf-self-size --> | Shared x86 and native ARM CI, measured with each runner's native toolchain | Every PR | More than +15% | **trial** — unstripped size and sections remain diagnostic only |
| Kernel instruction count <!-- perf-gate:kernel-icount --> | Shared x86 and native ARM CI | Every PR | More than `max(2%, 2 instructions)` per kernel | **blocking** — Sprint 53 already established the deterministic golden |
| Kernel `.text` bytes <!-- perf-gate:kernel-text --> | Shared x86 and native ARM CI | Every PR | More than +5% per kernel | **trial** — catches padding, relaxation, and encoding-width growth |
| Kernel runtime <!-- perf-gate:kernel-runtime --> | Fleet only: Linux x86 on `kasumi`/`hasu`, Darwin ARM on `nomad-1`; shared CI: **never** | Nightly | More than +10% median-of-medians **and** beyond four MADs | **trial** — runtime is the truth lane, protected from noisy false alarms |
| Arena peaks and interner hit rate <!-- perf-gate:internal-stats --> | Everywhere | Every PR and fleet run | Report only | **report-only** — useful diagnosis must not ossify internal design |
| Stage-1 self-compile time <!-- perf-gate:stage1-self-time --> | Fleet | Nightly history slot | Inactive until Sprint 58 | **inactive** — history starts with bootstrap; no invented result today |
| Full musl build <!-- perf-gate:musl-full-build --> | Fleet | Nightly history slot | Inactive until Sprint 57 is green | **inactive** — the corpus exists, but an incomplete build is not a metric |

Shared runners may record elapsed time for visibility, but no shared-runner
command is allowed to compare it.  `BENCH_SKIP_TIME=1` disables only the
wall/user+sys checks; RSS remains blocking.

## Measurement and baseline files

- `scripts/bench.sh` records compile-time, RSS, and internal counters in the
  Sprint 52 flat `metric=value` format.  `scripts/benchmark_gate.sh`
  applies the +30% fleet-time and +20% RSS limits.
- `scripts/size_gate.sh` builds all 21 self-checking kernel programs at
  `-O2` and `-Os`.  It records stripped and unstripped file bytes plus
  `.text`, `.data`, and `.rodata`; only stripped bytes gate.
- `scripts/kernel-static.sh` measures the symbol-bounded instruction count
  and complete object `.text` bytes.  Its two checks can be selected
  independently so instruction count remains blocking while text is trial.
- `scripts/runtime_gate.sh` consumes exactly the latest three compatible,
  uniquely dated runtime snapshots plus the accepted host/target baseline.
  For every Cgfried kernel metric, it computes the median of the three nightly
  medians and the median of their MADs.  It trips only when both conditions
  hold:

  ```text
  median_new > median_base * 1.10
  median_new - median_base > 4 * max(MAD_base, MAD_new)
  ```

Baselines never update themselves.  A baseline change is its own reviewed
commit and states the metric, old value, new value, and reason.  A corpus,
host image, linker, `afs-as`, or `afs-ld` change that moves bytes must
rebaseline in the same commit.  Unstripped bytes and section breakdowns are
reported so reviewers can distinguish compiler output from tool/debug churn.

Every new fleet compile and runtime artifact uses
`control_protocol=fleet-control-v2` and records `logical_cpus`,
`cpu_idle_pct`, `load1`, `governor`, `power_profile`, `scaling_driver`, and
`energy_performance_preference`. The control preflight samples aggregate CPU
idle over five seconds immediately before measurement. Linux computes the
idle percentage from aggregate `/proc/stat` counter deltas and deliberately
counts I/O wait as busy; Darwin uses the second sample from
`top -l 2 -s 5 -n 0`. Evidence is
load-controlled only when `load1 / logical_cpus` is at most 0.20 and aggregate
CPU idle is at least 85%. These capacity-normalized bounds admit ordinary
single-core desktop housekeeping on the 18- and 20-thread fleet machines
without treating a genuinely busy host as idle.

Linux evidence additionally requires the power profile to be `performance`,
and either the governor is `performance` or active Intel P-state reports the
effective tuple
`governor=powersave`, `scaling_driver=intel_pstate`, and
`energy_performance_preference=performance`. The raw `powersave` governor in
that Intel P-state tuple is truthful and must not be relabelled.

Darwin records the three Linux-only controls as `unavailable`; its governor
may be `performance` or `unavailable`. Current artifacts must contain one
valid value for every v2 field. Partial, empty, duplicate, malformed, or
unknown v2 provenance is status 3. A complete pre-v2 artifact remains eligible
under the original, stricter absolute `load1 <= 0.5` rule; this preserves the
accepted Kasumi evidence without altering its dated bytes. Older artifacts
that also predate the complete power-control tuple remain provenance-only.
When both sides are v2, timing/runtime comparisons additionally require the
same logical CPU count. Loads and idle percentages need not be byte-identical,
but every input must independently satisfy its protocol's controlled bounds.

## Trial state

All new gate classes begin in `state=trial`.  Trial runs execute the real
comparison and record trips, but `scripts/perf_gate.sh` prevents a genuine
regression status from failing CI; malformed data and harness failures still
fail.  Promotion to `blocking` requires 14 consecutive calendar days with
zero false trips, proven from dated history rather than the current clock.
The promotion is a standalone commit citing that quiet streak.

A blocking gate that false-trips twice in 30 days is demoted to trial in a
standalone commit linked to a tracked issue.  Thresholds do not silently
widen.  The fixture suite proves the 13-day refusal, exact 14-day promotion,
trial trip recording, and inactive behavior.

The newly deployed size and kernel-text gates therefore report on every PR
while still in trial.  Their blocking dry-run fixtures prove exact +15%/+5%
boundaries pass and the first value above each boundary fails.

## Step summaries and release reports

Every performance CI job appends a fixed-order table to
`GITHUB_STEP_SUMMARY` through `scripts/bench-summary.sh`.  Deltas are
always signed and every gate result prints its threshold.  Green checks show
the measurements too; the summary is not a failure-only log.

`scripts/perf-report.sh` writes
`.benchmarks/report-<version>.md` from accepted baselines, the latest fleet
runs, kernel goldens, and the Sprint 53 comparison dashboard.  It includes
host, date, revision, target, and input-file provenance for every table and
computes deltas from a previous report when supplied.  Sprint 62 consumes this
shape verbatim.  Reports are generated and committed with release tags, never
after the tag.  Tag CI regenerates the matching report from committed inputs
and rejects a missing file or any byte of drift.

## Escape hatches

`[bench skip]` is permitted only when the complete pull-request or push range
is documentation-only.  The CI verifier reads the exact event base-to-head
changed paths and rejects any tag whose diff touches
`src/`, `tests/`, a Makefile/build definition, a workflow, a toolchain
pin, or any other non-documentation path.

A blocking regression may be acknowledged only by an exact
`perf-override: #NNN` commit-message token naming the tracked issue that
owns it.  The policy checker records every valid token in the step summary;
misspelled and zero tokens fail closed, and CI verifies every referenced issue
exists and remains open through the GitHub API.  An override is visible debt,
not a baseline update.

Monthly audit command:

```sh
scripts/check_bench_policy.sh --audit --since 1.month
```

First audit: **2026-08-10** — no `[bench skip]` commits and no
`perf-override:` commits in the preceding month.

## Quarterly anti-drift review

Per-PR thresholds do not excuse compounded small regressions.
`scripts/bench-trend.sh --days 90 .benchmarks/runs/*.txt` emits a
zero-dependency Markdown/ASCII trend.  Any 90-day movement beyond that
metric's single-PR threshold gets a tracked issue even if no individual
commit tripped.  The review note, input range, and issue links are committed.
The first review is due **2026-11-10**.

## Exit-status contract

Performance scripts use status 0 for success, 1 for a real gate/policy
regression, and 3 for malformed, missing, duplicate, unreadable, or otherwise
untrustworthy input.  Trial mode may convert status 1 to a recorded success;
it never converts status 3.

## Initial deployment record

Deployment began on **2026-08-11**. Native x86 and ARM baseline artifacts were
accepted in `4be79b6e`. The early Kasumi, Hasu, and Nomad runs predate complete
power-control provenance, so they demonstrate immutable artifact routing and
trial reporting but are not controlled timing evidence.

The complete power schema and effective Intel P-state policy landed in
`15230a7b`; report provenance followed in `a432614c`. Fleet-control-v2 then
replaced the empirically invalid absolute-load rule with normalized load plus
measured CPU idle after idle Hasu and Nomad were observed near load 2.8 while
roughly 90% CPU-idle. All three nightly schedules are installed. Kasumi's
first controlled pair,
`2026-08-11T065745Z-kasumi` / `-kernels`, landed in `106efed5` with load
0.01/0.35 and the expected performance-profile Intel P-state tuple. Its
separate reviewed exact-copy baseline commit is `b87a2789`. Hasu's first v2
pair, `2026-08-11T160603Z-hasu` / `-kernels`, landed in `1643af0d` with
20 logical CPUs, load 0.89/0.98, and idle 97.54/97.49%; `787b9cec` accepted
its exact-copy baselines. Nomad's first v2 pair,
`2026-08-11T161721Z-nomad-1` / `-kernels`, landed in `2e152c54` with
18 logical CPUs, load 2.32/2.67, and idle 89.74/85.25%; `9c50e6d1` accepted
its exact-copy baselines. A LaunchAgent PATH omission discovered during that
run failed closed before measurement and was repaired in `7e7986d3`.

The byte-identical release report is committed as
`.benchmarks/report-0.0.1.md` in `1c868eaa` with SHA-256
`826bf44bd564fdf74c8d091466be83968eeda0a2b28b994171d4eb7d7da20db6`.
Sprint 54 stays open until each host has three distinct controlled UTC nightly
dates, the report reflects the final latest inputs, and final CI is green.
