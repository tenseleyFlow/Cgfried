# HANDOFF — read this before touching anything

You are picking up **Cgfried**, a from-scratch C17 compiler.

**WHERE THINGS STAND (2026-08-22): Sprints 0–57, 59, and 60 are CLOSED;
Sprints 59–60 closed out of order, so the contiguous ratchet remains 57.
Sprint 61 implementation and review are complete with an honest NOT READY
closeout. Phases 1–11 are CLOSED.**
Sprints 55–57 were completed out of numerical order while Sprint 54 collected
its controlled fleet soak; the current deterministic release report, closure
audit, and contiguous ratchet through Sprint 57 now close that gap. Sprint 58's
implementation, deterministic per-pass phase-dump playbook, and first complete
hosted native/cross activation are green; its 30-day bootstrap soak is RUNNING
at 4/30 after a required-lane reset on August 18 and remains operationally
OPEN. Supplemental full-lattice manual run
[`32603828216`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32603828216)
is green at exact head `8fb99082`; because it is a second August 22
observation, it neither advances the distinct-date count nor replaces the
scheduled Sunday weekly gate. Sprint 59's controlled Kasumi/Hasu
SQLite baselines and qualifying 15-variant nightly are independently audited
and complete. The
performance-gate lattice, native CI measurements, fleet runtime protocol,
reporting, policy checks, scheduler integration, and controlled-power model
are implemented. Kasumi, Hasu, and Nomad each have accepted controlled Sprint
54 evidence on three distinct UTC dates. Sprint 55's GNU tier table is **30
implemented / 6 parsed-ignored / 8 refused**.
Sprint 56's campaign machine, triage map, and 26,265-cell PASS ratchet are
complete. Sprint 57's pinned compile-the-world campaigns, truthful
staged-musl linkage proof, host baselines, exact gates, and campaign-driven
compiler repairs are integrated on `trunk`. Sprint 59's exact campaign
machine, compiler repairs, numeric scale policy, and closure evidence are
integrated while Sprint 58 continues collecting operational evidence. The old
D5 notes are retained only as implementation history. Sprint 60's defensive
audit is COMPLETE against full-CI-green baseline
`1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`: 12 fronts, 50 raw / 50
deduplicated findings (C12/H20/M16/L2), and 50 durable expected-failure
reproducers. Sprint 61 remediated all seeded and discovered debt, completed
its independent review, and now waits only on Sprint 58's independent soak.

**Known-wrong-but-SHIPPING is ZERO** — every open item on `trunk` is a named
refusal or a deliberate deferral.

On trunk, continue **Sprint 58's 30-day bootstrap soak**. Record only hosted
runs that satisfy the machine-readable daily/weekly lane contract in
`.docs/audits/bootstrap-soak.md`; a missing or red required run resets the
streak. No Sprint 61 remediation remains. Do not claim Sprint 58 closed, mark
Phases 12–13 READY, or begin the Sprint 62 release landing until the soak
reaches 30/30.

Sprint 55 came out of numerical order because campaign sprints 56–59 consume
it, 28 deferrals pointed at it, and it blocks HOSTED compilation on macOS and
FreeBSD. Confirmed empirically: extended asm and `__volatile` together took
musl from **716 to 1259 of 1361** translation units parsing.

---

## Sprint 60 defensive audit — COMPLETE; CLOSED OUT OF ORDER

