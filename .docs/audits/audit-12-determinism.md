# F12 determinism/reproducibility/performance evidence — CLOSED

- Review date: 2026-08-20
- Frozen compiler baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Audit/history integration reviewed through: `b83e07470e8f4b5234627c86f5b890632ea99171`
- Scope: repo-wide deterministic-output tests, `.benchmarks/`, performance and
  bootstrap scripts/configuration, relevant GitHub Actions history since
  Sprint 58, `.docs/audits/bootstrap-soak.md`, and git commit messages.

## Regression and checklist evidence

- The frozen baseline was built in a detached worktree. `make -j2
  bootstrap-O0 bootstrap-O2` passed both fixed points: each compared 113
  assembly files, 113 objects, `libcgf_rt.a`, and the final compiler with raw
  byte identity and no normalization. `make test-bootstrap` then passed the
  exact audit, controlled timing/gate tests, isolated phase trees, seeded
  localization, `audit-determinism.sh`, and the GCC/Clang O0/O2 int128 ABI
  differential.
- GitHub Actions bootstrap history from the Sprint 58 implementation through
  the review date contains 62 successful, two failed, and two cancelled runs.
  The two failures were both pre-soak native ARM64 O2 activation failures and
  are resolved. The two cancellations are the same August 18 system-toolchain
  installation stall at consecutive nightly performance commits; the later
  required x86 run is the one that reset the soak. No retained run shows an
  unresolved stage1/stage2 byte mismatch.
- Every Sprint 52/54 threshold was compared with its available noise evidence.
  Controlled compile-wall MAD/median ranges from 0.1137% to 1.6696%, safely
  below the +30% gate. Controlled musl wall/user/sys noise ranges from 0.1077%
  to 0.6653%, also safely below +30%. Kernel runtime has an operative `AND
  >4*max(MAD_base,MAD_new)` guard: 104 of 189 baseline Cgfried cells have a
  four-MAD floor above the nominal 10%, with the largest at 112.0192%, and the
  gate correctly uses that larger floor. Stripped size, kernel instruction
  count, and kernel text are deterministic integer/byte measurements, so
  their +15%, max(+2%, +2 instructions), and +5% thresholds have zero sampling
  noise. At the frozen baseline, `DET-M-01` and `DET-M-02` were the two
  evidence breaks: user+sys and RSS lacked dispersion, and stage1 reported a
  fabricated zero MAD from N=1. Both are resolved at `89b68ead`.
- Exactly five commits modified an already-existing
  `.benchmarks/baseline-*.txt`. Only `7aef30e8` records the accepted metric
  old-to-new values and why. `4be79b6e` has only a subject; `b87a2789` gives
  why but no old-to-new values; `787b9cec` and `9c50e6d1` quantify compile
  medians but replace runtime baselines using only “stable or improve” prose.
  `DET-M-03` records the four historical failures as one policy root cause;
  `89b68ead` adds the executable policy that prevents recurrence.
- All three standalone F12 reproducers returned zero on the frozen-baseline
  history/current evidence. They are now integrated into the shared lifecycle
  manifest and return the resolved result at `89b68ead`; the exact detached
  audit run is 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

Checklist complete: **Yes.** F12 closed Sprint 60 collection with three raw
and three deduplicated findings: zero Critical, zero High, three Medium, zero
Low, and two unverified observations. All three findings are remediated at
`89b68ead`; Sprint 58 remains operationally open at 2/30.

## Findings

~~ID: `DET-M-01`~~
~~Title: blocking compile gates have no per-metric noise evidence~~
~~Severity: Medium — the gates may still have conservative numerical bands, but~~
~~their claimed threshold honesty cannot be verified for user+sys or max-RSS,~~
~~a performance-evidence/documentation mismatch rather than compiler wrong code.~~
~~Reproducer: `tests/audit-regressions/det-m-01.sh`. It proves that~~
~~`benchmark_gate.sh` actively gates user+sys at +30% and max-RSS at +20%, then~~
~~checks all 27 corresponding metric families across the three controlled~~
~~compile baselines. All 27 have a gated central/max value and none has a MAD or~~
~~equivalent dispersion statistic.~~
~~Root cause: `tests/bench/timeit.c` and the Sprint 52 file schema emit only~~
~~`wall_ms_mad`; user and system time emit medians only, while RSS emits only the~~
~~maximum. `scripts/benchmark_gate.sh` nevertheless applies percentage gates to~~
~~those metrics without consuming raw samples or any noise statistic.~~
~~Affected sprints: 52, 54; also the Sprint 57 musl RSS activation and Sprint 58~~
~~stage1 user+sys lane inherit the same unsupported threshold family.~~
~~Manifest row, to be integrated by the shared fixture owner:~~
~~`DET-M-01<TAB>det-m-01.sh<TAB>blocking compile gates have no per-metric noise evidence`.~~

