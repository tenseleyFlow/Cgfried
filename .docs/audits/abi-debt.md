# ABI completeness — debt ledger (opened during Sprint 51)

Gaps where a psABI case is CLASSIFIED correctly but not lowered. Each is a
clean error naming its sprint, never an ICE and never a silent wrong answer.

| ID | Gap | Targets | Found by | State |
|---|---|---|---|---|
| ABI-001 | HFA arguments and returns (v0-v3) | arm64-linux, arm64-macos | Sprint 51 cross-determinism probe | **CLOSED** |
| ABI-002 | An aggregate that does not fit is split across registers and stack | x86_64 (all), arm64-linux, arm64-macos | Sprint 51 ABI differential | **CLOSED** |
| ABI-003 | Six varargs defects, and two deferrals that became reachable | x86_64 (all), arm64-linux | Sprint 51 ABI differential, varargs generation | **CLOSED** |
| ABI-004 | An ANONYMOUS aggregate is not passed by value in the varargs area | arm64-macos | Sprint 51 ABI differential on nomad-1 | **OPEN** |

## ABI-004 — Apple anonymous aggregates — OPEN

24 of 304 generated signatures disagree with clang on arm64-macos, and every
single minimized reproducer is the same shape: a COMPOSITE anonymous
argument. Scalars are clean.

    R v  A d  V s(a(17,c))          17 bytes -- indirect on AAPCS64
    R v  A f  V s(f,f,f)            an HFA
    R d  A h  V s(i,c,d,c)          a mixed 24-byte aggregate

Apple's varargs area holds every anonymous argument BY VALUE and contiguous
-- Sprint 50 measured exactly that against clang and recorded it. What is
missing is that the classifier still shapes an anonymous aggregate the way a
NAMED one is shaped, so it reaches the backend either as an indirect pointer
(over 16 bytes) or as HFA leaves, and the marshaller then gives each leaf its
own eightbyte slot. A three-float HFA occupies 24 bytes of varargs area where
clang uses 16.

### Where the fix goes

It is CALLER-ONLY. `lower_va_arg_apple` is already a plain cursor bump that
reads the aggregate contiguously and rounds the advance to 8, which is right;
the caller is what puts the bytes somewhere else. So there is no matching
callee change, and `ours x clang` in the other direction should stay green
throughout.

Anonymity is known only at the call site, where `lower_call` already computes
`IROPF_ANON`. Marking such an argument `ABI_ARG_STACK` there gets most of the
way: `abi_arg_place` re-plans a stacked aggregate into `ceil(size/8)`
eightbyte leaves, the marshaller gives each anonymous leaf a full eightbyte,
and a three-float HFA then occupies the 16 bytes clang uses instead of 24.

The catch is size. That re-plan caps at `ABI_MAX_LEAVES` (4), so it covers an
aggregate up to 32 bytes and no further. A larger anonymous aggregate needs
the OTHER stacked shape -- a byval pointer carrying `IROPF_ONSTACK`, which
means "copy the pointee onto the stack" -- and the arm64 marshaller does not
implement that yet; it honours `onstack` only on leaves. That is the real
work, and it is backend work.

Not attempted here: it needs a Mac in the loop to verify, and this sprint's
verified surface was already large. arm64-linux and x86_64 are unaffected --
AAPCS64 and SysV place anonymous arguments exactly as named ones, which is
the whole reason this is Apple-only.

## ABI-003 — varargs — CLOSED in Sprint 51

Teaching the generator to emit a variadic tail found six defects in one
afternoon, four on SysV and two on AAPCS64. Every one is in the CALLEE:
`va_arg` is where an ABI's register save area stops being a formality.

**SysV.**

1. `va_start`'s `gp_offset` counted the named arguments but not the hidden
   pointer of a MEMORY return, which really does occupy `rdi`. A large-struct
   -returning variadic function read its first anonymous integer back out of
   the caller's sret pointer.
2. The overflow cursor advanced by the size rounded up to SIXTEEN. Sixteen is
   the pre-alignment for a type whose own alignment exceeds 8; the advance
   rounds to EIGHT. Invisible with one MEMORY vararg, because only the second
   reads from a cursor the first moved. Both arm64 paths already had it right.
