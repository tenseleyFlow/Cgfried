# F04 IR + lowering — CLOSED

- Review dates: 2026-08-16 through 2026-08-20
- Baseline: `1c639e060ab38bf3daf9a4e2f2a431c9ca3041cb`
- Scope: `src/ir/`, `src/lower/`
- Confirmation oracles: GCC 16.1.1 as tiebreak; the SysV x86-64 psABI text is
  the law. Locked GCC 8.x and Clang 7 remain unavailable locally.
- Sequencing: F04 preceded the start of F05, which still precedes F09. F04
  reopened on 2026-08-19 for `IR-H-08` and closed its Sprint 60 collection on
  2026-08-20 after the complete lowering dispatch added `IR-C-09` and
  `IR-C-10`. F05 may now close when its own dispatch is complete, after which
  F09 may start.

## Findings

ID: `IR-C-01`
Title: a 16-byte long-double aggregate is returned by hidden pointer
Severity: Critical — wrong code emitted at an ABI boundary, and in the
gcc-caller direction the Cgfried callee stores 16 bytes through an `%rdi`
nobody set, which segfaults. That is memory corruption on valid C, not merely
a wrong value.
Reproducer: `tests/audit-regressions/ir-c-01.c`
Root cause: the SysV classifier treats a 16-byte aggregate whose entire
content is one x87 `long double` as MEMORY. The psABI classifies it
X87/X87UP: MEMORY is not among its classes, and post-merger rule (c) does not
fire because the size does not exceed two eightbytes, so the return goes on
the x87 stack in `st0`. `src/sema/layout.c:404-410` gives the X87/X87UP pair
to a bare `long double` correctly; the return path then demotes the enclosing
aggregate. Argument passing AGREES with GCC (both pass in memory) — the defect
is return-only.
Measured, all four link directions of `struct { long double v; }`:
GCC+GCC `2.5` (correct); CGF+GCC `0.0` (reads a never-written sret buffer);
GCC+CGF SIGSEGV; CGF+CGF `2.5`. **Both directions fail and Cgfried agrees with
itself** — the project's own signature for a shared assumption rather than a
placement bug, third recurrence after ABI-001 and ABI-002.
Boundary, measured: the three exactly-16-byte shapes diverge —
`struct { long double v; }`, `union { long double v; }`, and
`struct { long double v[1]; }`. Anything larger than two eightbytes agrees,
because those really are MEMORY; that is why `struct { long double v; char c; }`
is correct and why ordinary code never surfaced this.
Affected sprint: 19.
Cross-front note for F11: `tests/tools/abigen.c`'s `Kind` enum has no
`long double`, no `_Float128` and no bitfields, so the 300-signature-per-target
ABI differential cannot reach any of these shapes. The generator's repertoire
bounds what the differential can prove.

ID: `IR-L-02`
Title: the written operand type on `icmp`/`fcmp` is silently replaced
Severity: Low — polish. The text format accepts a type annotation that
disagrees with the operands and normalizes it away instead of diagnosing it.
No program is miscompiled: check 4 still requires the two operands to have one
type, so the comparison always happens at the operands' real width.
Reproducer: `tests/audit-regressions/ir-l-02.cgfir`
Root cause: `src/ir/parse.c:1234-1244` parses the operand type into `ty` and
passes it to `parse_atom`, which takes the type from the referenced value's
definition instead. `icmp` is the only arithmetic form whose result type is
fixed by law (i32), so it is the one place where check 4's
operand-vs-result comparison has nothing to catch the annotation against.
`iadd i32` over i64 operands is correctly rejected by check 4.
Affected sprint: 17.

~~ID: `IR-C-03`~~
~~Title: atomic pointer increment is split into a load and store~~
~~Severity: Critical — wrong code is emitted for valid C: concurrent increments~~
~~can lose updates even though each component access is individually seq_cst.~~
~~Reproducer: `tests/audit-regressions/ir-c-03.c`~~
~~Root cause: `src/lower/expr.c:2381-2388` sends atomic increment/decrement~~
~~through `lower_atomic_update` only when the operand is not a pointer. The~~
~~pointer case then follows the ordinary path: a seq_cst load, `ptradd`, and a~~
~~separate seq_cst store. C17 6.5.2.4 defines postfix increment in terms of~~
~~compound assignment except for the result value, and 6.5.16.2 requires~~
~~compound assignment on an atomic object to be one sequentially consistent~~
~~read-modify-write operation. GCC 16 emits `lock xaddq`; Clang 22 emits a~~
~~`lock cmpxchgq` retry loop. Cgfried emits two ordinary `movq` accesses with an~~
~~`mfence`, which orders the accesses but cannot make the pair indivisible.~~
~~Affected sprint: 20.~~
Resolution: RESOLVED 2026-08-20 by `892435be`.
Cluster hunt: covered prefix, postfix, `+=`, and `-=` pointer updates over
fixed-size and variably modified pointees; every case now scales before one
seq_cst `atomicrmw`, with result-value semantics checked at O0 through O3.

