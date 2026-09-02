# `aligned`, measured against gcc

Same discipline as `.docs/audits/packed-layout.md`: the numbers come from
running gcc on this machine before any code was written, not from the manual.
It was the next row of `docs/gnu-extensions.md`'s refused tier; it is now
implemented, and the sections below record what the measuring found.

```c
#define A(n) __attribute__((aligned(n)))
struct R1 { char a; int b; } A(16);
struct R2 { char a; } __attribute__((aligned));   /* no argument */
struct R3 { char a; int b A(8); };                /* member position */
struct R4 { char a; int b; } __attribute__((packed, aligned(8)));
struct R5 { char a; int b; } __attribute__((packed)) A(2);
struct R6 { char a; long b; } A(4);               /* WEAKER than natural */
typedef struct { char a; } T7 A(32);
typedef long long T8 A(1);
union  U1 { char a; } A(8);
struct S  { char a; int b A(1); };                /* aligned(1) on a member */
long long object A(1);
```

| construct | gcc size | gcc align | notable offset |
|---|---|---|---|
| R1 record `aligned(16)` | **16** | **16** | — |
| R2 bare `aligned` | **16** | **16** | — |
| R3 member `aligned(8)` | **16** | **8** | `b` at 8 |
| R4 packed + `aligned(8)` | **8** | **8** | `b` at 1 |
| R5 packed + `aligned(2)` | **6** | **2** | `b` at 1 |
| R6 `aligned(4)` under natural 8 | 16 | **8** | — |
| T7 on a typedef | **32** | **32** | — |
| T8 reduced typedef | 8 | **1** | — |
| U1 union `aligned(8)` | **8** | **8** | — |
| S member `aligned(1)` | 8 | 4 | `b` at **4** |
| reduced object | 8 | **1** | — |

## The rules those numbers imply

1. **Record and ordinary-member positions only RAISE.** R6 asks for 4 on a
   record whose natural alignment is 8 and gets 8; S asks for 1 on a member
   and the member stays at its natural offset 4. `_Alignas` instead makes a
   weakening a constraint violation (6.7.5p4). Member `aligned(1)` is
   therefore not a spelling of `packed`.
2. **Object, typedef, and declarator-type positions are exact.** They can
   reduce as well as increase alignment without changing size or type
   compatibility. A typedef request survives alias chains and controls global,
   static-local, automatic, member, and array layout, but does not mutate a
   named tag. A later explicit typedef request replaces an inherited request;
   redeclarations of one name retain the strongest effective alignment.
3. **Size rounds up to the record's alignment** (R1: 5 bytes of content, 16 of
   size). That falls out of the existing tail-padding step once
   `tag->align_override` is set.
4. **It composes with `packed` rather than conflicting** (R4, R5). `packed`
   forces the member offsets, `aligned` sets the record's alignment and the
   size rounds to it. Both halves are observable in one struct.
5. **The bare form is 16 on x86-64 AND arm64-linux** — measured on both, not
   assumed. It is gcc's `BIGGEST_ALIGNMENT`. If a target ever disagrees this
   belongs in `TargetLayout`, not in a constant.
6. **All six represented positions are live**: record (trailing, and between
   keyword and tag), member, object, function, typedef, and a pointer layer
   after `*` in a declarator.

## What is already in place

The `_Alignas` object work (commit `28b2501`) built every consumer this needs:

- record → `TagDecl.align_override`, honoured by `layout_struct`/`layout_union`
- member → `Member.align_override`, honoured by the member loop
- object → `Symbol.align_override` → `lower_object_align` → `IrGlobal.align`
  and the alloca align, with `lower_auto_align` refusing >16 automatics by name

Record and member consumers implement the raise-only half with a `>`
comparison. Objects now carry their effective exact alignment through
`Symbol.align_override`; typedef and pointer-layer requests use an arena-owned
`Type` copy so neither an interned basic type nor an underlying named tag is
mutated.

## What landed

The original four positions, both targets, were verified by execution rather
than by reading directives. `IrFunc.align` carries the function position and
round-trips as ` align(N)`; `cg_func_p2align` is the one place a byte count
becomes a directive exponent, shared so the two emitters cannot drift on a
rule that belongs to the attribute rather than to either backend.

Sprint 56.5 added the declarator-type position: `AstType` records an `aligned`
request on the exact pointer layer after `*`, and `Type` carries the folded
override without treating it as a compatibility qualifier. The pointer keeps
its natural size, `_Alignof` observes the exact request, containing aggregates
inherit it, and compatible redeclarations preserve the maximum effective
alignment.
Scalar IR loads and stores remain capped at their natural alignment for an
over-aligned type, as required by IR-C-11, and retain an honest lower claim for
an under-aligned type or object. Arrays whose element alignment exceeds
element size are rejected, matching gcc.

A grouped prefix attribute before the first `*` remains fail-closed because
that grammar position still has no represented type layer. It must not be
silently attached to the object or to an adjacent pointer layer.

The argument is folded in sema as a constant expression, which the audit called
"probably not much more work" and which was correct: `aligned(4 * 8)` works,
and a literal-only parser would have rejected it.

**The layout differential found a bug on its first run with generated
`aligned` attributes: 11 disagreements per 400, every one a UNION.**
`layout_union` never read `align_override` at all, so an alignment on a union
member was silently ignored for BOTH spellings while working correctly in a
struct — `_Alignas` had the same hole. Mutating the fix back out reproduces the
failure, so the lane genuinely covers it. 213 of every 400 generated files now
carry an `aligned`, and 91 are unions.

### A second arm64 frame bug, under the first

`csr_size` is the base a frame object's offset is measured from, and the fix
above assumed it was a multiple of 16 because the prologue stores register
PAIRS. It is not: `16 + ngp * 8 + nfp * 8` rounded to **8**, so an ODD number
of saved registers leaves it at 8 mod 16 and every 16-aligned object above it
lands 8 bytes out. The witness had `x29/x30` plus a lone `x19` — csr_size 24,
and `add x0, x29, #24` for an object that asked for 16.

x86_64 already reserves that gap, for vector homes and the variadic save area.
arm64 did not, and nothing noticed because until `_Alignas` reached an alloca
no frame object had ever asked for more than its type's natural alignment.
Rounding to 16 costs nothing: the whole frame is rounded to 16 regardless, so
it only moves where the slack sits.

It failed WITHOUT `CGF_SPILL_ALL=1` and passed WITH it — the reverse of the
usual direction, and a reminder that the spill lane is a second sample rather
than a superset.

## What was NOT in place, and now is

**The function position.** Done as sketched: one `u32 align` on `IrFunc`,
printed and round-tripped like `is_weak`/`visibility`, plus a max at the two
emit sites through the shared helper.

**The argument grammar.** Done the honest way: the parser records a
conditional-expression and sema folds it with the same evaluator `_Alignas`
uses. The one corner that refuses rather than guesses is TWO `aligned`
attributes on one declaration — gcc takes the largest, only one expression fits
in `GnuDeclAttrs`, and quietly keeping the wrong one is the failure mode the
tier table exists to prevent.

## Verifying it

`scripts/layout_diff.sh` again: extend `tests/tools/gen_layout.c` to emit
`aligned(N)` on records and members alongside the packed spelling it already
generates, with N drawn from powers of two both above and below the natural
alignment so rule 1's decline is exercised, not just its raise.

The object and function positions need the same treatment `_Alignas` got —
check the ADDRESS at run time, never `_Alignof`, which answers from the type
and is right even when placement is wrong.
