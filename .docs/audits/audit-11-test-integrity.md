# F11 tests/CI integrity — CLOSED

- Review date: 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Scope: `tests/`, `ci/`, `scripts/`, `.docs/audits/xfail-debt.md`
- Current-tree inventory: 111 compiler/runtime source modules and 78 unit-test
  translation units.

## Regression and checklist evidence

- XFAIL/debt replay covered every repository debt ID in the F11 scope.
  `XD-S08-FPHOST` is retired in the implementation: `scripts/check_bans.sh`
  was clean and `build/unit_tests --filter test_softfp` passed 16 tests / 329
  assertions. `XD-S12-SIZEOF` is also retired: c-testsuite `00040.c` was
  accepted by both Cgfried and GCC. The current c-testsuite differential ran
  all 220 files: 217 agreed, three were known-deferred, zero were new, and
  zero XPASSed. The meta-ledger control remains sound: `XF-0001` produced one
  XFAIL for the failing fixture and one hard XPASS with status 1 for the
  passing fixture. The torture ledger has zero active `TORT-NNN` rows.
- Expected-skip recount covered all 32 profiles. Seventeen profiles are empty;
  15 are nonempty and contain 18 profile records representing 15 unique skip
  lines. Every nonempty line matches the required `HARNESS_SKIP suite=...`
  grammar and no profile contains a duplicate. `TI-M-01` is the one enforcement
  break found by following every producer to `ci/check_skips.sh`.
- Ratchet history was inspected on the baseline's first-parent history for all
  expected-skip profiles, `tests/torture/passing.txt`, the ARM64 expected-
  failure ledgers, campaign `.expected` files, safe-mode allowlists, digest
  pins, and `ci/closed_sprints.txt`. The torture pass set has 25,944 current
  cells and accumulated 25,947 additions with zero deletions across its seven
  history entries. The closed-sprint ratchet moved monotonically 50, 51, 52,
  53, 57. Both ARM64 failure ledgers and both safe-mode allowlists are empty.
  Campaign ratchets currently contain 90 rows across ten files. No unexplained
  loosening was confirmed. Commit `6eed7ee4` reduced the QBE ARM executed set
  from 31 to 28, but the changed ratchet itself names each of the three added
  upstream/ABI exclusions; this is recorded below as an observation rather
  than counted as a finding.
- Direct-unit coverage was mapped module by module. Eleven of 111 source
  modules have no module-focused unit assertion; all eleven do have broader
  differential, script, corpus, or executable coverage, listed below. This is
  a coverage map, not eleven invented findings.
- Durable replay is integrated: `sh scripts/check-audit-fixtures.sh
  build/cgfried` completed 42 checks with 42 expected XFAILs, zero XPASS, and
  zero harness failures, including all three F11 shell reproducers.

Checklist complete: **Yes.** F11 closes with three raw and three deduplicated
findings: zero Critical, zero High, three Medium, zero Low, and one unverified
observation. It is **CLOSED for Sprint 60 collection**; remediation remains
Sprint 61 work.

## Findings

~~ID: `TI-M-01`~~
~~Title: the POSIX-shell expected-skip profile is never enforced~~
~~Severity: Medium — a CI/test harness can silently stop checking every shell~~
~~script, a test-integrity failure rather than a compiler semantic failure.~~
~~Reproducer: `tests/audit-regressions/ti-m-01.sh`. With an empty `PATH`, the~~
~~producer emits the committed skip, but the Makefile never checks it.~~
~~Root cause: the expected profile was added with the producer in commit~~
~~`b08d204c`, but the recipe omitted the producer/log/profile sequence.~~
~~Affected sprint: 28.~~
Resolution: RESOLVED 2026-08-20 by `141ffcad`. `make test` now records the
POSIX-shell producer and selects an exact empty or no-dash skip profile; both
tool-available and tool-missing paths are pinned.

~~ID: `TI-M-02`~~
~~Title: the primary XFAIL ledger reports retired float debt as open~~
~~Severity: Medium — the debt source of truth contradicts the implementation~~
~~and its own close-in-place policy.~~
~~Reproducer: `tests/audit-regressions/ti-m-02.sh`.~~
~~Root cause: Sprint 15 retired the implementation seam but did not close the~~
~~historical ledger row, and the earlier debt ID namespace never matched the~~
~~runner's enforced namespace.~~
~~Affected sprints: 8, 15.~~
Resolution: RESOLVED 2026-08-20 by `141ffcad`. `XD-S08-FPHOST` is closed in
place while retaining its historical implementation-debt ID; the ledger now
distinguishes such provenance rows from runner-visible `XF-NNNN` fixture IDs.

