# F05 optimizer — CLOSED

- Review date: 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Scope: `src/opt/`
- Sequencing: F04 initially settled before F05 began, reopened on 2026-08-19,
  and settled again on 2026-08-20 before this closure. Its new findings are
  owned by F04; none is an optimizer duplicate. F09 may now start.
- Durable reproducer checkpoint: `1ffacb50` (28 expected failures, zero
  unexpected passes or failures).

## Findings

ID: `OPT-H-01`
Title: pointer self-increment makes the alias solver diverge
Severity: High — valid C causes an internal compiler error, which is a High
finding under the Sprint 60 rubric.
Reproducer: `tests/audit-regressions/opt-h-01.c`
Root cause: `src/opt/alias.c:565-590` propagates the pointer-content offset of
the global slot through a load, and `src/opt/alias.c:658-662` adds four bytes
for the pointer increment before `src/opt/alias.c:565-569` stores it back to
the same slot. The resulting offset-hull recurrence is 0, 4, 8, ... .
`off_join` keeps widening that numeric interval, so the cap at
`src/opt/alias.c:868-876` fires even though the points-to bitsets have settled.
The durable gate invokes `-w -O2 -emit-ir`, which reaches the service through
its direct optimizer client. The default-on memory diagnostics also exercise
the same service at O0; `-Wno-mem` and `-w` both accept the O0 syntax-only
form, which confirms the second trigger path without changing service ownership.
Affected sprints: 32, 41, 42.

~~ID: `OPT-H-02`~~
~~Title: descending loop fusion reverses dependence direction~~
~~Severity: High — valid ISO C changes observable output at `-O3`. The O0~~
~~control and O3 with only fusion disabled both return 0; normal O3 returns 1.~~
~~Reproducer: `tests/audit-regressions/opt-h-02.c`~~
~~Root cause: `src/opt/dep.c:358-378` records the address coefficient as the~~
~~dependence stride, but `src/opt/dep.c:412-453` treats that value as an~~
~~iteration-order stride. A descending induction has the opposite ordinal~~
~~direction. That missing sign reversal makes the true negative dependence look~~
~~non-negative to `fusion_dependences_ok` at `src/opt/dep.c:624-677`, so~~
~~`fuse_pair` appends the second body after the first at `src/opt/dep.c:827-845`.~~
~~The resulting O3 IR stores `a[i]` and then loads `a[i - 1]` in one descending~~
~~loop body, whereas the original second loop must run only after every producer~~
~~iteration completes. The fixture is accepted and returns 0 with GCC and Clang~~
~~under strict C17 O3. The durable gate pins the enabled and disabled fusion~~
~~states and uses the O0 execution as a control.~~
~~Affected sprint: 35.~~
Resolution: RESOLVED 2026-08-20 by `5b030fc5`. Fusion converts each affine
coefficient from induction-value units to execution-ordinal units with the
signed loop step before querying dependences. Ascending/descending unit and
non-unit matrices, negative distances, multiplication overflow, and the
`INT64_MIN` conservative path are regression-pinned.

ID: `OPT-H-03`
Title: loop unrolling loses a live latch block parameter
Severity: High — valid ISO C triggers an internal compiler error at O3. The
host compiler and Cgfried O0/O2 execute the reproducer successfully.
Reproducer: `tests/audit-regressions/opt-h-03.c`
Root cause: `src/opt/unroll.c:559-575` accepts a two-block loop without
requiring a parameterless latch. `commit_full` seeds mappings only for header
parameters at `src/opt/unroll.c:722-724`, then clones the latch with that
incomplete map at `src/opt/unroll.c:727-730` and removes the original loop at
`src/opt/unroll.c:753`. A latch parameter carrying the preceding inlined
result is therefore left unmapped and becomes value id 0 after renumbering.
The same unchecked shape reaches the partial-unroll path. The durable fixture
uses an exact-trip full-unroll loop; O2 is the pass-absent control because the
unroller runs only at O3 and has no independent disable switch.
Affected sprints: 34, 35.

~~ID: `OPT-H-04`~~
~~Title: correlated pointer selects collapse to a false must-alias proof~~
~~Severity: High — valid ISO C returns the wrong result at O2/O3.  GCC and~~
~~Clang under strict C17 O3, plus Cgfried O0/O1, return the required result on~~
~~both runtime-selected paths.~~
~~Reproducer: `tests/audit-regressions/opt-h-04.c`~~
~~Root cause: `src/opt/simplify_cfg.c:368-445` turns the two conditional pointer~~
~~diamonds into opposite `select` expressions.  `src/opt/alias.c:678-685`~~
~~unions each select's two offsets into the same convex `[0,4]` range.  The~~
~~points-to set is one alloca for both pointers, so `alias_query` at~~
~~`src/opt/alias.c:1137-1147` mistakes equal abstract footprints for a~~
~~must-alias proof even though path correlation makes the concrete pointers~~
~~opposites.  `src/opt/gvn.c:390-410` then forwards the second store (`22`) into~~
~~the load through the first pointer.  The gate pins host/O0/O1 controls, both~~
~~runtime paths, and the simplify-CFG-to-GVN phase boundary.~~
~~Affected sprints: 31, 32.~~
Resolution: RESOLVED 2026-08-20 by `f6ffc1d1`. Equal convex hulls yield MUST or
pathwise coverage only for the identical operand or independently exact
locations. GVN and DSE retain observable accesses, while exact-location
precision and all 25 independent oracle cells remain green.