3. A multi-eightbyte all-SSE aggregate was read as 16 contiguous bytes out of
   the FP save area, where each xmm owns a SIXTEEN-byte slot -- so the second
   eightbyte was the first one's padding. It has to be gathered.
4. A MIXED pair (`struct { double d; short s; }`, one SSE eightbyte and one
   INTEGER) was pushed to the overflow area, where the caller never put it.
   This was the corner the file itself deferred, with a comment. Fixing it
   meant replacing the single "is this the FP path" flag with a count PER
   CLASS, because an aggregate can need both banks at once and a boolean
   cannot say so.

**AAPCS64.** Both were reachable only through work landed in this same sprint,
which is its own lesson about when to re-run a differential.

5. `named_gp`/`named_fp` were a second count maintained in parallel with the
   placement walk, and they missed the general register that an INDIRECT
   aggregate's pointer occupies. A callee taking a large struct seeded
   `__gr_offs` at -64 where gcc uses -56, so its first anonymous integer came
   out of `x0`. The save area itself stored `x1`-`x7` correctly, which is
   exactly how two sources of truth hide from each other. They are now one:
   `named_gp` IS the budget.
6. `va_arg` of an HFA hard-errored (a Sprint 49 deferral) and a variadic
   function with stack-passed NAMED parameters ICEd (a Sprint 48 one). Both
   are closed here. The first is the same gather as SysV finding 3 -- leaves
   sit one per 16-byte `q` slot, not adjacent. The second needed `__stack` to
   start past the named stack arguments, which selection already computes as
   `nsaa`; ABI-002's stacked HFA made it common rather than exotic.

Neither deferral was a wrong answer -- both were clean errors naming their
sprint, which is why they survived so long and why that policy is worth
keeping. The differential reports them as disagreements because a compile
failure IS one.

**Coverage.** 304 signatures per target (300 generated plus the checked-in
reproducers), both directions, clean on x86_64 and arm64-linux.

## ABI-002 — register exhaustion splits an aggregate — CLOSED in Sprint 51

Both psABIs say the same thing and we did neither. SysV 3.2.3 step 5: "if
there are no registers available for ANY eightbyte of an argument, the whole
argument is passed on the stack." AAPCS64 C.4 and C.12 say it for HFAs and
for general-register composites, and then go further — the exhausted bank is
PINNED at 8, so every later argument of that class stacks too even if a
register happens to be free.

We committed eightbytes one at a time, so an aggregate straddling the end of
the bank put its first half in a register and its second on the stack.

Two minimal reproducers, both found by generation rather than by anyone
writing the program:

    R v  A i  A l  A c  A s(a(16,c))  A s(a(15,c))     x86_64
    R v  A s(d,d,d,d)  A d  A s(d,d,d,d)               arm64