ID: `IR-C-04`
Title: backward goto before a VLA declaration leaks stack space
Severity: Critical — wrong code is emitted for valid C: every loop iteration
retains the previous dynamic allocation and sufficiently many iterations
exhaust the stack.
Reproducer: `tests/audit-regressions/ir-c-04.c`
Root cause: the label pre-pass records only the innermost VLA-bearing compound
for each label (`src/lower/lower.c:828-852`). A label before a VLA declaration
and the declaration itself share one compound, but the identifier's scope does
not begin until its declarator completes. `vla_restore_for_goto` therefore
stops immediately at the shared compound (`src/lower/stmt.c:133-150`) and emits
no `stackrestore` on the backward edge. GCC 16 and Clang 22 both accept the
C17 fixture. Cgfried's IR visibly contains `stacksave`, dynamic `alloca`, and
`br L.again()` with no restore between them.
Affected sprint: 20.

ID: `IR-H-05`
Title: volatile aggregate sources lose their access marker
Severity: High — a C volatile-access requirement and the Sprint 20 structural
law are violated; the current pipeline happens to retain the copies, but its
count/order tripwire cannot see them, making this latent wrong-code.
Reproducer: `tests/audit-regressions/ir-h-05.c`
Root cause: aggregate expressions travel through lowering as bare addresses.
`lower_assign` passes only the destination lvalue's volatile bit to
`lower_memcpy_aggregate` (`src/lower/expr.c:904-915`), while aggregate
initializers and temporaries pass zero. A volatile source assigned to an
ordinary destination therefore produces an unmarked `memcpy`. The reproducer
performs two required reads; both appear in O0 and O2 IR, but neither carries
`volatile`, so `ir_snapshot_volatile_order` excludes them. GCC 16 and Clang 22
both retain two source reads at O2.
Affected sprint: 20.

ID: `IR-H-06`
Title: sigsetjmp macro expansion loses the returns-twice marker
Severity: High — the supported GNU/POSIX spelling bypasses the Sprint 20
setjmp policy, allowing optimization of a function that must use the
conservative returns-twice rules.
Reproducer: `tests/audit-regressions/ir-h-06.c`
Root cause: direct-call recognition and verifier consistency both use an exact
three-name list: `setjmp`, `sigsetjmp`, and `_setjmp`
(`src/lower/expr.c:2316-2321`, `src/ir/verify.c:841-868`). On glibc,
source-level `sigsetjmp(env, mask)` expands to `__sigsetjmp(env, mask)`, so the
actual external symbol misses the list. The fixture mirrors that public-header
expansion without depending on the host header and shows a call to
`@__sigsetjmp` inside a function lacking the `setjmp` marker.
Affected sprint: 20.

ID: `IR-H-07`
Title: global relocation offset wraps the verifier bounds check
Severity: High — the verifier accepts structurally invalid IR that can place an
eight-byte relocation outside its initializer, a latent wrong-code boundary
if any producer emits the bad module.
Reproducer: `tests/audit-regressions/ir-h-07.cgfir`
Root cause: `src/ir/verify.c:1160` checks `offset + 8 > size` in `u64`.
At `UINT64_MAX`, the addition wraps to 7 and an eight-byte initializer passes.
The native IR parser, verifier, printer, and MIR path all accept and preserve
the impossible relocation. Bounds checks must avoid addition before proving
the range.
Affected sprint: 17.

