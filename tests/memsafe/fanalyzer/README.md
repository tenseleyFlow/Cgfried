# GCC `-fanalyzer` overlap sample

This is an optional, local comparison corpus for Sprint 42.  It contains
exactly 20 small translation units spanning five diagnostics shared by
Cgfried's `-Wmem` surface and GCC 10+'s `-fanalyzer`.

Run `make test-mem-fanalyzer`.  The command writes
`build/mem-fanalyzer/comparison.tsv` with one row per case and prints it.
The script enforces the exact case count and category metadata.  The table is
an observation artifact, not an expected-output file: GCC is a useful
comparison, but it is not the oracle for Cgfried's deliberately conservative
zero-false-positive tier.  A frontend error, compiler crash/ICE, unknown
`-Wmem` option, or unexpected compiler stdout fails the comparison instead of
being recorded as a silent `no` verdict.