## Probes without findings

- Deterministic generated campaign: `make BUILD=build audit-opt-generated`
  retained the canonical 16-case digest check, then the closure extension
  passed 128/128 header-free strict-C17 programs: 64 cases at seed 6001
  (digest `90353e7b41da8520`) and 64 at seed 6060 (digest
  `5271c498e0ceada9`). Every case agrees across GCC 16.1.1 and Clang 22.1.8
  at O0/O3, Cgfried O0, and verifier-backed Cgfried `OPT_EQ: all`. The
  generator cycles
  scalar/branch, direct IPO, map-reduce, nested-invariant, and independent
  adjacent-loop shapes.  Every generator/compiler/execution stage has a
  configurable 20-second default bound; the retained receipt records canonical
  host-oracle paths, device/inode identities, versions, seed, and digest, so
  aliases such as `cc`→`gcc` cannot masquerade as independent evidence. This
  is bounded generated coverage, not a claim that all C programs have been
  explored. Receipts: `build/opt-generated-64-seed6001.receipt` and
  `build/opt-generated-64-seed6060.receipt`.
- Pass/level bisection: 32 generated cases from seed 6060 ran through
  `scripts/opt_pass_bisect.sh`. All 320 compile-and-run cells passed: O0, O1,
  O2, Os, O3, and Ofast plus O3 with BCE, fusion, unswitch, or vectorization
  disabled. Every configuration used `CGF_VERIFY_AFTER_EACH=1` and retained
  every phase dump under `build/f05-bisect-20260820/`; no first failing level
  or pass toggle was found.
- Bounded alias oracle: `make BUILD=build audit-opt-alias` checked ten
  cells initially and 25 cells at closure across direct pointers, affine
  offsets, partial/contained/zero-size ranges, two block parameters,
  correlated and independent selects, strict/no-strict effective types, and
  character/union wildcards. Every `NO` and `MUST` answer is tested only in
  its proof direction against four independently enumerated selector
  environments, and query symmetry is mandatory; `MAY` remains
  conservatively acceptable. At Sprint 60 close, two expected-unsound cells
  captured the correlated and independent-select instances of `OPT-H-04`.
  Remediation `f6ffc1d1` removed that exception: both are ordinary MAY cells,
  and all 25 cells / 100 concrete environments remain sound.
- Verifier-backed full program coverage: 639/639 program fixtures passed with
  `CGF_VERIFY_AFTER_EACH=1` on 2026-08-20; the imported O2 matrix completed
  with every row classified by its existing policy.
- Alias ranges: defined C probes covered bounded affine, descending, nested,
  and split-residue pointer offsets under strict and no-strict modes; Cgfried
  O0/O3 matched GCC controls. No mismatch found. The direct
  `shifted_off`→`dep_affine_range_ctx` alias-service unit seam is recorded
  below as a bounded coverage observation, not a finding.
- LICM and strength reduction: the Sprint 34 driver, 11 LICM unit tests, and
  5 strength unit tests passed. Probes for return-path store sinking, atomic
  motion, and conditional wrapped induction found no mismatch. There is no
  driver-level disable control for either pass.
- Vectorization: the 19-check vector driver plus defined C probes for scalar
  peeling, nonzero starts, `<=`/`!=` exits, in-place maps, and simultaneous
  reductions agreed across O0/O3/Ofast and the vector-disabled controls. The
  probe was x86_64-only; native ARM64 execution remains open.
- Scalar and IPO: `scripts/opt_driver.sh` and `scripts/s33_ipo_driver.sh`
  passed with the host assembler; the focused optimizer unit slice passed
  119 tests / 893 assertions. A defined-C runtime probe covered same-base and
  disjoint pointer stores/loads, a call that mutates either aliased or
  disjoint local storage, correlated branch values, and internal-call
  specialization. GCC O3 and Cgfried O0/O2/O3 agreed for both argc-derived
  paths. This is targeted coverage, not a proof of all scalar transforms.

## Semantic-boundary dispatch

The actual pipeline is the 18 rows in `src/opt/pipeline.c:24-45`, grouped as a
scalar/IPO fixpoint, a fusion fixpoint, one vector pass, a loop fixpoint, and
the O3 unroll sequence.  The audit classified the dominant legality boundary
for every family rather than treating an optimizer level as one undifferentiated
license:

| Pass family | Primary proof / semantic boundary | Audit outcome |
|---|---|---|
| mem2reg, SCCP, simplify, CSE, DCE, simplify-CFG | dominance, per-read `undef`, no speculative trap, and CFG edge semantics | verifier-backed corpus and generated campaign passed; `IR-H-08` is a separate IR-text representation failure exposed after mem2reg |
| GVN, DSE | alias result must be a proof; effective type and byte range may only strengthen a `NO` answer | `OPT-H-04` filed: equal offset hulls do not prove equal dynamic addresses |
| jump-thread, IPO, inline | condition preservation, linkage/root reachability, ABI/returns-twice constraints | targeted scalar/IPO probes and O2 driver passed |
| fusion | affine dependence direction and no cross-loop ordering reversal | `OPT-H-02` filed for descending induction |
| LICM, strength, BCE | loop invariance, overflow/trip proof, bounds range and alias safety | focused drivers plus wrapped/conditional probes passed |
| unswitch, unroll | cloned CFG/live-parameter mapping and exact-trip safety | `OPT-H-03` filed for an unmapped latch parameter |
| vectorize | exact constant trip, legal memory independence, reduction/fast-math license | x86 runtime suite and incremental ARM64 IR probe passed; native ARM64 execution remains a separate evidence gap |

## UB-assumption dispatch

`OptConfig` has exactly three semantic controls (`src/driver/driver.c:803-809`).
The audit traced each control from source semantics through its optimizer
clients and exercised the disabling control:

| Semantic license | Encoding and consumers | Closure evidence |
|---|---|---|
| Signed overflow is undefined unless `-fwrapv` is active | lowering adds `IRF_NSW` only to signed arithmetic when `-fwrapv` is absent (`src/lower/expr.c:31-37`); loop-trip analysis, strength, BCE, fusion, vectorization, unswitch, and unroll also require the flag and/or reject `cfg.fwrapv` | Sprint 34 loop/wrap driver green; Sprint 35 structural, bail, and three-toggle corpus green (103/103 in each toggle run) |
| Strict effective-type violations are undefined unless `-fno-strict-aliasing` is active | the public contract is explicit in `src/opt/alias.h:19-39`; GVN, DSE, LICM, dependence/fusion, and vectorization pass the mode into the shared service | strict-alias differential returned O0/no-strict=0 and strict=1 with GCC agreement; the 25-cell oracle separately checked strict, no-strict, character, and union cases |
| Fast-math non-finite inputs and relaxed FP effects are licensed only by the complete bundle | `docs/fast-math.md` names reassociation, signed-zero, finite, reciprocal, and non-trapping licenses; simplify, simplify-CFG, LICM, unswitch, and vectorization query the bundle rather than an optimization level | Sprint 36 policy/runtime suite passed 19/19, including strict/disabled/reset controls and vector reductions |

Potentially trapping integer division is not speculated as a free-standing UB
license: LICM checks divisor and signed-overflow safety, and CFG speculation
rejects trapping operations. Uninitialized reads are not converted into a
global optimizer license; mem2reg preserves per-read `undef`, and the verifier
campaign exercises the resulting IR after every pass.

`-fsafe` is not a fourth optimizer license and therefore does not disable the
three language modes above. The driver optimizes the emission module first and
instruments the final IR afterward (`src/driver/driver.c:1622-1640`). The
dedicated safe-mode gate passed 56/56, and an `-fsafe -O3` vector runtime probe
printed `vector-runtime-ok` with `CGF_VERIFY_AFTER_EACH=1`.

## Finding revalidation

All four findings were freshly reproduced on 2026-08-20:

| ID | Control | Failing result |
|---|---|---|
| `OPT-H-01` | direct O2 optimizer client with warnings disabled | exit 4 and the pinned alias-solver non-convergence diagnostic |
| `OPT-H-02` | O0=0; O3 with fusion disabled=0 | ordinary O3=1 |
| `OPT-H-03` | O0=0; O2=0 | verifier-backed O3 exit 4 after unroll, with live value id 0 |
| `OPT-H-04` | host/O0/O1=0 on both selector paths | O2=1/1; phase dumps pin simplify-CFG load then GVN `ret i32 22` |

No new optimizer finding or audit-regression manifest row is required.

## Unconfirmed observations (excluded from findings)

- The direct `shifted_off`→`dep_affine_range_ctx` unit seam is not isolated;
  the defined-C range probes and shared-service oracle cover its public
  behavior, but this remains a coverage-improvement candidate.
- Vector runtime evidence is native x86_64. ARM64 IR was inspected earlier,
  but native ARM64 execution was not part of this bounded closure run.
- The historical Sprint 30 order differs from the current grouped pipeline,
  and Sprint 35 prose about BCE deleting later memory checks does not match
  the current optimize-then-instrument ordering. These are documentation
  alignment observations, not correctness findings.