**Both directions failed, and that is the diagnostic.** Our caller and our
callee agreed with each other and neither agreed with gcc, so `ours × ours`
passes and only a mixed link in both directions sees it. This is the third
time in three sprints that the same tell has appeared (ABI-001's HFA return,
Sprint 50's row 3, now this).

### Why classification could not decide it

`abi_classify_arg` is a pure function of the TYPE, and placement is a
function of the type *and everything before it*. The rule needs a running
budget, so `abi_budget_init`/`abi_arg_place` are one shared service that both
argument walks call — the call site in `expr.c` and the definition in
`lower.c`. Those two walks disagreeing is precisely the failure mode, so they
run the same sequence rather than each implementing it.

### Why ABI_ARG_STACK is not ABI_ARG_BYVAL

BYVAL means opposite things per ABI, which `lower.h` has warned about since
Sprint 48: on SysV codegen copies the pointee onto the stack, on AAPCS64 the
copy's ADDRESS rides one general register. "Stacked because the bank ran out"
is the SysV meaning on both targets, so it needed its own kind.

On AAPCS64 a stacked aggregate is re-planned into `ceil(size/8)` eightbyte
leaves, which makes both of the stack's properties fall out of machinery that
already existed: 8-byte slots, and a size rounded up to 8 — a stacked HFA of
three floats occupies 16 bytes, not the 12 its leaves suggest. The leaf TYPE
stays f64 for an FP-class aggregate so the backend can pin the same bank; a
bit copy of 8 bytes through an f64 is exact on arm64, where FP load/store
never canonicalizes.

Carried as `IROPF_ONSTACK` on call operands and `IR_PARAM_ONSTACK` on
parameter annotations. Neither grows anything: the operand flag is a spare
bit in the byte Sprint 50 added, and the `IR_ARG_*` kind field's three bits
were all spoken for.

### Coverage

300 generated signatures per target, both directions, clean. The two minimal
descriptors are permanent fixtures in `tests/abi_differential/repro/` and the
lane replays every one of them before it generates anything.

## ABI-001 — HFA arguments and returns — CLOSED in Sprint 51

AAPCS64 returns an aggregate of 1-4 homogeneous float or double leaves in
`v0`-`v3`, passing **no hidden pointer at all**. `abi_classify_ret` has
recognized this since Sprint 14 and set `ABI_RET_HFA`; nothing ever consumed
it. The `hidden` predicate is computed in exactly two places — the function
definition in `lower.c` and the call site in `expr.c` — and both listed only
`ABI_RET_SRET` and `ABI_RET_PAIR`. Lowering therefore built a value return
into a void IR function and the verifier caught it:

```
ir verify [4] in @scale: aggregate ABI return requires void IR return
                         and hidden ptr parameter 0
```

It reproduces natively on arm64, so the gap is the ABI and not the cross
path. `struct { float x, y, z; }` is ordinary graphics and math code.

**Why it cannot borrow the sret shape.** The classifier currently sets
`ir_abi = IR_ABIRET_SRET` for an HFA as a placeholder, which is exactly the
thing that must change: sret passes a hidden pointer in `x8`, an HFA passes
nothing and comes back in registers. The backend reads `IrFunc.abi_ret` to
decide, so the two need distinct values there. The work is:

1. A new `IrAbiRet` for HFA, carrying the leaf type and count.
2. Its round trip through `print.c` / `parse.c` / `struct_eq`, plus a
   verifier rule (leaf count 1-4, leaves homogeneous, FP only).
3. `hidden` includes it, and lowering stops emitting a hidden pointer.
4. arm64 backend, both sides: the callee loads `v0`-`v3` before `ret`; the
   caller stores them back into the destination after the call.
5. Fixtures, and a mixed link against clang in both directions — the only
   way to prove a return convention, since two halves that share a bug agree
   with each other.

Step 5 is the reason this is worth doing properly rather than quickly: the
Sprint 51 ABI differential harness (deliverable 6) exists to generate exactly
these shapes, and an HFA return is one of its named cliff edges.

## ABI-001, closed

Both halves were unconsumed, and the second was found only by fixing the
first. The RETURN built a value into a void IR function and the verifier
caught it as an ICE. The ARGUMENT fell through to `byval` and travelled as a
pointer where gcc passes `s0`-`s2` — silent, and self-consistent as long as
both sides were ours.

What it took:

1. `IR_ABIRET_HFA_F32`/`_F64` plus `IrFunc.abi_ret_n`, so the backend can
   tell an HFA from a real sret. They share the IR SHAPE — a hidden pointer
   the callee builds into — but sret spends `x8` on that pointer at runtime
   and an HFA spends nothing.
2. `IR_ARG_HFA` on the hidden-pointer argument, carrying the leaf count in
   bits 35..37. The call SITE needs the distinction too, and the leaf width
   falls out as size/count because an HFA is homogeneous by definition.
3. The argument is the existing eightbyte split with the leaf width as its
   STRIDE. Using 8 for a float HFA gathers every other leaf.
4. Callee returns through `v0`-`v(n-1)`; caller stores them back.

**The mixed link had to run in both directions.** After step 4 our callee was
correct and our caller still read `x0:x1`, so `ours × gcc` passed and
`gcc × ours` printed garbage. A same-compiler test agrees with itself either
way and would have shipped this.

## Why the probe found it and the corpus did not

`tests/corpus/x86_64/int/struct_ret.c` returns `struct Big { long a, b, c,
d; }` — four INTEGER leaves, so memory class, so sret. It passes on arm64.
No corpus fixture returned a float aggregate, and the HFA predicate's only
other consumer was argument passing, which was itself broken and unnoticed
for the same reason.

The lesson is the one the cross probe was written for: a property table that
lists what differs by target finds the untested corner faster than a corpus
grown from programs someone happened to write.
