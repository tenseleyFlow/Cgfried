# F09 memory safety — CLOSED

- Review date: 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Scope: `src/memsafe/`, `src/sema/safe.c`, the `-fsafe` driver/runtime
  boundary, and `doc/safe-mode.md`
- Sequencing: F05 was closed before this review began. F09 is closed with five
  durable expected findings; no compiler source was modified.

## Findings

~~ID: `MS-C-01`~~
~~Title: an exact warning opt-out weakens -fsafe initialization safety~~
~~Severity: Critical — `doc/safe-mode.md` guarantees that default-tier~~
~~memory-flow warnings and definite-uninitialized reads are errors, but an exact~~
~~`-Wno-mem-uninit-read` option silently disables one of those diagnostics under~~
~~`-fsafe`. A documented guarantee without an enforcing mechanism is Critical~~
~~under the Sprint 60 rubric.~~
~~Reproducer: `tests/audit-regressions/ms-c-01.c`~~
~~Root cause: `src/driver/args.c:1228-1242` appends group-level~~
~~`-Werror=mem` and `-Werror=uninitialized` after parsing the command line, but~~
~~the warning resolver gives an exact diagnostic option higher specificity than~~
~~a group option. Therefore the earlier exact disable remains authoritative.~~
~~The control without the exact opt-out exits 1 with~~
~~`[-Werror=mem-uninit-read]`; the reproducer exits 0. Automatic local-variable~~
~~zeroing does not repair the heap byte used by this test.~~
~~Affected sprints: 42, 45, 46.~~
Resolution: RESOLVED 2026-08-20 by `7a003d68`.
Cluster hunt: exercised exact, group, global, demotion, both argv orders, and
source-pragma attempts against every default-tier memory diagnostic plus
definite uninitialized reads; non-safe and optional-warning behavior stayed
unchanged.

ID: `MS-M-02`
Title: a nonheap equality guard leaves an infeasible leak path
Severity: Medium — a default-tier false positive on an ordinary ownership
idiom makes `-Wmem` noisy on real code, but does not miscompile or weaken a
safe-mode runtime guarantee.
Reproducer: `tests/audit-regressions/ms-m-02.c`
Root cause: the lifetime path state preserves the opened-resource fact across
`file != stdin` without correlating the equality branch with the fact that
`stdin` is nonheap. It consequently reports a leak on an infeasible path. In
curl `src/tool_parsecfg.c`, the only `fopen` result is closed when
`file != stdin`; when the close is skipped, the value is the published
standard stream instead.
Affected sprints: 42, 43.

ID: `MS-M-03`
Title: freopen replacement is falsely reported as leaked
Severity: Medium — a default-tier false positive rejects a conventional
standard-stream replacement under `-Werror=mem`, but does not miscompile.
Reproducer: `tests/audit-regressions/ms-m-03.c`
Root cause: the built-in resource model treats a successful `freopen` result
as newly acquired local ownership. It does not model the API invariant that
the return is the same stream object supplied as the third argument, still
published through `stderr`. This is the second curl diagnostic, at
`src/tool_stderr.c:63`.
Affected sprint: 43.

~~ID: `MS-C-04`~~
~~Title: -fsafe accepts a statically proven null dereference~~
~~Severity: Critical — `doc/safe-mode.md` explicitly says proven-null~~
~~dereferences in the default memory tier are errors, but the direct null~~
~~dereference compiles successfully under `-fsafe`. A documented guarantee~~
~~without an enforcing diagnostic mechanism is Critical under the Sprint 60~~
~~rubric.~~
~~Reproducer: `tests/audit-regressions/ms-c-04.c`~~
~~Root cause: `WARN_NULL_DEREFERENCE` is default-off and has no flow checker;~~
~~`WARN_MEM_NULL_CHECK` is a strict-tier allocation-result guard diagnostic, not~~
~~a dereference proof. The memory lifetime pass can retain a runtime check for~~
~~the access, but it emits no required compile-time error for this statically~~
~~known null value. Even explicitly enabling `-Wnull-dereference` produces no~~
~~diagnostic.~~
~~Affected sprints: 42, 46.~~
Resolution: RESOLVED 2026-08-20 by `b1bc4f91`.
Cluster hunt: covered literal, folded, branch-proven, member/subscript,
multi-step ptradd/bitcast, access, indirect-call, and correlation-saturation
paths while preserving one-way proof soundness. Zero-extent exemptions cover
both branch shapes, both multiplicative factors, direct IR operations, and
bounded source/destination library roles; unrelated `FILE *` and format
pointers remain independently checked.