ID: `IR-H-08`
Title: symbolic indirect calls fail optimized IR round-trip
Severity: High — valid ISO C terminates with the compiler's internal-error
exit when the documented `-emit-ir` mode asks the compiler to print its own
optimized IR.  Ordinary assembly and MIR emission still succeed, so this is a
diagnostic-format failure rather than a machine-code miscompile, but it makes
a supported compiler mode unusable on a defined source program.
Reproducer: `tests/audit-regressions/ir-h-08.c`
Root cause: after local function-pointer promotion, `mem2reg` replaces an
indirect callee value with an `IROP_SYMBOL` while deliberately retaining
`FUNCREF_INDIRECT` (`src/opt/mem2reg.c:1115-1117`).  That shape is verifier
legal (`src/ir/verify.c:664-668`).  The printer renders its pointer operand as
`@increment`, exactly the spelling it uses for a direct call
(`src/ir/print.c:466-479`); the parser necessarily reconstructs that spelling
as `FUNCREF_INTERNAL` (`src/ir/parse.c:1449-1485`).  The driver's structural
round-trip check then rejects the semantically changed module at
`src/driver/driver.c:626-633`.
Evidence: the strict-C17 host control and Cgfried `-O0 -emit-ir` succeed;
`-O1 -emit-mir` and `-O1 -S` also succeed.  With
`CGF_VERIFY_AFTER_EACH=1`, the first changed phase is mem2reg: lowering prints
an indirect `%9 = call i32 %6(...)`, while the post-mem2reg dump prints
`%3 = call i32 @increment(...)`.  Reparsing that last text is valid but loses
the call form, which is why replaying the dump alone cannot expose the bug.
Affected sprints: 17, 30.

ID: `IR-C-09`
Title: 16-byte-aligned Linux AAPCS64 composites ignore the even-register rule
Severity: Critical — valid cross-object calls disagree on argument registers.
With one scalar already in x0, an ordinary Linux AAPCS64 caller leaves x1
unused and passes this naturally 16-byte-aligned composite in x2:x3; Cgfried
passes it in x1:x2. Its Linux `va_arg` path makes the same mistake when reading
the general-register save area, so Cgfried agrees with itself while mixed-link
programs consume the wrong words. Apple arm64 intentionally omits the
even-register rule and remains the x1:x2 control.
Reproducer: `tests/audit-regressions/ir-c-09.c`
Root cause: `abi_arg_place` charges `need_gp` directly to `AbiBudget.gp`
without applying AAPCS64 rule C.10's even-NGRN adjustment for a composite whose
natural alignment is 16 (`src/lower/abi.c:338-408`). The matching Linux
`va_arg` walk increments `__gr_offs` by the composite width without first
rounding the negative save-area offset to 16 (`src/lower/expr.c:1091-1180`).
Evidence: the fixture's strict-C17 host control runs successfully. GCC 16's
Linux arm64 caller uses x0 followed by x2:x3, while Cgfried uses x0 followed by
x1:x2. Cgfried's Linux IR increments `__gr_offs` by 16 with no i32
add-15/and-minus-16 alignment step. The Apple target uses x1:x2, proving that
the gate distinguishes the target-specific register rules.
Affected sprints: 20, 48, 50.

ID: `IR-C-10`
Title: stacked 16-byte-aligned composites lose AAPCS64 stack alignment
Severity: Critical — valid cross-object calls disagree on incoming stack
locations. After eight scalar register arguments and one stacked scalar, both
Linux AAPCS64 and Apple arm64 must round the next stack argument offset from 8
to 16. Cgfried instead places the composite's two eightbytes at +8 and +16;
its callee reads those same wrong locations rather than +16 and +24. The
compiler is self-consistent, so only a psABI oracle or mixed-link direction
exposes the wrong-code boundary.
Reproducer: `tests/audit-regressions/ir-c-10.c`
Root cause: lowering flattens a small composite into separately annotated
eightbyte IR operands (`src/lower/expr.c:1953-2042` and
`src/lower/lower.c:946-1005`). Once flattened, neither arm64 placement walk
retains the composite's 16-byte alignment. The caller aligns each outgoing
leaf only to its scalar slot width (`src/cg/arm64/regalloc.c:1110-1125`), and
the callee independently does the same for incoming leaves
(`src/cg/arm64/isel.c:2270-2305`).
Evidence: both fixture host controls run successfully. On Linux and Apple,
Cgfried's caller emits a paired store combining the +0 stacked scalar with the
+8 first aggregate word, followed by the second word at +16. Both generated
callees mirror those offsets. GCC 16's Linux arm64 control stores the scalar at
+0 and the pair at +16/+24.
Affected sprints: 20, 47, 48, 50.

## Attack-surface dispatch

