# Cgfried performance report — 0.0.1

Generated from committed baselines, latest fleet results, static kernel goldens, and the Sprint 53 compiler comparison dashboard.

## x86_64-linux-gnu

| scope | metric | baseline | latest | delta | since prior report | provenance |
|---|---|---:|---:|---:|---:|---|
| hasu | self.maxrss_kb_max | 800 | 840 | +5.0% | n/a | baseline:bench-hasu.base.txt; latest:bench-hasu.current.txt |
<!-- perf-metric x86_64-linux-gnu hasu self.maxrss_kb_max 840 -->
| kasumi | self.maxrss_kb_max | 1000 | 1200 | +20.0% | +9.1% | baseline:bench.base.txt; latest:bench.current.txt |
<!-- perf-metric x86_64-linux-gnu kasumi self.maxrss_kb_max 1200 -->
| kasumi | self.stat.arena.ast.peak_kb | 500 | 525 | +5.0% | +2.9% | baseline:bench.base.txt; latest:bench.current.txt |
<!-- perf-metric x86_64-linux-gnu kasumi self.stat.arena.ast.peak_kb 525 -->
| kasumi | self.stat.intern.hit_pct | 90 | 91 | +1.1% | +1.1% | baseline:bench.base.txt; latest:bench.current.txt |
<!-- perf-metric x86_64-linux-gnu kasumi self.stat.intern.hit_pct 91 -->
| kasumi | self.sys_ms_median | 20 | 26 | +30.0% | +18.2% | baseline:bench.base.txt; latest:bench.current.txt |
<!-- perf-metric x86_64-linux-gnu kasumi self.sys_ms_median 26 -->
| kasumi | self.user_ms_median | 80 | 104 | +30.0% | +18.2% | baseline:bench.base.txt; latest:bench.current.txt |
<!-- perf-metric x86_64-linux-gnu kasumi self.user_ms_median 104 -->
| kasumi | self.wall_ms_median | 100 | 130 | +30.0% | +18.2% | baseline:bench.base.txt; latest:bench.current.txt |
<!-- perf-metric x86_64-linux-gnu kasumi self.wall_ms_median 130 -->

### Kernel static golden

| metric | value | provenance |
|---|---:|---|
| matmul-64.icount | 102 | golden:golden.txt |
| matmul-64.text | 105 | golden:golden.txt |

## Provenance

| kind | target | scope | source | host | host class | date | revision | tree | protocol | sysroot include | sysroot CRT | load1 | governor | power profile | scaling driver | energy performance preference | control protocol | logical CPUs | CPU idle % |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline | x86_64-linux-gnu | hasu | bench-hasu.base.txt | hasu | n/a | 2026-08-01T13:00:00Z | base-hasu | clean | runs=10,warmup=1 | /nix/store/base-glibc-dev/include | /nix/store/base-glibc/lib | 0.10 | powersave | performance | intel_pstate | performance | n/a | n/a | n/a |
| baseline | x86_64-linux-gnu | kasumi | bench.base.txt | kasumi | kasumi | 2026-08-01T12:00:00Z | base-kasumi | clean | runs=10,warmup=1 | n/a | n/a | 0.25 | powersave | performance | intel_pstate | performance | n/a | n/a | n/a |
| dashboard | all-targets | target-deterministic | dashboard.md | n/a | target-deterministic | 2026-08-02T15:00:00Z | dashboard-fixture | clean | sprint-53-static-dashboard-v1 | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| golden | x86_64-linux-gnu | target-deterministic | golden.txt | n/a | target-deterministic | 2026-08-02T14:00:00Z | golden-fixture | clean | sprint-53-kernel-static-v1 | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| latest | x86_64-linux-gnu | hasu | bench-hasu.current.txt | hasu | n/a | 2026-08-10T13:00:00Z | current-hasu | clean | runs=10,warmup=1 | /nix/store/current-glibc-dev/include | /nix/store/current-glibc/lib | 0.40 | powersave | performance | intel_pstate | performance | n/a | n/a | n/a |
| latest | x86_64-linux-gnu | kasumi | bench.current.txt | kasumi | kasumi | 2026-08-10T12:00:00Z | current-kasumi | clean | runs=10,warmup=1 | n/a | n/a | 0.30 | powersave | performance | intel_pstate | performance | n/a | n/a | n/a |

## Sprint 53 compiler comparison dashboard

Source: dashboard.md

> <!-- cgf-dashboard-provenance host_class=target-deterministic -->
> <!-- cgf-dashboard-provenance date_utc=2026-08-02T15:00:00Z -->
> <!-- cgf-dashboard-provenance cgf_rev=dashboard-fixture -->
> <!-- cgf-dashboard-provenance cgf_tree=clean -->
> <!-- cgf-dashboard-provenance protocol=sprint-53-static-dashboard-v1 -->
>
> # Dashboard fixture
>
> Static comparison evidence.
