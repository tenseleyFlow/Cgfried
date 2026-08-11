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

| kind | target | scope | source | host | host class | date | revision | tree | protocol | sysroot include | sysroot CRT |
|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline | x86_64-linux-gnu | hasu | bench-hasu.base.txt | hasu | n/a | 2026-08-01T13:00:00Z | base-hasu | clean | runs=10,warmup=1 | /nix/store/base-glibc-dev/include | /nix/store/base-glibc/lib |
| baseline | x86_64-linux-gnu | kasumi | bench.base.txt | kasumi | kasumi | 2026-08-01T12:00:00Z | base-kasumi | clean | runs=10,warmup=1 | n/a | n/a |
| golden | x86_64-linux-gnu | static | golden.txt | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| latest | x86_64-linux-gnu | hasu | bench-hasu.current.txt | hasu | n/a | 2026-08-10T13:00:00Z | current-hasu | clean | runs=10,warmup=1 | /nix/store/current-glibc-dev/include | /nix/store/current-glibc/lib |
| latest | x86_64-linux-gnu | kasumi | bench.current.txt | kasumi | kasumi | 2026-08-10T12:00:00Z | current-kasumi | clean | runs=10,warmup=1 | n/a | n/a |

## Sprint 53 compiler comparison dashboard

Source: dashboard.md

> # Dashboard fixture
>
> Static comparison evidence.
