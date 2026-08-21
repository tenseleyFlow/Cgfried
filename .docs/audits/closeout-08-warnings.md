# Closeout: Phase 08 — Warnings

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S37.1 `warnings.def` exists with the two tracer rows; grep gate: no `-W` string literal parsing outside `warn.c`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S37.2 Flag-parse unit table ≥ 40 rows green; pedwarn 3×3 table exhaustive; `-w` beats all. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S37.3 `-Wall`/`-Wextra` group masks match §3 lists exactly (unit test enumerates and counts them). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S37.4 Pragma fixture suite ≥ 15 files green incl. both macro-direction cases and mid-function toggles. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S37.5 Suppression law: system-header fixture (via `-isystem` include) silent by default, fires under `-Wsystem-headers`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S37.6 `.docs/warnings-matrix.md` covers every gcc-8 C Warning row; `check_warn_matrix.sh` wired into `make test`; zero statusless rows. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S37.7 Every emitted warning renders `[-Wflag]`; capturing-sink test asserts `warn_id` on all tracer diagnostics. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S37.8 Differential lane green (or exact expected-skip per profile, invariant 4). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S38.1 Every ### above has ≥ 1 fire + ≥ 2 nofire fixtures green; total ≥ 90 fixtures under `tests/warn/`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S38.2 Matrix rows for all §1–§16 flags flipped to `done` with fixture paths; `check_warn_matrix.sh` green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S38.3 Fallthrough regex suite: all 6 level-3 patterns accepted, level-2/4 differences pinned (≥ 10 fixtures). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S38.4 Differential lane: zero unannotated divergences vs gcc-8 across `tests/warn/`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S38.5 musl dry-run: zero FPs (every remaining warning has a triage note proving it genuine); counts snapshotted in `tests/warn/corpus-baseline.txt`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S38.6 Default-state audit: unit test asserts each flag's {off,on,Wall,Wextra} state equals the §-header claim (table truth enforced in code). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S39.1 §4 matrix: 100% non-empty cells covered by fire+nofire fixture pairs (script counts cells vs files; ≥ 60 pairs). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S39.2 All three positional-mixing texts + scanset quirk + scanf `%f` asymmetry fixtures green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S39.3 Level semantics: =1 vs =2 vs sub-flag carve-outs pinned (≥ 12 fixtures); `-Wformat` implies `-Wnonnull` verified. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S39.4 Builtin table covers every §2 row with a smoke fixture per family; target-gated rows skip correctly per profile (invariant 4). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S39.5 Differential: zero unannotated divergences vs gcc-8 on `tests/warn/format/`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S39.6 Matrix flips all `-Wformat*` rows to `done` or `out-of-scope` citing §Defer; `check_warn_matrix.sh` green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S39.7 musl dry-run: zero format false positives. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S40.1 `tests/warn/flow/` ≥ 50 fixtures green; every §4 exclusion and both §3 FP shapes have nofire fixtures; shapes fire under `=strict`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S40.2 Opt-level stability harness: identical firing sets across all five levels on the whole flow tree. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S40.3 Decisive-edge notes render on maybe-uninit and return-type fixtures (`// CHECK:` pinned text). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S40.4 Extensions: `-Wunreachable-code` silent on the `sizeof`-idiom fixture, fires on `if (0)`; `-Winfinite-recursion` fire/nofire pair; both marked WG_CGF_EXT in `--help=warnings`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S40.5 musl dry-run: zero flow FPs; baseline snapshot committed. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S40.6 Matrix: uninitialized/maybe-uninitialized/return-type rows `done`; unreachable-code row records gcc-8 no-op truth; infinite-recursion row records parity-plus; Phase 8 matrix has ZERO `planned-s3x` rows left — the phase DoD (index.md "Warnings parity" milestone). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S40.7 `flow.h` service boundary documented; grep gate: no `#include "warn/flow.h"` outside `src/warn/` until s42 (which lifts it). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
