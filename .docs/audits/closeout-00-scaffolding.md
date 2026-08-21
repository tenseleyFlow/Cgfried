# Closeout: Phase 00 — Scaffolding

Date:            2026-08-20
Baseline commit: `89b68ead`
Reviewer:        Independent Sprint 61 closeout review

## DoD items (from the phase's sprint files, every numbered item)

- [x] S0.1 `make` produces `build/cgfried` and `build/cgf` with gcc and clang, zero warnings, `-std=c11 -pedantic -Wall -Wextra -Werror` on every TU. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S0.2 `--version`, `-dumpversion`, `--help` match Deliverable 6 exactly; exit 0. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S0.3 `cgfried foo.c` exits 1 with a diagnostic naming Sprint 3; `make tools` and `make bootstrap` fail naming Sprints 2 and 58. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S0.4 All 5 exit codes reachable-or-reserved per the table; `main()` is the only `return`-to-OS site; `grep -rn 'exit(' src` hits only `diag.c`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S0.5 Unit binary runs green under `make test`; ≥ 12 container/diag assertions including the insertion-order and stable-sort determinism cases. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S0.6 `grep -rn 'qsort\|__attribute__' src/` returns nothing. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S0.7 Caret renders correctly for a tab-indented line; colors off under `NO_COLOR=1`, on under piped `CLICOLOR_FORCE=1`. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S0.8 LICENSE (GPL-3.0 verbatim), README, `.gitignore` present; `git status` clean after `make && make test` (nothing generated outside `build/`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S1.1 `build/cgf-test` builds under gcc and clang with the strict flag set; shares `src/util/` objects (no duplicated container code — `diff` proves nothing was copy-pasted into `tests/runner/`). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S1.2 Directive table implemented exactly as specified; all 9 directives parse; the 3 reserved ones error naming Sprints 30/24/17; unknown directive, unknown selector, and unknown XF-id each produce a configuration error (exit code distinct in the summary: `CONFIG ERROR`, nonzero exit). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S1.3 Meta-suite: ≥ 12 fixture cases green, including XPASS-is-failure, pipe-deadlock, and timeout-kill; runs twice byte-identically. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S1.4 Unit harness: registry generated, deterministic order; count printed by `unit_tests --list` equals `grep -rc '^void test_' tests/unit/*.c` total; all Sprint 0 assertions ported and green. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S1.5 `HARNESS_SKIP` line format exact; `ci/check_skips.sh` fails on any injected extra/missing/changed-count skip (test this in the meta-suite); `ci/expected_skips_linux-x86_64.txt` committed (empty). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S1.6 `.docs/audits/xfail-debt.md` exists with ledger rules; zero open entries. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S1.7 CI workflow runs all 4 jobs green on trunk; `scripts/check_bans.sh` and `scripts/check_format.sh` wired; `make` succeeds without clang-format installed. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S1.8 `make test` composes all stages; killing any single stage (inject a failing fixture) fails the whole target. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S2.1 Fresh clone + `git submodule update --init` + `make && make test` passes with **no Rust toolchain installed** (toolchain suite skips loudly, skip asserted in the local profile's expected-skip file). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S2.2 `make tools` builds both submodules; missing cargo produces the exact guidance error, not a cargo stack trace. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S2.3 Routing matrix unit tests: all 8+ combinations green; `CGF_LD=1` errors naming Sprint 27; empty-string env vars behave as unset. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S2.4 Smoke differential green in CI: `.text`/`.data`/`.rodata` section bytes identical between afs-as and gas for `smoke_x86_64.s`; broken fixture fails through both assemblers. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S2.5 `ENOENT` on each tool exits 3 with the `CGF_AS_PATH`/`CGF_LD_PATH` guidance string; assembler failure on a user `.s` exits 1 (verified with the broken fixture via `CGF_AS_PATH` pointed at each assembler). — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S2.6 `getenv("CGF_` appears exactly once in `src/` (in `env_override`); ban-check enforces it. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S2.7 `--help` Environment section and README document the routing table verbatim, including the submodule bump ritual and the "Rust for tools only" statement. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).
- [x] S2.8 CI: `toolchain` job green with submodule + cargo caching; all Sprint 1 jobs unchanged and still Rust-free. — EVIDENCE: [tracked implementation and validation record](../HANDOFF.md) and [permanent test entry points](../../Makefile).

## Open audit findings against this phase

None. At remediation baseline `89b68ead`, [the Sprint 61 burndown](burndown.md) records 0 Critical / 0 High / 0 Medium / 0 Low findings, and the audit lifecycle reports 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL.

## Verdict

Every numbered phase DoD item is evidenced and no audit findings remain.

READY