Resolution: RESOLVED 2026-08-20 by `89b68ead`. `timeit` now publishes paired
wall, CPU, and RSS median/MAD evidence; blocking compile and musl gates use the
larger of the nominal allowance and four measured MADs. Legacy receipts that
cannot support that comparison remain explicitly evidence-only.

~~ID: `DET-M-02`~~
~~Title: stage1 timing derives MAD from a single sample~~
~~Severity: Medium — every stage1 performance receipt publishes a zero noise~~
~~floor that the measurement protocol cannot estimate, making the trial gate's~~
~~evidence dishonest without causing a compiler semantic failure.~~
~~Reproducer: `tests/audit-regressions/det-m-02.sh`. The production command in~~
~~`scripts/bootstrap.sh` invokes `timeit -n 1 -w 0`; all 20 committed~~
~~`*-bootstrap.txt` receipts consequently report~~
~~`stage1.O2.wall_ms_mad=0.000000`. The later controlled receipts visibly vary:~~
~~Kasumi's seven comparable nights span 17,494.788–17,751.703 ms (1.460% of the~~
~~mean) and Hasu's span 15,873.430–16,029.785 ms (0.980%), so zero is not the~~
~~observed run-to-run floor. No stage1 baseline has been accepted, so every~~
~~night remains `warmup` and the advertised trial threshold is not exercised.~~
~~Root cause: `scripts/bootstrap.sh:314` deliberately requests one measured~~
~~sample while reusing the median/MAD schema; `scripts/bootstrap-time-gate.awk`~~
~~requires the resulting MAD field but never validates that the sample count can~~
~~support it.~~
~~Affected sprints: 52, 54, 58.~~
~~Manifest row, to be integrated by the shared fixture owner:~~
~~`DET-M-02<TAB>det-m-02.sh<TAB>stage1 timing derives MAD from a single sample`.~~

Resolution: RESOLVED 2026-08-20 by `89b68ead`. Stage1 timing now takes three
forced-rebuild samples. Its schema and gate require `samples >= 3` plus the
complete modern wall/CPU dispersion set; incomplete modern evidence fails,
while pre-repair baselines remain evidence-only.

~~ID: `DET-M-03`~~
~~Title: four baseline-bump commits omit required old-to-new evidence~~
~~Severity: Medium — accepted baselines are performance ratchets, and four of~~
~~five historical replacements cannot be audited from the required commit~~
~~message, a durable performance-policy/documentation mismatch.~~
~~Reproducer: `tests/audit-regressions/det-m-03.sh`. It reads the five commits~~
~~that modified an existing baseline and reproduces the four deficient message~~
~~classes: no body (`4be79b6e`), why without old-to-new values (`b87a2789`), and~~
~~whole runtime-baseline replacements described only as stable/improved~~
~~(`787b9cec`, `9c50e6d1`). `7aef30e8` is the sole complete control.~~
~~Root cause: the baseline ritual is prose-only. No CI or local policy check~~
~~binds a changed baseline to commit-message old-to-new values and a reason.~~
~~Affected sprints: 52, 54.~~
~~Manifest row, to be integrated by the shared fixture owner:~~
~~`DET-M-03<TAB>det-m-03.sh<TAB>four baseline-bump commits omit required old-to-new evidence`.~~

Resolution: RESOLVED 2026-08-20 by `89b68ead`. The benchmark-policy gate now
examines each baseline-changing commit independently and requires the exact
old/new metric union, including additions and deletions, plus rationale. A
temporary-repository regression proves incomplete, fabricated, and
range-hidden evidence is rejected and complete exact evidence is accepted.

### Shared checker lifecycle contract

The shared manifest contains all three rows in `PASS` state. The checker
dispatches each root-level shell fixture with the repository root as its sole
argument and preserves the established status contract: zero is a reproduced
finding, one is remediation, and two is malformed checkout/tooling. The clean
detached run at `89b68ead` reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Bootstrap incident ledger since Sprint 58

