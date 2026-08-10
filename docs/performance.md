# Compile-speed and memory measurement

Cgfried treats compile time and peak memory as versioned properties.  The
benchmark corpus, measurement program, raw samples, internal counters, and
accepted baselines all live with the compiler so a reported regression can be
reproduced rather than inferred from one wall-clock number.

## Running the protocol

```sh
make bench
BENCH_SKIP_TIME=1 make bench-gate   # shared-runner policy: RSS only
BENCH_HOST_CLASS=kasumi make bench-gate
scripts/profile.sh sqlite3          # Linux fleet profiling ritual
```

`make bench` builds and directly executes `build/timeit`; it never wraps the
compiler in a shell.  The default protocol discards one warmup and records ten
samples.  It reports wall median and median absolute deviation (MAD), user and
system medians, and maximum resident set size.  Raw samples remain under
`build/bench/raw/`.  Linux `wait4` reports RSS in KiB while macOS reports bytes;
`timeit` normalizes both to KiB. Its optional `-t SECONDS` deadline covers the
whole warmup-and-sample batch and kills the measured process group on expiry;
the Nomad thermal lane uses it to enforce the five-minute batch cap.

Every result records both `cgf_rev` and `cgf_tree`. A normal checkout is
marked `clean` or `dirty`; an exact exported commit used for an isolated fleet
run supplies the revision explicitly and is marked `exported-commit`.

The script refuses a one-minute load average above 0.5 unless
`CGF_BENCH_FORCE=1`.  Fleet Linux measurements should use the `performance`
governor.  A forced or non-performance run is useful for smoke testing and RSS
calibration, but its timing is provenance-only.  Run lanes in the fixed order
shown below.  A nomad-1 batch must remain under five minutes, with a two-minute
cooldown between longer batches.

| Lane | Corpus and fixed flags | Gate |
| --- | --- | --- |
| `sqlite3` | Official SQLite 3.50.4 amalgamation; `-fsyntax-only`, GNU C17, compiler intrinsics disabled | Wall, CPU, RSS |
| `self` | Every non-runtime `src/*.c`, sorted; `-fsyntax-only` | Wall, CPU, RSS |
| `many-tu` | 500 generated 200-line translation units | Wall, CPU, RSS |
| `musl` | Pinned local musl checkout and current build reach | Recorded; activates in Sprint 57 |

The three frontend lanes disable `-Wmem` and the IR-based return-flow warning.
That keeps SQLite the parser/sema stress named by Sprint 52 and prevents an IR
analysis benchmark from being mislabeled as frontend throughput.  Memory-flow
analysis already has dedicated musl and warning suites.

SQLite is pinned as version `3500400` in the Makefile.  The vendored
`sqlite3.c` has upstream SHA3-256
`9145255e83da6529e70121ee4d7a4c88fe83ca4511da0c9ed13d10842df36782`.
Before starting a measurement, `bench.sh` verifies the corresponding pinned
POSIX `cksum` identity (`2703132855:9282866`) against the actual vendored
bytes; it does not merely print the expected upstream digest.
Changing the source or flags is a corpus change and must land with a reviewed
baseline update.

The musl lane always starts from a fresh build directory and a truncated log,
then records either a green build or the current first failure. Today it fails
before the preprocessing workload completes, so a truthful musl
`guard_skips/includes` measurement is not yet available; Sprint 57 owns
advancing and activating that lane.

## Internal counters

`CGF_STATS=1` emits exactly four deterministic records to stderr:

```text
stat: arena.ast peak_kb=... blocks=... waste_pct=...
stat: arena.ir peak_kb=... blocks=... waste_pct=...
stat: intern lookups=... hits=... hit_pct=...
stat: pp includes=... guard_skips=... tokens=...
```

These counters diagnose a regression detected by the timer; they are not
timings.  `peak_kb` is reserved arena payload, while `waste_pct` is reserved
space not requested by clients, including alignment and unused block tails.
The AST arena intentionally spans a multi-input invocation because diagnostics
retain borrowed normalized and original source buffers until final rendering
and fix-it application.  Resetting it between translation units would create
dangling diagnostic sources.  The stats flag never changes stdout or output
artifacts, and failures still emit the complete schema.

## Gate and baseline policy

`scripts/benchmark_gate.sh BASELINE RESULT` rejects a regression above these
strict boundaries:

| Metric | Failure boundary | Enforcement |
| --- | --- | --- |
| Maximum RSS | greater than +20% | CI and fleet |
| Wall median | greater than +30% | fleet only |
| User + system medians | greater than +30% | fleet only |

`BENCH_SKIP_TIME=1` skips both timing comparisons and nothing else.  Missing,
duplicate, negative, or malformed gated metrics fail closed.  Internal stats
and MAD are report-only context.

Baselines are never updated by a benchmark run.  A baseline change is its own
reviewed commit and states the metric, old and new values, and one reason:
accepted feature cost, intentional corpus change, or host replacement.  Never
replace a baseline simply because a gate went red.

`scripts/fleet-bench.sh` selects the baseline matching its fleet hostname,
runs the full timing and RSS gate, and commits only the dated result under
`.benchmarks/runs/`. It never stages or changes a baseline.

## Profiling and the first optimization

On a Linux fleet host, `scripts/profile.sh` runs:

```text
perf record -g --call-graph dwarf -- cgf ...
perf report --stdio --percent-limit 1
```

Profile before changing code and preserve the before/after top functions with
the change.  The Sprint 52 development host did not have `perf`, so the first
optimization used Callgrind instruction attribution plus the same `timeit`
protocol.  The committed evidence is in
`.benchmarks/profiles/s52-scope-index.txt`.

The 10,000-declaration fixture placed 83.0% of retired instructions in linear
semantic-scope lookup.  Arena-backed indexes keyed by interned name pointers
cut the instruction count from 602,520,093 to 107,101,294 (82.22%) and the
five-run wall median from 719.293 ms to 21.565 ms (97.00%, after-MAD 1.26%).
Intrusive declaration chains remain the sole iteration path, preserving
shadowing and deterministic declaration-order dumps.

Current instrumentation also answered the planned tuning questions: SQLite's
interner hit rate is 95%, and the self lane's AST-arena waste is 2%, so neither
the `<90%` interner trigger nor the `>25%` self-arena trigger fired.  No
speculative pre-seeding or block-size change was made.

The token-vector audit found the shared `VEC_DECL` reserve path starts at eight
elements and doubles capacity, so growing a vector to N tokens performs
O(log N) reallocations rather than one reallocation per token. The unit growth
fixture pins the 8→16→32→64→128 behavior through 100 pushes; no vector change
was justified by the profile.