~~ID: `MS-C-05`~~
~~Title: far heap out-of-bounds pointers evade the runtime registry~~
~~Severity: Critical — an emitted `-fsafe` check accepts a heap access thousands~~
~~of bytes beyond its allocation, contradicting the documented heap-spatial~~
~~guarantee. This is a demonstrated guarantee failure and therefore Critical~~
~~under the Sprint 60 rubric.~~
~~Reproducer: `tests/audit-regressions/ms-c-05.c`~~
~~Root cause: the compiler correctly leaves one runtime access check immediately~~
~~before the dynamic load (`4 total, 3 discharged, 1 emitted`). At runtime,~~
~~`cgf_safe_check` calls `find_containing_locked` with the already-offset~~
~~pointer. `src/rt/cgf_safe_alloc.c:427-434` returns success when that address is~~
~~not contained in any registered raw allocation, classifying it as unknown~~
~~provenance. A sufficiently large offset lands beyond the allocation header,~~
~~payload, and canary, so the registry cannot recover the base allocation and~~
~~the process exits 0 instead of trapping.~~
~~Affected sprints: 44, 46.~~
Resolution: RESOLVED 2026-08-20 by `b287c2ef`.
Cluster hunt: terminal origin-relative derivation guards and pre-modular raw
index guards cover direct, loop, select, load/store, and helper-call transport;
signed extrema, scaled wrap, subtraction, one-past/crossing access, null,
adjacent allocations, foreign origins, quarantine/UAF, zero-byte allocations,
and the documented `uintptr_t` grammar are pinned. The guard is explicitly
no-capture/no-dereference in both lifetime and alias analysis. Atomic pointer
read-modify-write is rejected under `-fsafe` because it cannot be validated
before publication.

~~ID: `MS-C-06`~~
~~Title: asprintf ownership bypasses safe-runtime registration~~
~~Severity: Critical — safe mode classifies `asprintf`/`vasprintf` output as~~
~~owned heap storage, yet far accesses through that storage bypass the runtime~~
~~registry and therefore the documented heap-spatial guarantee.~~
~~Reproducer: `tests/audit-regressions/ms-c-06.c`~~
~~Root cause: `src/memsafe/checks.c` declares both functions as allocation~~
~~families whose result is written through argument zero, while the runtime and~~
~~linker wrap only nine other allocation APIs. The returned libc allocation has~~
~~no Cgfried header or registry entry, so residual checks treat it as foreign~~
~~and silently accept a far pointer. Safe mode must either register these~~
~~families or reject the unsupported ownership boundary.~~
~~Affected sprints: 43, 44, 46.~~
Resolution: RESOLVED 2026-08-20 by `88211779`.
Cluster hunt: direct calls, address-taking, compatible aliases, and effective
linker names for both unsupported families are rejected. Static and
incompatible same-name functions plus declarations renamed away from the libc
identities remain valid, so the boundary rule does not become a name-only ban.

## Real-corpus false-positive recount

Every emitted `-Wmem` diagnostic in the three required corpora was triaged:

| Corpus | Pinned identity and analyzed surface | Result | Triage |
|---|---|---|---|
| musl | commit `b306b16af15c89a04d8e0c55cac2dadbeb39c083`; exact identity-set baseline | 1,293/1,361 analyzed, 68 pinned syntax deferrals, zero diagnostics | no finding |
| SQLite | 3.46.1; pinned amalgamation, shell, and speedtest sources under the campaign flags | 3/3 translation units accepted, zero diagnostics | no finding |
| curl | 8.9.1 archive SHA-256 `f292f6cc051d5bbabf725ef85d432dfeacc8711dd717ea97612ae590643801e5`; the 222 source commands recovered from the successful pinned build | 222/222 accepted, two diagnostics | both false: `MS-M-02` and `MS-M-03` |

The curl recount is source-command coverage, not a claim that every
conditionally excluded curl source was analyzed. Its exact results and
per-source diagnostics are retained under `build/f09-audit/curl/`.

## Safe-mode claim dispatch

Every normative claim in `doc/safe-mode.md` was mapped to its enforcement
point and an executable check. The deferred-extension section is explicitly
future work and was not counted as a shipped guarantee.

| Documented contract | Enforcement mechanism and evidence | Audit outcome |
|---|---|---|
| `-fsafe` composes `-fcgf-safe`, both error groups, and zero automatic initialization | post-argv composition in `src/driver/args.c:1228-1242`; `scripts/safe_mode.sh` checks the dry-run surface, conflicts, exact/group/global opt-outs, demotions, both argv orders, and source pragmas | mechanism and precedence tests agree after `MS-C-01` closed at `7a003d68` |
| Safe-TU boundary only; unsafe TUs remain unchecked | instrumentation is performed only on each module compiled with `fcgf_safe`; safe-link tests mix explicitly allowed unsafe code | mechanism matches the stated limit |
| Heap spatial safety | retained accesses plus origin-relative derivation, raw-index, and `uintptr_t` round-trip guards; runtime suite covers OOB and transformation failures at O0/O2 | mechanism and transport/extrema tests agree after `MS-C-05` closed at `b287c2ef` |
| Heap temporal safety, bounded to 1,024 blocks or 8 MiB | wrapped allocation/free registry and FIFO eviction in `src/rt/cgf_safe_alloc.c`, constants at lines 29-30; runtime suite covers UAF reads/writes, double free, churn, and canaries | mechanism matches the documented finite-quarantine limit |
| Definite initialization and automatic scalar zeroing | flow and memory-uninitialized analyses precede lowering; `src/lower/stmt.c` inserts zero stores/memsets after analysis for fixed and variable automatic storage; focused flow/auto-init suites | required diagnostic floor is enforced after `MS-C-01` closed at `7a003d68` |
| Null safety | retained safe accesses call `cgf_safe_check`, whose null case traps before registry lookup; lifetime analysis rejects statically proven null accesses and indirect calls | compile-time and runtime halves agree after `MS-C-04` closed at `b1bc4f91` |
| Integer/pointer casts rejected except the stated `uintptr_t` grammar | `src/sema/safe.c` validates derivation, constant `+`, `-`, `&`, `|`, tag/mask order, integer origin, width, and final cast; 8 accepted and 9 rejected fixtures | mechanism and grammar tests agree |
| Pointer-overlapping unsafe unions rejected | recursive layout/range comparison in `src/sema/safe.c:620-749`; safe-mode tests cover nested, repeated, huge-array, bitfield, parameter, and system-header cases | mechanism and adversarial fixtures agree |
| Inline assembly rejected | parser/safe semantic rejection; `reject-asm` fixture | mechanism present |
| `setjmp`/`longjmp` families rejected | safe semantic pass identifies direct, decayed, aliased, and built-in family names; six rejection fixtures plus an ordinary error-return acceptance control | mechanism present |
| Variable `alloca` rejected; constant `alloca` and VLAs allowed | safe semantic expression check plus constant-expression test; rejection and acceptance fixtures | mechanism matches stated boundary |
| Volatile/device provenance loss rejected | safe cast checks and `reject-volatile-io` fixture | mechanism present |
| Accepted programs retain ISO C17 meaning | safe pass rejects unsupported forms rather than rewriting them; runtime instrumentation is opaque and terminal after optimization | no additional mismatch found beyond the two guarantee failures above |
| Every safe object carries versioned `.note.cgf.safe` | note emission plus the bounds-checked ELF64LE reader in `src/driver/safe_elf.c`, requiring `CGF\0`, type 1, version 1; `scripts/safe_mode.sh` checks safe and unsafe objects | mechanism present |
| Every explicit user object/archive is validated or exactly allowed | `safe_link_inputs_ok` validates every `LINK_OBJ`; allowance comparison is exact string equality; mixed-link tests cover absent, wrong, and exact paths | mechanism present; explicit archives have no separate focused fixture |
| `-l` libraries and driver CRT are exempt boundaries | only direct `LINK_OBJ` inputs enter note validation; link-library inputs and injected CRT do not | mechanism matches the documented limit |
| Raw `-Xlinker`/`-Wl,` channels are rejected | every `LINK_RAW` fails before link; object, response-file, and linker-script cases are tested | mechanism present |
| Unsafe callees may be called but their implementation is outside the guarantee | safe mode accepts boundary calls; annotations/summaries affect caller analysis only | mechanism and acceptance fixture agree |
| Dogfood compiles all compiler TUs with zero exemptions | `scripts/safe_dogfood.sh` compiles and note-checks 106 TUs, links, and smokes the result | fresh gate passed with zero exemptions |