| Date/run | Incident | Root cause and resolution | Latent status |
|---|---|---|---|
| Implementation-time first O2 fixed point | Stage output diverged before hosted activation. | `GvnOperand` padding was read semantically and inliner growth budget reset on each fixpoint visit. Keys were zero-initialized/compared fieldwise in the Sprint 58 implementation and growth state was made persistent in `526a6802`. | Resolved; frozen-baseline O2 raw identity freshly passed. |
| 2026-08-13 run `31654537964`, commit `4c688d80` | Native ARM64 O2 failed while stage0 built stage1: `src/cg/x86_64/isel.s:14732: conditional branch out of range`. Stage2 was never reached. | ARM64 narrow conditional emission exceeded architectural range in the self-host-sized function. `023e96c3` added the range-safe inverted-local-branch plus `b` form. | Resolved; the next run advanced to stage2. |
| 2026-08-13 run `31656282841`, commit `023e96c3` | Native ARM64 O2 stage1 completed, but its stage2 `src/cg/arm64/debug.s` contained invalid integer-register `fmov` instructions and would not assemble. | The resolving `526a6802` bundle invalidated cached NZCV across calls, opaque asm, and CAS; bounded long-branch decisions from final layout; preserved inliner budget across the full fixpoint; and completed IR clone state. The sprint ledger attributes convergence to these fixes and dedicated regressions. | Resolved; runs `31659892338`/`31659892782` passed, followed by the qualifying fixed point at `c6bf3cf6`. |
| 2026-08-18 runs `32087825756` and `32089117040`; qualifying row uses `32089117040` at `9ec43d92` | x86 O0 remained in system-toolchain installation for two hours, was cancelled before bootstrap, failed the evidence-manifest step, and retained no O0 artifact. x86 O2 and the separate ARM O0/O2 run passed. | Operational resolution is the mandated soak reset: the first streak ended at 5/30 and August 19 restarted it. The retained Actions metadata does not identify why package installation stalled, so that infrastructure subcause remains explicitly latent; no stage bytes existed to implicate compiler determinism. | Compiler mismatch: none. Infrastructure stall subcause: latent. Required evidence resumed green on August 19–20. |

The later x86 comparison-clobber repairs at `95196def` and `c6bf3cf6` fixed
torture runtime failures discovered while qualifying Sprint 58, but their
bootstrap workflow runs were green and did not contain stage1/stage2 identity
incidents. They are therefore not inflated into extra determinism events.

## Threshold-to-noise classification

| Gate threshold | Recorded/derived noise floor | Classification |
|---|---|---|
| Compile wall >+30% | Controlled baseline MAD/median 0.1137%–1.6696%. | Valid; threshold is above every recorded wall floor. |
| Compile paired CPU >+30% | Current controlled receipts carry paired CPU median/MAD. Pre-repair receipts lack the pair. | Valid for modern evidence at `89b68ead`: effective threshold is `max(30%, 4*MAD)`; legacy metrics are evidence-only. |
| Max-RSS >+20% | Current controlled receipts carry RSS median/MAD/max. Pre-repair receipts have only a maximum. | Valid for modern evidence at `89b68ead`: effective threshold is `max(20%, 4*MAD)`; legacy metrics are evidence-only. |
| Stripped corpus/self size >+15% | Exact deterministic byte count. | Valid; sampling-noise floor is zero. |
| Kernel icount >max(2%, 2 instructions) | Exact deterministic instruction count. | Valid; sampling-noise floor is zero. |
| Kernel text >+5% | Exact deterministic section-byte count. | Valid; sampling-noise floor is zero. |
| Kernel runtime >+10% **and** >4×max(MAD) | Four-MAD floor exceeds 10% in 104/189 cells and reaches 112.0192%; implementation requires both clauses. | Valid; the effective threshold is `max(10%, 4*MAD)` rather than the dishonest nominal 10%. |
| Musl wall/CPU >+30%; RSS >+20% | Current receipts record wall/CPU/RSS dispersion. Historical time MAD/median was 0.1077%–0.6653%. | Valid for modern evidence at `89b68ead`; all three families use their nominal-or-four-MAD floor. |
| Stage1 wall/CPU >+30% | Three forced builds now produce measurable wall/CPU dispersion; the schema rejects fewer samples or partial modern fields. | Valid for modern evidence at `89b68ead`; legacy N=1 baselines are evidence-only rather than gating inputs. |

No implemented threshold was confirmed below a correctly recorded applicable
noise floor. At `89b68ead`, every noisy blocking metric has a measured floor;
historical inputs that cannot support the comparison are never promoted into
gating evidence.

## Baseline-bump commit audit

