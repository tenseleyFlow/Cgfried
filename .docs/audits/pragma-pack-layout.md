# `#pragma pack` layout, measured before implementation

The `s56.86-pragma-pack-layout` tranche was measured on arm64 macOS 26.4.1
against Apple Clang 21.0.0 and Homebrew GCC 16.2.0 before Cgfried's layout
code changed. Both references agree on the common surface implemented here.

For this control record:

```c
struct S {
    char lead;
    long long value;
};
```

the measured layouts are:

| state | `sizeof` | `_Alignof` | `value` offset |
|---|---:|---:|---:|
| default | 16 | 8 | 8 |
| `pack(4)` | 12 | 4 | 4 |
| `pack(2)` | 10 | 2 | 2 |
| `pack(1)` | 9 | 1 | 1 |

`pack()` and `pack(0)` restore the target default. `push`, `push,n`,
`push,name`, `push,name,n`, `pop`, and `pop,name` agree between the two
references. A named pop restores the state saved by that checkpoint and
discards the checkpoint plus every newer entry. The references disagree on
the ambiguous three-operand `pack(pop, name, n)` extension, so Cgfried
deliberately excludes and diagnoses it.

## Composition rules

- The active value caps member alignment; it does not force alignment one.
- An explicit member `aligned(8)` under `pack(1)` is still capped to one, so
  the control record remains 9/1 with `value` at offset 1.
- A record-level `aligned(16)` under `pack(1)` raises the completed record to
  alignment 16 and size 16 without moving `value` from offset 1.
- A union retains the size of its widest member while its own alignment is
  capped.
- Any active pack cap uses continuous bit-field allocation like the packed
  attribute, but its record alignment remains capped at `n` rather than
  always falling to one. The measured `pack(2)` record
  `{ unsigned char a:7; unsigned int b:26; unsigned char z; }` is 6/2 with
  `z` at byte 5. The analogous `pack(4)` record with a 58-bit `unsigned long
  long` field is 12/4 with `z` at byte 9.
- A zero-width bit-field retains its natural allocation boundary. For
  `{ char a; unsigned long long :0; char z; }` under `pack(1)`, `z` is at byte
  8. Linux AAPCS64 also retains the base type's record-alignment contribution
  (16/8 total), while Apple and the x86-64 targets produce 9/1. This is the
  same existing target split as the GNU packed attribute.

## Apple SDK reproducer

The unmodified macOS 26.4 SDK's `<mach/message.h>` brackets its trailer
records with `#pragma pack(push, 4)` and `#pragma pack(pop)`. Before this
tranche Cgfried ignored the push and failed exactly two live XNU assertions:

| record | old Cgfried size | required size |
|---|---:|---:|
| `mach_msg_context_trailer_t` | 64 | 60 |
| `mach_msg_mac_trailer_t` | 72 | 68 |

With the measured cap implemented, the real header compiles with both
assertions enabled and restores default packing after the include. That makes
the former benchmark `mach/port.h` overlay—which replaced XNU's assertion
macro with an always-true assertion—both unnecessary and forbidden by the
native campaign gate.
