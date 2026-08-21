# Closeout: Phase 11 — Performance

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S52.1 `timeit` builds with the tree, zero deps; unit tests green on x86_64-linux and arm64-macos profiles. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S52.2 `make bench` produces results files for sqlite3/self/many-tu lanes with full provenance headers; musl lane runs and records its failure point. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S52.3 `CGF_STATS=1` prints all §4 stat lines; determinism tests green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S52.4 `.benchmarks/baseline-x86_64-linux-gnu.ci.txt` (RSS) and one fleet-host baseline committed with provenance; gate script exercised against both. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S52.5 `benchmark_gate.sh` fixture matrix green, including missing-metric = fail. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S52.6 One targeted optimization landed with its committed before/after profile summary and a measured ≥ noise-floor improvement on a named lane. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S52.7 `BENCH_SKIP_TIME=1` path verified in CI: time comparison provably skipped, RSS gate provably active (a seeded fake regression fails the job). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S53.1 ≥ 19 kernels build, self-check, and run on x86_64-linux and arm64-linux; arm64-macos skips print `HARNESS_SKIP` per discipline where applicable. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S53.2 Golden icount files committed for x86_64-linux-gnu and arm64-linux; gate exercised with a seeded regression (fails) and boundary pass. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S53.3 All sound §4/§5 rules landed with positive AND negative fixtures; negative flags-liveness fixtures demonstrably block the transform. Rules whose original mnemonic/phase description contradicted the shipped IR are corrected in the tables above rather than implemented unsafely. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S53.4 Full corpus green at `-O0..-Ofast` both targets after peepholes; OPT_EQ green with kernels included. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S53.5 `.benchmarks/kernels-vs-gcc.md` regenerates deterministically (static columns) and renders the runtime columns from a fleet run's dated file. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S53.6 Measured icount improvement ≥ 3% aggregate (sum over kernels) vs pre-sprint golden on each target, stated per-kernel in the landing commit. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S53.7 Addressing-mode pass shows icount wins on ≥ 3 named kernels with before/after diffs committed in the PR. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S54.1 `doc/perf-gates.md` lattice committed; every What×Where×When cell filled, "never"/"inactive" cells explicit; matches `ci/gates.d/` configs 1:1. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S54.2 Size gates live on every PR for x86_64-linux-gnu and arm64-linux; seeded +16% regression demonstrably fails, +14% passes (fixture + CI dry-run). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S54.3 Runtime gate script green on fixtures incl. both noise-guard directions; deployed in `state=trial` on all three fleet hosts with nightly runs landing in `.benchmarks/runs/`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S54.4 Trial-mode mechanism enforced by CI config, promotion rule documented, and the state machine fixture-tested. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S54.5 Every performance CI job posts the step summary; format golden-tested; a green run's summary shows baselines + deltas for all §1 every-PR metrics. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S54.6 `perf-report.sh` produces `.benchmarks/report-0.0.x.md` from current data with full provenance blocks; consumed shape agreed with Sprint 62 doc. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S54.7 `[bench skip]` verifier and `perf-override:` token checks live; first monthly audit date recorded in `doc/perf-gates.md`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S54.8 Placeholder lanes (stage1, musl) present in configs as `state=inactive` naming Sprints 58/57 — no silent stubs. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