## Escape and allowlist audit

`ci/safe-mode-allowlist.txt` and
`ci/safe-mode-allowlist.baseline.txt` contain zero active entries, so there
was no source exemption to re-justify. `scripts/check_safe_allowlist.sh`
requires `path:symbol owner justification`, rejects duplicate keys and any
key absent from the reviewed baseline, and permits shrinkage. The fresh policy
gate proved both directions.

The only link escape is the documented exact-path
`-fsafe-allow-unsafe=<path>`. A wrong path is rejected and the matching path
is accepted. System `-l` inputs and driver-injected CRT objects are explicit
documented trust boundaries, not hidden allowlist entries. Raw linker options
cannot be used to bypass validation.

## Probes without additional findings

- The focused warning suite passed 50/50 interprocedural cases and all 13
  exact ordered trace sequences.
- The runtime suite passed 27 checks at O0/O2, including UAF read/write,
  double/invalid free, free-time canary damage, ordinary OOB, allocator
  families, alignment, foreign/mixed allocation, interposition, threading,
  and instrumentation accounting.
- The safe semantic suite passed 56/56 construct and `uintptr_t` cases.
- The safe-mode link/policy gate passed composition, note validation,
  exact-path allowance, raw-link closure, documentation schema, and allowlist
  shrink/growth checks.
- Safe dogfood compiled 106 compiler translation units with `-fsafe`, found
  zero exemptions, linked the result, and passed the driver smoke tier.
- Inspection of `process_inst` accounted for loads, stores, memcpy/memmove,
  memset, string copies, atomics, compare-exchange, and `va_start`; retained
  checks are inserted only after the final optimizer pipeline.

## Unverified observations excluded from totals

- `doc/memsafe.md` still describes the historical musl split as 732 analyzed
  and 629 deferred. The current pinned executable baseline is 1,293 analyzed
  and 68 deferred. This stale explanatory count is outside the audited
  `doc/safe-mode.md` claim set and is not a separate F09 product finding.
- The safe-link implementation treats explicit archives as direct user
  inputs and rejects an unsupported archive unless exactly allowed, which is
  consistent with the documented policy. The focused safe-link script does
  not contain a dedicated archive case, so archive container/note variants
  remain a bounded coverage gap rather than a confirmed defect.

## Closure

F09 is closed with five raw and five deduplicated findings: three Critical and
two Medium. Every required review item has evidence: all real-corpus
diagnostics were classified, every safe-mode claim was dispatched to a
mechanism or a Critical finding, and the zero-entry allowlist plus link escape
surface was re-audited. The durable regression gate owns the five expected
failures; the focused existing suites remain green.
