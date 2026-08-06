# ABI completeness — debt ledger (opened during Sprint 51)

Gaps where a psABI case is CLASSIFIED correctly but not lowered. Each is a
clean error naming its sprint, never an ICE and never a silent wrong answer.

| ID | Gap | Targets | Found by | State |
|---|---|---|---|---|
| ABI-001 | HFA arguments and returns (v0-v3) | arm64-linux, arm64-macos | Sprint 51 cross-determinism probe | **CLOSED** |

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
