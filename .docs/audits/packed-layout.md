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

So rules 1-4 land first, and **a packed struct containing a bitfield stays a
hard error until rule 5 lands**. Implementing half of packed silently would
give a bitfield-bearing packed struct the wrong layout with no diagnostic,
which is the failure mode the whole tier table exists to prevent.

## Verifying it

`scripts/layout_diff.sh` is the oracle and needs no new machinery: it emits
`_Static_assert` lines built from OUR numbers and hands them to gcc, so gcc
accepting the file IS the proof. Extending `tests/tools/gen_layout.c` to emit
packed structs makes every generated case a packed case.

The one thing the generator must NOT do until rule 5 lands is emit a packed
struct containing a bitfield — it would hit the hard error rather than
produce a disagreement, which reads as a lane failure rather than as the
honest refusal it is.
