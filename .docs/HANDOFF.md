# HANDOFF — read this before touching anything

You are picking up **Cgfried**, a from-scratch C17 compiler. Sprints 0–50 are
complete; **Sprint 51 is at 6 of 7** and §1b-1 says exactly what to do next
and in what order. This file is the *transferable* part of what
was learned building them: the traps that cost real debugging time, the
invariants that look like style but are load-bearing, and the ritual the
work follows. It is not a substitute for the sprint files — it is the
thing that stops you re-learning what already hurt.

---

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

## 1. Current position

- **Sprints 0–50 complete; Phases 1–9 closed. Sprint 51 is at 6 of 7** — see
  §1b for what landed and §1b-1 for the ordered plan of what is next. Phase
  10 (second backend and targets) is under way on top of the completed
  preprocessor, frontend, sema, IR, x86_64 backend, driver, optimizer,
  warnings, and memory-safety phases.
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
- **EIGHT GNU attributes are implemented**: `weak`, `visibility`, `packed`,
  `aligned`, `alias`, `used`, `__asm__("name")` labels, `section`. §1b-1 has
  what each one taught and, under THE PICK, what to do next and why.
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
- **Known-wrong-but-shipping is now ONE item**: a void expression is accepted
  where a scalar condition is required (task #108). Everything else open is a
  NAMED refusal, which is a legitimate resting state — see §1b-1's THE PICK.
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
- **Do not run the two arm64 lanes in parallel.** `tests/runner/main.c` builds
  every scratch path from a literal `build/test-work/` rather than from its
  own BUILD tree, so `make -j test-a64-corpus test-a64-spill-all` has both
  clobbering the same files and fails nearly the whole corpus with "the
  assembler rejected cgfried-generated assembly … line 0". It is a false
  failure and reproduces nowhere sequentially — same shape as the ppfuzz
  `build/fuzz-work/case.c` bug (task #110). This is the second time a shared
  hardcoded scratch path has manufactured a spectacular phantom result.
- **VLAs work on both targets**, as of the deferral reckoning (§1b-1). They
  did not: arm64 ICEd on every one and x86 silently miscompiled any VLA in a
  function that also passed arguments on the stack. Multidimensional VLAs
  and pointers-to-VLA were wrong too.
- The arm64 e2e corpus is **61/61**, and **61/61 again under
  `CGF_SPILL_ALL=1`**. Keep both green; the spill lane exists because exactly
  one fixture at one level once caught a stale NZCV producer index. Run them
  ONE AT A TIME — see the shared-scratch trap above.
- Verified at the end of the Sprint 51 session, sequentially and locally
  (never two suites at once — §1b-2): gcc and clang `make test` both rc=0,
  **625 unit tests / 4,263,106 assertions**, 514 and 477 fixture profiles,
  arm64 corpus and spill-all 55/55 each, ABI differential 304/304 per Linux
  target in both directions. Re-verified after the VLA campaign: same unit
  counts, 516 and 477 fixture profiles, arm64 corpus and spill-all **57/57**
  each, musl warning lane unchanged at 716/1361 parsed / 645 deferred / 186
  oracle-matched / zero false positives. Re-verified again after `section` and
  the summary-table shape gate: gcc and clang `make test` both rc=0 at **626
  unit tests / 4,263,123 assertions** and 477 fixture profiles, arm64 corpus
  and spill-all **61/61** each, memsafe 90/90 + interproc 50/50 + foundation
  14, safe-mode 56/56, e2ediff 10/10, a64 objdiff 20 identical / 0 pinned.
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

## 1b. WHERE THE WORK IS RIGHT NOW (Sprint 51 at 6 of 7)

**Sprint 50 (arm64-macos) is CLOSED**: three DoD gates met as written, three
partial with the reason named and ticketed. The gate-by-gate audit is at the
end of `.docs/sprints/10-backend-arm64/s50-arm64-macos.md`.

**Sprint 51 is at 6 of 7 deliverables.** D1 (host/target split, `--target=`,
`--sysroot=`), D2 (PIC/PIE per architecture, `-shared`), D3 (TLS), D4 (musl
and FreeBSD bring-up), D5, and D6 (the ABI differential) have landed and are
verified. **D7 (the cross-target DWARF differential) is BLOCKED** — see
below, it is not a harness task.

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

## 1b-1. THE NEXT WORK, in the order it should be done

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

### SPRINT 55 IS UNDER WAY. Read this before touching it.

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

#### EIGHT implemented, and what each one taught

`weak`, `visibility`, `packed`, `aligned`, `alias`, `used`, `__asm__("name")`
labels, `section`.

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

#### THE PICK, if you are asking what to do next

**Sort the open items first, because they are not the same kind of thing.**

*Named refusals -- LEAVE THEM.* Sprint 53's over-aligned stack objects,
SEC-MACHO-001, and the six still-refused attributes all fail loudly and say
what is missing. That is the tier table's whole point. Closing them is feature
work, not gap-closing.

*Silently wrong today -- these are the real ones.* Only two:

1. **`.rodata` -- MY PICK.** Const globals go in `.data`. Every `const`
   global in every program sits in a WRITABLE segment. It is also the root of
   the `"aw"` vs `"a"` divergence documented under `section`, so fixing it
   RETIRES that entry instead of leaving a permanent footnote. Self-contained:
   a read-only flag on `IrGlobal`, set from the type's constness at lowering,
   consumed by two emitters.
   **MEASURE FIRST:** gcc puts a const global whose initializer contains a
   RELOCATION into `.data.rel.ro` under PIC, not `.rodata`. Get that wrong and
   `-shared` builds hand the dynamic linker a read-only segment it must write.
2. **Task #108** -- a void expression accepted where a scalar condition is
   required. Small, and adjacent to the void-conditional typing fixed this
   session.

*Coverage, not correctness.* **AS-SET-002** blocks arm64 EXECUTION coverage for
aliases; the recipe including the cursor trap is in
`.docs/audits/afsld-elf-debt.md`, so it is a focused change now rather than an
exploration. **PACKED-001** should stay open: nothing consumes an over-claimed
load alignment today, and the evidence for that is recorded rather than
assumed.

After those: `constructor`/`destructor`/`cleanup` as a group (they share the
"runs outside main" story and need `.init_array`), then D2 extended asm, D4
statement expressions + typeof, and **D5 `__GNUC__` LAST**.

#### D5 IS A PROMISE, AND ONE FACT GOVERNS EVERY FIXTURE UNTIL THEN

While `__GNUC__` is undefined, glibc's `sys/cdefs.h` does
`#define __attribute__(xyz)`. **No hosted fixture can test any attribute** --
the preprocessor deletes it and the fixture passes no matter what the compiler
does. The first packed comparison "disagreed with gcc on every row" for exactly
this. EVERY attribute fixture is freestanding.

The reverse is the D5 risk: the day `__GNUC__` is defined, every attribute in
every system header goes live at once. The implemented table is the obligation
list.

#### HABITS THAT PAID, REPEATEDLY

- **Measure gcc BEFORE writing code.** It overruled the sprint's own tiering
  three times, and preparing `aligned` is what uncovered `_Alignas` on an
  object doing nothing at all.
- **Check the ARTIFACT, not the instruction.** `readelf -sW` vs the emitted
  directive; the ADDRESS at run time vs `_Alignof`, which answers from the TYPE
  and is correct even when placement is wrong; linking and RUNNING vs reading
  assembly -- both `alias` bugs were invisible until a real link.
- **Mutate every new gate before trusting it.** Three gates this session were
  wrong or vacuous on first run, including one I wrote (`ASM_CHECK-NOT`) that
  walked straight into F-S22-MIRCHECK: a new directive kind must ALSO be listed
  in `directive.c`'s `add_dir` or it parses, validates, and asserts nothing.
- **A local green suite is not the fuzz job.** `make test` runs a 2,000-
  iteration smoke; CI runs 100,000 under sanitizers. That 50x gap caught a real
  ICE (seed 76632). Run the full 100k locally before pushing anything that adds
  a fixture to `tests/programs`.


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

**THE ASSEMBLER ROUTING TRAP — it has now bitten FOUR times**, the last one
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
