# `packed` layout, measured against gcc

Sprint 55 says to pin gcc's layouts **before** writing code, because packed
bitfields are the part everyone gets wrong. These numbers come from running
gcc on this machine, not from reading the manual.

```c
struct A { char a; int b; } __attribute__((packed));
struct B { char a; int b; };                              /* control */
struct C { char a; int b __attribute__((packed)); };      /* member-level */
struct D { char a; short b; long c; } __attribute__((packed));
struct E { char a; int b; } __attribute__((packed, aligned(4)));
struct F { char a; int b : 3; int c : 30; } __attribute__((packed));
struct G { char a; int b : 3; int c : 30; };              /* control */
struct H { char a; double d; } __attribute__((packed));
```

| struct | gcc size | gcc align | notable offset |
|---|---|---|---|
| A packed{char,int} | **5** | **1** | `b` at 1 |
| B unpacked control | 8 | 4 | `b` at 4 |
| C member-level packed | **5** | — | `b` at 1 |
| D packed{char,short,long} | **11** | — | `c` at 3 |
| E packed + aligned(4) | **8** | **4** | — |
| F packed bitfields | **6** | — | — |
| G unpacked bitfields control | 8 | — | — |
| H packed{char,double} | **9** | — | — |

## The rules those numbers imply

1. **Member alignment becomes 1.** D places `short` at 1 and `long` at 3;
   H places `double` at 1. Nothing is padded to its natural boundary.
2. **The record's own alignment becomes 1** (A). This is the half that is
   easy to miss: forcing member offsets without also dropping the record
   alignment gives the right offsets and the wrong `sizeof`, because the
   tail padding survives.
3. **`aligned(N)` overrides in both directions** (E): it raises the record
   alignment back to 4 and the size rounds up to 8. So packed sets a floor,
   not a fixed value, and the two attributes compose rather than conflict.
4. **Member-level `packed` is the same rule applied to one member** (C).
5. **Bitfields allocate bit-contiguously with no storage-unit alignment**
   (F = 6 vs G = 8). `char a` takes bits 0-7, `b:3` bits 8-10, `c:30` bits
   11-40 — 41 bits, rounded to 6 bytes. Unpacked, `b:3` would start a fresh
   4-byte container at byte 4.

## Implementation order, and why

Rules 1-4 are one change to the member loop in `src/sema/layout.c`. Rule 5
touches the bitfield container logic, which Sprint 14 records as already
having produced two bugs (a zero-width member claiming a whole container, a
narrow member claiming its declared type's container).

So rules 1-4 landed first, and **a packed struct containing a bitfield stayed a
hard error until rule 5 landed in Sprint 56.5**. That staging prevented the
half-implemented layout from silently producing a wrong ABI.

## What landed

All five rules are implemented. Rule 5 applies to both a packed record and a
member-level suffix after the width. Nonzero fields allocate from the current
bit without the ordinary declared-unit straddle check; explicit `aligned`
still wins (even `aligned(1)` starts a bitfield at the next byte), and
zero-width fields retain their allocation barrier. Linux
AAPCS64 also retains the zero-width base type's record-alignment contribution,
while SysV and Apple do not. Those target answers were measured directly from
GCC and are pinned across the closed five-target table.

Layout is only half of rule 5. A packed 64-bit field may begin at bit 7 and
therefore occupy nine bytes, beyond the IR's largest scalar unit. Lowering uses
bytewise gather/scatter with per-byte read-modify-write masks for packed
bitfields; static initializer images already used exact bit positions. This
also keeps volatile accesses honest without over-claiming alignment. An
`_Atomic` member of a packed struct remains refused permanently rather than
pending: arm64's exclusive instructions require natural alignment, and an
atomic that quietly is not one is worse than a diagnostic.

Beyond the layout rules, two things had to change that this file did not
anticipate:

**The SysV classifier.** The psABI puts an aggregate that "contains unaligned
fields" in MEMORY however small it is, and nothing implemented that clause
because nothing could reach it before packed. gcc confirms the rule AND its
boundary: `struct { char a; float x; float y; } packed` goes to the stack,
while `struct { int b; } packed` -- alignment 1, every field still at its
natural offset -- travels in an ordinary integer register. So the test is the
field OFFSET, not the record's alignment. AAPCS64 needs no equivalent: its
placement is size-based, and aarch64-gcc still passes a packed two-float struct
as an HFA in `s0`/`s1`.

**The alignment a member access CLAIMS.** `lv_of` takes it from the type, so a
packed `int` at offset 1 was claiming 4. The IR verifier calls under-alignment
honest and over-alignment an error, so lowering now clamps to 1 for a packed
member and for any member of a packed record.

### Ledgered limit: PACKED-001

The clamp is per-access, so it does not follow a chain: in `p.in.x`, where `in`
is a NAMED struct member of a packed record, the access to `x` is typed against
`struct Inner` -- which is not packed -- and still claims the natural
alignment. The offset is correct; only the claim is optimistic.

Nothing consumes an over-claimed load alignment today, which is why this is a
ledger entry rather than a bug: x86 emits `movdqu` for every vector move, and
the arm64 backend reads the alignment field only for `alloca`. The real fix is
to thread an lvalue's alignment through the member chain, and it should land
with whatever first needs alignment-directed instruction selection.

### The trap that makes hosted fixtures useless

glibc's `<sys/cdefs.h>` does `#define __attribute__(xyz)` when `__GNUC__` is
undefined, and `__GNUC__` stays undefined until the END of Sprint 55. A fixture
that includes any system header therefore has its attributes deleted by the
preprocessor and passes no matter what layout does. The first draft of this
work "disagreed with gcc on every row" for exactly that reason, and every
packed fixture is freestanding because of it.

The same fact is a warning about the reverse direction: the day `__GNUC__` is
defined, glibc stops neutralizing and every attribute in every system header
becomes live at once.

## Verifying it

`scripts/layout_diff.sh` is the oracle and needs no new machinery: it emits
`_Static_assert` lines built from OUR numbers and hands them to gcc, so gcc
accepting the file IS the proof. Extending `tests/tools/gen_layout.c` to emit
packed structs makes every generated case a packed case.

The generator now emits bitfields inside packed records and may attach
member-level `packed`/`aligned` after a bitfield width. A generated record is
packed one time in three and any member may carry its own `packed`; the layout
differential therefore exercises rule 5 rather than suppressing it. Focused
fixtures separately pin the bit positions and byte images that `offsetof`
cannot express for a bitfield, including a cross-container 31-bit field and
the nine-byte 64-bit boundary case.

The gate was then MUTATED before being trusted: forcing the member offsets
while letting the record keep its alignment — precisely the trap named in rule
2 — drops the lane from 400/400 to 277/400, and it fails on `_Alignof` rather
than on any offset. That is the whole reason the fixtures assert alignment and
`sizeof` and not just offsets.
