# `-fcgf-safe` microbenchmarks

This directory is Sprint 44's small runtime-overhead benchmark set. It covers
allocation/free churn, pointer chasing, and library-copy-heavy code. The
`bench` mode of `scripts/safe_runtime.sh` compiles each program twice and
prints elapsed-time and maximum-RSS ratios for `-fcgf-safe` against the same
compiler without instrumentation. A small Linux `wait4` helper records both
metrics without depending on a particular external `time` executable.

The Sprint 44 budgets are `< 2.5x` elapsed time and `< 2x` maximum RSS. The
numbers remain advisory because this lane measures runtime-instrumentation
overhead, while Sprint 52's committed protocol and gates measure compiler
throughput and compiler RSS. The script reports an over-budget result but does
not hide or weaken a runtime check to improve it; Sprint 54 owns integrating
runtime-performance gates into the full lattice.

Recorded 2026-08-03 on the Sprint 44 GCC build (one `wait4` sample per
program):

| Program | Time | Maximum RSS |
| --- | ---: | ---: |
| allocation churn | 0.68x | 1.28x |
| pointer chase | 2.04x | 1.11x |
| memcpy-heavy | 0.82x | 1.00x |

The allocation benchmark keeps a realistic live working set before each free
wave. Measuring a single tcache-sized allocation/free pair would mostly time
the required quarantine bookkeeping rather than a program workload. The
pointer-chase result is the relevant residual-check ceiling; its hot accesses
cannot currently be discharged statically.
