# Native Apple `va_list` header interoperability

The `s56.87-arm64-macos-va-list-header` tranche was measured on arm64 macOS
26.4.1 against the installed Apple SDK before changing Cgfried's shipped
`<stdarg.h>` or cursor validation.

## Baseline

In Cgfried's deliberate strict-C17 mode, `__GNUC__` remains undefined. The
Apple SDK consequently selects its documented non-GCC `void *` spelling when
`<stdio.h>` reaches `sys/_types/_va_list.h`. Cgfried's later `<stdarg.h>` then
attempted to redefine `va_list` as its canonical `__builtin_va_list` (`char
*`), and the compiler rejected both the conflicting typedef and every
`va_start`/`va_arg`/`va_end` use of the SDK-owned cursor.

The inverse order failed too: Cgfried's `<stdarg.h>` defined `char *va_list`,
but did not publish Apple's public `_VA_LIST_T` ownership guard, so the SDK
later attempted the incompatible `void *` declaration. The former forced
campaign header and `include_next` benchmark overlay both worked around this
same missing ownership handshake.

GNU17 did not reproduce the collision because Cgfried truthfully advertises
its GCC-compatible surface in GNU modes and Apple's header selects
`__builtin_va_list`. The fix must not broaden that compiler-identity promise
into strict ISO modes.

## Implemented contract

- Cgfried's full Apple `<stdarg.h>` publishes `_VA_LIST_T` when it owns the
  typedef, preventing a later SDK redeclaration.
- When the SDK arrived first, `<stdarg.h>` retains its existing `void *`
  typedef instead of issuing a conflicting declaration.
- The re-entrant `__need_va_list` partial-include protocol defines only
  `va_list`, consumes the request, and publishes the Apple ownership guard;
  it does not expose the `va_*` macros.
- On arm64 macOS only, semantic validation accepts the SDK's unqualified
  `void *` lvalue as the ABI-equivalent one-pointer cursor. It continues to
  reject rvalues, qualified cursors, and non-void pointer types.
- Linux targets neither define `_VA_LIST_T` nor accept `void *` as a
  `va_list` cursor.

This preserves Cgfried's strict-ISO compiler identity policy while making
both legal Apple standard-header orders operational. The external stdarg
compatibility header and the last self-build SDK overlay are retired.

## Native evidence

The required native gate compiles both header orders at O0 and O2 in strict
C17, exercises `va_start`, `va_arg`, and `va_end`, retains native integer-128
arithmetic, and continues to compile the real XNU packing assertions. The
complete 107-file self corpus reaches the unmodified SDK with no forced
header or include overlay. The pinned PCRE2 campaign supplies the real-world
`<stdio.h>`-before-`<stdarg.h>` order: all 35 Cgfried translations, 35
retained project objects, 32 archive members, three linked products, and three
upstream tests pass while provenance records `compat_header=none`.

## Hosted evidence

PR #182 implementation head
`27ba21e0928a486833ffa51b749d2724a52194ad` completed with 28 successful
checks, nine intentional platform/policy skips, and no failure or pending
check. Standard CI run
[`37531949153`](https://github.com/tenseleyFlow/Cgfried/actions/runs/37531949153)
passed all 24 executed jobs plus its expected tag-only skip. In particular,
the native macOS ARM64 job ran the strict-C17 SDK boundary with policy
`native-apple-stdarg-pack-v3`; both native Linux PCRE2 jobs passed without a
compatibility header; the sanitizer and torture jobs passed; and the frontend
fuzzer completed 100,000 iterations from seed 1 with zero findings.

Pull-request bootstrap run
[`37531949122`](https://github.com/tenseleyFlow/Cgfried/actions/runs/37531949122)
and exact-head push bootstrap run
[`37531943804`](https://github.com/tenseleyFlow/Cgfried/actions/runs/37531943804)
both passed their applicable x86_64 O0/O2 jobs. The first implementation
attempt exposed only pinned clang-format 22 drift in the new ABI unit; commit
`27ba21e0` applies that mechanical formatting correction, and the complete
matrix above is against the corrected exact head.
