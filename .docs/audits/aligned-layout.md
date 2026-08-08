# `aligned`, measured against gcc

Same discipline as `.docs/audits/packed-layout.md`: the numbers come from
running gcc on this machine before any code was written, not from the manual.
`aligned` is the next row of `docs/gnu-extensions.md`'s refused tier.

```c
#define A(n) __attribute__((aligned(n)))
struct R1 { char a; int b; } A(16);
struct R2 { char a; } __attribute__((aligned));   /* no argument */
struct R3 { char a; int b A(8); };                /* member position */
struct R4 { char a; int b; } __attribute__((packed, aligned(8)));
struct R5 { char a; int b; } __attribute__((packed)) A(2);
struct R6 { char a; long b; } A(4);               /* WEAKER than natural */
typedef struct { char a; } A(32) T7;
union  U1 { char a; } A(8);
struct S  { char a; int b A(1); };                /* aligned(1) on a member */
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
| U1 union `aligned(8)` | **8** | **8** | — |
| S member `aligned(1)` | 8 | 4 | `b` at **4** |

## The rules those numbers imply

1. **It only ever RAISES.** R6 asks for 4 on a record whose natural alignment
   is 8 and gets 8; S asks for 1 on a member and the member stays at its
   natural offset 4. This is the opposite of `_Alignas`, which is a constraint
   VIOLATION when it weakens (6.7.5p4) — `aligned` silently declines instead.
   `aligned(1)` is therefore **not** a spelling of `packed`.
2. **Size rounds up to the record's alignment** (R1: 5 bytes of content, 16 of
   size). That falls out of the existing tail-padding step once
   `tag->align_override` is set.
3. **It composes with `packed` rather than conflicting** (R4, R5). `packed`
   forces the member offsets, `aligned` sets the record's alignment and the
   size rounds to it. Both halves are observable in one struct.
4. **The bare form is 16 on x86-64 AND arm64-linux** — measured on both, not
   assumed. It is gcc's `BIGGEST_ALIGNMENT`. If a target ever disagrees this
   belongs in `TargetLayout`, not in a constant.
5. **All four positions are live**: record (trailing, and between keyword and
   tag), member, object, function.

## What is already in place

The `_Alignas` object work (commit `28b2501`) built every consumer this needs:

- record → `TagDecl.align_override`, honoured by `layout_struct`/`layout_union`
- member → `Member.align_override`, honoured by the member loop
- object → `Symbol.align_override` → `lower_object_align` → `IrGlobal.align`
  and the alloca align, with `lower_auto_align` refusing >16 automatics by name

Each of those already implements "only ever raises" with a `>` comparison, so
rule 1 needs no new logic — only the value has to arrive.

## What is NOT in place

**The function position.** `IrFunc` has no alignment field, and both emitters
hardcode the padding before a function label (`src/cg/x86_64/emit.c` writes
`.p2align 4`). One `u32 align` on `IrFunc`, printed and round-tripped like
`is_weak`/`visibility` already are, plus a max at the two emit sites.

**The argument grammar.** gcc accepts a constant expression, so
`aligned(sizeof(long))` and `aligned(2 * 8)` are legal. The `cgf_` attribute
parser only takes an integer-constant token. Accepting a literal and
hard-erroring on anything else is a defensible first cut ONLY if the error
names the limitation; silently taking the wrong alignment is the failure mode
the tier table exists to prevent. `_Alignas` already parses a full
conditional-expression and folds it in sema — reusing that path is the honest
version and is probably not much more work.

## Verifying it

`scripts/layout_diff.sh` again: extend `tests/tools/gen_layout.c` to emit
`aligned(N)` on records and members alongside the packed spelling it already
generates, with N drawn from powers of two both above and below the natural
alignment so rule 1's decline is exercised, not just its raise.

The object and function positions need the same treatment `_Alignas` got —
check the ADDRESS at run time, never `_Alignof`, which answers from the type
and is right even when placement is wrong.