| Commit | Existing baselines modified | old-to-new + why contract |
|---|---:|---|
| `4be79b6e` | 4 | Fail: subject only; neither values nor reason. |
| `7aef30e8` | 1 | Pass: all three accepted RSS old-to-new values, workload change, and timing-provenance exclusion. |
| `b87a2789` | 2 | Fail: controlled-host reason present; no old-to-new metric values. |
| `787b9cec` | 2 | Partial/fail: compile medians quantified and control reason present; changed runtime baseline only called stable/improved. |
| `9c50e6d1` | 2 | Partial/fail: compile medians quantified and control reason present; changed runtime baseline only called stable. |

Initial baseline-creation commits were inspected but are not “bumps” because
no prior baseline existed. Dated `.benchmarks/runs/` commits do not mutate a
baseline and are likewise outside this exact ritual.

## Attack-surface dispatch

- Bootstrap stability history: complete over local implementation history,
  all 66 hosted bootstrap workflow runs since activation, the soak ledger, and
  the August 18 reset. Every failure/cancellation is classified above; no
  unresolved compiler stage mismatch remains.
- Gate-threshold honesty: complete over all S52/S54 configured threshold
  classes and their current activated descendants (musl and stage1).
  `DET-M-01`/`DET-M-02` are resolved by measured dispersion, noise-aware
  thresholds, and an explicit evidence-only boundary for legacy receipts.
- Baseline drift: complete over every commit that modified an existing
  `.benchmarks/baseline-*.txt`; `DET-M-03` records the four historical message
  violations, and the policy gate at `89b68ead` prevents recurrence.
- Repo-wide deterministic-output tests: frozen-baseline O0/O2 bootstrap,
  bootstrap meta-suite, static audit, and int128 ABI differential all passed.

## Exact verification commands and results

Remediation evidence at `89b68ead`:

- `make -j2 test-bench` — PASS; paired timing/RSS math, 58 benchmark-gate
  cases, kernel gates, musl gates, and baseline-policy checks are green.
- `make -j2 test-bootstrap` — PASS; bootstrap meta, determinism audit, and
  GCC/Clang O0/O2 int128 ABI checks are green; the meta-suite proves three
  forced stage1 builds.
- Detached `make -j2 all && make test-audit-fixtures` — PASS; 55 PASS / 0
  XFAIL / 0 XPASS / 0 FAIL.

Original Sprint 60 collection evidence at the frozen baseline:

- `make -j2 bootstrap-O0 bootstrap-O2` at detached
  `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb` — both PASS; 113 assembly, 113
  object, runtime archive, and compiler comparisons per level.
- `make test-bootstrap` at that worktree — PASS; bootstrap meta, determinism
  audit, and GCC/Clang O0/O2 int128 ABI checks all green.
- `gh run list --workflow bootstrap.yml --limit 100 --json ...` — 62 success,
  two failure, two cancelled since 2026-08-13T00:00Z.
- `gh run view 31654537964 --json ...` plus job `94305971460 --log-failed` —
  ARM64 O2 stage1 branch-out-of-range failure.
- `gh run view 31656282841 --json ...` plus job `94312389460 --log-failed` —
  ARM64 O2 stage2 invalid-`fmov` assembler failure.
- `gh run view 32089117040 --json ...` — x86 O0 toolchain-install cancellation,
  failed evidence manifest, no bootstrap step/artifact.
- `git log --all --reverse --format='%H' -- .benchmarks` plus per-commit
  `git diff-tree`/`git show -s --format='%B'` — five existing-baseline
  modifiers, one compliant and four deficient.
- `tests/audit-regressions/det-m-01.sh .` — status 0, `27/27` gated user/sys/RSS
  families lack dispersion.
- `tests/audit-regressions/det-m-02.sh .` — status 0, `20/20` stage1 receipts
  report zero MAD from the production N=1 command.
- `tests/audit-regressions/det-m-03.sh .` — status 0, `4/5` existing-baseline
  modifier commits omit complete old-to-new evidence.
- `sh -n tests/audit-regressions/det-m-01.sh
  tests/audit-regressions/det-m-02.sh
  tests/audit-regressions/det-m-03.sh` — status 0.

## Unverified observations

- `scripts/bench-trend.sh` flags kernel runtime at +10% without evaluating the
  4-MAD guard, although its label admits that the per-run gate also requires
  MAD. This can create noisy quarterly flags, but it is a report/triage path,
  not the blocking/trial gate, so it is excluded from totals.
- The two August 18 x86 O0 jobs stalled in the package-install step before
  cancellation. Actions metadata proves where and when, but not the external
  package-manager/runner cause. This is explicitly latent infrastructure
  diagnosis, not a compiler determinism finding.