~~ID: `TI-M-03`~~
~~Title: every live c-testsuite debt row cites an obsolete failure cause~~
~~Severity: Medium — filename-only membership lets unrelated failures inherit~~
~~stale waivers.~~
~~Reproducer: `tests/audit-regressions/ti-m-03.sh`, with minimized cases under~~
~~`tests/audit-regressions/support/ti-m-03/`.~~
~~Focused replay showed:~~

- ~~`00210.c` was attributed to unsupported GNU attributes, but now fails on a
  function returning a function.~~
- ~~`00216.c` was attributed to `__builtin_va_list`, but now fails on GNU range
  designators.~~
- ~~`00219.c` was attributed to `__builtin_va_list`, but now fails on a
  duplicate compatible `_Generic` association.~~

~~Root cause: the differential ratchets only filename membership and never~~
~~asserts that a debt ID or reason still matches the current diagnostic.~~
~~Affected sprints: 10, 28, 55.~~
Resolution: RESOLVED 2026-08-20 by `141ffcad`. The ledger has a validated,
sorted tab schema with stable debt IDs and literal diagnostic fingerprints;
drift is a new failure. The focused contract suite and the real 220-file lane
pass at 217 agreement / 3 known-deferred / 0 new / 0 XPASS.

### Shared checker dispatch contract

The integrated shared checker includes root-level `*.sh` in both manifest/file
bidirectionality scans. It invokes `TI-M-01` and `TI-M-02` with the repository
root and `TI-M-03` with the repository root plus `$CGF`. Each script has the
same explicit status contract: zero means the baseline finding reproduced and
calls `xfail`, one means remediation and calls `xpass`, and two means malformed
checkout/tooling and calls `fail`. Support files remain below `support/` and
are not treated as top-level manifest fixtures.

## Expected-skip recount

Zero records: `a64asmdiff`, `afsld`, `ctestsuite`, `debug`, `fpdiff`,
`headerdiff`, `initdiff`, `inlinediff`, `layout`, `linux-x86_64`, `muslwarn`,
`objdiff`, `ppdiff`, `rtdiff`, `tinyccwarn`, `toolchain`, `warndiff`.

One record: `a64asmdiff-clang`, `a64asmdiff-noelf`,
`a64asmdiff-notools`, `afsld-notools`, `ctestsuite-norefs`, `debug-nogdb`,
`meta`, `muslwarn-norefs`, `objdiff-gasonly`, `posixsh`,
`tinyccwarn-norefs`, `toolchain-notools`, `warndiff-nogcc8`.

Two records: `debug-notools`. Three records: `debug-notools-nogdb`.

## Source modules without direct unit coverage

The criterion here is a module-focused unit assertion, not incidental linkage
into `build/unit_tests`. Existing broader protection is stated so this map does
not overclaim a total coverage absence.

| Source module | Existing non-unit or indirect protection | Risk |
|---|---|---|
| `src/cg/arm64/debug.c` | `scripts/a64_debug_lane.sh` ELF/DWARF/CFI inspection | Medium |
| `src/cg/data.c` | backend emit/corpus paths exercise segment selection indirectly | Medium |
| `src/driver/deps.c` | driver matrix and dependency-output corpus coverage | Medium |
| `src/driver/driver.c` | smoke, driver matrix, and all compiler subprocess tests | Medium |
| `src/driver/safe_elf.c` | safe-mode composition/link scripts | High |
| `src/main.c` | every executable/compiler subprocess invocation | Low |
| `src/rt/cgf_safe_alloc.c` | memory-runtime executable tests | High |
| `src/rt/cgf_safe_diag.c` | memory-runtime failure-path tests | High |
| `src/rt/dso_handle.c` | hosted link/atexit corpus | Medium |
| `src/rt/fp128.c` | `rt_diff.sh` bit-level runtime differential | High |
| `src/rt/int128.c` | `rt_diff.sh` directed/random runtime differential | High |

## Attack-surface dispatch

- XFAIL/debt audit: complete; `TI-M-02` and `TI-M-03` resolved.
- Skip-discipline audit: complete over 32 profiles and 18 profile records;
  `TI-M-01` resolved.
- Ratchet-file honesty: complete over the named ratchet families; no confirmed
  unexplained loosening.
- Direct-unit coverage: complete over 111 source modules; 11 gaps mapped.

## Unverified observations

- Commit `6eed7ee4` changed QBE ARM from 31 executed cases / one exclusion to
  28 executed cases / four exclusions. The commit message is only “ground QBE
  ARM gate in native baseline,” but the ratchet row itself names
  `dynalloc.ssa` as an upstream non-entry-allocation exclusion and both vararg
  cases as upstream stack-alignment exclusions. Because the changed artifact
  contains concrete reasons and host/Cgfried parity remained zero failures,
  this is not counted as an unexplained loosening. A stricter policy could
  require those reasons in the commit body as well.
