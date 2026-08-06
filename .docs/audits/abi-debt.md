# ABI completeness — debt ledger (opened during Sprint 51)

Gaps where a psABI case is CLASSIFIED correctly but not lowered. Each is a
clean error naming its sprint, never an ICE and never a silent wrong answer.

| ID | Gap | Targets | Found by |
|---|---|---|---|
| ABI-001 | Returning a homogeneous floating-point aggregate (v0-v3) | arm64-linux, arm64-macos | Sprint 51 cross-determinism probe |

## ABI-001 — HFA returns

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

## Why the probe found it and the corpus did not

`tests/corpus/x86_64/int/struct_ret.c` returns `struct Big { long a, b, c,
d; }` — four INTEGER leaves, so memory class, so sret. It passes on arm64.
No corpus fixture returned a float aggregate, and the HFA predicate's only
other consumer was argument passing, which works.

The lesson is the one the cross probe was written for: a property table that
lists what differs by target (`char` signedness, long-double format,
aggregate classification, varargs shape) finds the untested corner faster
than a corpus grown from programs someone happened to write.