- **Verifier coverage holes — probed.** A single-field textual
  mutation sweep over all 47 valid `.cgfir` fixtures, plus eleven hand-built
  invalid programs, found the verifier rejecting ten of eleven: wrong `ret`
  type, a value returned from a void function, a missing return value, edge
  argument count and type mismatches, duplicate block labels, an instruction
  after a terminator, a block with no terminator, a load through a non-pointer,
  and a non-i64 `ptradd` offset. The written comparison type acceptance is
  `IR-L-02`; a non-textual boundary review additionally found the relocation
  arithmetic wrap in `IR-H-07`.
  One mutation family was a FALSE POSITIVE of the harness rather than a
  finding: retyping `iadd i32 undef, 0` to `iadd i64 undef, 0` produces valid,
  self-consistent IR, because `undef` and an integer constant both adapt. A
  mutation harness has to produce genuinely invalid IR, and changing a type
  does not always do that.
- **`alloca 0` is accepted** by the verifier. Recorded here rather than filed:
  a zero-extent object is exactly what the alias service's byte-offset hulls
  cannot represent, and it is the documented reason empty structs are refused
  — but no C program reaches a zero-size static alloca today, because sema
  refuses both the empty struct and the FAM-only struct. Chasing it surfaced a
  reachable layout divergence instead, filed as `SEMA-H-06` on F03. Revisit if
  F05 finds a pass that can synthesize one.
- **ABI lowering vs psABI text: confirmed `IR-C-01`, `IR-C-09`, and
  `IR-C-10`.** The audit's rule that the document is the law and gcc only the
  tiebreak is what found them. The existing 300-signature differential is green
  precisely because its generator cannot emit long double and does not vary a
  naturally 16-byte-aligned composite across the register/stack boundaries.
- `long double` and `_Float128` argument passing, and bitfield-bearing
  aggregates: probed by mixed link against GCC in both directions; all agreed
  except the return path above. `struct { int a:3; int b:5; int c:20; }` by
  value and by return, and `struct { char c; int a:3; long b:40; }`, agree
  exactly.
- Atomic increment/decrement: pointer operands fail the Sprint 20 law; filed as
  `IR-C-03`. Integer and floating operands take the indivisible paths. Plain
  scalar accesses retain their IR flags. Floating backend accesses remain the
  distinct `X64-C-01` finding on F06.
- VLA scope exits: nested-compound exits remain covered, but the label pre-pass
  conflates a compound with the later declaration point; filed as `IR-C-04`.
- Volatile scalar and bitfield paths retain markers; aggregate source copies
  do not, filed as `IR-H-05`.
- The ISO `setjmp` header path expands to recognized `_setjmp`; glibc's
  `sigsetjmp` path calls unrecognized `__sigsetjmp`, filed as `IR-H-06`.
- Remaining VLA exits: focused IR probes confirm normal fallthrough, `break`,
  and `continue` each restore the live token; `return` correctly relies on
  frame teardown. The existing two-scope `goto` and 100,000-iteration runtime
  fixtures pass, as does evaluate-once `sizeof(VLA)`. The same-compound
  backward edge is the sole minimized mismatch and is filed as `IR-C-04`.
- Verifier mutation of non-textual fields (flags, subop, annotation words)
  rejected the illegal combinations already covered by checks 7, 10, 12, and
  13. Module-level boundary arithmetic produced `IR-H-07`; no other acceptance
  survived minimization.
- **Optimized text round-trip — confirmed `IR-H-08`.**  The existing indirect
  call round-trip unit covers an SSA-pointer callee only.  Promotion can make
  that operand symbolic without making the call direct, a shape the verifier
  permits but the text format cannot distinguish.  The expected-failure gate
  keeps the `-O0` control, strict host acceptance, and the verifier-enabled
  `-O1 -emit-ir` failure distinct.
- **AAPCS64 placement boundaries — complete.** Named fixed arguments,
  anonymous Linux register-save-area reads, Apple's no-even-register control,
  and caller/callee stack placement were each probed with naturally
  16-byte-aligned composites. Register placement and Linux `va_arg` produce
  `IR-C-09`; the shared Linux/Apple stack-alignment loss produces `IR-C-10`.

## Closeout

- Findings: 10 total — 5 Critical, 4 High, 0 Medium, 1 Low.
- Unverified observations: 0.
- Dispatch: complete for Sprint 60 collection.
- Expected-failure gate: 39/39 fixtures reproduce their ledgered outcomes;
  0 XPASS and 0 unexpected FAIL.

## Unverified observations

None recorded.
