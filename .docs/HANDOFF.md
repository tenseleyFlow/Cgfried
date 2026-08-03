# HANDOFF — read this before touching anything

You are picking up **Cgfried**, a from-scratch C17 compiler. Sprints 0–44
are complete and CI-green. This file is the *transferable* part of what
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

- **Sprints 0–44 complete; Phases 1–8 closed.** Phase 9 (memory safety) is
  under way on top of the completed preprocessor, frontend, sema, IR,
  x86_64 backend, driver, and optimizer.
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
  11 exact trace sequences. The musl gate analyzes 732/1,361 pinned TUs,
  explicitly baseline-defers 629 pre-Sprint-55 GNU-syntax TUs, emits zero
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
- **Next action: Sprint 45** —
  `.docs/sprints/09-memory-safety/s45-autofix-transforms.md`, adding auto-fix
  transforms and machine-applicable fixits.
- **Pending parallel-test follow-up:** `tests/fuzz/ppfuzz.c` still writes every
  run to `build/fuzz-work/case.c`. Two concurrent full suites can replace that
  file between the Cgfried and gcc oracle runs and report false differentials.
  Sprint 41 validation reproduced this with concurrent Clang/sanitizer runs
  and proved both isolated 2,000-case runs clean. Give each process/build a
  private work directory before relying on parallel local `make test` runs.

Metrics to compare against after your changes (all must hold or improve):

```
unit: 560 tests, 96179 assertions, 0 failures
cgf-test: total=504 pass=504 fail=0 xfail=0 xpass=0 skip=0 config=0
memsafe warning fixtures: 89/89; exact ordered trace sequences: 11
interprocedural memsafe fixtures: 50/50; exact ordered trace sequences: 13
focused memsafe units: 52 tests / 764 assertions
safe runtime: 27 checks, 0 skips; three interim benches within 2.5x/2x
musl -Wmem gate: 732 analyzed, 629 pinned deferrals, 0 memory diagnostics, <90s
format warning fixtures: 203/203; flow warning fixtures: 77/77; all warning fixtures: 477/477
format matrix: 64 semantic rows / 128 fire+nofire fixtures
GCC 8 warning differential: 409 exact + 36 normalized CGF-only + 32 annotated, 0 unannotated
musl warning dry-run: 711 parsed, 650 deferred, 183 genuine, 0 false positives
TinyCC warning dry-run: 10/30 parsed, 20 deferred, 0 format warnings, 0 false positives
warning matrix: 222/222 raw GCC 8 C rows accounted for
OPT_EQ corpus: 50/50 at O0/O1/O2/O3/Os/Ofast; verifier-after-each also green
ctestsuite_diff: 220 files, 215 agree, 5 known-deferred, 0 new, 0 xpass
header_diff: 148 macro/type lines byte-identical to gcc
rt_diff: 2317 result lines identical to libgcc
driver_matrix: 39/39 rows agree with gcc
objdiff: 38/38 · e2ediff: 10/10 · afsld lane: 12 fixtures
debug_info lane: 81 checks with tools/gdb; 6 addr2line rows
pp_dm_check: 182 predefines checked; __CGFRIED__=1; __GNUC__ absent
memsafe foundation: 14 deterministic fixtures; 17 focused units / 264 assertions
```

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

---

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

---

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
  the upstream commit, the 732 analyzed / 629 deferred split, and SHA-256
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

## 8. Deferrals you will trip over

`grep -rn "Sprint 55" src/` and friends — every deferral names its
sprint in the diagnostic. Current counts: **Sprint 55** (22 sites: GNU
`__attribute__`, `typeof`, `__builtin_types_compatible_p`,
`__builtin_choose_expr`), **Sprint 51** (6: `-shared`/`-fPIC`/`-fpie`),
**Sprint 49** (6: arm64 fp128 soft-float bodies), **Sprint 57** (2:
cross-TU sema). `_Complex` is out of v0.1.0 entirely.

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
make BUILD=build bench-safe
make BUILD=build test-mem-fanalyzer       # optional GCC 10+ comparator
make BUILD=build musl-sweep               # pinned 732/1361 analyzed, <90s
make BUILD=build check-format-matrix
CGF_TEST_CC=build/cgfried build/cgf-test --profile linux-x86_64 tests/programs

# CI:
gh run list --limit 3
gh run watch <id> --exit-status
gh api repos/tenseleyFlow/Cgfried/actions/jobs/<job-id>/logs   # failed-job detail
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