Baseline `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb` is frozen for the audit.
Its required x86 bootstrap [run
31865512724](https://github.com/tenseleyFlow/Cgfried/actions/runs/31865512724)
passed O0/O2, and full standard CI [run
31865512754](https://github.com/tenseleyFlow/Cgfried/actions/runs/31865512754)
completed with all 20 executed jobs green and one expected skip. All twelve
fresh-context, reproducer-first fronts are closed. The required F04 -> F05 ->
F09 dependency chain was respected, F12 ran last, and an independent
cross-front review found no root-cause aliases.

- `.docs/audits/audit-00.md` records 50 raw / 50 deduplicated findings:
  12 Critical, 20 High, 16 Medium, and 2 Low. Eleven unverified observations
  are segregated and excluded.
- `tests/audit-regressions/manifest.tsv` and the front reports map exactly in
  both directions. `scripts/check-audit-fixtures.sh build/cgfried` reports
  50 XFAIL, 0 XPASS, and 0 FAIL.
- The complete normal regression suite is green at 713 unit tests,
  4,292,653 assertions, and 639 program fixtures. Bootstrap, all
  differentials, campaign/meta gates, audit aliases, format, and ban checks
  are green. `make BUILD=build-san test-san` also completed successfully,
  including all 50 expected audit failures, cross-target/ABI/runtime lanes,
  and all three sanitizer fuzz smokes.
- The frozen-baseline-to-closeout diff under `src/`, `runtime/`, and `include/`
  is empty: Sprint 60 landed only reproducers, audit tooling, and ledger
  evidence. Remediation begins in Sprint 61.

Sprint 61 has closed all Critical findings and all 20 seeded High findings.
The `IR-H-07` arithmetic review added and closed `IR-H-09`. Broad validation
after the seeded High tier then exposed `IR-H-12`, a valid-C SysV aggregate
stack-marker verifier ICE introduced by the `IR-C-10` repair; `IR-H-12` is
closed at `92c77528`, zero High findings remain, and Medium/Low remediation is
active. `DRV-M-01` then closed at `a91a8c42`; the test-integrity cluster closed
at `141ffcad`; the preprocessor/frontend diagnostic cluster closed at
`385f8aa8`; the backend Medium cluster closed at `fb641618`; `PP-L-03` closed
at `addf16b7`; `IR-L-02` closed at `7a4b9cb6`; the memory-safety Medium
cluster closed at `687c92a7`; and the determinism/performance-evidence cluster
closed at `89b68ead`, reducing Sprint 61 audit debt to zero.
Sprint 58 remains at 4/30; Sprint 60's
out-of-order closure does not advance the contiguous closure ratchet or permit
Phase 13/release sign-off.

---

## Sprint 61 remediation and review — COMPLETE; final verdict NOT READY

The phase-boundary preflight is complete. Commit `4b36ba37` promotes the
aggregate/front ledgers, burndown, and future closeout files to tracked
artifacts while leaving unrelated local audit material ignored. Commit
`b2e1ac18` gives every audit fixture an explicit `OPEN`/`PASS` lifecycle:
OPEN defects remain XFAIL and repair-before-ledger is XPASS/red; PASS repairs
stay green and any regression is FAIL/red. The fail-closed lifecycle meta-test
is part of `make test-audit-fixtures`; the starting inventory is 0 PASS / 50
XFAIL / 0 XPASS / 0 FAIL.

The initial burndown was 12 Critical / 20 High / 16 Medium / 2 Low. The first
cluster hunt added `SEMA-C-08`, an AAPCS64 unnamed-nonzero-bitfield aggregate
alignment mismatch, at `555fc32a`. `SEMA-C-01` closed at `0eca9832` and
`SEMA-C-02` closed at `d4d674ec`, and `IR-C-03` closed at `892435be`, so live
debt is 0/20/16/2 after `X64-C-02` closed at `45a9d0dc`, `MS-C-01` closed at
`7a003d68`, and `SEMA-C-08` closed at `bdd523c0`, followed by the `IR-C-11`
atomic-alignment discovery at `c7d43927` during the `X64-C-01` sibling hunt,
`IR-C-04` closed at `1b61459a`, `IR-C-01` closed at `eb528221`, and `IR-C-11`
closed at `eb364980`, `X64-C-01` closed at `45f0282c`, and `MS-C-04` closed at
`b1bc4f91`, and `IR-C-09` closed at `3bc1ae02`. The `MS-C-05` design review
then added `MS-C-06`, the unregistered `asprintf`/`vasprintf` allocator-model
mismatch. `IR-C-10` closed at `ea41dd88`, `MS-C-06` closed at `88211779`, and
`MS-C-05` closed at `b287c2ef`; the Critical barrier is clear. `SEMA-H-07`
then closed at `21acc9fc`, followed by `RT-H-01` at `d3be586f`, `OPT-H-04`
at `f6ffc1d1`, `A64-H-01` at `2b4ab767`, `IR-H-06` at `1fc6bdb6`, and
`OPT-H-02` at `5b030fc5`, so live debt is 0/14/16/2 and High remediation is
active. The `IR-H-07` arithmetic review then recorded `IR-H-09` at `81ca7347`,
raising live debt to 0/15/16/2 until that parser-length defect is remediated.
`IR-H-07` then closed at `ed607727`, returning live debt to 0/14/16/2.
`OPT-H-01` closed at `264bb13d`, reducing live debt to 0/13/16/2.
`A64-H-02` and `A64-H-03` closed together at `2cd05946`, reducing live debt to
0/11/16/2.
`SEMA-H-03` closed at `1da90129`, reducing live debt to 0/10/16/2.
`SEMA-H-05` closed at `a2e158c9`, reducing live debt to 0/9/16/2.
`SEMA-H-04` closed at `40d84413`, reducing live debt to 0/8/16/2.
`SEMA-H-06` closed at `52eeaa91`, reducing live debt to 0/7/16/2.
`IR-H-09` closed at `7f3774f6`, reducing live debt to 0/6/16/2.
`IR-H-08` closed at `088c9934`, reducing live debt to 0/5/16/2.
`FE-H-01` and `FE-H-02` closed together at `b1c29124`, reducing live debt to
0/3/16/2.
`OPT-H-03` closed at `43b0de28`, reducing live debt to 0/2/16/2.
`PP-H-01` closed at `54cf3e67`, reducing live debt to 0/1/16/2.
`IR-H-05` closed at `54435c86`, reducing live debt to 0/0/16/2 and clearing
the High tier.
Broad validation then recorded `IR-H-12` at `074c83d9`, raising live debt to
0/1/16/2 until the caller/callee SysV aggregate stack-marker contract is
repaired.
`IR-H-12` closed at `92c77528`, restoring live debt to 0/0/16/2 and clearing
the High tier again.
`DRV-M-01` closed at `a91a8c42`, reducing live debt to 0/0/15/2. A clean
detached build, all 55 audit lifecycle checks, and the assembler exit/signal
matrix are green at that exact code commit.
`TI-M-01`, `TI-M-02`, and `TI-M-03` closed together at `141ffcad`, reducing
live debt to 0/0/12/2. Exact available/missing-tool skip profiles, the focused
c-testsuite debt contract, the real 220-file differential, and a clean
detached 41 PASS / 14 XFAIL / 0 XPASS / 0 FAIL audit run are green.
`PP-M-02`, `PP-M-04`, `FE-M-03`, `FE-M-04`, and `FE-M-05` closed together at
`385f8aa8`, reducing live debt to 0/0/7/2. Exact macro-note anchors and order,
adversarial recovery cases, 74+74 preprocessor differential comparisons, and
a clean detached 46 PASS / 9 XFAIL / 0 XPASS / 0 FAIL audit run are green.
`X64-M-03` and `A64-M-04` closed together at `fb641618`, reducing live debt to
0/0/5/2. Multi-digit inline-assembly execution, decoded ARM64 frame rows,
peephole-boundary survival, and a clean detached 48 PASS / 7 XFAIL / 0 XPASS /
0 FAIL audit run are green.
`PP-L-03` closed at `addf16b7`, reducing live debt to 0/0/5/1. Exact
300-frame unit coverage, the 272-frame unique-name audit fixture, preserved
capped presentation, and a clean detached 49 PASS / 6 XFAIL / 0 XPASS / 0
FAIL audit run are green.
`IR-L-02` closed at `7a4b9cb6`, reducing live debt to 0/0/5/0. Integer,
floating, and forward-reference mismatch coverage, verified textual round
trips, 5,000 IR-fuzzer iterations, and a clean detached 50 PASS / 5 XFAIL / 0
XPASS / 0 FAIL audit run are green.
`MS-M-02` and `MS-M-03` closed together at `687c92a7`, reducing live debt to
0/0/3/0. Path-local MUST origins now make standard-stream equality pruning
sound across selects, non-pointer overwrites, joins, and origin-capacity loss;
standard-stream `freopen` stays escaped while owned-stream controls stay
locally owned. The 67-test memory-safety unit lane, 92/92 warning corpus, 12
exact trace sequences, musl meta-gate, and a clean detached 52 PASS / 3 XFAIL
/ 0 XPASS / 0 FAIL audit run are green.
`DET-M-01`, `DET-M-02`, and `DET-M-03` closed together at `89b68ead`, reducing
live debt to 0/0/0/0. Blocking compile, musl, and stage1 gates now consume
paired wall/CPU/RSS dispersion and use the larger of their nominal allowance
and four MADs; stage1 takes three forced-build samples; legacy evidence cannot
silently become a gate; and every baseline-changing commit is checked against
its exact old/new metric union and rationale. `make -j2 test-bench`, `make -j2
test-bootstrap`, and a clean detached 55 PASS / 0 XFAIL / 0 XPASS / 0 FAIL
audit run are green at that exact code commit.
The full 55-finding lifecycle is therefore 55 PASS / 0 XFAIL / 0 XPASS / 0
FAIL with live debt 0/0/0/0. All 15 Critical fixes retain cluster-hunt records.
The post-triage release XFAIL bar is N=0 and is satisfied. Seed 61 selected six
of 55 remediated findings (10.9%); an independent fresh-detached-worktree
review rebuilt every exact resolving commit, reran the full audit lifecycle,
and read each fixing diff against its root cause. All 6/6 passed, so no sample
doubling or front-wide escalation fired.

Fourteen phase closeout documents now cover all 429 numbered DoD items from
Sprints 0–61. The structural/meta closeout gate is green. The production
Sprint 62 entry gate exits red for exactly two honest verdicts: Phase 12 is
NOT READY because Sprint 58 is 4/30, and Phase 13 inherits that dependency.
Phases 00–11 are READY. This is the sole remaining release-entry blocker.
The gate consumes tracked `ci/closeout-dod.tsv` rather than the ignored local
roadmap, so a fresh clone proves the exact 62-sprint / 429-item inventory.

Final closeout validation completed with `make -j2 test` green: 794 unit
tests / 4,293,663 assertions, 693 program fixtures, 104 permanent corpus
fixtures, 55/55 audit regressions PASS, all compiler differentials and fuzz
smokes green, and the expected missing-tool/sysroot skips ledgered exactly.
That run found and permanently repaired two stale test-policy ratchets:
`s36_isa_driver.sh` now expects 104 corpus files / 624 optimization-level
objects after `X64-M-03`, and `check_posix_sh.sh` now distinguishes valid POSIX
awk functions embedded in shell scripts from shell-only syntax. Both have
focused meta-tests in ordinary `make test`.
One central integrator owns `manifest.tsv`, the tracked front ledgers, and
`burndown.md` so
each finding's fixture-state flip, strikethrough, and count update travel with
its fix. The completed Critical lanes were split to avoid shared-file
conflicts:

- `SEMA-C-01` — RESOLVED at `0eca9832`; its cluster hunt also repaired the
  sibling multiplication overflow path.
- `SEMA-C-02` — RESOLVED at `d4d674ec`; its sibling-hunt discovery
  `SEMA-C-08` is also RESOLVED at `bdd523c0`.
- `IR-C-01` — RESOLVED at `eb528221`; pure 16-byte `long double` aggregates
  return through `st0`, with zero-width struct bitfields excluded from the
  wire classification.
- `IR-C-03` — RESOLVED at `892435be`; atomic pointer updates now use one
  scaled seq_cst RMW. This unlocks the `X64-C-01` backend repair.
- `IR-C-04` — RESOLVED at `1b61459a`; declaration-granular VLA checkpoints
  restore the stack across backward gotos, including same-scope and `for`-init
  cases.
- `IR-C-11` — RESOLVED at `eb364980`; target-neutral verification now rejects
  under-aligned scalar seq_cst accesses before either backend can rely on an
  invalid indivisibility guarantee.
- `IR-C-09` — RESOLVED at `3bc1ae02`; Linux AAPCS64 composite arguments and
  `va_arg` honor the even-register rule, Apple remains unchanged, and the
  shared marker is verifier-hardened.
- `IR-C-10` — RESOLVED at `ea41dd88`; Linux and Apple retain 16-byte source
  alignment after composite flattening, caller/callee stack walks agree, and
  Linux's adjacent bare-`_Float128` slot rule is pinned on both sides.
- `X64-C-02` — RESOLVED at `45a9d0dc`; frame sizes and far offsets are
  represented and emitted without 32-bit truncation.
- `X64-C-01` — RESOLVED at `45f0282c`; floating seq_cst accesses retain their
  atomic contract, wide helpers pull in `libatomic`, and content-aware `-###`
  remains free of filesystem and external-tool side effects.
- `A64-H-01` — RESOLVED at `2b4ab767`; tentative TLS definitions bypass ELF
  COMMON and retain `.tbss`, TLS symbol typing, and TLSLE relocations, while
  ordinary COMMON and the effective Mach-O zero-fill form remain unchanged.
- `A64-H-02` / `A64-H-03` — RESOLVED at `2cd05946`; the shared post-relocation
  address materializer retains immediate fast paths, uses reserved non-aliasing
  scratch registers for arbitrary signed addends, and keeps TLS/GOT relocation
  forms plus branch-relaxation size accounting correct.
- `IR-H-06` — RESOLVED at `1fc6bdb6`; lowering and verification share the
  exact returns-twice symbol set after asm-spelling normalization, including
  glibc's `__sigsetjmp` expansion while excluding near names.
- `IR-H-07` — RESOLVED at `ed607727`; relocation bounds are checked with
  ordered subtraction, so exact fits remain valid and wrapped or short
  eight-byte relocation ranges are rejected before backend consumption.
- `IR-H-08` — RESOLVED at `088c9934`; textual IR distinguishes symbolic
  indirect callees from direct calls and preserves that form through
  optimizer-created parser/printer structural round trips.
- `MS-C-01` — RESOLVED at `7a003d68`; the required `-fsafe` diagnostic floor
  is immutable.
- `MS-C-04` — RESOLVED at `b1bc4f91`; statically proven null accesses and
  indirect calls are unweakenable safe-mode errors, with path-aware zero-byte
  exemptions that do not suppress unrelated pointer checks.
- `MS-C-06` — RESOLVED at `88211779`; compatible external
  `asprintf`/`vasprintf` identities are rejected because their returned storage
  cannot enter the safe-runtime registry, while static, incompatible, and
  renamed non-allocator identities remain valid.
- `MS-C-05` — RESOLVED at `b287c2ef`; safe heap pointer derivations retain
  origin, index, scale, direction, and supported `uintptr_t` provenance until
  runtime validation, while analysis treats the guards as no-capture and
  no-dereference. Atomic pointer arithmetic is an explicit `-fsafe` refusal.
- `SEMA-H-07` — RESOLVED at `21acc9fc`; `_Generic` constant evaluation follows
  only sema's selected association in the caller's constant-expression mode,
  including nested selections and every integer-constant-expression context.
- `SEMA-H-03` — RESOLVED at `1da90129`; old-style declarations and definitions
  compare their promoted signatures symmetrically while K&R declaration-list
  scope, dependent arrays, counts, and GCC-compatible warning modes remain
  intact.
- `RT-H-01` — RESOLVED at `d3be586f`; `__SIZEOF_LONG_DOUBLE__` now comes from
  the canonical target layout, preserving Apple arm64's 8-byte binary64 model
  while every other current target remains 16 bytes.
- `OPT-H-04` — RESOLVED at `f6ffc1d1`; equal alias hulls no longer prove
  identity or pathwise store coverage unless the operand is identical or both
  locations are independently exact. GVN/DSE and the 25-cell oracle pin the
  soundness boundary.
- `OPT-H-02` — RESOLVED at `5b030fc5`; fusion dependence strides now include
  the signed induction step, so descending and non-unit loops use execution
  order while overflow and extreme steps conservatively block transformation.
- `OPT-H-01` — RESOLVED at `264bb13d`; alias points-to progress and offset
  growth are separated, with a finite precision window followed by monotone
  widening that preserves sound MAY results and converges on recursive updates.

Every Critical closure requires its own sibling-hunt record. A newly found
Critical or High reopens the barrier. The final ledger is 0/0/0/0, all Sprint
61 closeout verification and ledger finalization are complete, and the blunt
verdict remains NOT READY only until Sprint 58 reaches 30/30.

---

## Parallel Sprint 59 project ladder — COMPLETE; CLOSED OUT OF ORDER

Sprint 59's code, descriptors, exact-result contracts, compiler repairs,
designated-host SQLite baselines, and qualifying post-baseline nightly are
complete on `trunk`. Sprint 58 remains open, so this closure does not advance
the contiguous ratchet beyond 57.

- `ci/campaigns/FORMAT.md`, `ladder.yml`, and the retrofitted Sprint 57
  descriptors define one audited eight-project inventory. The linter sees
  eight descriptors, ten expected files, and one ladder; seeded missing,
  extra, reordered, duplicate, malformed, and changed rows all fail.
- The recurring matrix has exactly 15 variants across x86-64, native ARM64,
  and the two x86-64 musl-static lanes. Publication validates every artifact's
  exact rows and generates deterministic aggregate and failure reports.
- Native x86 zlib passes all ten exact rows, including all four upstream
  self-tests and the GCC output differential. Lua passes all nine native rows
  at O0 and O2 and reaches `final OK !!!` in the full 5.4.7 `all.lua`; the
  Cgfried-built musl-static stack passes its four-row full-suite bar. Curl
  passes its ten-row configure/build/test-support/version contract with zero
  driver ICEs and an exact five-row probe-deviation ledger.
- SQLite compiles the 3.46.1 amalgamation and shell at O0/O1/O2/O3/Os. Its
  32-statement shell smoke is byte-identical to the GCC lane at O0 and O2,
  and `speedtest1` completes with verification
  `111130:1e792c9db61996c477b8ab5ce2d690052e8dae74824a430a`. The canonical
  Sprint 52 scale profile records 10 measured runs after one warmup and keeps
  the absolute O2 ceiling at 60 seconds. Exact revision `e52b8891` produced
  controlled passing baselines at `2026-08-13T230920Z`: Kasumi O0
  2,094.713636 ms / 1,248,164 KiB and O2 18,203.856393 ms / 11,490,048 KiB;
  Hasu O0 2,406.116677 ms / 1,228,200 KiB and O2 17,733.074318 ms /
  11,450,836 KiB. The complete immutable artifacts live under
  `.benchmarks/runs/2026-08-13T230920Z-{kasumi,hasu}-sqlite/`.
- Eight stable compiler findings are fixed and regression-pinned:
  deterministic source-span interning; bounded alias/dependence/IPO/BCE
  scale; call-argument provenance through IPO/BCE; dynamic x86 block-edge
  copies beyond 32 values; GNU `__PRETTY_FUNCTION__`; static nested-member
  address folding; ABI provenance through loop clone/unswitch/unroll; and
  final materialization of out-of-range ARM64 frame addresses. The last repair
  makes Curl's 37.8 KiB spill frame compile and cross-assemble correctly.
- The TinyCC warning ratchet legitimately advances from 14 parsed / 16
  deferred to 15 / 15: `x86_64-gen.c` was rejected only because glibc
  `assert` expands to `__PRETTY_FUNCTION__`. It now has zero Cgfried-only
  format warnings. The permanent fixtures now pin the frontend-fuzz sequence
  to `ca30f3f7c77c82be`.
- Fresh closure validation passes the complete repository suite: 713 unit
  tests, 4,292,653 assertions, all 639 program fixtures, every
  corpus/differential/optimizer/runtime/cross/policy gate, and clang-format 22.
  A 2,000-iteration frontend-fuzz smoke completed with zero findings; the crash
  ledger is clean.

### Sprint 59 closure evidence

1. **COMPLETE:** exact revision `e52b8891444ff883d8dfc6ae5202e9410e039cae`
   is pushed; its required hosted x86-64/native ARM64 activation and O0/O2
   bootstrap jobs are green (CI `31752532760`, bootstrap `31752532771`).
2. **COMPLETE:** `scripts/fleet-sqlite.sh` captured one warmup plus ten O0/O2
   measurements on both controlled designated hosts at that exact revision.
   Both immutable artifacts classify fleet-control-v2 as controlled, pass the
   absolute gate, preserve the original policy byte-for-byte, and have been
   independently checked against their raw samples.
3. **COMPLETE:** all eight numeric medians/max-RSS values are committed in
   `ci/campaigns/sqlite-baselines.conf`. The +30% wall, +20% RSS, and 60-second
   absolute O2 thresholds are unchanged; partial/unmeasured policy fixtures
   remain fail-closed, and the public `compile.baseline-policy` row is
   invariant.
4. **COMPLETE:** qualifying nightly
   [`31761472470`](https://github.com/tenseleyFlow/Cgfried/actions/runs/31761472470)
   passed all 15 jobs at exact head
   `f8aed0f5aa16331af4dbaa600212b4bf25df2583`. Its artifact inventory is the
   exact 28-item contract: 13 campaign-result envelopes, 14 detailed evidence
   artifacts, and `campaign-ledger-evidence`. The reporter recorded
   `campaign-publish-reports: PASS captured=15 matched=15 published=0`; all 15
   metadata envelopes are exact `source=campaign` / `state=match`, all checks
   pass in both directions, all producer/aggregate pairs are byte-identical,
   and `failures/manifest.tsv` records zero failures. The downloaded aggregate
   ZIP matches API SHA-256
   `dcbeb83c2ac991a6df9baf15eaa1ac6cfde991ad585463bf6c1983bf850700d4`.

`ci/closed_sprints.txt` is 57. Sprint 59 is closed out of order; its evidence
does not manufacture Sprint 58 bootstrap-soak days or advance the contiguous
ratchet while Sprint 58 remains open.

---

## Parallel Sprint 58 self-host campaign — IMPLEMENTED; SOAK RUNNING (4/30)

Sprint 58's compiler, runtime, deterministic bootstrap/playbook machinery, and
hosted CI definitions are integrated. The first hosted streak reached 5/30
through August 17, then reset on August 18 when the required x86 O0 job was
cancelled before bootstrap and retained no artifact. August 19–22 are days
1–4 of the current streak; the ledger still needs 26 consecutive dates, so
do not call the sprint closed until that operational obligation is complete.

- `make bootstrap-O0` and `make bootstrap-O2` perform raw stage1/stage2
  comparisons over all 113 compiler/runtime assembly files, all 113 objects,
  `libcgf_rt.a`, and `cgfried`; no normalization is permitted. Both local x86
  fixed points pass. `make bootstrap-repro-O2` also passes across `-j8` versus
  `-j1` and distinct output roots.
- The bootstrap compiles the runtime with Cgfried. The safe allocator lock is
  strict C11 `_Atomic`, int128 helpers use the two-`u64` ABI without host
  `__int128`, and the binary128 boundary uses `_Float128` when self-hosted.
  The int128 ABI/value differential passes under GCC, Clang, Cgfried O0, and
  Cgfried O2.
- O2 bootstrap feasibility repairs are deliberately bounded: inline analysis
  caches call/fact data and caps oversized callers, GVN/DSE cap deterministic
  memory work, and LICM retains canonicalization/verification while declining
  motion in oversized loop functions. Inliner growth state persists across a
  top-level fixed point so `20020506-1.c` converges instead of repeatedly
  consuming a reset budget. Unit coverage pins every cutoff.
- The first O2 fixed point found a real historical nondeterminism bug:
  `GvnOperand` equality compared padding bytes. Keys are now zero-initialized
  and compared field-by-field. The static audit rejects this whole-object
  `memcmp` pattern and carries a seeded regression alongside the existing
  readdir, padded-write, and pointer-output faults.
- `CGF_DUMP_IR=all` emits an exclusive-create ordered tree spanning parse,
  sema, lowering, every optimizer invocation with deterministic
  sequence/fixpoint indices, legalization, MIR, and assembly.
  `scripts/bisect-nondet.sh` fails closed on absent groups and identifies the
  first differing TU and phase boundary. `make test-bootstrap` exercises the
  real phase tree, collision refusal, exact localization, and three injected
  fault families.
- `.github/workflows/bootstrap.yml` defines required x86 O0/O2 checks, nightly
  native ARM64 O0/O2, and weekly x86 reproducibility plus cross-host ARM64.
  Cross-host evidence authenticates both hosted run manifests against the
  workflow commit and retains both bootstrap reports, consumed stage
  manifests, and the exact ARM header archive in the final 90-day artifact.
- The controlled Kasumi/Hasu O2 stage1 timing receipt is wired into the Sprint
  54 dashboard under the existing fleet-control-v2 protocol. It is a trial
  gate and does not manufacture a baseline or Sprint 54 evidence.
- ARM flag selection now treats calls, opaque asm, and atomic CAS as NZCV
  clobbers, with table-driven regressions requiring a fresh comparison after
  each. Conditional branches use compact encodings only when range is proved;
  otherwise the emitter uses a local inverse branch plus an unconditional
  long edge. The former 505 KiB hosted assembly failure is pinned.
- x86 selection has one authoritative pre-append MIR effect table and
  materializes a pending compare before arithmetic, calls, stack-allocation
  expansions, atomic XADD/CMPXCHG, or inline asm with a `cc` clobber. Atomic
  AND/OR/XOR retry loops materialize before their synthetic block transition;
  an impossible cross-block EFLAGS provenance now ICEs. Direct-clobber and
  retry-loop tables pin the invariant. This repaired the `loop-3.c` O2/O3/Os
  miscompile and the related O3 `20000815-1.c` failure.
- Full hosted run
  `https://github.com/tenseleyFlow/Cgfried/actions/runs/31665602629` is green
  for x86 O0/O2, native ARM64 O0/O2, x86 `-j8` versus `-j1`
  reproducibility, raw 113-file cross-host ARM64 assembly identity, and
  same-toolchain objects, runtime archive, and compiler identity. The final
  90-day artifact is `sprint58-bootstrap-arm64-cross-final`; all manifests
  bind commit `c6bf3cf6a91f50dbd561afd9c1cecd19f8a72f83` with
  `normalization=none`. A fresh local download independently reverified all
  seven embedded provenance hashes, 113/113 assemblies, 113/113 objects, the
  runtime archive, and the compiler. The exact x86 required contexts also
  passed in push run `31665586963` and are configured on `trunk`.
- Full hosted run
  [`31762814206`](https://github.com/tenseleyFlow/Cgfried/actions/runs/31762814206)
  supplies the second distinct UTC date at exact head
  `e75ff34c6b03214281ed637c7bbcb38228e76496`. All seven jobs passed and the
  exact eight-artifact set is retained. A fresh audit matched every raw ZIP to
  its GitHub API SHA-256, verified all eight fixed-point stage manifests and
  seven embedded cross-provenance hashes, and byte-compared all 113 raw ARM64
  assembly files plus the complete 228-file same-toolchain final payload with
  `normalization=none`.
- Matching-head x86 push run
  [`31857187648`](https://github.com/tenseleyFlow/Cgfried/actions/runs/31857187648)
  and scheduled native ARM64 run
  [`31863013882`](https://github.com/tenseleyFlow/Cgfried/actions/runs/31863013882)
  supply the third consecutive UTC date at exact head
  `d9693498ef236d5088c45e3e7adb120593808eed`. All four applicable O0/O2 jobs
  passed. Their exact four artifacts match the GitHub API's retained sizes and
  raw SHA-256 digests; 1,356 run-manifest hashes, eight 228-entry stage
  manifests, and 912 stage1/stage2 payload comparisons verified with zero
  differences. ARM O2's int128 and binary128 ABI/value differentials passed.
  August 15 was Saturday, so the weekly cross-host and reproducibility lanes
  were correctly not due. Independent review approved the paired evidence.
- Matching-source hosted torture streams atomically regenerated the ratchet
  and triage report. At that hosted checkpoint the totals were 25,933 PASS,
  6,620 SKIP, and 8,097 classified failures across 40,650 cells; 90 buckets
  and 81 live policy decisions, with zero stale/unresolved decisions. Reversed
  stream order produced
  both committed outputs byte-identically. Final candidate run `31665586870`
  then passed the exact x86 ratchet plus the complete standard CI matrix,
  including ARM-QEMU/native, sanitizers, the 100k frontend fuzz lane,
  musl/QBE campaigns, toolchain, format, and policy gates. Hosted x86 run
  `31686587082` subsequently promoted 15 additional PASS cells with zero
  regressions; its retained matrix regenerates the 25,933-cell ratchet
  byte-identically.
- `.docs/audits/bootstrap-soak.md` is **RUNNING at 4/30**. The first streak
  started on August 13, included the complete Sunday activation on August 16,
  and reached 5/30 on August 17. It reset on August 18 at `9ec43d92`: x86 run
  [`32089117040`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32089117040)
  cancelled O0 during system-toolchain installation, skipped bootstrap,
  failed the evidence-manifest step, and retained no O0 artifact. ARM run
  [`32097369403`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32097369403)
  passed both lanes but cannot cure the missing daily x86 lane. Matching-head
  x86/ARM pairs on August 19 (`af914c89`, runs `32205833254`/`32214058949`),
  August 20 (`6460c3c2`, runs `32321924998`/`32330135984`), August 21
  (`db6f114a`, runs `32437193020`/`32445410094`), and August 22 (`9cdc3e87`,
  runs `32544159147`/`32550348530`) are current days 1–4. Continue recording
  distinct UTC dates and every due weekly cross/reproducibility result; any
  missing or red required run breaks the streak. Supplemental manual run
  [`32603828216`](https://github.com/tenseleyFlow/Cgfried/actions/runs/32603828216)
  passed all seven jobs and retained all eight artifacts at exact head
  `8fb99082`. A fresh download verified all seven embedded provenance hashes,
  both 113-entry assembly manifests, and all 228 corresponding payload files
  byte-for-byte. It is additional current-head evidence, not a fifth UTC date
  or a substitute for the scheduled Sunday run. The August 16–22 scheduled
  ZIP/internal hashes have not yet been independently recomputed, so do not
  overstate that separate evidence.

`ci/closed_sprints.txt` is 57. Sprint 58 remains operationally open during its
soak and therefore owns the next contiguous closure step.

---

## Parallel Sprint 57 compile-the-world campaign — COMPLETE

Integrated worktree and branch:
`/home/mfwolffe/GithubOrgs/tenseleyFlow/Cgfried` on `trunk`.

- All inputs are immutable and verified before use: musl
  `b306b16a`, libc-test `12343315`, Chibicc `90d1f7f1`, TinyCC `38059770`,
  and QBE `d62b154d`. Campaigns export pristine archives below
  `build/campaigns/`; no reference checkout is built in place.
- The musl hybrid builds route 1,254 non-complex C translation units through
  Cgfried, 68 explicitly deferred `_Complex` units through GCC, and 32
  assembler inputs through the host toolchain. Two clean builds reproduce all
  1,354 routed objects byte-for-byte. Static hello, all 21 Sprint 53 kernels,
  and libc-test parity against a separately GCC-built musl pass with zero
  Cgfried-only failures. Both libc-test lanes use the pinned musl GCC specs;
  retained link maps prove the exact staged `Scrt1.o` and sole staged
  `libc.a`, while ELF checks reject `PT_INTERP` and `DT_NEEDED`. Both lanes
  retain the same 29 upstream/environment failure records.
  `CAMP-MUSL-001` (`_Complex`) and `CAMP-MUSL-002` (shared/TLS) are the only
  published scope exclusions.
- Chibicc passes 41/41 programs in both the Cgfried and pristine host-GCC
  lanes. TinyCC passes 132/132 `tests2` and 24/24 `testspp` cases in both
  lanes. Native x86-64 QBE passes 32/32 cases in both lanes. On real ARM64,
  both independently built QBE lanes pass the same 28 cases and share four
  pinned-upstream exclusions: declared-skip `dark.ssa`, an unselected
  non-entry-block `alloc8` in `dynalloc.ssa`, and two vararg tests whose emitter
  violates AAPCS64 stack alignment. The exact gate requires zero Cgfried-only
  failures; QEMU is not used because it masks the two hardware SIGBUS faults.
- `ci/campaigns/FINDINGS.md` records 37 fixed compiler defects and eleven fixed
  campaign-integrity defects, each with a minimized regression or exact gate.
  Notable repairs include distinct `__func__` objects, aggregate override
  semantics, global definition emission after forward relocations, undefined
  weak/hidden ELF attributes, local aggregate template relocations, optimizer
  provenance preservation, inline-asm operand locality, soft-float carry, and
  absolute numeric-pointer member/index initializer folding.
- Exact bidirectional results contain 13 musl rows, seven Chibicc rows, ten
  TinyCC rows, seven x86 QBE rows, and eight ARM64 QBE rows. The expected-gate
  meta-test rejects missing, extra, reordered, duplicate, malformed, or
  changed rows.
- PR CI runs musl and native x86 QBE. The scheduled/manual workflow runs all
  four campaigns and QBE on real x86-64 and ARM64 runners, retaining configs,
  routes, logs, failure sets, and exact result artifacts.
- Fresh local validation is green: 695 unit tests / 4,284,201 assertions,
  full GCC and Clang strict builds, the complete repository suite, ShellCheck,
  campaign expected-gate meta-tests, and all four locally runnable campaign
  gates. Expected local skips remain only optional-tool/reference lanes and
  match their committed ledgers.
- The last matching-source-provenance x86-64 and real-ARM64 torture baseline
  preserved every committed PASS cell and added 985. A final stable-source
  x86-64 gate after the x87 special-value repair added five more x86-64 PASS
  cells whose ARM64 counterparts were already present: the Sprint 57 close
  ratchet contained exactly 25,910 PASS cells. Sprint 58's matching-source
  refresh above supersedes those totals. Reversed input order regenerated both
  Sprint 57 published artifacts byte-identically.

No Sprint 57 implementation work remains. Sprint 54's real three-date evidence
has closed the gap, so `ci/closed_sprints.txt` now includes Sprint 57.

---

## 0. Sprint 56 parallel campaign worktree — COMPLETE

Integrated campaign implementation:
`/home/mfwolffe/GithubOrgs/tenseleyFlow/Cgfried` on `trunk`.
The current Sprint 56.5 ledger/declarator refresh is isolated in
`/home/mfwolffe/GithubOrgs/tenseleyFlow/Cgfried-s56` on
`s56.5-ledger-refresh`.

- Imported byte-pristine gcc c-torture and c-testsuite corpora contain 2,016
  compile, 1,752 execute, 78 IEEE, and 219 c-testsuite cases.  Both import
  verification and deterministic fixture suites pass.
- Full O0/O1/O2/O3/Os matrices completed for `x86_64-linux-gnu` and
  `arm64-linux`: 20,325 cells per target, 40,650 total. After the Sprint 58
  bootstrap repairs, Sprint 61 remediation, and the Sprint 56.5 declarator
  and packed-bitfield tranches, outcome totals are 26,265 PASS, 6,620 SKIP,
  and 7,765 classified failures.
- The v2 streams share source/compiler/harness/manifest provenance.  The final
  harness hash is
  `b6e50c45f810d83e0b9e5b5adcc722f8ec2a5e2afdc98a0611386507b01a07b5`.
  Volatile GNU-ld identifiers and section offsets are normalized; 451 linker
  failures per target collapse into four semantic fingerprints.
- `.docs/audits/torture-triage.md` has 100% bucket coverage: 79 total buckets,
  70 durable overlay decisions, zero stale, zero unresolved, and no misc
  bucket. The overlay contains 46 `fix-sprint:s56.5-*`, 18 `out-of-scope`,
  and six `wontfix-0.1.0` decisions. No TORT XFAIL was minted.
- `tests/torture/passing.txt` is the exact sorted 26,265-cell PASS set. The
  `e941403d` refresh promoted 103 x86-64 and 98 arm64 cells with zero
  regressions; `3eb97e5c` then promoted all ten `pr43188.c` cells and retired
  the declarator-type-attributes bucket, again with zero regressions.
  `e227d4f1` implemented suffix attributes on bitfields plus packed-bitfield
  layout/lowering and promoted 110 cells (11 cases across both targets and all
  five optimization levels) with zero regressions. The
  arm64 stream uses the repository's cross binutils, QEMU, and
  `/usr/aarch64-linux-gnu/include`; an earlier host-header stream was
  quarantined and never published. Combined gating passes, and reversed input
  order regenerates both committed artifacts byte-identically.
- Fresh validation is green: `make torture-import-verify
  torture-import-meta torture-meta`, full `make test` (801 unit tests /
  4,293,770 assertions, 698 program fixtures, and every
  corpus/differential/fuzz/cross/policy gate), `make bootstrap-O0`, and `make
  bootstrap-O2`. Both bootstraps reproduce 113 assembly files, 113 objects,
  the runtime archive, and the compiler byte-identically with no normalization.
  Expected local skips are only optional-tool/platform lanes and match their
  committed ledgers.
- CI runs the complete x86 matrix on every PR and the native arm64 matrix on
  the scheduled runner.  Matrix publication and baseline refresh are atomic,
  target-complete, and provenance checked.

No original Sprint 56 campaign-infrastructure work remains. Sprint 61 repairs
and the Sprint 56.5 declarator and packed-bitfield tranches retired every
bucket they fixed; the remaining compiler debt is enumerated by the 46 live
`s56.5-*` policy decisions. Sprint 54 and Phase 11 subsequently closed on
their independent fleet evidence.

---

## 0a. Sprint 54 / Phase 11 closeout — COMPLETE

The implementation, three-date controlled evidence, current release report,
closure audit, and ratchet are complete. Pre-control artifacts remain
provenance-only. The absolute load limit was reopened for one evidence-backed
repair: idle Hasu and Nomad measured roughly 90% aggregate CPU idle while
ordinary desktop housekeeping held their one-minute load near 2.8.

### Implemented and verified

- The ten-row lattice in `doc/perf-gates.md` maps 1:1 to `ci/gates.d/`:
  controlled-host compile time (+30%), RSS (+20%), stripped program/compiler
  size (+15%), kernel instruction count (`max(2%, 2)`), kernel `.text` (+5%),
  MAD-guarded runtime (+10% and beyond four MADs), report-only internals, and
  explicit inactive Sprint 57/58 lanes.
- Shared x86 and native ARM CI measure RSS, sizes, and kernel static metrics;
  timing comparisons remain forbidden on shared runners. Trial gates record
  trips without hiding infrastructure status 3. The seeded boundary proofs,
  13/14-day state transition, escape-hatch policy, summary goldens, scoped
  multi-host release report, and 90-day trend classifier are fixture-pinned.
- Fleet-control-v2 records logical CPU count, one-minute load, and a fixed
  aggregate CPU-idle preflight. It accepts only normalized load
  (`load1 / logical_cpus`) at most 0.20 and CPU idle at least 85%, in addition
  to the platform power rules. Current artifacts require the complete v2
  tuple; partial, empty, malformed, or wrong-host evidence fails with status
  3. Complete v1 evidence remains controlled only under its original stricter
  `load1 <= 0.5` bound, preserving the immutable accepted Kasumi pair.
- Linux fleet controls model effective state, not a misleading raw governor:
  `performance` is accepted directly, while active `intel_pstate` is accepted
  as `power_profile=performance`, `governor=powersave`, and
  `energy_performance_preference=performance`. Artifacts that predate this
  complete power tuple remain provenance-only and cannot silently enter a
  timing gate.
- `make test-perf-gates`, the 29-case control helper, the 58-case compile
  gate, the 35-case runtime gate,
  kernel comparison tests, ShellCheck, the POSIX-shell check, formatting, and
  `git diff --check` all passed after the control-model fixes. Independent
  review approved the implementation and directly reproduced the two repaired
  no-baseline/empty-field failures.
- The first post-v2 shared-CI run exposed one integration regression:
  `bench.sh` tried to classify GitHub's ephemeral runner hostname as a fleet
  host. Recognized `ci`, `shared-ci`, and `arm64-ci` classes now take an
  explicit RSS-only producer path that requires `BENCH_SKIP_TIME=1`, omits
  fleet-only v2 provenance, and never invokes the fleet controller. The gate
  independently refuses those classes in every timing mode, including a
  fixture with forged controlled-looking Kasumi provenance; unknown classes
  still fail closed through the fleet controller.
- `scripts/bench-summary.sh`, `perf-report.sh`, and `bench-trend.sh` emit
  deterministic Markdown with signed deltas, named thresholds, host-scoped
  provenance, prior-report markers, and honest report-only classifications.
  `[bench skip]` and `perf-override: #NNN` remain narrow and audited.

### Controlled deployment evidence

- All three schedulers are installed with pushes enabled. Kasumi and Hasu use
  user-systemd pre/post hooks that enter the performance profile, verify the
  effective governor/driver/EPP tuple, and restore the saved profile even when
  the nightly command fails. Nomad uses LaunchAgent label
  `com.tenseleyflow.cgfried-fleet-perf` and records the Linux-only fields as
  `unavailable`.
- Kasumi's first controlled run is
  `2026-08-11T065745Z-kasumi` / `-kernels` in artifact commit `106efed5`:
  compile load 0.01, runtime load 0.35, performance profile,
  `intel_pstate`/`powersave`, performance EPP, clean tree, no trip. The
  artifacts compared provenance-only against the incompatible legacy
  baselines, then were accepted byte-for-byte in the separate reviewed
  baseline commit `b87a2789`.
- Hasu's first controlled fleet-control-v2 run is
  `2026-08-11T160603Z-hasu` / `-kernels` in artifact commit `1643af0d`:
  20 logical CPUs, compile load 0.89 at 97.54% idle, runtime load 0.98 at
  97.49% idle, performance-profile `intel_pstate`/`powersave` with performance
  EPP, clean tree, and no trip. The exact-copy compile/runtime baselines were
  accepted separately in `787b9cec`.
- Nomad's first controlled fleet-control-v2 run is
  `2026-08-11T161721Z-nomad-1` / `-kernels` in artifact commit `2e152c54`:
  18 logical CPUs, compile load 2.32 at 89.74% idle, runtime load 2.67 at
  85.25% idle, truthful Darwin-unavailable power fields, clean tree, and no
  trip. Two earlier LaunchAgent attempts failed closed with status 3 and wrote
  no artifacts because the generated PATH omitted `/usr/sbin`, hiding
  `sysctl`; `7e7986d3` repaired both scheduler templates and regression-tested
  `/usr/sbin:/sbin` preservation. The exact-copy Nomad baselines were accepted
  separately in `9c50e6d1`.
- The installed schedules added clean, no-trip second-date pairs for Kasumi
  (`2026-08-12T011528Z`, commit `6db164e0`) and Hasu
  (`2026-08-12T013538Z`, commit `8d5898bc`). Both carry the complete
  fleet-control-v2 tuple and preserve their host-specific power contract.
- Nomad's clean, no-trip second-date pair is `2026-08-12T055505Z` in commit
  `d5b99a5f`: compile load 3.34 at 87.66% idle and runtime load 2.87 at
  86.13% idle across 18 logical CPUs, with the truthful Darwin-unavailable
  power tuple.
- Kasumi's clean, no-trip third-date pair is `2026-08-13T011528Z` in commit
  `abea4d5a`, and Hasu's is `2026-08-13T013538Z` in commit `6d8f6fde`.
  Both carry complete fleet-control-v2 provenance plus the Sprint 58
  controlled bootstrap timing receipt. Nomad's clean, no-trip third-date pair
  is `2026-08-13T055503Z` in commit `881659ba`: compile load 3.03 at 87.53%
  idle and runtime load 3.50 at 86.43% idle across 18 logical CPUs, with the
  truthful Darwin-unavailable power tuple. This completes the required three
  distinct UTC dates on every fleet host.
- Controlled musl warmups from compiler revision `3ff951d0` were accepted as
  exact-copy Kasumi/Hasu baselines in `8e422f8e`. Post-baseline runs
  `2026-08-13T221954Z-{kasumi,hasu}-musl-full-build.txt` are v2-controlled,
  clean-tree, `trial-pass`, and pass the +30% wall / +20% RSS gate without
  mutating those baselines. Artifact commits are `7e0482b9` and `991b2bec`.
- `.benchmarks/report-0.0.1.md` was regenerated twice byte-identically from
  the current controlled fleet baselines/latest artifacts plus committed
  CI/static evidence. Its SHA-256 is
  `92f26a1f90b3ea5ae1414c58701ebac43cc4caaf410a51da90b7b630ad2cab9b`;
  every selected fleet input classifies controlled.
- Older Kasumi, Hasu, and Nomad artifacts collected before the new fields
  landed remain useful deployment provenance only. They are not controlled
  timing/runtime evidence and must not be relabelled or used to manufacture a
  three-night history.
- The shipping five-second preflight observed Hasu's 20 logical CPUs at 91.45%
  idle with load 1.01, and Nomad's 18 logical CPUs at 90.20% idle with load
  2.27. Linux derives idle from aggregate `/proc/stat` deltas with I/O wait
  treated as busy; Darwin uses the second `top -l 2 -s 5 -n 0` sample. Both
  hosts pass fleet-control-v2; no user workload needs to be stopped to chase
  the retired absolute threshold.

### Closure result

- The full local repository suite passes with 711 unit tests and 4,288,526
  assertions, all performance/campaign contracts, every differential and
  cross lane available locally, formatting, POSIX-shell checks, and the clean
  fuzz ledgers.
- The exact baseline revision `8e422f8e` passed hosted bootstrap at O0/O2 and
  the complete standard CI matrix. Raising the contiguous ratchet to 57
  exposes no stale source deferrals; both stage1 and musl lanes are active in
  trial state.
- Sprint 54 and Phase 11 are closed. Sprint 59 later closed out of order;
  resume Sprint 58's independent soak above.

### Checkout hygiene

`afs-as` and `afs-ld` may remain dirty from pre-existing submodule work, and
the many untracked `build-*` directories are local artifacts. Do not stage or
delete them as part of Sprint 54. `AGENTS.md`, `CLAUDE.md`, and
`.docs/sprints/` remain ignored local project memory; `.docs/HANDOFF.md` is
tracked.

---

## 0b. HISTORICAL D5 IMPLEMENTATION RECORD — superseded

Everything in this section records the state before Sprint 55 closed. Its
commands, blocker states, and counts are not current instructions; §0a wins.

### Former resume point — D5, `__GNUC__`

**THREE of D5's blockers are DONE and pushed. A FOURTH was found by
widening the probe, and it is the next task.** Sprint 55 is NOT closed.

| blocker | state |
|---|---|
| 1. `__builtin_bswap16/32/64` | **DONE** — `b8631a90` |
| 2. integer `mode(M)` | **DONE** — `faf0bbcd`; took the header probe from 1/19 clean to 18/19 |
| 3. `_Float128` | **DONE** — `de84d2e2`; `math.h` compiles, bit-identical to gcc at every level |
| 4. **GNU named variadic macros** | **NOT STARTED** ← next; fully measured below |
| 5. the `__GNUC__` predefine itself | LAST, after 4 |

### THE ONE THING TO UNDERSTAND FIRST

**Defining `__GNUC__` is a PROMISE, not a predefine.** glibc's
`sys/cdefs.h` gates dozens of declarations on it; today, with it
undefined, glibc *neutralizes* `__attribute__` itself, which is why every
attribute fixture in the tree is freestanding. **The day `__GNUC__` is
defined, every attribute in every system header goes live at once**, and
answering yes and then rejecting one of gcc 8's extensions is worse than
answering no. That is why the predefine goes in LAST.

### BLOCKER 4 — GNU named variadic macros. Everything below is measured.

`#define M(args...)` and `#define M(fmt, rest...)`. Blocks `netdb.h`,
`arpa/inet.h` and `sys/socket.h` — so, sockets.

**HOW IT WAS FOUND, and the lesson:** the handoff previously said "three
blockers, measured." That measurement covered **19** headers. Widening the
same probe to **30** found this one. *The probe's coverage bounds its
conclusion* — widen it again before believing four is the final number.

```sh
C=build-d5/cgfried
for h in stdio stdlib string ctype errno time math limits unistd fcntl \
         sys/types sys/stat sys/mman signal pthread setjmp inttypes stdint \
         locale assert wchar wctype dirent netdb arpa/inet sys/socket \
         sys/wait sys/time termios poll; do
  printf '#include <%s.h>\nint main(void){return 0;}\n' "$h" > one.c
  echo "$h -> $($C -std=gnu17 -D__GNUC__=8 -D__GNUC_MINOR__=3 \
      -D__GNUC_PATCHLEVEL__=0 -fsyntax-only one.c 2>&1 |
      grep -m1 'error:' | cut -c1-100)"
done
```
Currently **27 of 30 clean**; the three failures are all this feature.
Compile each header ALONE and take only its FIRST error — a dozen at once
gives ~40 mostly-cascade errors.

**The measured semantics (gcc 16, `-std=gnu17`):**

- The name **REPLACES** `__VA_ARGS__` rather than adding to it: inside
  `#define M(a...)`, using `__VA_ARGS__` is an ERROR ("can only appear in
  the expansion of a C99 variadic macro"). A parameter literally named
  `__VA_ARGS__` is the same error.
- In a plain `#define M(...)`, the identifier `args` is just an ordinary
  token — the name is per-macro, not global.
- `#define M(a...)` then `#define M(...)` is a **redefinition** warning, so
  the ISO redefinition compare must consider the variadic name.
- The named tail must be LAST: `M(a..., b)` is "expected ')' after '...'".
- Zero arguments works; `#` stringizes the tail (`S(3, 4)` → `"3, 4"`).
- `-pedantic` gives `-Wvariadic-macros`, "ISO C does not permit named
  variadic macros". **That registry id already exists**
  (`WARN_VARIADIC_MACROS`, `WG_PEDANTIC`/`WD_PEDANTIC`/`WL_PEDWARN`), so
  the pedwarn is free.
- Expansions to pin: `M(1,2)` → `f(1,2)`; `N(9,8,7)` with
  `#define N(x, rest...) g(x, rest)` → `g(9, 8,7)` (note the spacing).

**THE CODE SHAPE IS FAVOURABLE — it is one index, not a second mechanism.**
`find_param()` in `src/pp/macro.c:137` returns `m->nparams` for the
variadic slot, and *everything* keys off that: substitution, stringize, and
the `, ## __VA_ARGS__` comma swallow at ~line 631. So:

1. `MacroDef` (`src/pp/pp.h:238`) gains an interned `va_name`, NULL meaning
   `__VA_ARGS__`.
2. The parameter loop (`src/pp/macro.c:~200`, where the refusal lives now)
   accepts `IDENT ...`, sets `is_variadic` and records the name.
3. `find_param` matches `va_name` when set **instead of** `__VA_ARGS__`
   (measured: they are exclusive), and the `__VA_ARGS__`-outside-a-variadic
   error at line 281 needs the same treatment.
4. The redefinition compare must include `va_name`.

**The refusal names Sprint 55 itself**, so `check_deferrals` would flag it
at close regardless: a deferral to the CURRENT sprint must be resolved
before that sprint ends, in one direction or the other.

### D5 — what has NOT been measured yet

- **arm64-macos and FreeBSD have their own lists.** Run the same per-header
  first-error probe there. Apple's `sys/cdefs.h` uses `__attribute__` with
  NO `__GNUC__` gate at all, so its failure mode differs in kind.
- The DoD wants `__GNUC__` **8 / 3 / 0** in gnu modes and **absent** in
  `-std=c*` modes, both fixtured. The split is load-bearing.
- Expect the musl gates to MOVE. Both are exact pins; re-pin deliberately
  with a sentence of justification.

### WHAT IS NOT YET VERIFIED at `de84d2e2` — do this FIRST

The three blockers are implemented and each was proved against gcc
directly. What has NOT been re-run end to end since the final
comment-only reformat:

```sh
make BUILD=build-v-gcc   CC=gcc   test          # rc must be 0; FAIL count lies
make BUILD=build-v-clang CC=clang test
make test-a64-corpus && make test-a64-spill-all  # SEQUENTIALLY, never -j
scripts/musl_warn_dryrun.sh build-v-gcc/cgfried  # exact pinned numbers
make musl-sweep
```

The last full gcc run before that reformat was green with **zero FAIL
lines**; the only gate that fired was `check_bans`, tripped by the literal
`__attribute__` inside two of my own COMMENTS — now reworded, and
`check_bans`/`check_format`/`check_gnu_tiers`/`check_deferrals` are all
clean. So the expectation is green, but it is an expectation, not a run.

**Specifically unproven:** the arm64 lanes have not seen `_Float128` or
`mode` execute. `tests/corpus/x86_64/fp/gnu_float128.c` was written to run
there (its rank assertion branches on `__aarch64__` precisely because the
answer differs), and it cross-compiles clean, but it has not run under
qemu. The clang lane has not been run at all since D5 started.

### HOUSE RULES that a compaction will drop

- **Commit often, in chunks.** Terse imperative subject, under ~250 chars
  unless the body earns more. **Never co-author. No "Generated with" or
  session trailers.** Tests and CI are first-class.
- **`AGENTS.md`, `CLAUDE.md` and `.docs/sprints/` are GITIGNORED and must
  NEVER be committed.** `git add` refuses `.docs/sprints` and helpfully
  suggests `-f` — **do not take that hint.** Sprint-file corrections stay
  local and go in the COMMIT MESSAGE instead. `AGENTS.md` is the reality
  snapshot and syncs to `CLAUDE.md` with `cp AGENTS.md CLAUDE.md`.
- `.docs/audits/*.md` and `.docs/HANDOFF.md` ARE tracked, via explicit
  gitignore negations.
- The shell here is **fish** for the user, and the assistant's `Bash` tool
  runs **zsh**, which does **not word-split an unquoted expansion** — `cgf
  $flags file` passes ONE argument. Use `${=flags}`. This silently made every
  fixture answer "unrecognized command-line option", which then read as "the
  fixture is vacuous".
- Task list: **#125 is D5**, and carries the same measurements as §0a. #123
  (switch label out-of-range check) and #112 (`.bss` for an explicitly
  zero-initialized global) are open pre-existing finds, unrelated to D5.

### THE NUMBERS AS OF THIS HANDOFF — what "unchanged" looks like

Green on `trunk` at `de84d2e2` under gcc, EXCEPT that the full chain has
not been re-run since the last comment-only reformat -- see "WHAT IS NOT
YET VERIFIED" below. If one of these MOVES after a change, that is the
signal; re-pin deliberately and say why.

| gate | value |
|---|---|
| unit tests | 628 tests / 4,263,151 assertions, 0 failures |
| x86_64 fixtures (`tests/corpus` + `tests/programs`) | **672 / 672** |
| arm64 corpus, and under `CGF_SPILL_ALL=1` | **76 / 76** each, both ledgers EMPTY |
| tier table (`check_gnu_tiers.sh`) | **26 implemented / 6 parsed-ignored / 8 refused** |
| musl warning sweep | **1259 / 1361** parsed, 102 deferred, 414 oracle-matched, **zero false positives** |
| musl memory sweep | **1276 / 1361** analyzed, 85 pinned deferrals, **zero `-Wmem` diagnostics** |
| ISA driver (`s36_isa_driver.sh`) | 95 corpus files / 570 object checks |
| fuzz digest | `fe3cdda07126f524` (at `--iters 5000`) |
| sanitized 100k | 0 findings; `tests/fuzz/crashes/` holds only `README.md` |
| gcc / clang `make test` | rc=0 both |

**Expect the musl numbers to MOVE on D5** — that is the point of the
deliverable. Both are exact pins; changing them is a deliberate act with a
sentence of justification, not a silent re-run.

### WHERE THE CODE IS, for D5's three items

The attribute machinery is one table plus one record plus three functions
that must each enumerate every field. Adding an attribute means touching all
of these — and **`gnu_attrs_merge` and `gnu_attrs_any_symbol_property` sit
next to each other on purpose**, because forgetting one means forgetting the
other in the same glance (that omission is how seven attributes were once
silently dropped on function parameters).

| what | where |
|---|---|
| attribute classification (`GA_IMPLEMENTED`/`GA_IGNORE`/`GA_UNSAFE`/`GA_UNKNOWN`) | `src/parse/gnu_attrs.def` |
| the parsed record | `GnuDeclAttrs` in `src/attr.h` |
| merge across declarations | `gnu_attrs_merge`, `src/parse/attr.c` |
| "is this a symbol property?" | `gnu_attrs_any_symbol_property`, same file |
| per-attribute parse handlers + dispatch | `src/parse/attr.c` (`parse_*_attr`, then the `GA_IMPLEMENTED` switch) |
| `__x__` spelling normalizer | `gnu_attr_norm_name`, `src/parse/attr.c` — shared by the classifier and the `format` archetype |
| carried onto the Symbol | `carry_symbol_attrs` + `gnu_attrs_merge(&sym->gnu, &d->gnu)`, `src/sema/decl.c`. **`Symbol.gnu` is a whole `GnuDeclAttrs`**, so a new field rides along free |
| builtin table | `src/builtins.def` — no accept-anything fallback by design |
| builtin lowering | `src/lower/expr.c`, beside `SEMA_BUILTIN_EXPECT` |
| the predefine table | `pp_predefine_all` (grep for the policy comment saying NO `__GNUC__`) |
| binary128 today | `src/util/softfp.c`, `src/lower/f128.c`, `src/rt/` (`__*tf3`) |
| tier table + its gate | `docs/gnu-extensions.md`, `scripts/check_gnu_tiers.sh` |

**Fixture placement decides which bugs a test can see.** `tests/corpus` runs
on x86_64, under `CGF_SPILL_ALL=1`, AND under both arm64 lanes; it is outside
`FE_FUZZ_CORPUS`, so it costs no digest repin. `tests/programs` runs under
none of those and costs a 100k. Executable fixture → corpus; compile-failure
or warning-count fixture → programs.

**Every fixture needs its SILENT half.** Most of these checks could fire on
everything and still pass a firing-only test. D3's five each carry one: an
undeprecated sibling member and enumerator, a call in a condition, a variadic
function with no attribute and no table row, a null in an unlisted position,
a callee that really can fall through.

### Sprint 55 against its own Definition of Done

| DoD | state |
|---|---|
| 1. tier table, every row fixtured, CI-gated | **met** — 23/6/8, `check_gnu_tiers.sh` |
| 2. musl `syscall_arch.h` + `src/internal/` compile clean | **met** — verified directly |
| 3. extended asm O0–Os both targets + early-clobber execute fixture | **met** |
| 4. **16 attributes** with semantics tests + packed differential | **met** — 16 of 16 |
| 5. `__GNUC__=8` in gnu17, absent in c17, both fixtured | **THE ONLY OPEN ONE** ← D5 |
| 6. deferred constructs hard-error naming the sprint | **met** |
| 7. zero new warnings building cgf itself; bootstrap lanes green | **met** |

### BACKGROUND: what D4 and D3 closed, and what they found

Read these only if you need the reasoning behind a decision. §0a above is the
live work.

### D4 — what is DONE

- statement expressions `({ ... })` — `56a94d14`
- `typeof` / `__typeof__` / `__typeof` + `__auto_type` — `2245382c`
- `__builtin_types_compatible_p` / `__builtin_choose_expr` — `4d433873`
- `__thread`, `__extension__` — `tests/corpus/x86_64/int/gnu_thread_extension.c`.
  `__thread` is a pure ALIAS: it produces the same `AST_SC_THREAD_LOCAL` as
  `_Thread_local`, proved by diffing the emitted assembly for both spellings
  (**byte-identical**). `__extension__` is SWALLOWED at the top of
  `parse_unary_expr`; the declaration position has worked since Sprint 9.
  **The fixture needs `// ENV: CGF_AS=0`** — the bundled assembler has
  neither `%fs:` nor `@tpoff` on x86 (TLS-004) and the driver refuses by
  name. That line is INERT on arm64, where the lane sets `CGF_AS_PATH` and
  an explicit path beats a mode, so the fixture still executes through the
  bundled assembler there. Its absence is what made the first full run come
  back 89/90 under both compilers with arm64 clean — an asymmetry that reads
  like an x86 codegen bug and is really assembler routing.
- `[0]` arrays and `__builtin_constant_p` — **already worked**, found by a
  13-line survey before any planning. PROBE THE LIST BEFORE PLANNING IT.

### D4 IS CLOSED. What the last four items cost, and what they found

- **case ranges** `case lo ... hi:` — `fbf381d7`. Ranges do NOT enter the IR
  switch value table (`case 0 ... 1000000` would be a million entries); each
  becomes one bounds test, `d = scrut - lo` then unsigned `d <= hi - lo`.
  The wrap is what makes a negative range work.
- **`a ?: b`** — `2b669d91`, plus the two `-pedantic` pedwarns gcc emits for
  it and for case ranges, plus the `__extension__` suppression they require.
- **`__label__` and empty structs — REFUSED** (`f5ce6442`), with the reason
  MEASURED rather than argued. See below; this is the interesting part.
- **`__builtin_offsetof` array designators and `,##__VA_ARGS__` ALREADY
  WORKED.** Found by a survey before any planning — the same lesson `[0]`
  arrays taught. PROBE THE LIST BEFORE PLANNING IT; it is now 4 for 4.

**THE THREE THINGS WORTH CARRYING FORWARD:**

1. **Implementing case ranges found THREE pre-existing gaps in switch label
   checking, two of them REQUIRED diagnostics** (task #123). `case 1:` twice
   and `default:` twice were both accepted silently, picking the first. They
   land with ranges because overlap between two ranges, between a range and
   a plain label, and between two plain labels are the same question about
   the same intervals — one list answers all three. The out-of-range case
   label (`-Wswitch-outside-range`) is still open.
2. **THE ENUMERATION HAZARD, again, in the lowering.** Three loops had to
   agree on which cases are table entries; I taught the sizing loop about
   ranges and not the filling loop, so the array was sized for the singles
   and written with all of them. The overflow corrupted the arena and IR
   verify reported a branch to block id 3894. One predicate now.
3. **A GATE I ADDED WAS VACUOUS, and fixing it found an older one.**
   `check_gnu_tiers`'s refused-row check grepped src/ for a token, and the
   COMMENT explaining a refusal contains the same words — so mutating the
   message away left the gate green. Tightening it to require the token in a
   STRING LITERAL exposed that the `mode` row had been vacuous since it was
   written: its token `mode(` never appears in that refusal at all. All
   seven rows are mutation-verified now.

**A LANE-CONTENTION TRAP THAT LOOKS LIKE A COMPILER BUG.** The sanitized
100k came back with EIGHT "hang (spawn timeout)" findings, which the harness
wrote into `tests/fuzz/crashes/` where their presence fails the build. Not
one was real: every case compiles in under 15ms on both the plain and the
sanitized compiler, four of the eight are EMPTY FILES, and the seeds run in
consecutive runs (35990-35993). The cause was mine -- I ran the two musl
sweeps concurrently with the fuzz run, which is the rule this file already
states. **A fuzz finding whose only symptom is a TIMEOUT is a claim about
the machine as much as about the compiler**: reproduce it standalone before
believing it, and check whether the input is even non-empty.

**REFUSING IS A RESULT.** Both refusals rest on measurement, and the empty-
struct one is the sharper: gcc gives them size zero, and `struct E arr[3]`
then has `&arr[0] == &arr[1]`. So the extension does not add a size, it
breaks the distinct-address property the shared alias service and the
memory-safety lattice are both built on. Demand was measured first — musl 0,
glibc's C headers 0 (every `/usr/include` hit is C++), Linux uapi 1 inside
`__DECLARE_FLEX_ARRAY`. Its old message named Sprint 55, the sprint that
examined and declined it, so `check_deferrals` would have flagged it at
close: **a deferral to the CURRENT sprint must be resolved before it ends,
in one direction or the other.**

### The historical D4 list, with the semantics that were measured

**Do not re-derive these. They are gcc-measured and some are
counterintuitive.**

- **case ranges** `case 1 ... 3:` — inclusive at BOTH ends. A REVERSED range
  (`3 ... 1`) is a gcc **warning** ("empty range specified"), not an error.
  A single-value range (`1 ... 1`) is fine.
- **`a ?: b`** — the left operand is evaluated **exactly once**, on both the
  true and false paths (measured with a call counter both ways). The refusal
  is at `src/parse/expr.c` in the `PUNCT_QUESTION` case.
- **empty struct / union** — `sizeof` is **0**. ⚠ **READ THE EXISTING
  COMMENT AT `src/sema/decl.c` (`any_named`) BEFORE IMPLEMENTING.** Whoever
  wrote it refused zero sizes deliberately: they break the "distinct objects
  have distinct addresses" property later passes assume. This is the one
  remaining D4 item with a documented hazard — check what layout, alias
  analysis and the memory-safety engine do with a zero-extent object rather
  than assuming gcc's acceptance settles it.
- **`__label__`** — local labels; parser + scope work.
- **`__builtin_offsetof` array designators** — separate, smaller.
- **`,##__VA_ARGS__`** comma-swallow — a Sprint 5 deferral, and the only
  item that is PREPROCESSOR work. The sprint file corrects itself here: gcc
  accepts it in ALL modes, so match gcc.

### D3 IS CLOSED — 16 of 16

`deprecated`, `warn_unused_result` and `format` landed in `87e4bd39`, all
byte-identical to gcc 16. They were chosen together because each is a
DIAGNOSTIC with no codegen consequence and each already had a registry
warning id waiting — the infrastructure was built, only the wiring missing.

- **`deprecated`** fires at the USE and never at the declaration; the
  DEFINITION of a deprecated function is silent. Six positions.
- **`warn_unused_result`**: `(void)must()` STILL WARNS, the opposite of
  `-Wunused-value` where the cast IS the acknowledgement. The check looks
  THROUGH a cast to void; that asymmetry is why it cannot be folded in.
- **`format`** wires Sprint 39's whole checker to user functions, taking
  precedence over BOTH the builtin name table and `-ffreestanding`.

**A CORRECTION I HAD ALREADY WRITTEN DOWN BEFORE MEASURING PROPERLY.** I
claimed in a source comment that gcc does not support `deprecated` on an
ENUMERATOR in C. It does. My probe put the attribute BEFORE the enumerator
name, where gcc rejects it outright ("expected identifier before
`__attribute__`"), and I had grepped only for `warning:` — so the ERROR was
invisible and its absence read as "gcc is silent here". **A filtered slice
of a compiler's output is not a measurement.** The legal position is AFTER
the name, and our parser did not accept attributes there at all.

`nonnull` and `noreturn` followed in `f1af918f`, and each had one fact worth
keeping:

- **bare `nonnull` is not "no positions", it is EVERY POINTER PARAMETER**,
  so the two forms carry separate state — a zero mask would be
  indistinguishable from "all". And `(int *)0` is NOT a null pointer
  constant by 6.3.2.3p3, yet gcc warns for it, so the check strips an
  explicit pointer cast while `conv_is_npc` stays strict for its other
  caller (which decides the TYPE of a conditional and needs the narrow
  rule).
- **`noreturn`'s value is FALSE POSITIVES REMOVED**, not a diagnostic
  added: a function ending in a call that never returns drew "control
  reaches end of non-void function", and a variable set on every surviving
  path drew "may be used uninitialized". It joins C11 `_Noreturn` and the
  library-name list at ONE decision rather than becoming a second
  mechanism.

`malloc`, `pure` and `const` are deliberately NOT implemented: they are
optimization licenses where a user's lie becomes a miscompile, which is a
different risk class from a missed diagnostic. They stay parsed-ignored.

### Then, in order

1. **D5's blocker 4** (GNU named variadic macros), then the `__GNUC__`
   predefine itself, then re-probe the headers on arm64-macos and FreeBSD.
   Only after every blocker does the predefine go in, because until then
   defining it makes hosted compilation WORSE than it is today.
2. **Then Sprint 55 closes**, and its DoD gate 5 with it.
3. **Sprints 52, 53, 54** — compile speed, codegen quality, perf gates.
   All three are performance work, which is why they were safe to defer.
   Sprint 53 also inherits a MEASURED item from D5: a runtime
   `__builtin_bswap64` is 64 x86 instructions where gcc emits `bswap`,
   because there is no IR opcode for it (see the comment at its case in
   `src/lower/expr.c` for why adding one is a 17-file change).

### THE VERIFICATION RITUAL, and the one lever that saves an hour

Before anything lands: gcc `make test`, clang `make test`, BOTH arm64 lanes
(`test-a64-corpus`, `test-a64-spill-all`), and — whenever the fuzz digest is
repinned — a sanitized 100k (`ASAN_OPTIONS=detect_leaks=0`, and check
`nm -D` for `__asan_init` on BOTH binaries first). For anything touching
warnings or analysis, also `scripts/musl_warn_dryrun.sh <cgfried>` and
`make musl-sweep`; both print exact pinned numbers.

The exact commands, since three of them have a gotcha:

```sh
make BUILD=build-v-gcc   CC=gcc   test          # rc must be 0; FAIL count lies
make BUILD=build-v-clang CC=clang test
make test-a64-corpus && make test-a64-spill-all  # SEQUENTIALLY, never -j together
# sanitized tree: NOT `SAN=1` -- that is not a thing. This is:
make BUILD=build-san-d4 \
  EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
  build-san-d4/cgfried build-san-d4/fuzz_frontend
ASAN_OPTIONS=detect_leaks=0 ./build-san-d4/fuzz_frontend --iters 100000 \
  ./build-san-d4/cgfried tests/fixtures tests/programs
# the digest gate runs --iters 5000, NOT 2000. Repin with:
./build-d4/fuzz_frontend --hash --iters 5000 ./build-d4/cgfried \
  tests/fixtures tests/programs | awk '{print $4}' > ci/fuzz_sequence_digest.txt
```

**`make test` returning rc != 0 with ZERO `FAIL` lines is NORMAL** — it means
a GATE failed, and gates print their own message. `grep -E "^make.*Error"`
the log and read upward. The ones that have caught me: `check_bans` (it greps
the WHOLE file, so the literal `__attribute__` in a COMMENT trips it),
`check_format`, `check_warn_matrix` (every new registry row must also land in
`.docs/warnings-matrix.md` WITH a fixture), and the fuzz digest.

**`tests/corpus` IS FREE; `tests/programs` COSTS ~20 MINUTES.**
`FE_FUZZ_CORPUS` is `tests/fixtures tests/programs`, so ANY add/delete there
moves the mutation digest and obligates the full 100k. The type-query
builtins alone cost THREE of those. An EXECUTABLE fixture belongs in
`tests/corpus` — it runs on x86_64, under `CGF_SPILL_ALL=1`, and under both
arm64 lanes, and costs nothing. Only compile-FAILURE fixtures need
`tests/programs`. **Batch a feature's fixtures into ONE verification pass.**

Run the suites ONE AT A TIME (two at once on this box produce failures that
do not reproduce), and never edit `src/` while a chain is in flight — it
rebuilds from source. **This is not advisory.** Running the two musl sweeps
alongside a 100k produced EIGHT "hang (spawn timeout)" findings, which the
harness wrote into `tests/fuzz/crashes/` where their presence fails the
build. Not one was real: every case compiled in under 15ms on both the plain
and the sanitized compiler, and four of the eight were EMPTY FILES. **A fuzz
finding whose only symptom is a TIMEOUT is a claim about the machine at least
as much as the compiler** — reproduce it standalone before believing it.

Build trees: pass `BUILD=` a RELATIVE path. An absolute one poisons the tree
(the debug lane concatenates BUILD with the cwd, `.d` files stop matching,
and header changes stop triggering rebuilds — the symptom was 55 clang unit
failures reading `no tag S` that vanished on a clean tree).

### WHAT KEEPS BITING, in the order it bit

1. **THE ENUMERATION HAZARD.** A list that must name every case forgets one.
   Six sites built an `ATY_BASE` from a specifier soup and I patched one —
   `typeof(int) b = 1;` resolved to TY_ERROR while `sizeof(typeof(a))`
   worked. Fix is always ONE helper, never N+1 lines: `soup_fill_identity`,
   `ir_arg_carry_provenance`, `gnu_attrs_any_symbol_property`. Same shape
   found `__volatile` missing from two asm-qualifier loops while its sibling
   `__inline` was present — that one gated every musl TU with atomics.
2. **A CHECK THAT CANNOT TELL "VERIFIED" FROM "NEVER RAN."** A mutation
   whose injected bug did not compile reported PASS. `grep -c FAIL` called
   three build/gate failures clean. A loop label printed `-O2` while never
   passing `$O`. **zsh does not word-split** an unquoted expansion, so
   `cgf $flags file` sent one argument and every fixture answered
   "unrecognized option" — read as "silent, therefore vacuous". READ THE
   OUTPUT, NOT A COUNT OF IT. **PROVE THE BINARY IS THE ONE YOU CHANGED** —
   compare its mtime to `date`; that caught two stale-binary reads.
3. **MEASURE gcc BEFORE WRITING CODE.** It has overruled the sprint file
   FOUR times this sprint, most recently on `__builtin_choose_expr` — the
   file said the unselected arm is "untype-checked beyond parse" and gcc
   type-checks it. Implementing as written would accept code gcc rejects,
   and no good-faith fixture would catch it because every natural test puts
   a VALID expression in the dead arm.
4. **A PLAUSIBLE CALL THAT DOES NOTHING.** `type_qualify(t, 0)` returns the
   type UNCHANGED when asked for zero qualifiers — it adds, it does not set.
   `types_compatible_p(const int, int)` answered 0 until `conv_strip_quals`
   replaced it. Likewise a first dead-branch rule tested the statement
   BEFORE a case label when the scanner had already descended past it: it
   compiled, passed everything, and never fired.
5. **AN OBSOLETE REFUSAL ASSERTION PER FEATURE.** SEVEN so far, and they
   come in pairs — a `tests/programs` fixture AND a unit assertion in
   `tests/unit/test_expr.c`, which fails on a DIFFERENT run than the fixture
   does. Most were CONVERTED to pin a boundary that still errors (file-scope
   `({...})`; `typeof` in ISO mode; `a ?: b`'s `-pedantic` pedwarn); one was
   deleted because nothing remained. The gate tells you a boundary MOVED;
   deciding whether one REMAINS is judgment. **Expect two per D5 feature.**
6. **A FILTERED SLICE OF gcc's OUTPUT IS NOT A MEASUREMENT.** I grepped for
   `warning:` while probing `deprecated` on an enumerator, concluded gcc does
   not support it in C, and wrote that into a source comment. gcc had emitted
   an *error* — my probe put the attribute BEFORE the enumerator name, which
   gcc rejects outright — and the filter hid it. Show errors AND warnings,
   and check the exit code. **Probe the LEGAL position before concluding
   about a feature**; attribute placement is load-bearing in C.
7. **A GATE YOU ADD CAN BE VACUOUS, AND FIXING IT MAY FIND AN OLDER ONE.**
   `check_gnu_tiers`'s refused-row check grepped `src/` for a token, and the
   COMMENT explaining a refusal contains the same words — so mutating the
   diagnostic away left it green. Requiring the token in a STRING LITERAL
   exposed that the `mode` row had been vacuous since it was written: its
   token `mode(` never appears in that refusal at all. **Mutate every gate
   you write, in both directions.**
8. **READ THE RULE, DO NOT COPY ITS NEIGHBOUR.** I repinned the fuzz digest
   at `--iters 2000` because that is what the line above it in the Makefile
   used; the gate runs 5000. Two repins agreed with each other and neither
   agreed with `make test`.
9. **`.docs/sprints/` IS GITIGNORED AND MUST NEVER BE COMMITTED.** `git add`
   refuses it and suggests `-f`. Do not take that hint. Sprint-file
   corrections stay local; record them in the commit message instead.

---

## 0b. D2, EXTENDED ASM — DONE. What it cost, and what it found.

**`cgf` compiles inline asm with operands, constraints and clobbers on both
targets.** The interesting part of this section is not the feature: it is that
the operand slice turned up **three bugs in code that was not the operand
slice**, and that the one bug IN it was invisible to every lane but one.

### What works

Extended asm with operands, on BOTH targets, matching gcc at -O0/-O1/-O2/-Os:

- x86_64 constraint matrix `4 102 77 9 10 6 11 7` — `r`, `i`, `m`, `+r`,
  width modifiers `%b0/%w0/%k0/%q0`, symbolic `%[name]`, `%%`, fixed
  registers (`a b c d S D`), tied operands, named clobbers.
- **musl's syscall shape executes**: `"=a"(r)` tied to `"0"(n)` with `D`/`S`/
  `d` inputs and `rcx`/`r11`/`memory` clobbers, making a real `write(2)`.
- arm64 the same matrix, verified against `aarch64-linux-gnu-gcc` under qemu.

### THE BUG IN IT: an asm operand cannot be spilled, and only one lane looks

**`CGF_SPILL_ALL=1` was wrong on BOTH targets.** Every spilled asm operand
reloaded into the same scratch register:

```
	movq	-32(%rbp), %r11      <- input a reloaded into r11
	movq	-40(%rbp), %r11      <- input b reloaded into r11, clobbering a
#APP
	movl %r11d, %r11d            <- all three operands are one register
```

x86 printed `2 8` where `11 7` is right.

**The fix is an ALLOCATOR FEATURE, not a patch at the reload site**, and that
distinction is the whole lesson. An operand constrained `"r"` must BE in a
register; x86 has exactly two scratches to reload through, so three register
operands cannot be satisfied by reloading AT ALL. No cleverness at the reload
site can produce a register that does not exist. `CgInterval.no_spill`
(`src/cg/shared.h`) says so, and `cg_linear_scan` honours it in three places
that all had to agree: `spill_all` skips such an interval, eviction refuses to
pick one as a victim, and the spill-vs-evict heuristic inverts for it —
someone else goes, whatever the relative lengths. Both backends set it from a
`collect_no_spill` pass over their own `ASM` instructions.

If the pool ever genuinely runs out there is a backstop ICE naming inline-asm
register pressure. gcc reports that case as *"'asm' operand has impossible
constraints"* and we cannot, because the allocator holds no diagnostic context
and no span; reaching it needs more simultaneously-live register operands than
the machine has allocatable registers.

**Mutation-verified**: putting the `spill_all` guard back reproduces
`4 102 77 9 10 6 2 8` exactly — the original wrong answer, not merely *a*
failure.

### A CLAIM I HAD DOCUMENTED WAS FALSE. Both copies are now corrected.

I wrote that early clobber is IMPLIED by this allocator, reasoning that
`cg_intervals_build` extends a def and a use at one instruction point to that
same point, so an output can never share a register with an input. **That is
true of ordinary allocation and was FALSE in the spill path**, where operands
were reloaded into a small fixed scratch set and genuinely collided. It is
true again now, but for a reason that had to be BUILT rather than one that
came for free — `no_spill` keeps asm operands out of that path entirely. The
corrected wording is in `tests/corpus/x86_64/int/asm_operands.c` and the
`case IR_ASM:` comment in `src/cg/x86_64/isel.c`.

**The fixture MOVED to `tests/corpus/x86_64/int/` as part of the fix**, and
that is the durable half. `tests/corpus` is run under `CGF_SPILL_ALL=1` on
x86_64 AND under both arm64 lanes, so the fixture now executes in three places
that spill everything. In `tests/programs` it executed in none of them, which
is exactly why a bug on both targets survived a green suite.

### CI CAUGHT A FIFTH BUG THE LOCAL SUITE DID NOT, and it was two bugs

**`memsafe-musl-zero-fp` failed on the first push** with an ICE
(`lower_irtype on non-scalar type kind 17`, which is `TY_ARRAY`) compiling
musl's `src/network/res_msend.c`. **The local gap was mine: there are TWO
musl lanes and I ran only one.** `test-musl-warnings` and the memory sweep
(`musl_mem_warn.sh`, the `musl-sweep` target) compile different TU sets, and
a green warning sweep says nothing about the memory one.

A STACK TRACE found it in one step where reading would have taken many --
`break cgf_ice; run; bt` showed `lower_local_init` calling itself, which is
the compound-literal path. The trigger is musl's

```c
.msg_iov = (struct iovec [2]){
    { .iov_base = (u8[]){ ql>>8, ql }, .iov_len = 2 },
```

and it turned out to be **two pre-existing bugs stacked, both reachable from
ordinary C with no extension of any kind**:

1. **The ICE.** An array-typed operand reached `lower_scalar_convert`. The
   array-to-pointer decay rule existed ONLY at `AST_EXPR_CAST` -- fine for a
   decay sema materialized as a cast node, useless for one that arrives
   without it, which an initializer element does. The rule now lives in
   `lower_scalar_convert` itself, so every caller inherits it instead of the
   two that happened to be known.
2. **A SILENT WRONG ANSWER THE ICE WAS HIDING.** With the crash fixed the
   fixture printed 183, then 167 -- varying per run, i.e. uninitialized
   stack. `(u8[]){a,b}` **never had its bound deduced from its
   initializer**: `sizeof((int[]){1,2})` reported "incomplete type", and a
   NESTED one allocated too small and stored only its first element.
   `array_size_from_init` had existed for declarations since Sprint 9 and the
   compound-literal path never called it; `sema_array_complete_from_init` is
   now the one rule with two callers.

**Third time in this project that an honest ICE sat on top of a silent
miscompile** (after the VLA reckoning and `_Alignas`). Fixing the crash is
where the search STARTS.

The payoff is large: the musl memory sweep went **733 -> 1084 of 1361 TUs
analyzed**, deferrals 628 -> 277, still zero diagnostics; the warning sweep
gained one TU and three sign-compare warnings, all oracle-matched.

**AND THE PROJECT'S OWN RULE CAUGHT ME.** I wrote the regression fixture's
`CHECK` line from prediction -- `4` where gcc prints `4660`. "Every corpus
expectation gcc-verified before pinning" exists for exactly that, and without
running gcc first I would have pinned a wrong expectation into the corpus.

### AND A SIXTH, FOUND BY PROBING THE FIFTH'S EDGES

Checking what else the bound-deduction fix might touch turned up that **a
file-scope compound literal could not be a static initializer at all**, in
any form -- gcc accepts every one:

```c
static struct S val   = (struct S){1, 2};     /* aggregate value  */
static struct S *addr = &(struct S){3, 4};    /* worked already   */
static int *sized     = (int[3]){5, 6, 7};    /* array, decays    */
static int *unsized   = (int[]){8, 9};
```

Pre-existing, independent of the deduction work (it reproduces on the
pre-change compiler and for the SIZED form too), and **two bugs wearing one
symptom**, which is why fixing the diagnosed half would have left the
complaint standing:

- the ARRAY forms decay to an address, and `constexpr`'s `eval` had NO
  compound-literal case at all, so only the explicit `&(T){...}` route ever
  reached the address-constant code;
- the AGGREGATE form is an IMAGE rather than an address, and the
  static-initializer check in `declare_one` validated it as a SCALAR
  constant -- asking `eval` for a value the literal does not have.

**The audit is the part worth keeping.** I first wrote this up as "a rule on
the explicit path, missing on the implicit-decay path", generalizing from the
`lower_scalar_convert` bug. That was WRONG and measuring disproved it: an
array VARIABLE and a string literal both come through implicit decay
correctly at file scope. The gap was one missing case, not a class, and a
grep finds only four decay-aware sites in the tree. **A pattern inferred from
two instances is a hypothesis; check before telling anyone to go hunting.**

The carve-out is deliberately narrow and pinned by negatives: `= f()`,
`= other_struct`, an automatic-storage literal, an incompatible literal type,
and a pointer to an automatic array all still reject, matching gcc's verdict
on every one.

### THE THREE BUGS IT FOUND ELSEWHERE — none of them in the asm code

**An asm ESCAPES the pointers it is handed, and the memory analysis did not
know it.** `char *p = malloc(16); asm("" :: "r"(p));` was diagnosed
**-Wmem-leak** — a false positive on the shape every libc's `arch/` directory
uses, in a checker whose whole contract is proof-only. A template is opaque
exactly as an unknown call is, and both directions count: an input can be
published, and an output operand is an ADDRESS the template writes through.

**TWO places enumerate instructions here and I fixed the wrong one first.**
`alias.c`'s `mark_escapes` was missing `IR_ASM` and is the one that LOOKS
responsible; adding it changed nothing, because the leak check reads the
lifetime lattice in `memsafe/lifetime.c`, whose own `process_inst` switch had
no `IR_ASM` case either. Both rows are in now, and the alias one carries a
comment saying no fixture reaches it — it stays because `alias_escapes` has
other clients and escaping MORE is the safe direction, not because anything
proves it necessary. **I had already written the fix up as the alias row
before running the suite**; the suite is what corrected it.

**The memory-BARRIER rows do not cover this either, and that distinction is
worth keeping.** `dce`, `dse`, `gvn`, `licm`, `dep` and `vectorize` all gained
an `IR_ASM` row while the operand slice was being written; every one of them
orders accesses. Ownership is a different question, and a pass can be
perfectly ordered while still believing it owns the block. Six correct barrier
rows did not add up to one escape row.

*(The other pass that must name `IR_ASM` and does not need to: the INLINER
copies `in->callee` verbatim, which for an asm is an index into the MODULE's
asm table, so it stays valid across inlining. Checked rather than assumed —
the same field means a function index on a call.)*

**Three warning-engine false positives**, all pre-existing, all made
REACHABLE by extended asm parsing 350 more musl TUs, each measured against
**gcc 8 in the `gcc:8` container AND gcc 16** before a line was written
(`src/sema/decl.c`, `src/sema/warn_expr.c`) — and each now carrying a fixture,
because the musl gate is an aggregate and an aggregate cannot say WHICH rule
regressed:
  1. an ALIAS TARGET did not count as a use, so musl's
     `weak_alias(dummy, __stdout_used)` warned "defined but not used" for
     both variables and functions. Same fact IPO learned separately.
  2. a FILE-SCOPE `volatile` warned as unused; gcc exempts it (the object is
     a hardware register and its existence is the point) but still warns for
     a LOCAL volatile — measured both.
  3. `-Wsign-compare` fired on provably non-negative operands. gcc suppresses
     when the signed side came from an unsigned type NARROWER THAN INT
     (musl's `val[c] >= base`, seven in one file) or is a 0/1-valued logical
     or relational result (`remain > !!f->buf_size`).
- **`tests/warn/corpus-baseline.txt` repinned** to the new musl numbers:
  1066 parsed / 295 deferred / 270 oracle-matched / **zero false positives**.
- **One entry added to `tests/warn/corpus-genuine-divergences.txt`**:
  `lookup_name.c:137 maybe-uninitialized`. `family` really IS uninitialized on
  that switch's default path and our note says so; gcc's silence is the miss.
  Triaged rather than suppressed — weakening a true warning to make a gate
  pass is how a warning engine stops being worth running.
  Seven fixtures cover them: two under `sign-compare/`, two under
  `unused-variable/` (INCLUDING the fire case for a LOCAL volatile — the
  exemption's negative half, so widening it cannot pass silently), one under
  `unused-function/`, and `wmem/leak/nofire-asm-escape.c` for the alias row.
- **New fixtures** `tests/corpus/x86_64/int/asm_operands.c` (executes under
  three spill-all lanes; carries BOTH targets' templates),
  `tests/programs/gnu/asm_mem_output.c`, `asm_two_reg_outputs_refused.c`;
  `asm_operands_refused.c` DELETED because the form now works.
- **`scripts/s36_isa_driver.sh` repinned 83→84 corpus files and 498→504
  object checks** — it asserts both counts exactly, in both directions, which
  is what made the corpus move announce itself instead of silently widening
  the ISA gate's coverage claim.
- **`ci/fuzz_sequence_digest.txt` repinned to `403ee999ec3fb17b`** with the
  sanitized 100k actually run (`__asan_init` confirmed present in BOTH
  binaries first). Note the digest moved TWICE: the corpus changed once when
  fixtures were added to `tests/programs` and again when `asm_operands.c` left
  it for `tests/corpus`.

### Boundaries deliberately drawn, with the measurement behind them

- **One REGISTER output; any number of MEMORY outputs.** A second register
  output would mean widening `CgMirView.inst_def` (one def per instruction)
  across both backends. Counting musl's asm sites for our two targets says
  that is rarely what a second output is: x86_64 has 181 one-output sites
  against 27 two-output, aarch64 172 against 19, and the two-output cases are
  dominated by a MEMORY second output (`"=m"`, `"=Q"` in the atomics) which
  consumes no register. Refused by name, and it is now a row in the tier
  table's REFUSED tier rather than a TODO in a comment.
- `asm goto` and label lists stay refused.

### THE LANE GEOMETRY THAT DECIDED THIS, worth internalizing

Three lanes run `tests/corpus`: x86_64 native, x86_64 under `CGF_SPILL_ALL=1`,
and arm64 under both. `tests/programs` gets none of the spill-all treatment.
So **where a fixture LIVES decides which bugs it can see**, and for anything
touching register allocation that is not a filing preference — a fixture in
`tests/programs` is invisible to the one lane that stresses the allocator.
The rule to carry: if the claim is about registers, the fixture goes in
`tests/corpus` and EXECUTES.

### Build trees, and a rule I broke

`build-s55-asm` was the verification tree and `build-a64dev` the arm64
development tree. **I armed a verification chain against `build-s55-asm` and
then rebuilt that same tree**, which produced `could not spawn compiler` on
two fixtures — not a compiler bug, my own violation of *freeze the tree while
a verification run is in flight*. The clang lane was at 83/83 when it was
killed. Keep dev and verification in separate BUILD dirs.

---

**`cleanup` is DONE** (the eleventh attribute, and the first that is a
LOWERING feature rather than a symbol property). What it taught, because two
of these cost real debugging time:

- **It reuses Sprint 20's VLA scope chain and shares none of its rules.**
  `VlaScope` is `LexScope` now, with two lists. A restore of the outermost
  token subsumes the inner ones; a cleanup call subsumes nothing. `return`
  restores NO VLA token (the epilogue reclaims the frame) and must run EVERY
  cleanup (a call has no epilogue equivalent). Where a scope owes both, the
  CALLS COME FIRST — the pointer handed to a cleanup is into storage the
  restore releases.
- **TWO BUGS IN THE NEW CODE, both the same shape**, both found by executing
  against gcc rather than by reading: a `goto` walk that ran off the top of
  the scope stack and fired an enclosing cleanup TWICE. The first reused
  `label_vla`, which is FILTERED to VLA-bearing compounds and is therefore
  NULL for every label in a function with no VLA. The second recorded only the
  label's innermost compound, which a jump INTO a scope leaves off the goto's
  stack entirely. The answer is the label's whole CHAIN and the innermost
  COMMON compound. **The VLA sibling needs neither fix because 6.8.6.1p1
  forbids the jump that would break it** — an adjacent feature's shortcut can
  be correct only because a language rule protects it, so check for that rule
  before copying the shortcut.
- **A MUTATION CHECK THAT DID NOT COMPILE REPORTED PASS.** The injected bug
  left an unused variable, `-Werror` failed the build, make kept the previous
  binary, and `>/dev/null 2>&1` hid all of it — so the check ran the
  UNMUTATED compiler and passed, which reads exactly like "this fixture does
  not catch that bug". **Never suppress the build output of a mutation
  check.** Same family as `rsync -a` preserving an old mtime so make skips the
  rebuild, which happened in the same session.
- **A pre-existing silent drop, for SEVEN attributes.** The function-parameter
  path warned only if `packed || weak || visibility` — the three that existed
  when it was written — so `aligned`, `alias`, `used`, `section`,
  `constructor`, `destructor` and the asm label had been dropped there without
  a word. `gnu_attrs_any_symbol_property` now enumerates every field and sits
  beside `gnu_attrs_merge`, which carries the same obligation. Bug #116's
  shape exactly: **any list that must name every attribute is a silent-drop
  waiting to happen — put it next to the other one.**

This file is the *transferable* part of what was learned building all of it:
the traps that cost real debugging time, the invariants that look like style
but are load-bearing, and the ritual the work follows. It is not a substitute
for the sprint files — it is the thing that stops you re-learning what already
hurt.

---

## ARCHIVE BOUNDARY — everything below is historical

The remainder of this file preserves pre-Sprint-54 implementation history and
lessons. Its peer-level headings, including “Current position”, “THE NEXT
WORK”, “UNDER WAY”, and old `ci/closed_sprints.txt` values, describe the state
when those sections were written. They are not current instructions. For all
resume decisions and current sprint/ratchet state, §0a above is authoritative.

## 0. Where the record actually lives (READ FIRST)

Three tiers, and **two of them are gitignored**:

| What | Where | In git? |
|---|---|---|
| Reality snapshot: what is implemented, sprint by sprint | `AGENTS.md` (copy: `CLAUDE.md`) | **NO** |
| Per-sprint plan + appended implementation notes | `.docs/sprints/**/*.md` | **NO** |
| Debt ledgers (XFAIL, afs-ld ELF gaps) | `.docs/audits/*.md` | yes |
| This file | `.docs/HANDOFF.md` | yes (negated in `.gitignore`) |

**If you are on a fresh clone, `AGENTS.md` and the sprint files are not
there.** They are the most valuable documents in the project. Get them
from the previous machine before starting; without them you have the
code and this file and nothing else.

`AGENTS.md` is the honest snapshot — one section per sprint, written
*after* implementation, recording what actually happened including the
corrections to the sprint file's own text. Keep updating it (and `cp
AGENTS.md CLAUDE.md`). **Never commit either.**

---

## 1. Historical position at the end of Sprint 51

- **Sprints 0–51 CLOSED; Phases 1–9 closed. Sprint 55 is UNDER WAY** — see
  §1b-1's *WHERE THE WORK IS* for the resume point. Phase 10 (second backend
  and targets) is under way on top of the completed preprocessor, frontend,
  sema, IR, x86_64 backend, driver, optimizer, warnings, and memory-safety
  phases. Sprint 55 was taken OUT OF NUMERICAL ORDER because 28 deferrals
  pointed at it and it blocks hosted compilation on macOS and FreeBSD plus
  the musl campaign (Sprint 57) that precedes the bootstrap (Sprint 58).
- **`--target=` and `--sysroot=` exist now** (Sprint 51 D1) over the closed
  five-target set, so the compiler's architecture is no longer the target.
  `cgf_target_selected()` is the target, `cgf_target_host()` is the host, and
  `scripts/check_target_seam.sh` gates the split. Do not reintroduce a host
  sniff in target-dependent code.
- **`_Thread_local` works** on both Linux targets (local-exec). It did not,
  silently, for fifty sprints — see §1b.
- **Sprint 51 is CLOSED** (all seven deliverables) and **Sprint 55 is under
  way** — see §1b-1. `ci/closed_sprints.txt` is 51; raising it forced an
  audit and found only the `-g` gate the DWARF work had already removed.
- **Inline asm: BASIC works, operands refused by name.** An asm template is
  TARGET-SPECIFIC ASSEMBLY, so a fixture in `tests/corpus` — which the arm64
  lanes re-run under qemu — must select on `__x86_64__`/`__aarch64__` or it
  cannot pass there. Mine did not, and both lanes said `unknown mnemonic
  addl`.
- **ELEVEN GNU attributes are implemented**: `weak`, `visibility`, `packed`,
  `aligned`, `alias`, `used`, `__asm__("name")` labels, `section`,
  `constructor`, `destructor`, `cleanup`. §1b-1 has what each one taught, and
  *WHERE THE WORK IS* has what to do next and why.
- **Two bugs that had SHIPPED were found by adding the tenth**, and the second
  is the one to remember. `section` was printed in the IR text and parsed
  nowhere, so `-emit-ir` ICEd on any program using it. And **every symbol
  attribute was dropped on a definition with a prior plain declaration** —
  `weak` emitted GLOBAL where gcc emits WEAK, which is musl's `weak_alias`
  shape and exactly what the code comment claimed to handle. Both were
  invisible because every fixture writes the attribute and the definition
  together, and `section`'s only tests are `ASM_CHECK`s that never touch the
  IR text. **When a new feature misbehaves in a shape an old one shares, test
  the old one in that shape before believing your own explanation** — this one
  first read as gcc's known priority-dropping bug and was a broader bug of
  ours underneath it.
- **arm64-linux emits DWARF and `.eh_frame`**; `addr2line` resolves a linked
  executable. `src/cg/debug.c` is now the ONE line-table and CU-DIE emitter
  for every target, which also makes task #93's variable DIEs a write-once
  job rather than per-backend.
- **The GNU attribute surface is classified** by what ignoring each one
  costs; `weak` and `visibility` are implemented and agree with gcc's symbol
  table on both targets. **`packed` and `aligned` are both implemented**
  (`.docs/audits/packed-layout.md`, `.docs/audits/aligned-layout.md` — each
  records what the measuring found that the plan had not anticipated).
- **`_Alignas` on an OBJECT works.** It did not, on any target, for the whole
  project: ISO C11 6.7.5 validated and then discarded. Members had worked
  since Sprint 14, which is what hid it.
- **`.set` works on BOTH afs-as paths** — PR #28 for x86 (`fb50d2f`), PR #29
  for arm64 and Mach-O (`a6690e2`). AS-SET-002 is CLOSED, the driver's by-name
  refusal is gone, and `attr_alias.c` lives in `tests/corpus` so aliases
  EXECUTE on arm64 through the bundled assembler.
- **A ledgered recipe is a hypothesis, not a plan.** AS-SET-002's was wrong in
  its first step (the deferral it prescribed already existed; the real gap was
  one variant in `may_resolve_with_labels`) and right in its trap (the
  emission cursor). Re-measure before trusting a recipe you wrote earlier —
  and REBUILD first: the first reproduction ran a stale
  `afs-as/target/release/afs-as` and reported an error the source no longer
  produces.
- **Known-wrong-but-shipping is now ZERO.** Everything open is a NAMED
  refusal or a deliberate deferral — see §1b-1's *WHERE THE WORK IS*.
- **Controlling expressions are constrained** (was task #108, and it was
  bigger than its title). All twelve scalar positions — `if`/`while`/`do`/
  `for`, `?:`, `&&`, `||`, `!` — were unchecked for BOTH void and aggregate
  operands, and both failures were silent: a void operand became
  `icmp ne i32 undef, 0` (branch on an undef), an aggregate became
  `icmp ne ptr @s, 0` (an address, never null, so `if (1)`). `switch` takes
  the stricter integer rule; a pointer or float there used to reach lowering
  and ICE in the IR verifier.
- **THE EXEMPTION IS THE HARD HALF of any new constraint.** `if (arr)` and
  `if (fn)` are legal — an array or function converts to a pointer — but a
  STATEMENT condition is typed WITHOUT decaying, so they arrive undecayed and
  the first draft rejected correct C. Lowering was already right about them
  (that address is never null, so the condition is always true), which is why
  the `_ok` fixture EXECUTES rather than only compiling.
- **Never grep for a word that appears in both success and failure.** I
  checked whether `switch` was already covered with `grep -c error`, saw a
  non-zero count, and excluded it — the count was the ICE's own
  "internal compiler error" line. The 100k fuzz found what that missed.
- **Const globals reach read-only memory**, as of the `.rodata` work. One
  shared rule (`cg_global_segment`) both backends call: const with no
  relocation is `.rodata`, const with one is `.rodata` without PIC and
  `.data.rel.ro` with it, because under PIC the loader must WRITE that word.
  Mach-O spells the pair `__TEXT,__const` / `__DATA,__const`. **macOS is
  always position-independent** whatever the flags say
  (`cgf_target_always_pic`) — getting that wrong puts a loader-written
  pointer in `__TEXT`, which is read-only AND code-signed. Constness is the
  object's own qualifier looked through ARRAY-ELEMENT qualification (6.7.3p9);
  a const member of a non-const record does not count, matching gcc.
- **The memory summary tables now select on SHAPE, not on the name alone.**
  Both matched a callee with `strcmp` and nothing else, so `strcpy(buf, "")`
  with no `#include` — whose implicit declaration returns int — attached the
  row's pointer facts to an integer result and ICEd in the alias validator.
  Three ICEs, two tables, pre-existing since at least Sprint 47; found by the
  100k fuzz lane gating unrelated work (seed 64271). Sprint 39's format table
  had always done this correctly; gcc calls the mismatch
  `-Wbuiltin-declaration-mismatch`. **Any new table keyed on a libc name owes
  the same check**, and its fixture owes BOTH directions — dropping a row is
  the safe answer, so a too-strict gate disables the analysis silently.
- **The runner's scratch is per-invocation now** (was task #110). It
  defaults to a directory beside the runner binary, so two BUILD trees are
  isolated without asking, and `CGF_TEST_WORK` overrides it — needed because
  the two arm64 lanes share one `$(BUILD)/cgf-test`, which the binary's own
  location cannot separate. `make -j test-a64-corpus test-a64-spill-all` now
  gives 63/63 twice; it used to fail nearly the whole corpus with "the
  assembler rejected cgfried-generated assembly … line 0". check_bans keeps
  the literal out. **Third instance of this shape** after ppfuzz's 128 phantom
  findings; `tests/unit/` still has a benign one (task #113).
- **FREEZE THE TREE while a verification run is in flight.** Editing or
  rebuilding under a running `make test` produces results from a tree that
  never existed. It cost two discarded runs in one session — both mine, not
  the harness's — and the tell is a lane failing in a way that does not
  reproduce.
- **VLAs work on both targets**, as of the deferral reckoning (§1b-1). They
  did not: arm64 ICEd on every one and x86 silently miscompiled any VLA in a
  function that also passed arguments on the stack. Multidimensional VLAs
  and pointers-to-VLA were wrong too.
- The arm64 e2e corpus is **63/63**, and **63/63 again under
  `CGF_SPILL_ALL=1`**. Keep both green; the spill lane exists because exactly
  one fixture at one level once caught a stale NZCV producer index. They may
  now be run in PARALLEL — each gets its own scratch (task #110).
- Verified at the end of the Sprint 51 session, sequentially and locally
  (never two suites at once — §1b-2): gcc and clang `make test` both rc=0,
  **625 unit tests / 4,263,106 assertions**, 514 and 477 fixture profiles,
  arm64 corpus and spill-all 55/55 each, ABI differential 304/304 per Linux
  target in both directions. Re-verified after the VLA campaign: same unit
  counts, 516 and 477 fixture profiles, arm64 corpus and spill-all **57/57**
  each, musl warning lane unchanged at 716/1361 parsed / 645 deferred / 186
  oracle-matched / zero false positives. **Latest full local verification**
  (after `section`, the summary-table shape gate, `.rodata`, AS-SET-002, the
  scalar/switch constraints and the runner scratch): gcc and clang
  `make test` both rc=0 at **626 unit tests / 4,263,123 assertions**, arm64
  corpus and spill-all **63/63** each AND 63/63 with both lanes run in
  PARALLEL, memsafe 90/90 + interproc 50/50 + foundation 14, safe-mode 56/56,
  e2ediff 10/10, a64 objdiff 20 identical / 0 pinned, x86 objdiff 38/38,
  c-testsuite 215/220 with 0 new, 100k sanitized fuzz 0 findings, safe-dogfood
  102 TUs / zero exemptions. All 14 CI jobs green on `trunk`.
- **After `cleanup`** (both lanes rerun end to end, sequentially): gcc and
  clang `make test` both rc=0, **626 unit tests / 4,263,124 assertions**,
  565 program fixtures, 477/477 warning, 90/90 memsafe + 50/50 interproc,
  56/56 safe-mode, objdiff 38/38, e2ediff 10/10, c-testsuite 215/220 with
  0 new, musl unchanged at 716 parsed / 645 deferred / 186 oracle-matched /
  zero false positives, **arm64 corpus and spill-all 66/66 each with both
  ledgers still empty**, and a sanitized 100k frontend fuzz at 0 findings
  (digest repinned to `076a1a46ac3703b4` for the 8 new corpus files; both
  binaries checked for `__asan_init` before the run was believed).
- `cgf hello.c -o hello && ./hello` works on **x86_64-linux AND
  arm64-linux**. On arm64 the compiler emits its own assembly, assembles it
  with the bundled afs-as into ELF objects byte-identical to
  `aarch64-linux-gnu-as`, and links. The e2e corpus is 43/51 there, with the
  one gap ledgered by cause — the same split on real hardware and under
  qemu, so it is a backend gap, not an emulator artifact.
- **Sprint 49 is CLOSED — all seven DoD gates met as written. Sprint 50
  (arm64-macos) is IN PROGRESS; see §1b.** See the DoD audit table at the end
  of `.docs/sprints/10-backend-arm64/s49-arm64-linux.md` before assuming any
  arm64 property holds.
- `cgf hello.c -o hello && ./hello` works. Multi-TU works. Hosted
  programs against system glibc work on Arch *and* Debian/Ubuntu.
- `-g` emits DWARF v4 line tables and every object carries `.eh_frame`;
  gdb break/next/four-frame backtraces work at `-O0` and `-O2`, including
  GDB 15's shorter x86 prologue scanner via an entry definition row plus
  `prologue_end` on the first executable row.
- Phase 7 is closed: every `-O` level reaches a real pass manager. O1 runs
  mem2reg → sparse conditional constant propagation → exact simplify →
  block-local CSE → DCE → general CFG cleanup. O2+ adds GVN, DSE and bounded
  jump threading, then internal-only IPO and a bottom-up SCC inliner. O2 adds
  canonical natural-loop analysis, LICM and affine-IV strength reduction; O3
  adds exact bounded full unroll. O2 now adds edge-sensitive BCE; O3 adds
  factor-four constant partial unroll, loop unswitching, and conservative
  adjacent-loop fusion behind a shared region cloner and affine dependence
  service. O3/Ofast now add a constant-trip, statically proven SSE2 loop
  vectorizer; Ofast additionally licenses FP reductions through one documented
  fast-math bundle. Scalar, loop and unroll groups are separate fixpoints so CFG
  cleanup cannot oscillate with canonicalization. The
  50-program corpus remains behaviorally equal across O0/O1/O2/O3/Os/Ofast.
- Sprint 37 opens Phase 8 with a 157-row warning registry, GCC-specificity
  option policy, mandatory diagnostic suffixes, location-sensitive GCC
  diagnostic pragmas, macro/system provenance, strict runner assertions, and
  a complete 222-row GCC 8 parity matrix. Existing frontend warnings are
  migrated onto the same policy engine. Warning-option classification and
  pragma-name validation are centralized in `src/warn/warn.c` and protected by
  `scripts/check_warn_seams.sh`; the manual is `docs/warnings.md`.
- Sprint 38 implements the AST/sema warning set: unused/shadow/conversion,
  prototype/K&R/VLA, switch/return, expression-shape, range and indentation
  diagnostics. PP comment metadata drives the exact GCC 8 fallthrough levels;
  conversion proofs reuse target widths and `softfp`. The suite has 166
  frontend fixtures and 197 warning fixtures overall. A real GCC 8 container
  lane reports 179 exact matches, 18 documented divergences and zero
  unannotated differences. The strict same-mode musl pass compiles 706/1,361
  sources, explicitly defers 655, finds 181 oracle-backed warnings and zero
  false positives.
- Sprint 39 adds the dedicated `src/warn/format.{c,h}` checker for printf,
  scanf, strftime and strfmon grammars. Builtin recognition is target-,
  linkage- and rough-signature-aware, including every fixed parameter; all
  conditional literal alternatives are checked; default promotions,
  positional operands, output-pointer qualifiers, target wchar types,
  GNU/FreeBSD rows and glibc `__isoc99_*` redirects are pinned. POSIX
  `strfmon` fill/flag ordering, decorated percent rejection and all GCC 8 y2k
  conversions are regression-tested. The 64-row type matrix generates 128 fire/nofire
  fixtures, and the complete format tree is 203/203. The real GCC 8 lane now
  covers 400 warning fixtures: 379 exact, 20 annotated, one narrowly normalized
  Cgfried-only unbounded-scanf extension and zero unannotated differences.
  musl remains zero-false-positive, and a new TinyCC lane is also
  zero-false-positive over every source the current frontend accepts.
- Sprint 40 closes Phase 8 with cloned-IR flow analysis for definite and maybe
  uninitialized reads, self-init, unreachable regions, non-void falloff and
  infinite recursion. Lowering records source provenance that optimized IR
  cannot reconstruct; mem2reg supplies pre-rewrite definite-assignment facts;
  diagnostics remain byte-stable across O0/O1/O2/O3/Os. The flow corpus is
  77/77 across 385 level runs, the complete warning tree is 477/477, and the
  real GCC 8 lane has zero unannotated differences. A bounded CFG workspace
  reduced the 1,200-use review stress case from 23.013s to 0.455s.
- Sprint 41 opens Phase 9 with the shared memory-analysis substrate. The alias
  service now owns stable allocation sites, byte-offset hulls through memory,
  stored-pointer contents, and transitive reachability for both optimizer and
  memsafe clients. `src/memsafe/` adds the five-state lifetime lattice,
  persistent source-qualified traces, the eleven allocation-family rows, and
  bounded path splitting at 8 states / 256 splits / 4 predicates. The driver
  runs it on a dedicated read-only post-opt module only under
  `CGF_MEMSAFE_DUMP=1`; default behavior and generated assembly remain
  byte-identical, and `-Wmem` is still unknown.
- Sprint 42 ships the first user-visible memory-safety layer. `-Wmem` is
  default-on and reports proof-only intraprocedural use-after-free,
  double-free, leak, constant/affine OOB, uninitialized heap read, and
  free-nonheap findings; `-Wmem-realloc-zero` is opt-in and
  `-Wmem-strict` holds pass-to-unknown UAF. Realloc success/failure paths are
  correlated, every diagnostic has an ordered proof trace, and exact warning
  policy/pragma behavior is fixture-pinned. The semantic corpus is 89/89 with
  11 exact trace sequences. The musl gate analyzes 733/1,361 pinned TUs,
  explicitly baseline-defers 628 pre-Sprint-55 GNU-syntax TUs, emits zero
  memory warnings, and completes in about 13 seconds.
- Sprint 43 makes those checks interprocedural through deterministic bottom-up
  summaries over the shared callgraph. Five checked `cgf_*` ownership
  attributes, a 55-row libc/`FILE *` summary table, write ranges and
  multi-parameter return aliases feed call-site state without inlining. The
  default-on annotation-mismatch lint keeps declared contracts honest. The
  new corpus is 50/50 with 13 exact traces; musl remains zero-diagnostic.
  `<cgfried/memsafe.h>` is portable under host GCC/Clang, and `make install`
  now copies the whole include tree to the path used by installed compiler
  discovery, resolving the reported installed-`cgf` `stddef.h` failure.
- Sprint 44 ships `-fcgf-safe`: one exact-emission memsafe traversal
  statically discharges proven accesses and terminal-splices opaque checks for
  the residue, including the backend-generated 24-byte `va_start` write. Nine
  GNU ld wrappers add 32-byte headers, canaries, poison and a 1,024-block/8-MiB
  quarantine. Private registry lookup preserves the foreign-pointer
  no-false-abort law; diagnostics are stdio-free and deterministic. Runtime
  coverage is 27/27 across seven O0/O2 failures, correct and mixed programs,
  static link, threads, trap mode and real-C discharge accounting; all three
  interim benches are below the 2.5x time/2x RSS budget. Fresh GCC, Clang and
  complete ASan+UBSan suites pass. Valgrind 3.25.1 reports zero errors/leaks
  over the seven instrumentation units; the runtime executable has no definite
  or possible leaks, with only its bounded quarantine still reachable.
- Sprint 45 ships GCC-compatible parseable fixits and copy-only application to
  `*.cgf-fixed`, retaining original source bytes so CRLF, missing final newline,
  UTF-8 byte columns, physical aliases and symlinks are handled deterministically.
  Conflicting/touching edits are withheld and advisory edits are never applied
  by `=all`. Four conservative transform families cover early-return leaks,
  bounded copy APIs, missing allocation checks and `sizeof` mismatches; every
  family has explicit no-fire cases and no path suggests `strncpy`. Inserted
  identifiers are resolved conservatively at their lexical point: active
  `snprintf`/`free` macros and local shadows suppress edits, while annotation
  macros must match the shipped replacement-token shapes. A `free` prototype
  appearing only after the return cannot authorize an earlier suggestion.
  `-ftrivial-auto-var-init=zero|pattern` lowers after warning analysis, including
  dynamic VLA initialization, while must-fact annotation inference provides the
  mechanical annotate-then-ratchet workflow. The dedicated lane proves 12 exact
  parseable records, original-byte preservation, GCC sanity, O0/O2 equivalence
  and the annotation round trip.
- Sprint 46 ships the documented `-fsafe` per-TU contract. It composes
  instrumentation, default-tier memory errors and zero auto-init; rejects six
  unmodelled construct families with deployment alternatives; permits only the
  exact pointer-derived `uintptr_t` grammar; and emits a versioned ELF safety
  note that is validated before safe links. `make safe-dogfood` rebuilds all
  91 compiler TUs, verifies every marker and smoke-tests the result with zero
  exemptions. Dogfood found and fixed the call-site allocator
  ceiling, the `%rdx`/`%rcx` fixed-register conflict, fabricated-pointer map
  values and automatic-address constant folding. The contract truthfully
  defers stack/global spatial instrumentation; VLA acceptance is not described
  as a runtime-bound guarantee.
- Sprint 47 opens the ARM64 backend with verified pre-RA MIR, exact
  instruction selection, ABI-neutral call metadata, shared target-independent
  liveness/linear scan, SP/XZR form checking, memory pairing, and monotone
  branch relaxation. The contract is intentionally pre-RA: Sprint 48 owns
  AAPCS64 and physical allocation, Sprint 49 owns assembly/object/execution,
  and Sprint 51 owns public cross-target routing. Ten `.cgfir` modules pin 93
  coverage labels as exact deterministic full snapshots; they are labels, not
  93 independent fixtures. The assembly oracle compares afs-as Mach-O and GNU
  as ELF `.text` bytes for a fixed fragment plus all 5,334 logical immediates.
  Constant bulk-memory expansion is capped at 64 KiB; larger/dynamic forms
  fail immediately naming Sprint 49.
- **Next action: Sprint 48** —
  `.docs/sprints/10-backend-arm64/s48-arm64-regalloc-abi.md`, adding ARM64
  register allocation and AAPCS64 lowering.
- **Pending parallel-test follow-up:** `tests/fuzz/ppfuzz.c` still writes every
  run to `build/fuzz-work/case.c`. Two concurrent full suites can replace that
  file between the Cgfried and gcc oracle runs and report false differentials.
  Sprint 41 validation reproduced this with concurrent Clang/sanitizer runs
  and proved both isolated 2,000-case runs clean. Give each process/build a
  private work directory before relying on parallel local `make test` runs.

Metrics to compare against after your changes (all must hold or improve):

```
unit: 598 tests, 4262592 assertions, 0 failures
cgf-test: total=505 pass=505 fail=0 xfail=0 xpass=0 skip=0 config=0
memsafe warning fixtures: 89/89; exact ordered trace sequences: 11
interprocedural memsafe fixtures: 50/50; exact ordered trace sequences: 13
focused memsafe units: 52 tests / 764 assertions
safe runtime: 27 checks, 0 skips; three interim benches within 2.5x/2x
autofix transforms: source-copy, four transforms, auto-init and annotation ratchet green
musl -Wmem gate: 733 analyzed, 628 pinned deferrals, 0 memory diagnostics, <90s
format warning fixtures: 203/203; flow warning fixtures: 77/77; all warning fixtures: 477/477
format matrix: 64 semantic rows / 128 fire+nofire fixtures
GCC 8 warning differential: 409 exact + 36 normalized CGF-only + 32 annotated, 0 unannotated
safe mode: 56/56 fixtures; 8 accept + 8 reject uintptr cases; mixed-link green
safe dogfood: 91/91 object notes; 0 symbol exemptions; smoke green
musl warning dry-run: 716 parsed, 645 deferred, 186 genuine, 0 false positives
TinyCC warning dry-run: 12/30 parsed, 18 deferred, 0 format warnings, 0 false positives
warning matrix: 222/222 raw GCC 8 C rows accounted for
OPT_EQ corpus: 51/51 at O0/O1/O2/O3/Os/Ofast; verifier-after-each also green
ctestsuite_diff: 220 files, 215 agree, 5 known-deferred, 0 new, 0 xpass
header_diff: 148 macro/type lines byte-identical to gcc
rt_diff: 2317 result lines identical to libgcc
driver_matrix: 39/39 rows agree with gcc
objdiff: 38/38 · e2ediff: 10/10 · afsld lane: 12 fixtures
debug_info lane: 81 checks with tools/gdb; 6 addr2line rows
pp_dm_check: 182 predefines checked; __CGFRIED__=1; __GNUC__ absent
memsafe foundation: 14 deterministic fixtures; 17 focused units / 264 assertions
frontend fuzz digest: e39a0b1f9c71243f; 2,000 sanitized smoke iterations, 0 findings
ARM64 MIR: 10 modules / 93 exact coverage labels; 1 GiB bulk expansion rejected boundedly
ARM64 assembler differential: 1 fixed fragment + 5,334 logical immediates
ARM64 Valgrind: 10/10 modules, 0 errors, 0 leaks
```

Local Sprint 47 validation note (2026-08-03): fresh GCC, Clang and complete
ASan+UBSan suites pass with 598 unit tests / 4,262,592 assertions and 505/505
program fixtures. The ARM64 lane passes 10 byte-exact deterministic MIR
modules / 93 coverage labels and rejects the 1 GiB bulk-memory stress case
before expansion. The assembler differential passes its fixed fragment plus
all 5,334 production-accepted logical immediates; local GNU-as absence is one
exact expected skip, while the CI toolchain job requires afs-as and GNU as
with zero skips. The dataflow-aware division-UB lint accepts all 67 real
fixtures and rejects its planted self-tests. With the glibc loader debuginfo
cache described in §3.5, Valgrind 3.25.1 reports zero errors and zero leaks
over all 10 successful ARM64 MIR modules. Independent re-review covered 61
files, found zero remaining issues at every severity, and approved the sprint.

Local Sprint 38 validation note (2026-08-01): fresh GCC and Clang full suites
pass with 480 unit tests / 94,590 assertions and 496/496 program fixtures. The
complete ASan+UBSan suite passes with leak detection disabled for the host
ptrace policy. The real GCC 8 container reports 179 exact + 18 documented / 0
unannotated warning differences; the musl lane reports 706 parsed, 655
deferred, 181 oracle-backed warnings and zero false positives. Frontend fuzzing
reproduces digest `428755e13c99b029` with zero findings. Valgrind 3.25.1 still
could not start inside the command sandbox because its private loader lacked
the mandatory `memcmp` redirection symbol. A host-scope retry during Sprint 40
proved that Valgrind itself works on this machine; this historical note records
the absence of a valid Sprint 38 run, not a host incompatibility.

The closing review also pinned nested brace-elision with a persistent
current-object cursor, rejects an unbraced scalar initializer for an aggregate,
and makes oversized initializer-image integer writes fail before any invalid
shift. The independent re-review found no remaining blockers.

Local Sprint 39 validation note (2026-08-02): fresh GCC, Clang and complete
ASan+UBSan suites pass with 483 unit tests / 95,206 assertions, 496/496 program
fixtures and 400/400 warning fixtures. The real `gcc:8` container uses a
compiler built inside that container (a host build requires GLIBC_2.33/2.34 and
cannot execute there) and reports 379 exact, 20 annotated, one explicitly
normalized Cgfried-only unbounded-scanf warning, and zero unannotated
differences. The format matrix is 64/64 rows and 128/128 generated fixtures;
musl and TinyCC both report zero format false positives. Valgrind 3.25.1 again
failed before loading the touched format path inside the command sandbox. The
later Sprint 40 host-scope retry established that the sandbox loader, not the
machine, caused that failure; no valid Valgrind result was collected for the
Sprint 39 path at the time.

Local Sprint 40 validation note (2026-08-02): fresh GCC 16.1, Clang 22.1 and
complete ASan+UBSan suites pass with 491 unit tests / 95,291 assertions,
504/504 program fixtures, 477/477 warning fixtures and 77 flow fixtures across
385 byte-stable optimization-level runs. The real `gcc:8` container reports
409 exact warning sets, 36 narrowly normalized Cgfried-only warnings, 32
documented divergences and zero unannotated mismatches. musl parses 709/1,361
sources, explicitly defers 652, observes 181 oracle-matched warnings and has
zero false positives; c-testsuite remains 215/220 with five known deferrals.
The complete sanitizer run uses `ASAN_OPTIONS=detect_leaks=0` for the host
ptrace policy; a separate 100,000-iteration sanitized frontend run has zero
findings. Seven permanent malformed-source fixtures intentionally repin the
frontend-fuzz digest to `597efab493bdc971`. Host-scope Valgrind 3.25.1
Memcheck runs over every changed Sprint 40 unit path report zero errors and
pass. A full unit-binary run is also memory-clean, though the unrelated
process-spawn unit changes behavior under Valgrind instrumentation and makes
that aggregate invocation exit nonzero.

The post-CI fuzz hardening rejects invalid parameter storage classes and void
parameters, malformed `va_list` cursors, floating/pointer comparisons and
invalid compound assignments before lowering. Pointer/integer conditional
recovery now materializes its cast, while old-style no-prototype functions
carry an explicit IR contract that the verifier accepts and inline/IPO decline
to transform. These guards convert all 17 minimized frontend-fuzz ICE seeds
into ordinary source diagnostics or valid warning-only recovery.

Local Sprint 41 validation note (2026-08-02): fresh GCC and Clang full suites
each pass 515 unit tests / 95,609 assertions; the focused alias set passes 26
tests / 157 assertions. The fourteen foundation fixtures have exact
source-qualified event chains on at least ten programs, and their double runs
are byte-identical. They cover all three budget boundaries,
verifier-after-each, a 300-branch `<2s` gate, and
an assembly comparison proving the private dump stage is emission-inert. The
complete GCC suite passes with 504/504 program fixtures, 477/477 warning
fixtures, all three fuzz smokes, differentials, bans, seams, and formatting.
The complete ASan+UBSan suite passes with `ASAN_OPTIONS=detect_leaks=0` for the
host ptrace policy. Host-scope Valgrind 3.25.1 reports zero errors and zero
leaks for the 17-test memsafe set, the 26-test shared-alias set, and an
end-to-end gated analysis run. Independent final review found no remaining
issue.

Local Sprint 42 validation note (2026-08-02): fresh GCC and Clang full suites
and the complete ASan+UBSan suite pass with 534 unit tests / 95,771
assertions, 504/504 program fixtures, 477/477 warning fixtures, and 89/89
memory-warning fixtures plus 11 exact proof traces. The pinned musl memory
sweep analyzes 732/1,361 TUs, defers 629 exact identities, emits zero `-Wmem`
diagnostics, and finishes in 13 seconds; the warning sweep now parses 711 TUs
and records 183 oracle-matched warnings with zero false positives. The
20-case GCC `-fanalyzer` comparison is complete. A host-scope ASan+UBSan
frontend fuzz run completes 100,000 fixed-seed iterations with zero findings;
the deterministic smoke digest is `d6abeca631b6cd2e`. Host-scope Valgrind 3.25.1
first found 244 conditional reads from uninitialized `MsFact.extent` state;
zero-initializing facts and canonicalizing unknown extents fixed the defect.
The final 30-test memsafe unit run and an end-to-end aggregate-provenance run
both report zero Memcheck errors and zero leaks. Independent final review
approved the implementation with no remaining findings.

Local Sprint 43 validation note (2026-08-02): fresh GCC, Clang and complete
ASan+UBSan suites pass with 551 unit tests / 96,086 assertions, 504/504
program fixtures, 477/477 warning fixtures, 89/89 intraprocedural memory
fixtures, and 50/50 interprocedural fixtures with 13 exact ordered traces. The
sanitizer run uses `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot
initialize under this host's ptrace policy; AddressSanitizer and Undefined
BehaviorSanitizer remain enabled for the complete suite. The
55-row builtin summary table and all five ownership annotations have focused
unit and corpus coverage. The pinned musl memory sweep still analyzes
732/1,361 TUs, defers 629 exact identities and emits zero `-Wmem`
diagnostics; the general warning sweep reports 183 oracle-backed warnings and
zero false positives. Summary dumps are byte-identical across repeated runs.
Valgrind 3.25.1 passes the 45-test / 692-assertion memsafe set with 1,695
allocations and frees, zero bytes live, zero leaks and zero errors; the
two-hop mixed-return compiler regression is independently clean. The
installed-layout follow-up is closed: the install manifest now copies the
complete include tree, and the ownership portability header compiles cleanly
under both host GCC and Clang.

Local Sprint 45 validation note (2026-08-03): fresh GCC, Clang and complete
ASan+UBSan suites pass with 582 unit tests / 96,373 assertions, 505/505
program fixtures, 477/477 warning fixtures, 89/89 intraprocedural memory
fixtures and 50/50 interprocedural fixtures. The dedicated autofix lane proves
the copy-only/source-byte laws, exact format records, all transform positives
and negatives, auto-init layering and annotation round-trip. The pinned musl
memory sweep remains 732/1,361 analyzed, 629 deferred and zero diagnostics;
the new permanent program fixture legitimately repins the frontend-fuzz digest
to `e39a0b1f9c71243f`. The sanitizer suite uses
`ASAN_OPTIONS=detect_leaks=0` only because LeakSanitizer cannot initialize
under this host's ptrace policy; ASan and UBSan exercise the complete suite.
Valgrind 3.25.1 is usable on this machine: the diagnostic, autofix, trivial
auto-init, summary and lifetime groups total 57 passing tests with every heap
block freed and zero errors, and an end-to-end auto-init compiler run is also
clean. Earlier Valgrind startup failures were sandbox-loader-specific evidence,
not a machine incompatibility.

Local Sprint 46 validation note (2026-08-03): fresh GCC, Clang and complete
ASan+UBSan suites pass with 583 unit tests / 96,392 assertions, 505/505
program fixtures and 56/56 safe-mode fixtures. Safe dogfood rebuilds all 91
compiler translation units, verifies every safety note with zero exemptions and
passes its smoke test. The bounded union-layout proof accepts matching large
and nested layouts, rejects crossed layouts and ranges above `INT64_MAX`, and
rejects a repeated S0-S28 type graph within 64 MiB and three seconds. A fresh
Valgrind 3.25.1 run over the nested-large regression reports 556 allocations
and frees, zero bytes live and zero errors. Independent final review approved
the implementation with no remaining correctness or performance blockers.
CI review moved musl `src/network/getaddrinfo.c` from deferred to analyzed after
the automatic-address fold repair; the exact gate is now 733 analyzed / 628
deferred with zero memory diagnostics.

---

## 1b. HOW SPRINTS 50 AND 51 GOT HERE (both CLOSED; background)

**Sprint 50 (arm64-macos) is CLOSED**: three DoD gates met as written, three
partial with the reason named and ticketed. The gate-by-gate audit is at the
end of `.docs/sprints/10-backend-arm64/s50-arm64-macos.md`.

**Sprint 51 is CLOSED, all seven deliverables.** D1 (host/target split,
`--target=`, `--sysroot=`), D2 (PIC/PIE per architecture, `-shared`), D3
(TLS), D4 (musl and FreeBSD bring-up), D5, D6 (the ABI differential) and D7
(arm64 DWARF line tables + `.eh_frame` CFI). What follows is the record of
how it got there; the LIVE work is Sprint 55 — see §1b-1.

### Sprint 50 (arm64-macos), still true and still needing its lanes run

- The Mach-O dialect, the whole seven-row Apple divergence table, SDK
  discovery, the ld64 link recipe, and afs-ld as the second link lane.
- **The Mach-O object differential: 10 objects identical to Apple's
  assembler on section bytes, relocations AND symbols, nothing pinned.**
- `codesign --verify` on both linkers' products — each ad-hoc signs.
- `tests/macos/run.sh` (7 programs x 2 linkers, signatures checked) and
  `scripts/macho_objdiff_lane.sh`, both run by a `macos-15` CI job that
  fails if either lane SKIPS.

**Run both macOS lanes after any emitter change.** They skip loudly off
arm64 Darwin, so on Linux you get nothing — and the ABI differential's
arm64-macos mode is a third lane that only exists there (§7b has the sync
recipe).

### The open tickets, and what changed

- **#100 hosted macOS compilation** needs Sprint 55. Apple's `sys/cdefs.h`
  uses `__attribute__` UNCONDITIONALLY — it does not guard on `__GNUC__` the
  way glibc does, so the trick that makes hosted Linux work does not apply —
  and `__DARWIN_ALIAS` needs `__asm` labels. Neither can be faked: dropping
  every attribute is wrong the moment one is `packed`, `aligned` or
  `noreturn`. **Sprint 51 widened this to 2 of 5 targets**: x86_64-freebsd
  is blocked the same way. The arm64-macos and FreeBSD corpora are
  freestanding-only until Sprint 55, which is a large part of why §1b-1
  takes 55 out of order.
- **#101 thread-local storage — CLOSED for local-exec on both Linux
  targets** this session (§1b). TLS-003 (Mach-O), TLS-004 (afs-as
  relocations) and TLS-005 (initial-exec/general-dynamic) remain in
  `.docs/audits/tls-debt.md`. Still a **hard prerequisite for Sprint 58**:
  the runtime uses `_Thread_local` and the bootstrap compiles the runtime
  with cgf, so TLS-004 must land before the bootstrap can use the bundled
  assembler.
- **#93 variable DIEs** (`.docs/audits/debug-info-debt.md`) and **#105 arm64
  DWARF** are the same area; #105 is the blocker (§1b-1 STEP 3).
- **#104 ABI-004** is new — Apple anonymous aggregates, diagnosed and
  ledgered, see above.

### What the ABI differential is, and why it earns its keep

`tests/tools/abigen.c` + `scripts/abi_differential_lane.sh`. A seeded
generator emits a function signature as a TEXT DESCRIPTOR, then a matched
caller/callee pair. The lane compiles one half with cgf and the other with
the reference compiler, links them, and runs — **both directions, every
time**. Three targets: x86_64 (gcc), arm64-linux (aarch64-linux-gnu-gcc under
qemu), arm64-macos (clang, on nomad-1).

Run it:

```sh
make test-abi-diff                       # both Linux targets
CGF_ABI_DIFF_TARGET=arm64-linux CGF_ABI_DIFF_COUNT=300 \
  sh scripts/abi_differential_lane.sh build/cgfried
```

The descriptor form is what makes minimization tractable: shrinking is text
surgery (drop an argument line, unwrap a composite one level) and never has
to re-derive the generator's random state. Minimized reproducers live in
`tests/abi_differential/repro/` and the lane replays **all eleven** before it
generates anything.

**THE DIAGNOSTIC WORTH INTERNALIZING.** When a signature disagrees, look at
WHICH DIRECTION fails:

- one direction  -> a placement bug on that side;
- **both directions -> a shared assumption.** Our caller and our callee agree
  with each other and neither agrees with the reference. `ours x ours` passes,
  so no same-compiler test can ever see it.

That tell has now appeared four times in three sprints (ABI-001's HFA return,
Sprint 50's row 3, ABI-002, ABI-003). Check it first.

### What it found (all closed, all in `.docs/audits/abi-debt.md`)

- **ABI-002** — an aggregate that cannot be placed ENTIRELY in registers goes
  entirely to memory; we committed eightbytes one at a time and split them.
  AAPCS64 additionally PINS the exhausted bank at 8. Placement depends on the
  type AND everything before it, so classification alone cannot decide it:
  `abi_budget_init`/`abi_arg_place` is now one shared service that both
  argument walks call, because those two walks disagreeing IS the failure
  mode.
- **ABI-003** — six varargs defects, found the moment the generator learned
  variadic tails. All six in the CALLEE. Three are one fact: **a register
  save area is laid out BY REGISTER, not by object**, so multi-eightbyte SSE
  aggregates and HFA leaves must be GATHERED out of 16-byte slots rather than
  read contiguously, and a mixed SSE+INTEGER pair needs both banks at once
  (a single "is this the FP path" boolean could not say so, and pushed every
  mixed pair to the overflow area). Two long-standing Sprint 48/49 deferrals
  fell out with them.
- **ABI-004 — OPEN.** arm64-macos is 24/304 and every minimal is a COMPOSITE
  anonymous argument. Apple holds anonymous arguments in the varargs area BY
  VALUE and contiguous; the classifier still shapes an anonymous aggregate
  like a named one. **Caller-only** — `lower_va_arg_apple` already reads
  contiguously. Marking it `ABI_ARG_STACK` at the call site covers up to 32
  bytes via the `ceil(size/8)` leaf re-plan; anything larger needs the arm64
  marshaller to honour `IROPF_ONSTACK` on a byval POINTER, which it does not
  yet. A checked-in fixture (`sysv-va-overflow-slot8`) is its standing
  witness: 10 of the 11 pass on macOS and that one does not.

### D3, thread-local storage — landed this session

`_Thread_local` had parsed and typed correctly since Sprint 16 and NEVER
LOWERED. Until Sprint 50 it became an ordinary global and every thread shared
one copy — four threads incrementing one a thousand times each printed 1000
where gcc printed 0, with no diagnostic anywhere.

- `IrGlobal.is_tls` carries it, round-trips as ` tls`. It is a property of the
  OBJECT, not of any reference, so backends ask the module (`ir_sym_is_tls`)
  rather than threading it through operands.
- x86_64: `.tdata`/`.tbss` with the T flag, `@tls_object`, STT_TLS symbols
  matching gcc. The address is BUILT, not folded into each access —
  `movq %fs:0` then `leaq sym@tpoff` (R_X86_64_TPOFF32) — so one code path
  serves loads, stores and address-of, and an addend rides the lea's
  displacement where a relocation cannot carry it. `fold_addr` refuses a
  thread-local for the same reason it refuses a GOT symbol.
- arm64: `mrs tpidr_el0` + `:tprel_hi12:` / `:tprel_lo12_nc:`.
- **AN ORDERING RULE, arm64-only.** gas rejects a TLS relocation naming a
  symbol it has not yet seen DEFINED in a TLS section ("Accessing `x` as
  thread-local object"). Functions precede data in our output, so
  thread-locals are emitted FIRST — what gcc does. A `.type ... @tls_object`
  declaration up front is NOT enough. x86 gas accepts either order, so this
  presented as a codegen bug until the emitted text was read.
- Two boundaries are clean errors, not guesses: an EXTERN thread-local emits
  no global so nothing downstream can tell it is thread-local (answering "it
  is not" is the original silent miscompile) — it needs initial-exec,
  TLS-005; and the BUNDLED assembler has no `%fs:`/`@tpoff`/TPOFF32
  (TLS-004), so the driver says so and points at `CGF_AS=0` rather than
  letting afs-as reject correct assembly and calling it "a cgf emission bug".
- **The test that matters runs four threads** and requires main to still see
  0 — `tests/programs/tls/tls_threads_are_separate.c`, `OPT_EQ: all`, run
  natively on x86_64 and under qemu on arm64. Section spellings prove nothing
  about semantics.

---

## 1b-1. Historical Sprint 55 work order

**Sprint 55 is the live one.** Skip to *WHERE THE WORK IS* below for the
resume point; the STEPS above it are the closed Sprint 51 record.

The user's standing position, stated explicitly: *"I'm generally not
comfortable leaving deferrals sitting around for long periods."* The plan
below was agreed with that in mind. Do it in this order.

### STEPS 1 and 2 — DONE. What they found is the reason to keep the habit.

Commits `9ac0686`, `6858686`, `0593cea`, `d48ead5`, `0bf917e`.

The plan called STEP 1 a renaming job. **It was audited by REACHABILITY —
compile a program that should hit each message — never by reading**, and
that is the entire lesson: eight of the nine were dead defensive text, and
the ninth was a missing C feature that then implicated four more defects.

| what | where | how it read before |
|---|---|---|
| arm64 ICEd on **every VLA** | `cg/arm64/regalloc.c` | "lands in Sprint 49", two sprints closed |
| x86 **silently miscompiled** a VLA in any function that also passed args on the stack | `cg/x86_64/regalloc.c` | no diagnostic at all |
| **2-D VLAs** wrong on both targets at every level | `lower/expr.c` | no diagnostic at all |
| **pointer to VLA** (`char (*p)[n]`) computed its stride in a non-dominating block | `lower/stmt.c` | IR verify check 1 |
| two more passes **dropped call-argument provenance** | `opt/inline.c`, `opt/loop_tree.c` | IR verify check 9 |

Details worth keeping:

- **The honest ICE was hiding the silent wrong answer.** x86's dynamic
  alloca handed back the new `rsp` as the object's base, while outgoing
  stack arguments are stored at `[rsp + k]` against that same `rsp` — so an
  argument list overwrote the first `out_args` bytes of the VLA. Both
  backends now reserve the 16-rounded outgoing area BELOW the object.
- **A VLA element's static layout size is 0**, so the dedicated subscript
  path in `lower/expr.c` scaled every row index of `int m[r][c]` by a
  literal zero and every row aliased row 0. `sizeof(m)` and `sizeof(m[0])`
  stayed correct throughout — they reach the runtime size by another route —
  so nothing that checked sizes could see it. `ptr_index` (the `p + n` path)
  had always been right; only the shortcut was not.
- **C17 6.7.6.2p4**: the size expression of a VLA type is evaluated at the
  DECLARATION, whether or not an object is created. `char (*p)[width]`
  declares a pointer, so nothing walking array chains saw a VLA.
- Two gates found things I did not: **the musl warning lane** caught the
  pointer-to-VLA regression as a one-TU drop in its parsed count, and **the
  arm64 MIR verifier** rejected `sub sp, sp, xN` outright — encoding 31 is
  XZR in the shifted-register form and the emitter has no extended form —
  instead of letting it assemble into nonsense.

**How all of it survived, and this generalizes**: the arm64 e2e corpus is
the x86 corpus re-run under qemu, and **no program in it used a VLA**. The
VLA fixtures stop at IR/MIR level, where the arm64 backend never sees them.
A corpus inherited from another target covers only what that target's
authors happened to write. Two executed programs now cover the shapes
(`tests/corpus/x86_64/int/vla_{calls,shapes}.c`); the arm64 corpus went
55 -> 57 and spill-all likewise, both ledgers still empty.

**The preventive**: `scripts/check_deferrals.sh` is in `make test`. No
message in `src/` may defer to a sprint at or below `ci/closed_sprints.txt`.
The roadmap is not in the repo, so that file is the in-tree source of truth
and raising it when a sprint closes forces the audit. Anti-vacuity checked:
set to 51 it catches the driver's live `-g on arm64` deferral.

Reproduce the audit any time with:

```sh
grep -rn 'lands\? in Sprint [0-9]*' src/      # then check ci/closed_sprints.txt
sh scripts/check_deferrals.sh
```

### STEPS 3 and 4 — DONE. **SPRINT 51 IS CLOSED**, all seven deliverables.

`246fe90`, `7977dca`, `ec32b34`, `6a28c43`, plus `359e9d9` for ABI-004.

**ci/closed_sprints.txt is now 51.** Raising it is the ratchet working:
it FORCES an audit of anything still naming 51, and the only such deferral
was the `-g` gate, which the DWARF work removed. Nothing needed renumbering.

**D7 (arm64 DWARF).** The seam was smaller than this file predicted --
`A64Inst` already carried `loc` -- so `src/cg/debug.{c,h}` takes an ordered
`CgDebugRow` sequence and both backends feed it. Proven inert on landing:
15 assembly outputs with `-g`/`-g3`/`-O2 -g` byte-identical to the previous
compiler, plus the 81-check x86 lane.

**CFI genuinely did not share, and the reason is worth keeping.** x86's FDE
is sixteen FIXED bytes because the prologue always moves the CFA by 16.
AArch64 allocates the whole frame in its first instruction, so the FDE
carries the frame size and no two functions share a program -- which is why
`frame_emit_prologue` records its own shape rather than the encoder
re-deriving which branch ran. Three CIE fields differ from x86 and EACH
would corrupt an unwind silently if copied: code alignment 4 (not 1),
return-address register x30 (not the synthetic 16), initial CFA naming SP as
register 31. `scripts/a64_debug_lane.sh` checks all three, verified by
mutation.

That lane reuses `tests/debug/dwarf_lines.c`, the x86 lane's fixture,
deliberately: a line table is a claim about SOURCE, so both targets must
resolve the same markers to the same lines. That IS D7's cross-target
agreement, and reusing the fixture made it free. Cross tools only -- nothing
runs -- so it lives in `make test` on an x86 host.

**ABI-004 closed at 315/315** with clang on nomad-1, from 24/304
disagreeing. Read the ledger entry: it was WRONG TWICE and both corrections
came from measuring clang, not from reading the note. See
`.docs/audits/abi-debt.md`.

**A cheaper future**, recorded because it changes a plan already written
down: `src/cg/debug.c` is now the ONE line-table and CU-DIE emitter for
every target, so DBG-001..004 (the variable DIEs behind the linker
diagnostic in task #93) get written ONCE rather than per backend.
`.docs/audits/debug-info-debt.md` has been corrected to say so.

### Historical state: Sprint 55 was under way

Taken out of numerical order on purpose: 28 deferrals pointed at it, it
blocks HOSTED compilation on macOS and FreeBSD, and Sprints 56/57 need it.

**THE ORGANIZING IDEA, and it decides everything else.** One question:
*what happens if we ignore this attribute?*

- cost is a diagnostic or a missed optimization -> accept, warn under
  `-Wattributes` (gcc's own flag; its default is flipped to ON to match)
- changes LAYOUT, LINKAGE or BEHAVIOUR -> hard error until implemented
- never heard of it -> accept and warn, exactly as gcc does, because a
  compiler that rejects a name it has never heard of cannot read next
  year's headers

`src/parse/gnu_attrs.def` is that table and `docs/gnu-extensions.md` is its
prose, gated by `scripts/check_gnu_tiers.sh`.

#### ELEVEN implemented, and what each one taught

`weak`, `visibility`, `packed`, `aligned`, `alias`, `used`, `__asm__("name")`
labels, `section`, `constructor`, `destructor`, `cleanup`.

- **`packed`**: drops the RECORD's alignment as well as its members'. Force
  the offsets alone and every offset a reader checks is right while `sizeof`
  keeps its tail padding. Injecting exactly that takes the layout differential
  400/400 -> 277/400, failing on `_Alignof` and never on an offset.
- **`aligned`**: the INVERSE of `_Alignas` -- it only ever RAISES, so a weaker
  request is silently declined and `aligned(1)` is NOT a spelling of `packed`.
- **`alias`**: two bugs only a real LINK showed. IPO deleted a static function
  reachable only through its alias (a `.set` is not a relocation, so the
  callgraph never saw it), and `.weak_definition` is Mach-O's spelling that
  ELF rejects.
- **`used`**: reaches the same IPO root set an alias target does, from the
  other direction.
- **asm labels**: rename the SYMBOL; the C identifier stays for source and
  diagnostics, which is why `lower_link_name` is a separate accessor. One of
  the TWO blockers for hosted macOS -- `__DARWIN_ALIAS` uses it.
- **`section`**: a named section forces PROGBITS, so an UNINITIALIZED object
  there gets real bytes rather than a `.bss` reservation -- otherwise it lands
  outside the section the author named.
- **`constructor`/`destructor`**: 65535 is the DEFAULT, not a maximum -- gcc
  emits the same plain `.init_array` for the bare form and for an explicit
  65535. A LOWER priority runs FIRST, and the destructor order is the exact
  mirror. Adding them found that EVERY symbol attribute was dropped on a
  definition with a prior plain declaration (bug #116).
- **`cleanup`**: the only LOWERING one. Its scope walk is not the VLA walk
  even though it shares the chain -- see the header for the two bugs that
  taught it, and for why the VLA shortcut is safe only because 6.8.6.1p1
  forbids the jump that would break it.

#### SPRINT 55 SCOREBOARD (checked against the sprint file, not recalled)

Five deliverables:

| | Deliverable | State |
|---|---|---|
| **D1** | Acceptance-tier table | **Done** — 12 implemented / 7 parsed-ignored / 6 refused, gated by `check_gnu_tiers.sh` |
| **D2** | Extended asm | **Done** — basic AND operand forms on both targets, §0b |
| **D3** | Full `__attribute__` set | **11 of a targeted 16** |
| **D4** | Expression/statement extensions | **Not started** |
| **D5** | `__GNUC__` policy | **Not started** — verified undefined in BOTH `-std=gnu17` and `-std=c17` |

D4 is the largest remaining chunk and is a LIST rather than one feature:
statement expressions `({...})`, `typeof` / `__auto_type`,
`__builtin_types_compatible_p` / `choose_expr` / `constant_p`,
`,##__VA_ARGS__` comma-swallow (a Sprint 5 deferral), `__builtin_offsetof`
array designators, case ranges `1 ... 5`, `a ?: b`, `[0]` arrays, empty
structs, `__label__`, `__thread`, `__extension__`.

Of the seven DoD gates: #1, #3 and #6 are met — asm passes O0–Os on both
targets and under every spill-all lane. #4 is partial (11 of 16 attributes,
packed differential green at 2000/2000); #2, #5 and #7 are open.

#### WHERE THE WORK IS, AND WHAT TO DO NEXT

**Everything that was silently wrong is fixed. Start from a clean slate.**

Closed in the session that wrote this: `section` (the 8th attribute), the
memory-summary shape gate, `.rodata`, AS-SET-002 (afs-as PR #29, so aliases
EXECUTE on arm64), the scalar/switch controlling-expression constraints, and
the runner's per-invocation scratch. **Known-wrong-but-shipping is ZERO** --
every open item is either a named refusal or a deliberate deferral.

**NEXT, in order:**

1. **D4 -- UNDER WAY.** Statement expressions are DONE; §0c has the state of
   the rest of the list, and **task #122 is the single biggest lever left in
   the sprint**.
2. **D5 `__GNUC__` -- LAST, and read the section below before starting it.**
3. **Then Sprints 52, 53, 54** -- compile speed, codegen quality, perf gates.

---

## 0c. Historical D4 progress record

**A SURVEY BEFORE IMPLEMENTING FOUND TWO ITEMS ALREADY WORKING.** `[0]`
arrays and `__builtin_constant_p` need nothing; the sprint file lists them
because it was written before they landed incidentally. Probe the list before
planning it -- 13 one-line compiles answered it.

**DONE: statement expressions `({ ... })`.** Value is the last EXPRESSION
STATEMENT; a trailing declaration or a trailing `if` makes the whole thing
void; empty `({ })` is legal; file scope is refused as gcc refuses it. A
`cleanup` variable inside runs at the statement expression's OWN `}` -- after
the value is materialized, before the enclosing expression continues, pinned
as `CTT` in `tests/corpus/x86_64/int/stmt_expr.c`. All measured against gcc
first; the plausible alternative ordering passes every test that does not
observe ordering.

Landing it retired THREE obsolete refusal assertions, each caught by a gate
that is exactly enforced in both directions -- a `tests/programs` fixture, a
unit-test line (CONVERTED rather than deleted, since the file-scope case is
still an error), and two c-testsuite ledger rows, taking that differential
from **215 to 217 agreeing**. Note `00214.c`'s ledger reason blamed
`__builtin_expect` "lands in Sprint 28", long closed: **`check_deferrals.sh`
scans `src/`, not test ledgers**, so deferral rot hides there.

**NEXT AND LARGEST: task #122.** `asm` accepts `volatile` and `__volatile__`
but not `__volatile` -- the middle GNU spelling, no trailing underscores.
`KW_ALT_VOLATILE2` ALREADY EXISTS in `keywords.def`; it is simply missing from
the two qualifier loops in `src/parse/stmt.c`, while its exact sibling
`KW_ALT_INLINE2` is present in one of them. The enumeration hazard again.

musl's `arch/x86_64/atomic_arch.h` uses it, so it blocks every TU that
includes atomics: fixing it takes the memory sweep from **1084 to 1276 of
1361** analyzed, deferrals 277 -> 85.

**It is deliberately NOT committed**, and the reason generalizes: the
newly-parsed TUs surface NINE warning false positives across six checkers,
and the musl warn lane's ORACLE (host gcc 16) now FAILS on three files where
the gcc 8 parity baseline merely warns -- gcc 14 made an implicit function
declaration an error, and `-Dweak=` strips the declarations musl relies on.
Both patches are preserved and the task carries the evidence. Do the oracle
severity fix first, then the nine.

**D2 IS DONE** -- basic and extended asm both, on both targets. §0b is its
record, including the three bugs the operand slice turned up in code that was
not the operand slice.

**STILL OPEN, sorted by what kind of thing they are:**

*Named refusals -- LEAVE THEM.* SEC-MACHO-001 (`section` on Mach-O needs a
SEGMENT,SECTION pair), PACKED-001, and the six still-refused attributes all
fail loudly and say what is missing. That is the tier table working. Closing
them is feature work, not gap-closing, and PACKED-001 in particular should
stay open: nothing consumes an over-claimed load alignment today, and the
evidence for that is recorded rather than assumed. The former over-aligned
automatic-object refusal was retired during Sprint 53 closeout on both
backends.

*Real but not urgent.*
- **Task #93 -- `-g` emits no variable DIEs**, so a debugger cannot name a
  data symbol's source line. A genuine `-g` gap and a substantial standalone
  DWARF piece; `src/cg/debug.c` being the ONE shared emitter now makes it a
  write-once job rather than per-backend.
- **Task #112 -- an explicitly zero-initialized global goes to `.data`**
  where gcc uses `.bss`. Cosmetic (file size), both writable, values correct.
  If you take it: a CONST all-zero object must keep real `.rodata` bytes,
  never `.bss`, because `.bss` is writable.
- **Task #113 -- unit tests write helper files to a fixed scratch path.**
  The benign remnant of #110: fixed filenames, identical content, `EEXIST`
  tolerated. Worth doing only if a third instance appears or either test
  starts writing content that varies per run.
- **Task #102 -- afs-as lacks x86_64 `@GOTPCREL`/`@PLT` operand syntax**, so
  `-fPIC` on x86 needs `CGF_AS=0`. Upstream work.

*The musl campaign (Sprint 57) and the bootstrap (Sprint 58)* are the real
destination, and `_Thread_local` had to work before either -- it does now.

#### HABITS THAT PAID, REPEATEDLY

- **Measure gcc BEFORE writing code.** It overruled the sprint's own tiering
  three times; preparing `aligned` uncovered `_Alignas` on an object doing
  nothing at all; and measuring PIC levels for `.rodata` caught that macOS is
  unconditionally position-independent, where `__TEXT` is read-only AND
  code-signed.
- **Check the ARTIFACT, not the instruction.** `readelf -sW` vs the emitted
  directive; the ADDRESS at run time vs `_Alignof`, which answers from the
  TYPE and is right even when placement is wrong; a SIGSEGV on write vs a
  `.section .rodata` line; linking and RUNNING vs reading assembly.
- **NEVER grep for a word that appears in both success and failure.** I
  checked whether `switch` was already constrained with `grep -c error`, saw a
  non-zero count, and excluded it -- the count was the ICE's own
  "internal compiler error" line, and the code then carried a comment
  asserting something false.
- **Mutate every new gate before trusting it.** Several gates have been
  vacuous on first run, including `ASM_CHECK-NOT`, which walked into
  F-S22-MIRCHECK: a new directive kind must ALSO be listed in `directive.c`'s
  `add_dir` or it parses, validates, and asserts nothing.
- **THE EXEMPTION IS THE HARD HALF of any new constraint.** Both constraints
  added recently nearly rejected correct C -- `(void)f()` for the cast rule,
  `if (arr)` and `if (fn)` for the scalar rule. Write the `_ok` fixture
  first, and make it EXECUTE when the claim is a runtime one.
- **A local green suite is not the fuzz job.** `make test` runs a 2,000-
  iteration smoke; CI runs 100,000 under sanitizers. That gap has now caught
  two real ICEs (seeds 76632 and 47924). Run the full 100k locally before
  pushing anything that touches `tests/programs` -- and note that re-pinning
  the digest CHANGES THE MUTATION SEQUENCE, so a corpus edit is never inert.
- **A ledgered recipe is a hypothesis.** AS-SET-002's was wrong in its first
  step and right in its trap. Re-measure before trusting one you wrote.
- **FREEZE THE TREE while a verification run is in flight**, and never use
  `git checkout <file>` to undo a scratch mutation in a file with uncommitted
  work -- it reverts everything. Both cost real time in one session.
- **A LOOP LABEL IS NOT EVIDENCE.** Verifying the arm64 asm fix I wrote a
  `for O in -O0 -O1 -O2 -Os` loop that printed the level in its output and
  did not pass `$O` to the compile. Four identical lines, four labels, ONE
  configuration actually tested. Same family as reading a count instead of
  the output: the report was generated by the harness, not by the thing under
  test.
- **AN AGGREGATE GATE CANNOT NAME WHAT REGRESSED.** The musl zero-false-
  positive lane is the best tool in this repo for FINDING warning bugs and a
  poor one for holding them fixed -- it says "1066 parsed, zero false
  positives", never which rule. Every fix it motivates gets its own fixture,
  and an exemption gets the FIRE case too (a local `volatile` must still
  warn), or widening the exemption later passes silently.
- **A TEST THAT NEVER RUNS LOOKS EXACTLY LIKE A TEST THAT PASSES.** The
  sources declared 628 unit tests and `--list` reported 627:
  `gen_unit_registry.sh` matches `^void test_...(TestCtx *` on ONE LINE, and
  clang-format had wrapped one declaration, so it was never registered and had
  never run since the day it was written. **Second occurrence** -- Sprint 36
  hit the identical trap and its note claims the count "is asserted by the
  full suite", which was not true of any check that existed. A fix that
  renames one test stops that instance, not the next one;
  `scripts/check_unit_registry.sh` (in `make test`) compares DECLARED against
  REGISTERED, because comparing the registry to itself proves nothing.
- **`check_bans` HAS NOW BEEN WIDENED TWICE FOR THE SAME REASON.**
  `tests/corpus/` was missing until the first packed program landed there;
  `tests/warn/` and `tests/memsafe/` until the first fixture asserting a
  warning ABOUT an attribute did. A fixture directory holding compiled C
  belongs on that exemption list the day it is CREATED. Mutate after widening
  -- an exemption that switches the gate off is silent.
- **ZERO `FAIL` LINES IS NOT A PASS.** Three consecutive `make test` stops
  this session -- a `-Werror` build break, `check_bans`, `check_format` --
  produced no `FAIL` line at all, because none of them was a failing test.
  `grep -c FAIL` would have called all three clean. Read the exit code and
  the tail.
- **THE SHELL HERE IS zsh, WHICH DOES NOT WORD-SPLIT.** A loop doing
  `flags=$(sed -n '1s|// FLAGS: ||p' "$f"); cgf $flags "$f"` passes the whole
  string as ONE argument, so every fixture came back "unrecognized
  command-line option" -- and since the check counted `warning:` lines, that
  read as "0 warnings, the fixture is vacuous". It was the harness. Use
  `${=flags}` in zsh, or pass the flags explicitly. Third counting mistake of
  one session, after a loop label that did not reach the compiler and a
  `grep -c FAIL` on a build break.

#### D5 IS A PROMISE, AND ONE FACT GOVERNS EVERY FIXTURE UNTIL THEN

While `__GNUC__` is undefined, glibc's `sys/cdefs.h` does
`#define __attribute__(xyz)`. **No hosted fixture can test any attribute** --
the preprocessor deletes it and the fixture passes no matter what the compiler
does. The first packed comparison "disagreed with gcc on every row" for exactly
this. EVERY attribute fixture is freestanding.

The reverse is the D5 risk: the day `__GNUC__` is defined, every attribute in
every system header goes live at once. The implemented table is the obligation
list.


## 1b-2. Process traps from the Sprint 50/51 sessions

**READ THE CI LOG BEFORE BLAMING THE INFRASTRUCTURE.** Four consecutive
runs (`c26dc62`, `07f18cc`, `dcc27d0`, `4d55d69`) were recorded here as "the
Actions outage created no runs". Runs WERE created and WERE failing, on a
bug of ours, for days. `gh run view <id> --log` returns nothing in this
environment; use:

```sh
gh run view <id> --json jobs --jq '.jobs[] | select(.conclusion!="success") | .name'
gh api repos/tenseleyFlow/Cgfried/actions/jobs/<job-id>/logs --allow-escape-sequences
```

**THE ASSEMBLER ROUTING TRAP — it has now bitten FIVE times**, the fourth
being those four red runs: `scripts/musl_cross_lane.sh` invoked `cgf`
directly and never routed its own assembler. cgf's default
assembler is the BUNDLED afs-as. Any lane that invokes `cgf` directly must
say `CGF_AS=0` (host target) or `CGF_AS_PATH=<cross as>` (cross target) when
afs-as is not built — and a Rust-free CI job never builds one.

- Sprint 49's native arm64 lane lost three rounds to it.
- **It is what made CI red for seven consecutive runs.**
  `tests/cross/determinism.sh` compiles and LINKS in its crt-probe check, died
  at "assembler not found" before the probe ran, and then reported *"the crt
  probe ignored --sysroot"* — because it only greps for the path it wanted to
  see. The check's bug, not the driver's: **a check that greps for success
  must distinguish "looked and found the wrong thing" from "never got that
  far."** Both the routing and the message are fixed.
- The ABI lane would have lost its first CI run the same way; its routing is
  now INSIDE the lane rather than left to the caller, verified by hiding
  afs-as and running both targets.
- **The FIFTH is the interesting one, because it wore a different face.**
  The first `__thread` corpus fixture came back 89/90 under BOTH host
  compilers with **arm64 clean at 74/74** — which reads like an x86 codegen
  bug, since a feature working on the harder target and failing on the
  easier one is the wrong way round. It was routing: the bundled assembler
  has no `%fs:`/`@tpoff` on x86 (TLS-004) and the driver refuses BY NAME,
  while arm64's `:tprel_hi12:` has been in afs-as since Sprint 51. The
  fixture needs `// ENV: CGF_AS=0`, which is inert on arm64 because the lane
  sets `CGF_AS_PATH` and an explicit path beats a mode. **A per-target
  split in a result is evidence about the TOOLCHAIN at least as often as
  about the compiler** — read the stderr before reading the codegen.

**Reproduce CI's Rust-free condition instead of guessing:**

```sh
mv afs-as/target/release/afs-as /tmp/  &&  mv afs-ld/target/release/afs-ld /tmp/
make test          # this is what CI actually runs
mv /tmp/afs-as ... # put them back
```

Hide BOTH or the `debug-notools` skip ledger will not match and you will chase
a phantom.

**Two full `make test` runs concurrently on this box produce failures that do
not reproduce sequentially** ("could not run produced binary", timing gates).
Verify ONE AT A TIME. Concurrency also produced a spectacular false positive:
ppfuzz hardcoded `build/fuzz-work/case.c` regardless of `BUILD`, so two trees
overwrote each other's input between the write and the two spawns and the
differential reported **128 diffs that reproduce nowhere**. Fixed (the scratch
dir now sits beside the binary), but the lesson stands.

**The fuzz digest hashes CORPUS CONTENTS.** Adding, removing or EDITING a
fixture moves it. Re-pin `ci/fuzz_sequence_digest.txt` LAST, after the
fixtures are final — re-pinning mid-edit just means doing it twice.

**`rsync --exclude 'build*'` matches any path component**, so it silently
omitted `src/ir/build.c` and the remote link failed on three missing symbols.
Anchor the exclusions (`/build`, `/build-*`).

**`pkill -f` / `pgrep -f <pattern>` match shells you did not mean.** Three
separate incidents. `pkill -f 'BUILD=build-s51-v'` killed the session's own
command, twice, because the pattern was in that shell's command line. Then a
`while pgrep -f 'BUILD=build-s52c'; do sleep; done` wait loop hung forever:
a LEFTOVER wrapper shell from an earlier command still carried that string,
so the condition could never go false. Waiting on `pgrep -f` is waiting on
"no process anywhere mentions this text", which is not the same as "the job
finished". Use a PID, a sentinel file, or the harness's own completion
notification.

---

## 1c. Concerns and judgement calls worth inheriting

Things a reader cannot reconstruct from the diff, written down because the
next person will otherwise re-derive them or, worse, quietly undo them.

**The Sprint 49 DoD is 7 of 7 — but it took three counts to get the number
right.** Two earlier revisions of that line disagreed with their own table.
The habit that fixed it: count the `**met**` rows, do not trust the prose. The audit table at the end of
`.docs/sprints/10-backend-arm64/s49-arm64-linux.md` says which is which. Do
not let "Sprint 49 complete" in a changelog become "arm64 is done" in your
head — the corpus is 50/51 and the remaining failure is real.

**The corpus ledger is a floor, not a ceiling.** 50 of 51 e2e programs pass
on arm64. That is a good deal short of "the arm64 backend works", and the
corpus is 51 small programs written to exercise x86. Sprint 57's musl
campaign will be a far harsher test. Treat the current arm64 backend as
"boots and runs simple programs", not as production.

**Do not start Sprint 50 (arm64-macos) before the backend gaps close.**
Sprint 50 is a new object format AND a new ABI on top of the SAME isel and
regalloc. With an aggregate-return crash or an NZCV structural bug still
live, every Sprint 50 failure would be ambiguous — Mach-O, Apple ABI, or the
shared backend? The aggregate-return case was the sharpest example: Sprint 50
is precisely about ABI differences in varargs and aggregates. That is why the
gap work was sequenced first, and the reasoning still holds for whatever
remains.

**`lower/f128.c` runs after the optimizer deliberately.** `simplify.c` folds
f128 through the same softfp core (Sprint 31). Moving the pass earlier — which
looks tidier, since it is nominally "lowering" — would hide constant f128
arithmetic behind opaque calls and silently lose that folding. The header
comment says so; do not "fix" the placement.

**The in-place `rewrite_as_call()` trick has a precondition.** It works only
because a call's operands ARE its arguments and its type IS its result, so an
f128 add and a call to `__addtf3` have identical shape. The moment a rewrite
needs a different operand count or an extra instruction — which comparisons
do — that trick does not apply and real insertion is required. Do not try to
stretch it.

**I corrected two user-facing reports this session; both times the reported
symptom was real and the reported CAUSE was not.** The installed-compiler
bug was reported as by-name-vs-full-path and was actually
installed-vs-dev-tree (the full path to the INSTALLED binary fails
identically). Reproduce before believing a causal story, including your own.

**Two of my own commits fixed causes I had inferred rather than read.** See
§3.6. The crt-multiarch work was genuinely needed, so it was not wasted, but
it was not the diagnosis I claimed it was. If you catch yourself writing "the
cause is X" without having seen X in a log or a debugger, stop and go get the
evidence — it is nearly always cheaper than the round trip you are about to
spend.

**Unverifiable CI steps are worth deferring.** The native arm64 lane took
three rounds partly because I shipped job steps I could not test locally. Two
Sprint 49 gates (char-sign and the atomics hammer on native hardware) are
still open specifically because I chose not to add a fourth unverified step
at the end of a long session. That was the right call; make it again if you
are in the same position.

## 2. The ritual (not optional)

1. **Read the sprint file first, in full.** It names its own pitfalls
   deliberately. Do not skim it.
2. Implement. Check the Definition-of-Done items — they are numeric;
   check them numerically, not by vibe.
3. **Run every lane before pushing**: `make test`, `make test-san`,
   `make BUILD=build-clang CC=clang all`, and valgrind over the paths
   you touched.
4. **When the sprint file and reality diverge, fix the file** in the
   same change, and append implementation notes to it.
5. Update `AGENTS.md`, `cp AGENTS.md CLAUDE.md`.
6. **Commit in chunks** (never one monolith), terse imperative
   messages under ~250 chars unless elaboration earns its space.
   **Never co-author. Never add "Generated with" trailers.**
7. Push, then watch CI in the background.
8. **Never leave a sprint partially finished.**

The user runs a **staggered CI policy**: continue into the next sprint
while CI verdicts land; if a run fails, *step back* and fix the prior
sprint before continuing. Honor it — do not stall waiting, and do not
plough on ignoring red.

---

## 3. Environment traps — the expensive class

Every one of these cost a CI round-trip or a container session. They
share a shape: **the code was correct on this machine and wrong
somewhere else.**

### 3.1 CI's `/bin/sh` is dash (hit THREE times)

Bashisms die there and nowhere else. Sprint 15: `echo` interpreting
backslash escapes. Sprint 28: `<(process substitution)`. Gated now by
`scripts/check_posix_sh.sh` (in `make test`), which parses every
harness script under dash and greps *code* (not comments) for behavior
bashisms.

**A parse check is not enough — RUN new lanes under dash:**
`dash scripts/your_lane.sh build/cgfried`.

### 3.2 CI's `test` / `test-san` jobs have no Rust (F-S25-RUSTFREE)

The bundled assembler `afs-as` is not built there. Compiler tests must
stay Rust-free (the Sprint 2 law). `make test` therefore prefixes
assemble/link lanes with `CGF_AS=0` when afs-as is absent — loudly,
never silently (`AS_LANE` in the Makefile).

**Reproduce locally by hiding the binary:**
`mv afs-as/target/release/afs-as{,.hidden}` — this trick has caught two
distinct bugs. Put it back afterwards.

### 3.3 Arch (dev) vs Ubuntu (CI) glibc differ in ways that matter

- **Header layout**: Debian/Ubuntu put glibc's `bits/` headers under
  `/usr/include/<triple>` only. Without that dir in the search list,
  `#include <stdio.h>` cannot resolve *at all*. Arch keeps everything
  in `/usr/include`, which is exactly why it hid.
- **`libc.a` internals**: Ubuntu's `setlocale.o` takes a TLS
  initial-exec path Arch's does not; its `.eh_frame` ships a `"zRS"`
  CIE. Both broke afs-ld static links on CI only.
- **`fwrite` is annotated nonnull** on CI's glibc, so writing an empty
  buffer is UB there and silent here (F-S26-FWRITE0). This is the
  repo's recurring **zero-length-UB family** — `memcmp(NULL,0)` was an
  earlier member. Guard every zero-length libc call.
- **binutils version**: `ld` 2.44 scans index-less archives; 2.42
  refuses. `gas` 2.42 and 2.44 pad NOP fill differently
  (F-S24-NOPVERSION). Never assert a version-dependent outcome —
  assert *agreement* between the two drivers instead.

**Root-owned build artifacts**: a podman run that mounts the repo
WRITABLE leaves root-owned files under `build/`, and the next native
build dies with "Permission denied" opening a `.d` file. Clear them with
`find build -not -user "$(id -un)" -delete` — the parent dirs are
user-owned so unlink is permitted. Mount `:ro` to avoid it.

**The tool for all of this is podman**, and it is cheap:

```sh
podman run --rm -v "$PWD":/w:ro docker.io/library/ubuntu:24.04 sh -c '
  apt-get update -qq >/dev/null && apt-get install -y -qq build-essential >/dev/null
  mkdir -p /t/bin && cp -r /w/include /t/include && cp /w/build/cgfried /t/bin/
  cd /t && printf "#include <stdio.h>\nint main(void){puts(\"ok\");return 0;}\n" > h.c
  CGF_AS=0 ./bin/cgfried h.c -o h && ./h'
```

**Verify anything touching headers, static linking, or libc under
podman before pushing.** It turns a 25-minute CI round-trip into 90
seconds.

### 3.4 A hosted compile is a different test from a freestanding one

Everything through Sprint 27 exercised freestanding paths. The moment
Sprint 28 compiled a program that includes `<stdio.h>` on a foreign
distro, two latent bugs surfaced — and fixing them **unblocked 58
c-testsuite programs** that had been pinned as deferred for unrelated
reasons. When something feels stuck at a suspiciously round number,
suspect the environment, not the feature.

### 3.5 Valgrind may need loader debug symbols, not a different machine

On this Arch glibc 2.44 host, Valgrind 3.25.1 can fail before the target
program starts with a mandatory `ld-linux` `memcmp` redirection error. That is
not compiler evidence and it is not a Valgrind incompatibility. Fetch the
loader's build-id debuginfo into a writable cache, then reuse that cache:

```sh
DEBUGINFOD_CACHE_PATH=/tmp/cgfried-debuginfod \
  debuginfod-find debuginfo <loader-build-id>
DEBUGINFOD_CACHE_PATH=/tmp/cgfried-debuginfod \
  valgrind --error-exitcode=99 --leak-check=full your-command
```

For the 2026-08-03 loader the build id was
`d1e0e87f381ead4885f87135128da0c20166f55f`. Always use `set -e` in a
multi-fixture Memcheck loop; otherwise a startup failure can be followed by a
misleading final success message.

---

### 3.6 READ THE LOG — both fetch commands fail QUIETLY (Sprint 49)

This cost three CI round-trips on one job, and two of the three "fixes"
were for causes that had been INFERRED rather than read.

- `gh run view --log` and `gh run view --job <id> --log` **print nothing
  here**. No error, no exit code — just empty output, which reads exactly
  like "the log is empty".
- `gh api repos/O/R/actions/jobs/<id>/logs` refuses with *"the response
  contains terminal escape sequences"* unless you pass
  **`--allow-escape-sequences`**.

The invocation that works:

```sh
id=$(gh run list --branch trunk --limit 1 --json databaseId -q '.[0].databaseId')
jid=$(gh api "repos/O/R/actions/runs/$id/jobs" --jq '.jobs[]|select(.name=="JOB")|.id')
gh api --allow-escape-sequences "repos/O/R/actions/jobs/$jid/logs" \
  | sed 's/\x1b\[[0-9;]*m//g' | tail -30
```

The failure mode is not "I could not get the log", it is "getting the log
was harder than guessing, so I guessed." A plausible cause stated as a
diagnosis is worse than saying you do not know: it produces a commit that
looks like a fix. If you cannot read the log, say so and make the CI step
print what you need on failure.

### 3.7 A table mixing string literals with runtime-built entries (Sprint 49)

The crt probe's rows were four string literals. Making one target-derived
(`/usr/lib/<multiarch>`) meant building it into a buffer — and
`cgf_probe_crt_dir` RETURNS whichever row matched, so a caller-stack buffer
dangled. Invisible on Arch, where the match is the literal `/usr/lib64`;
broken on every Debian-layout host, which is all of CI.

The shape generalizes: **when a lookup table mixes static and constructed
entries, a lifetime bug hides behind whichever row a given host happens to
match.** The guard is a test that runs where the constructed row wins —
`test_toolchain_crt_probe` is host-sensitive on purpose, and its comment
says so. Do not "simplify" it into a pure-function test.

Related: the Debian multiarch tuple is **not** the target name. Debian
spells arm64 `aarch64-linux-gnu`; the closed target set calls it
`arm64-linux`. `cgf_target_multiarch()` exists for exactly that, with a
unit test asserting the two differ.

### 3.3b The qemu cross lane compiled against HOST headers (Sprint 49)

The driver builds its system include list for the TARGET
(`/usr/include/aarch64-linux-gnu`, `/usr/include`), but on an x86 host those
are the HOST's headers. **Arch has no multiarch directories at all**, so
`/usr/include` holds everything and a cross compile there silently succeeds
against x86 glibc headers. Ubuntu keeps glibc's `bits/` under
`/usr/include/x86_64-linux-gnu` and has no aarch64 directory, so the same
compile fails outright.

No corpus fixture noticed for a whole sprint, because **none of them includes
a SYSTEM header** — they hand-declare `int printf(const char *, ...)`, and the
only `#include` in the whole corpus is `<stdarg.h>`, which is ours.
`tests/corpus/char_sign` is the first that does, and it went red on CI while
passing locally.

`scripts/a64_corpus_lane.sh` now passes `-isystem $SYSROOT/include` in the
cross branch. Note what this means: **the lane had been wrong the entire
time** and only ever looked right because Arch's layout let host headers
stand in. If you add a fixture that includes a system header and it passes
locally, check which `stdio.h` it actually opened (`-v` prints the search
list) before believing it.

### 3.7c "Verified locally" means nothing if you did not COMMIT it

The afs-as submodule bump went red on a lane I had run and watched pass
minutes earlier. The edit that made it pass (`UNENCODABLE=""`) was in the
working tree and never staged — the commit took `afs-as` and `HANDOFF.md`
only. So the local run proved a tree that trunk did not have.

`git status` before pushing costs a second and would have caught it. The
underlying error was treating the local run as the proof and the commit as
bookkeeping; they are one step, and the artifact CI tests is the commit.

What saved it was the ratchet failing in the direction that looks harmless:
**"neon is pinned unencodable but assembled"**. A lane that only checked for
NEW failures would have let trunk carry a stale pin, silently claiming a
fixture was unsupported when it had started working. Enforce debt lists in
both directions, always.

### 3.7b A CI step that redirects to a log can THROW IT AWAY (Sprint 49)

```yaml
run: |
  make some-lane > out.log 2>&1; s=$?   # <-- WRONG
  cat out.log
  [ $s -eq 0 ]
```

GitHub runs `run:` blocks under `bash -e`. When `make` fails, `-e` fires on
that first line and the step exits **before `cat out.log`** — so the entire
diagnostic goes into a file nobody ever prints, and the log shows nothing but
`Process completed with exit code 2`. The flaw is invisible while the step
passes, which is every run until the one you need it.

Use a condition context, which `-e` does not trigger on:

```yaml
run: |
  s=0
  make some-lane > out.log 2>&1 || s=$?
  cat out.log
  [ "$s" -eq 0 ]
```

Also: **GNU make exits 2 for ANY failed target**, not with the recipe's
status. `exit code 2` from a make step tells you nothing about the cause;
do not read it as a signal.

### 3.8 `RT_TARGET` is evaluated at PARSE time (Sprint 49 follow-up)

`RT_TARGET := $(shell $(BUILD)/cgfried -dumpmachine || echo ...)` runs when
make reads the Makefile — which on a fresh checkout is **before the compiler
it probes exists**. So the fallback is not an edge case; it is what every
clean build uses. It was hardcoded `x86_64-linux-gnu`, which on the native
arm64 runner filed the arm64 runtime under `x86_64-linux-gnu/` where the
driver never looks, and every long-double program failed to link. It is now
derived from `uname`, because for a native build the host IS the target. A
cross build must still say `make RT_TARGET=arm64-linux` — the corpus lane
does.

Two smaller traps found fixing it:

- **An unescaped `)` inside `$(shell ...)` closes the function call.** A
  shell `case` with `Linux/arm64)` truncated the expansion and `RT_TARGET`
  came out EMPTY — silently, producing `build//libcgf_rt.a`. Use make
  conditionals, or check `make -p | grep '^RT_TARGET'` before believing it.
- The corpus lane's "no libcgf_rt.a beside $CGF" guard is what turned this
  from two mysterious fixture failures into one sentence naming the cause.
  Guards that name a build gap earn their keep.

## 4. Architectural laws (violating these is a silent miscompile)

These are written in the code as comments; they are repeated here
because each one was learned the hard way.

- **THE OPERAND-ORDERING LAW** (Sprint 21, violated 3× since): in isel,
  operands must *materialize before* their consumer emits. Every
  violation looked like a wrong answer far from the cause
  (F-S25-ICMPIMM64 was the third).
- **Dominance is not block-layout order** (F-S30-FWDSSA). mem2reg can expose
  a value whose dominating definition is visited later by isel around a
  backedge. Operand materialization reserves the value's vreg first; the later
  definition bridges into that stable identity. Never restore the assumption
  that a layout-order use already has a selected definition.
- **ARM64 MIR is ABI-neutral through Sprint 47.** Calls preserve exact callee,
  argument annotation, result, variadic, and noreturn metadata, but no physical
  AAPCS64 register may be assigned before Sprint 48.
- **ARM64 NZCV dependencies are explicit indices.** Any insertion, deletion,
  pairing, or relaxation before a flags consumer must repair `flags_src`; a
  nearby flags-setting instruction is not an implicit substitute.
- **Register 31 is an instruction-form property.** SP and XZR share encoding
  bits but are distinct MIR identities. Validate every position, including
  memory base/index and both legal GP sides of FP/GP `fmov`; the XZR-source
  forms `fmov dN,xzr` / `fmov sN,wzr` are the +0.0 idioms.
- **Two-edge conditional MIR occupies eight emitted bytes.** The taken edge
  uses the narrow conditional range; the explicit false edge is an ordinary B
  with an independent imm26/local-target check.
- **Optimizer scratch is phase-local.** Printer/verifier/dominance/pass
  analysis state must use short-lived arenas; verify-after-each fixpoints must
  not grow the module arena per invocation. Persistent mem2reg undef records
  are the exception, and they record consuming `Span`s plus declaration/name
  provenance for Sprint 40.
- **No host FPU in the constant engine.** All float math goes through
  `src/util/softfp.c`. `scripts/check_no_host_fpu.sh` enforces it.
  Rounding happens in exactly one place (`round_pack`) so no value is
  ever rounded twice. The current `SoftFloat` does not retain NaN payload or
  signaling state, so the optimizer must not fold an operation or conversion
  whose result is NaN: those bits are observable through IR bitcasts.
- **`undef` is per-read freedom, not a reusable unknown value.** Simplify and
  CSE must not merge or reflexively cancel expressions whose operands can be
  undef or whose operation can produce undef (div/rem/shift). SCCP treats an
  undef branch as overdefined and keeps every successor executable.
- **SCCP stays sparse and verifier-clean.** Value changes visit real use lists,
  executable edges are indexed directly, and block parameters meet executable
  incoming edges only. Rewriting a constant terminator must immediately prune
  its now-orphaned blocks; CFG cleanup scratch is exact-sized, never capped at
  a guessed function size. When `switch` becomes `br`, clear `case_val` so the
  printer/parser structural law still holds.
- **Local CSE is not load CSE.** Sprint 31's table is block-scoped and pure-op
  only. Loads remain distinct across every store until Sprint 32's shared alias
  service can prove otherwise.
- **One alias service, two clients.** `src/opt/alias.{c,h}` owns points-to,
  offset and escape facts for both optimization and Sprint 41 memory safety.
  Queries are pure and any IR mutation invalidates the context; rebuild it.
  Character/union effective types suppress only type-based NoAlias — proven
  distinct objects and disjoint byte ranges remain structurally NoAlias.
- **GVN inherits the undef law.** May-undef is transitive through operands,
  and an undef-tainted stored value cannot be forwarded to a load. Treating
  either as a reusable congruence class silently correlates independent reads.
- **Jump-thread caps include terminators.** Both the 12-instruction clone cap
  and 1.15x growth accounting charge the replacement branch. Generated clone
  names are source-independent and collision-checked so long labels cannot
  make printed IR unparsable.
- **One interprocedural callgraph, two clients.** `src/opt/ipo.c` owns direct
  and unknown callees, address-taken facts, and deterministic Tarjan SCCs for
  both IPO and inlining. Never fork the analysis. A recursive callee is never
  inlined even from outside its SCC: cloning its recursive edge creates a
  fresh eligible site and otherwise expands without bound.
- **Memory summaries are may-effects unless a checked annotation says must.**
  An inferred conditional free joins toward unknown; it never manufactures a
  proven freed state. Recursive/indirect top summaries and exhausted
  block-parameter correlation budgets degrade toward silence. Infer first,
  diagnose annotation mismatches second, then apply the annotation contract.
  Attribute metadata must be cloned and ABI-parameter indices remapped by
  every IR transform that copies a function. A top summary may retain local
  inference for mismatch diagnostics, but callers may consume its return
  ownership only when `cgf_returns_owned` explicitly supplies that contract.
- **Pinned effects have explicit pass policies.** Ordinary passes preserve
  the exact volatile/seq_cst sequence; the inliner may add only metadata-equal
  clones while leaving originals ordered; IPO may delete whole functions but
  must preserve every surviving function's pinned sequence. New mutating
  passes must choose a policy deliberately.
- **`-g` currently disables inlining.** Sprint 29 promises concrete function
  breakpoints and backtrace frames at O0 and O2, while the DWARF backend has no
  abstract-origin/inlined-subroutine records yet. `inl_debug_info` is the
  explicit boundary; removing it without richer DWARF silently destroys the
  debugger contract.
- **Loop analysis is invalid after any IR mutation.** `loop_tree_build` is
  pure scratch state. Canonicalization performs one boundary edit, renumbers,
  then rebuilds dominators and the loop tree before touching another loop;
  arena-backed block-array growth makes retained `IrBlock *` especially
  dangerous.
- **LCSSA is transitive through outside joins.** A loop-defined live-out must
  cross an exit parameter and continue through outside block parameters until
  every outside use names an outside definition. Fixing only direct exit uses
  leaves later joins pointing back into the loop.
- **LICM execution means exits and every backedge source.** Dominating every
  exit is insufficient when an inner cycle can bypass the candidate forever.
  Calls, atomics, cmpxchg, `va_start`, `stacksave`, and `stackrestore` are
  memory-state barriers for both load hoisting and store sinking.
- **Signed no-wrap is explicit IR provenance.** `IRF_NSW` comes only from
  signed source arithmetic when `-fwrapv` is off. Opcode-changing rewrites
  clear it; unsigned and `-fwrapv` arithmetic never acquire it.
- **Loop IDs are transaction-stable, not eternal.** `LoopInduction` and
  `TripInfo` keep block/value IDs only across a planned mutation. Any
  renumber/compaction invalidates them; rebuild dominance and the loop tree.
  `loop_clone_region` preallocates blocks/values, remaps block params, results,
  internal edges and LCSSA exits, and preserves flags/locations/pinned
  metadata without retaining raw block pointers across array growth.
- **Sprint 35 partial unroll is constant-trip only.** Exact trips 9–12 use a
  serial remainder peel plus a factor-four loop, preserving FP order and
  pinned-operation metadata. Runtime `TripInfo` currently recognizes syntax
  but does not prove modular termination, so runtime-bound unroll remains the
  explicit `unroll_runtime_unsupported` bail.
- **Unswitch clones static effects but preserves dynamic effects.** Exactly one
  specialized loop executes per invocation, so cloning volatile operations is
  legal when the invariant condition DAG is speculation-safe. `IRF_NSW`
  arithmetic is not hoisted without a non-overflow proof. The shipped cap is
  deliberately stronger than the roadmap: at most one unswitch per function
  and at most 2× function growth.
- **Dependence unknown always forbids restructuring.** Exact affine distances
  use the sign `iteration_b - iteration_a`; distinct pointer expressions prove
  independence only through non-unknown, disjoint points-to object sets. Byte
  offsets/types alone cannot prove different bases. Fusion additionally
  requires exact constant trips because syntactically equal runtime guards do
  not prove both loops terminate. A second-loop external operand must dominate
  the first loop's preheader, and any direct second-loop live-out forbids the
  rewrite.
- **BCE facts are edge-sensitive and proof-only.** Ranges are keyed by value
  and incoming edge, step overshoot is retained, and `-fwrapv`/subword modular
  crossings bail. `IRF_BOUNDS_CHECK` is the Sprint 44 provenance bridge: only
  marked or ordinary user comparisons proven constant may be folded.
- **Fission/interchange are deferred, not stubbed.** There is no aggressive
  loop flag or no-op CI lane. Their whole-iteration reordering needs a broader
  dependence proof and the later torture/self-host evidence base.
- **Sprint 36 vectorization is exact-constant-prefix only.** A nonmultiple trip
  executes `trip % VF` scalar iterations first, then the vector loop. Runtime
  trips, runtime alias versioning, if-conversion, SLP and min/max reductions are
  hard bails/deferrals. The first successful source-loop rewrite canonicalizes
  and rebuilds LCSSA before looking for reductions; otherwise the reduction's
  live-out is not in the form the vectorizer proves.
- **Do not splat loop-varying values.** Only invariants may become `vsplat`.
  The induction variable needs a lane sequence, and a scalar recurrence read by
  its own update is a prefix scan, not a reassociable reduction. Both mistakes
  survived ordinary positive cases and now have dedicated negative fixtures.
- **Vector IR has a deliberately closed ABI.** Exactly six 128-bit types exist;
  calls, parameters and returns reject them. Vector loads/stores reuse ordinary
  memory opcodes, supported arithmetic is an explicit verifier matrix, and
  vector spill/edge homes are 16 bytes. An odd callee-save count reserves an
  eight-byte frame gap so those homes, align-16 allocas and vararg save areas
  remain aligned.
- **Fast math is one argv-ordered bundle, not independent half-policies.**
  `-Ofast` selects O3 plus the bundle; `-ffast-math`/`-fno-fast-math` toggle it;
  a later `-O` resets to that level's default. Component spellings warn and are
  inert. `__STDC_IEC_559__` stays undefined in every mode because dynamic fenv
  semantics are not implemented.
- **The ISA ceiling is a closed allow table.** Unknown objdump mnemonics fail;
  checking only a denylist cannot prove SSE2. The 50-file corpus at six levels
  must produce exactly 300 audited objects. afs-as PR #21 added the nine packed
  instructions newly emitted here, with gas-parity immediate ranges.
- **No host `sizeof` / conditional compilation in `src/sema/`.**
  `char` is unsigned on arm64-linux; a host assumption there
  miscompiles every cross build with no diagnostic
  (`scripts/check_sema_target.sh`).
- **`src/target.c` is the SOLE target-fact site.** Headers are written
  against predefined macros so one header set serves every target.
- **One register allocator at every opt level** (Sprint 22).
  `check_bans.sh` greps `regalloc.c` for `opt_level`.
- **`link_inputs` is NEVER reordered** (Sprint 27) — archive extraction
  is position-dependent and a drop-in `cc` must reproduce gcc's
  order-sensitive failures. Gated in `check_bans.sh`.
- **The bootstrap rule**: `src/rt/` must never use 128-bit `/` or `%`,
  because Sprint 58 compiles it with cgf and those lower to calls to
  the very functions defined there.
- **Determinism defenses exist because Sprint 58 needs a byte-identical
  bootstrap**: insertion-ordered strmap, stable mergesort, numeric
  `.byte` emission, `ar rcsD`. Do not "simplify" them away.
- **`__GNUC__` stays undefined until Sprint 55** — glibc headers
  neutralize `__attribute__` themselves because of it. Load-bearing.
- **Warning checkers consume existing semantic truth.** Conversion checkers
  inspect materialized implicit casts and target widths; float proofs use
  `softfp`. Statement/switch/fallthrough checks run only after ordinary sema.
  Do not re-derive conversions or introduce checker-local option state.
- **Unused bookkeeping follows the root object, not just the leaf lvalue.**
  `s.member` and true array-decay subscripts write the declared aggregate or
  array; `p[i]` reads the pointer and writes an untracked pointed-to object.
  Preserve this distinction when adding new lvalue forms.
- **Warning oracles run in the same compile mode.** GCC 8 emits some warnings
  only during `-S`, so those fixtures say `-S`; the harness never silently
  broadens only the oracle. Strict-C89 oracle copies blank runner metadata
  line-preservingly. The musl oracle must exit successfully before any of its
  warnings can certify a CGF result.
- **Format checking runs after semantic conversions.** Compare the materialized
  promoted argument type; do not independently replay default promotions.
  Scanf is the intentional inverse: its stored-object type is unpromoted and
  pointer-qualified at every level. Conditional format expressions may carry
  multiple literal alternatives, and every distinct alternative must be
  checked.
- **Format extensions follow the target libc contract, not one “GNU” bit.**
  printf `%m` and apostrophe grouping are Linux features (gnu + musl); `I`,
  glibc redirects, syslog and asprintf are GNU-libc rows; BSD err/warn rows are
  FreeBSD-only. Scanf `m` allocation is POSIX and is accepted on every current
  hosted target. Target wchar signedness comes from `TargetSpec`.
- **Differential normalization is an assertion, not a filter.** The sole
  Sprint 39 exception is fixture-marked `-Wformat-unbounded-scanf`: the harness
  first requires exactly one extra Cgfried diagnostic with that ID, removes
  only it, then compares the remaining set. A general “ignore Cgfried extras”
  path would make the oracle vacuous.
- **Default `-Wmem` findings require proof.** Fire only on a singleton
  allocation site or a must-nonheap target; imprecise aliasing, lost path
  correlation, and unknown byte ranges degrade toward silence. Unknown calls
  escape ownership in the default tier; their possible UAF is
  `-Wmem-strict`. Realloc is pending until a null/non-null result branch
  resolves success versus failure.
- **The musl memory budget covers lowered TUs, not parse failures.** Unsupported
  Sprint 55 GNU syntax stops before analysis IR. The CI gate therefore pins
  the upstream commit, the 733 analyzed / 628 deferred split, and SHA-256
  digests of both normalized identity sets. Do not
  describe a deferred TU as warning-clean; newly parseable files must move the
  checked baseline and enter the zero-diagnostic set.
- **No silent stubs.** A placeholder that returns a plausible value is
  worse than one that aborts. Every deferral names its sprint in the
  diagnostic; `src/rt/fp128.c` aborts rather than computing wrong math.

---

## 5. Harness traps (a test that passes vacuously is worse than none)

- **F-S22-MIRCHECK**: a directive was missing from the runner's
  dispatch table, so *nine goldens passed vacuously* for a whole
  sprint. When adding a directive, add it to `add_dir` **explicitly**
  and write a meta-fixture that proves it can FAIL.
- **OPT_EQ must have an anti-vacuity fixture.** It runs every listed level
  through the normal compile/run contract and compares runtime stdout+exit;
  `tests/runner/meta/opt_eq_fail.c` deliberately differs by level and must
  report both names.
- **Never write a listing into the directory being listed** — the
  redirect races the command (green here, red on CI).
- **A harness's notion of "target" was a fact about ITSELF** (Sprint 49).
  `cgf-test` took its target from `cgf_target_host()` — its own binary's
  architecture. Correct-looking for four sprints, because runner and
  compiler-under-test always matched. The moment an x86 runner drove an
  arm64 compiler, every `ASM_CHECK(x86_64-linux-gnu):` in the shared corpus
  applied to arm64 assembly and SEVEN fixtures failed for that reason alone.
  `CGF_TEST_TARGET` overrides it; `CGF_TEST_RUN` prefixes the command that
  executes a produced binary. Ask of any harness property: *is this about
  the harness, or about the thing under test?*
- **Shared fixtures get a LEDGER, not edits.** The e2e corpus is 51
  gcc-verified expectations used by both targets. arm64 debt lives in
  `ci/expected_a64_corpus_failures.txt` — one line per (fixture, opt level)
  with its cause — enforced EXACTLY in both directions, so a new failure and
  a repaired one both fail the lane. Same shape as the `UNENCODABLE` pin in
  `scripts/a64_objdiff_lane.sh`. Never edit 51 shared files to record one
  target's gap.
- **POSIX `sh` has no locals.** `is_pinned() { for name in ...; }` clobbered
  its caller's `$name`, so every fixture after the first was tested under the
  last pinned fixture's identity. The lane reported 13 failures that were one
  bug. Give helper loops distinctive variable names.
- **Assert the artifact exists before comparing behavior.** Two
  missing binaries compare `127 == 127` and pass while proving nothing.
- **Expectations must be gcc-verified before pinning.** Four staged
  expectations from Sprints 18–23 were simply wrong (hand-computed
  sums; the ulp at 2^63 is 2048 not 1024; `va_list` is an *array* type
  so callees advance the caller's cursor). The rule was instituted in
  Sprint 25: probe gcc, then pin.
- **Derive paths from `BUILD`, never from a glob** — `build*/…` handed
  the sanitizer lane the wrong tree's artifact.
- **XPASS is a failure.** If a ledger entry starts agreeing, delete it.
- The **fuzz digest** (`ci/fuzz_sequence_digest.txt`) pins the mutation
  sequence. It changes legitimately when the corpus grows — re-pin
  then, and only then.

---

## 6. Gates (`make test` runs all of these)

| Gate | Protects |
|---|---|
| `check_bans.sh` | qsort/attributes/strtok/rand, single `getenv` site, one allocator, no `link_inputs` reorder, emit.c workarounds must cite a findings ID |
| `check_posix_sh.sh` | harness scripts parse under dash |
| `check_format.sh` | clang-format 22 (pinned; `CGF_FORMAT_REQUIRED=1` in CI) |
| `check_no_host_fpu.sh` | no float/double in the constant engine |
| `check_sema_target.sh` | no host assumptions in sema |
| `check_pp_seams.sh` | no stale `LANDS_IN_SPRINT` markers in `src/pp/` |
| `check_verify_coverage.sh` | every IR verifier check has a firing fixture |
| `check_fuzz_crashes.sh` | a crash in `tests/fuzz/crashes/` fails the build until fixed |
| `check_skips.sh <profile>` | the exact HARNESS_SKIP set per profile |
| `check_ub_division.sh` | ARM64/x86 differential fixtures cannot depend on divide-by-zero or signed-min/-1 UB; simple local propagation is tracked |
| `a64_mir_lane.sh` | ARM64 pre-RA MIR is verified, deterministic, and byte-exact against full snapshots |
| `a64_asm_diff.sh` | afs-as Mach-O and GNU-as ELF encode identical `.text`, including all 5,334 logical masks and production-packed fields |

Differential lanes (each is an *oracle*, not a golden): `header_diff`,
`rt_diff`, `driver_matrix`, `objdiff_lane`, `afsld_lane`,
`debug_info_lane`, `e2e_gcc_diff`, `ctestsuite_diff`, `layout_diff`, `fp_diff`,
`init_diff`, `inline_diff`, `lex_diff`, `parse_diff`, `spill_all_lane`,
`opt_driver`, `s33_ipo_driver`, `s34_loop_driver`.
Sprint 38 adds `warn_diff` (real GCC 8 in CI) and `musl_warn_dryrun`; both use
the same compile mode on both sides, and the latter rejects oracle failures.
Sprint 39 expands `warn_diff` to the full 400-fixture tree, adds a generated
64-row/128-fixture format matrix gate, and adds `tinycc_warn_dryrun` with the
same strict oracle-subset contract as musl.
Sprint 42 adds `test-mem-warnings` (11 exact proof-trace sequences), the
89-file semantic `tests/memsafe/wmem` corpus, optional 20-case
`test-mem-fanalyzer`, and the pinned `musl-sweep` zero-diagnostic CI lane.
Sprint 43 adds `test-mem-interproc` (50 fixtures plus 13 exact traces),
`header_portability.sh` under both host compilers, exact `__CGFRIED__`
predefine checking, and the same pinned musl zero-diagnostic budget with
summaries enabled.
Sprint 44 adds `test-mem-runtime` and `bench-safe` for deterministic runtime
failures, mixed/static linking and the overhead budget. Sprint 45 adds
`test-mem-autofix`, covering parseable/application semantics, all transform
families, auto-init and the annotation ratchet. Sprint 46 adds
`test-safe-mode`, `safe-dogfood`, the exact ELF-note mixed-link lane,
`check_safe_mode_doc.sh`, and the shrink-only symbol allowlist gate. The
normative contract is `doc/safe-mode.md`; `.docs/sprints/09-memory-safety/
s46-findings.md` records the VLA/stack correction and dogfood defects.
Sprint 47 adds `test-a64-mir`, `test-a64-asm-diff`, and
`check-ub-division`. Local runs may use Clang's integrated AArch64 assembler
with an exact skip; the CI toolchain job builds afs-as, installs GNU binutils,
and requires both oracles with zero skips.

**Design differentials so the oracle can't be faked.** The best ones in
this repo: `layout_diff` hands gcc `_Static_assert`s built from *our*
numbers (gcc accepting the file *is* the proof); `rt_diff` links one
probe against both runtimes (proving correctness and symbol
compatibility at once, since the link wouldn't resolve otherwise).

---

## 7. Submodule ritual (afs-as, afs-ld)

Both are separate repos under `FortranGoingOnForty/`. **Fix gaps
upstream; never work around them locally.** Six PRs merged so far
(afs-as #18/#19/#20/#21, afs-ld #17/#18).

Three steps, and stopping after step one is the classic mistake:

1. branch + commit *inside* the submodule, push, open PR, watch its CI
   (`gh pr checks N --watch`), merge;
2. `git checkout trunk && git pull && cargo build --release` in the
   submodule;
3. **back in Cgfried, `git add afs-ld && git commit`** to bump the pin.

Their quality gates: `cargo test`, `cargo clippy --all-targets -- -D
warnings`, `cargo fmt --check`. Rust is for **tools only** — the
compiler and its tests never require it.

Open upstream debt: `.docs/audits/afsld-elf-debt.md` (LD-ELF-001..005,
with two already fixed and recorded).

---

## 7b. Cross-target verification (arm64, Sprints 47-51)

The host constraint is GONE (2026-08-04): `aarch64-linux-gnu-gcc` and
`qemu-user-static` are installed. arm64 can be built AND executed here.

**`--target=` exists as of Sprint 51**, so an arm64 object no longer requires
cross-BUILDING the compiler — `cgf --target=arm64-linux -c` works from the
native binary, which is how the ABI differential drives it. The corpus lane
still cross-builds, because it wants an arm64 compiler running under qemu
(two levels of emulation) rather than a cross-compiler:

```sh
make CC=aarch64-linux-gnu-gcc BUILD=build-a64 build-a64/cgfried
sh scripts/a64_corpus_lane.sh      # does this for you, then runs the corpus
```

Under qemu the compiler's own `execve` of `as`/`ld` reaches the HOST, so
they must be routed by absolute path (`CGF_AS_PATH`, `CGF_LD_PATH`,
`CGF_CRT_DIR`) or you silently get x86 objects. `scripts/a64_corpus_lane.sh`
builds that wrapper; copy it rather than re-deriving it. **`CGF_AS=0` is NOT
the answer for a cross target** — it means "system as", which is the host's.

`scripts/qemu-run.sh` is the ONE place that knows how to run a foreign
binary. It is a passthrough on an arm64 host, which is what lets a single
lane serve both. It exits **125** when it cannot run at all — distinct from
any corpus exit code, so "could not run" never reads as "ran and failed".

`clang --target=aarch64-linux-gnu -O1 -S` remains the ABI oracle, and since
Sprint 51 the **ABI differential** (§1b) is the stronger one: it links our
half against gcc's and RUNS it, both directions.

**The arm64 corpus is the x86 corpus re-run.** That is efficient and it is
also a blind spot — it only covers what the x86 authors happened to write.
It is why arm64 VLAs stayed broken through two sprints and x86 VLAs were
silently wrong at the same time (§1b-1). When you fix a target-specific gap,
add a program that exercises it to the EXECUTED corpus, not just an IR-level
fixture -- that is exactly what the VLA fixtures had failed to do.

CI has three arm64 lanes: `test-arm64-native` (real `ubuntu-24.04-arm`
hardware — leaner image, so install what you need and remember the default
assembler is the BUNDLED afs-as, i.e. pass `CGF_AS=0` in a Rust-free job),
`test-arm64-qemu` (which also runs the ABI differential at 250 signatures per
arch), and the object differential inside `toolchain`.

**nomad-1 (Darwin arm64) has the full Apple toolchain AND cargo.** Remote
commands need `sh -lc` — it defaults to fish, and a non-interactive `sh -c`
does not get Homebrew's PATH. That single fact was misread as "cargo is
missing" for an entire sprint. Sync with anchored excludes:

```sh
rsync -a --delete --exclude '/build' --exclude '/build-*' --exclude '/.git' \
  --exclude '/afs-as/target' --exclude '/afs-ld/target' ./ nomad-1:/tmp/cgfried-s51/
ssh nomad-1 'sh -lc "cd /tmp/cgfried-s51 && make -j8 build/cgfried build/abigen"'
```

`make all` FAILS on macOS — `src/rt/fp128.c` needs `mode(TF)`, which Apple
clang lacks on arm64. Build the specific targets you need.

## 8. Deferrals you will trip over

Every deferral names its sprint in the diagnostic:

```sh
grep -rn 'lands in Sprint' src/ | sed 's/.*Sprint \([0-9]*\).*/\1/' | sort -n | uniq -c
```

Counts drift; re-grep rather than trusting a number written here. As of the
Sprint 51 session the shape is: **Sprint 55** (~18 — GNU `__attribute__`,
`typeof`, `__builtin_types_compatible_p`, `__builtin_choose_expr`; the
largest cluster by far and the reason §1b-1 STEP 4 takes 55 out of order),
**Sprint 23** (~13), **Sprint 19** (~9), **Sprint 57** (cross-TU sema).
`_Complex` is out of v0.1.0 entirely.

**A deferral naming a CLOSED sprint is a lie in the ledger**, and nine of
them existed at the end of Sprint 51. They were audited by REACHABILITY —
compile a program that should hit each one — and the outcome is in §1b-1.
Eight were dead defensive messages; the ninth was arm64 VLAs, a live
user-visible ICE on a shipped target, and pulling on it found four more
defects including a SILENT x86 miscompile. Audit by reachability, never by
reading: a message that looks alarming may be unreachable, and a message
that looks routine may be the only thing standing between a user and
standard C. `scripts/check_deferrals.sh` now keeps the numbers honest, but
it cannot tell reachable from dead — only you can.

`-g` on arm64-linux hard-errors naming Sprint 51 — deliberate, not an
oversight: emitting arm64 objects that silently lack a line table would break
the `-g` contract without saying so. See §1b-1 STEP 3.

---

## 9. Quick verification cheat-sheet

```sh
make                      # build (also builds libcgf_rt.a)
make test                 # everything: units, fixtures, all lanes, all gates
make test-san             # ASan+UBSan, own build tree — CATCHES WHAT test MISSES
make BUILD=build-clang CC=clang all
make tools                # cargo-build afs-as/afs-ld (Rust; tools only)

# after touching headers / static links / libc — before pushing:
#   run the podman snippet in §3.3

# single lanes while iterating:
sh scripts/header_diff.sh build/cgfried
sh scripts/rt_diff.sh build/cgfried
sh scripts/driver_matrix.sh build/cgfried
sh scripts/debug_info_lane.sh build/cgfried
sh scripts/opt_driver.sh build/cgfried
sh scripts/s35_loop_driver.sh build/cgfried build/cgf-test
sh scripts/s36_vector_driver.sh build/cgfried build/cgf-test
sh scripts/s36_isa_driver.sh build/cgfried
CGF_DIFF_GCC8=gcc-8 sh scripts/warn_diff.sh build/cgfried
sh scripts/musl_warn_dryrun.sh build/cgfried
sh scripts/tinycc_warn_dryrun.sh build/cgfried
make BUILD=build test-mem-warnings
make BUILD=build test-mem-interproc
make BUILD=build test-mem-runtime
make BUILD=build test-mem-autofix
make BUILD=build test-safe-mode
make BUILD=build safe-dogfood
make BUILD=build test-a64-mir
make BUILD=build test-a64-asm-diff   # both afs-as + GNU as in CI toolchain job
make BUILD=build test-a64-corpus     # 55 e2e fixtures under qemu
make BUILD=build test-a64-spill-all  # the same, every interval spilled
make BUILD=build test-abi-diff       # ABI differential, both Linux targets
make BUILD=build check-ub-division
make BUILD=build bench-safe
make BUILD=build test-mem-fanalyzer       # optional GCC 10+ comparator
make BUILD=build musl-sweep               # pinned 733/1361 analyzed, <90s
make BUILD=build check-format-matrix
CGF_TEST_CC=build/cgfried build/cgf-test --profile linux-x86_64 tests/programs

# the ABI differential at soak scale (a disagreement is an ABI bug):
CGF_ABI_DIFF_TARGET=arm64-linux CGF_ABI_DIFF_COUNT=300 \
  sh scripts/abi_differential_lane.sh build/cgfried
# on nomad-1, for arm64-macos (clang is the reference; gcc has no darwin/arm64):
ssh nomad-1 'sh -lc "cd /tmp/cgfried-s51 && CGF_ABI_DIFF_TARGET=arm64-macos \
  CGF_ABIGEN=build/abigen sh scripts/abi_differential_lane.sh build/cgfried"'

# reproduce CI's Rust-free condition BEFORE blaming CI (§1b-2):
mv afs-as/target/release/afs-as /tmp/ && mv afs-ld/target/release/afs-ld /tmp/
make test        # hide BOTH or the debug-notools skip ledger will not match

# CI:
gh run list --limit 3
gh run watch <id> --exit-status
gh api repos/tenseleyFlow/Cgfried/actions/jobs/<job-id>/logs \
  --allow-escape-sequences        # `gh run view --log` returns NOTHING here
gh workflow run ci                # push events raised during an Actions
                                  # outage are LOST; re-trigger by hand
curl -s https://www.githubstatus.com/api/v2/components.json | \
  python3 -c "import sys,json;print([c['status'] for c in \
  json.load(sys.stdin)['components'] if c['name']=='Actions'][0])"
```

Useful env knobs (all read in `toolchain.c`, the single `getenv` site):
`CGF_AS=0` (system gas), `CGF_LD=1` (bundled afs-ld, `-static` lane),
`CGF_CRT_DIR`, `CGF_INCLUDE_DIR`, `CGF_SPILL_ALL=1`,
`CGF_DUMP_BAD_IR=path`, `CGF_VERIFY_AFTER_EACH=1`,
`CGF_OPT_BAIL_LOG=1`. `-ftime-report` is a driver flag, not an env knob.
Sprint 35 adds independent bisection toggles:
`CGF_OPT_DISABLE_UNSWITCH=1`, `CGF_OPT_DISABLE_BCE=1`, and
`CGF_OPT_DISABLE_FUSION=1`. Sprint 36 adds
`CGF_OPT_DISABLE_VECTORIZE=1`.

---

## 10. The user's context

They are relearning C from K&R in `~/scratch/C/ch{1-5}` and want `cgf`
as their daily compiler for it. That is the acceptance test that
actually matters to them — hosted programs, real headers, readable
diagnostics, and now source-level gdb stepping/backtraces on cgf-built
binaries.

They care about: honest reporting (say what failed and show the
output), no silent shortcuts, tests and CI as first-class, and
commits that would be pleasant to bisect.
