# Sprint 36 vectorization seeds

These four small kernels are a non-gating baseline for the comparative
performance work in Sprints 53–54. They compare observable output and count a
narrow set of packed operations in each compiler's assembly; they do not claim
speedup or benchmark stability.

Run:

```sh
sh scripts/s36_vector_seeds.sh build/cgfried
```

The checked-in table records the 2026-08-01 x86-64 run with the available
system GCC. Regenerate it after a vectorizer or backend change and investigate
output changes before interpreting instruction counts.

| Kernel | Level | Output | Cgfried packed ops | GCC packed ops |
| --- | --- | ---: | ---: | ---: |
| vector add | `-O3` | 1506500 | 3 | 1 |
| integer sum | `-O3` | 1502501 | 10 | 2 |
| FP dot | `-Ofast` | 72 | 20 | 5 |
| matrix inner loop | `-O3` | 5280 | 0 | 0 |
