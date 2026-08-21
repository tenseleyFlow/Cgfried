# Closeout: Phase 01 — Preprocessor

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S3.1 `cgf -E` on every directive-free fixture matches gcc token stream (0 diffs). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S3.2 ≥ 40 unit assertions across lex/splice/loc tests; `make test` green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S3.3 `pp_loc_expansion` + `pp_loc_resolve` round-trip a 3-deep chain in a unit test. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S3.4 Any BOL `#` exits 1 naming Sprint 4; no other directive path reachable. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S3.5 Trigraphs off by default, on under `-trigraphs`, both fixture-pinned. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S3.6 `PpToken` is ≤ 32 bytes (`_Static_assert` in `pp.h`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S3.7 ASan/UBSan build of the test suite clean (`make test-san`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S4.1 All `#if` evaluator unit cases pass (≥ 40 assertions), incl. every row of the rules table. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S4.2 Include search order proven by fixture for both forms; `-v` prints the searched list; depth cap fires at exactly 200. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S4.3 Skipped-region rules: `#garbage`/bad tokens inside false branches ignored; all mismatch errors fire with the opener's location in a note. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S4.4 `#line` remaps diagnostics; physical locations still correct internally (unit test via `pp_loc_resolve`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S4.5 Macro table: define/undef/redefinition-compare (incl. spacing) unit-tested; `-D/-U` order-sensitive fixture passes. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S4.6 Every Sprint-5/6 seam hard-errors with the sprint number in the message; grep-able marker `LANDS_IN_SPRINT(5)` used at every such site. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S4.7 Differential run over conditional corpus: 0 diffs vs gcc. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S5.1 All 9 pitfall fixtures pass with gcc-identical `-E` token streams. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S5.2 tinycc `tests/pp/*.c` imported (Sprint 6 formalizes; smoke-run now): ≥ 90% passing, failures triaged with XFAIL IDs. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S5.3 Placemarker/paste/stringize unit tests: ≥ 60 assertions. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S5.4 3-level expansion-loc chain unit test walks to correct file loc. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S5.5 `cgf -dM -E </dev/null` matches gcc's core-integer macro subset per the deliverable-3 table; `__GNUC__` absent (fixture-asserted). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S5.6 `#include <stdio.h>` then `-E` completes without error against system glibc headers (the no-`__GNUC__` policy's acid test; parse NOT required — only PP success). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S5.7 `SOURCE_DATE_EPOCH` fixture proves byte-identical `-E` output across two runs; a run WITHOUT it set is also self-identical within the run. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S5.8 ASan/UBSan test suite clean; no hide-set leaks (arena-allocated). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S6.1 **Milestone gate**: differential run green — 0 unexplained diffs vs gcc over fixtures + imported corpora; every deliberate divergence has a findings-table row; every failure an XFAIL with stable ID. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S6.2 tinycc pp suite: 100% pass-or-XFAIL'd, ≥ 95% pass; `.expect` mismatches triaged in the findings table. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S6.3 `_Pragma`: 6+ fixtures incl. in-macro-expansion case; `-E` re-emission matches gcc modulo documented normalization. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S6.4 `#pragma once`: symlink/hardlink dedupe fixtures pass; `(dev,ino)` rule + bind-mount edge documented in code comments and findings table. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S6.5 Guard detector: 1 positive + ≥ 6 negative unit shapes; `guard_macro` populated, zero behavior change proven by unchanged differential run. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S6.6 `HARNESS_SKIP` counts exact in the gcc-only CI profile. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S6.7 `make test-ppdiff` wall time < 60 s on the dev box (perf sanity, not a gate; number recorded for Sprint 52). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S7.1 Backtrace fixtures: ≥ 10 stderr goldens incl. elision, `defined here` dedupe, `#line` interaction; all PP diagnostics inside expansions render chains (audited list in the PR description). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S7.2 Benchmark: >5x speedup on the 1000-header corpus, both numbers committed in the sprint closeout note; `CGF_PP_STATS` counts exact in fixtures. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S7.3 Fast-path off/on differential: byte-identical over the entire corpus. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S7.4 `make fuzz-smoke` (2000 iters × both modes) green in CI under ASan/UBSan; ≥ 3 fuzzer-found inputs promoted to permanent fixtures (if fewer found, the closeout says so explicitly — do not manufacture). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S7.5 Fuzzer reproduces any finding from seed alone on a clean checkout. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S7.6 Sprint-6 differential suite still 0 unexplained diffs (no regressions). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S7.7 PP phase closeout: no `LANDS_IN_SPRINT(3..7)` markers remain under `src/pp/` (`grep` gate in CI); remaining markers name Sprint ≥ 8, 37, 52, or 55 only. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
