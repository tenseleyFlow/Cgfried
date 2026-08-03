# Warning controls

Cgfried keeps warning names, default states, and group membership in one
registry. The registry drives option lookup, diagnostic suffixes, and
`--help=warnings`, so the command is the authoritative list of recognized
warning flags:

```sh
cgf --help=warnings
```

Sprint 37 provides the control machinery and preprocessor diagnostics.
Sprints 38–40 add the frontend, format, and flow warning checkers. Sprint 42
adds the default-on intraprocedural memory warning group.

## Command-line options

| Option | Effect |
| --- | --- |
| `-Wfoo` / `-Wno-foo` | Enable or disable one warning. |
| `-Wall`, `-Wextra`, and other groups | Enable or disable the warnings in that group. `-Wall` does not mean every warning. |
| `-Wformat`, `-Wformat=1`, `-Wformat=2`, `-Wformat=0` | Select the base format group, add the level-2 format checks, or disable both levels. Other levels and parameterized negative forms are rejected. |
| `-Wmaybe-uninitialized=strict` | Report conservative, undecided maybe-uninitialized cases that the default mode suppresses. |
| `-Wmem` / `-Wno-mem` | Enable or disable the default-on proof-only intraprocedural memory group. |
| `-Wmem-strict` | Enable heuristic memory checks that do not meet the default zero-false-positive policy. |
| `-Wmem-realloc-zero` | Warn about C17's implementation-defined zero-size realloc behavior; off by default. |
| `-Werror` / `-Wno-error` | Promote all enabled warnings to errors, or remove that global promotion. |
| `-Werror=foo` | Enable `foo` and promote it to an error. |
| `-Wno-error=foo` | Keep `foo` as a warning even under `-Werror`. It does not enable `foo`. |
| `-pedantic` / `-Wpedantic` | Enable supported ISO conformance diagnostics as warnings. |
| `-pedantic-errors` | Enable supported ISO conformance diagnostics as errors. |
| `-w` | Suppress every warning-controlled diagnostic, including diagnostics promoted by `-Werror` or `-pedantic-errors`. Hard errors remain visible. |
| `-Wsystem-headers` | Allow warnings whose primary diagnostic provenance is in a system header. |
| `-Wfatal-errors` | Accepted for GCC option compatibility; diagnostic termination remains governed by the existing error-limit contract. |

Specificity takes priority over command-line position. An exact warning flag
such as `-Wno-unused-variable` outranks a group such as `-Wunused` or `-Wall`
whether it appears before or after that group. A narrower group outranks a
broader group. Options at equal specificity use the last occurrence. The same
rule lets `-Wno-error=foo` override global `-Werror` in either order, while
later `-Werror` and `-Wno-error` occurrences decide the global state.

`-w` is absolute rather than positional. A later enable or promotion option
does not make a warning visible again.

An unrecognized positive `-Wfoo` option produces a warning and compilation
continues. An unrecognized negative `-Wno-foo` option is silent when it is the
only issue, matching configure-probe behavior; if another diagnostic occurs,
Cgfried adds a note identifying the unrecognized negative option. Unknown
`-Werror=foo` and `-Wno-error=foo` options are command-line errors.

## Diagnostic suffixes

Every warning-controlled diagnostic ends with the option that controls it:

```text
[-Wpragmas]
[-Werror=pragmas]
```

The first form marks a warning; the `-Werror=` form means the same diagnostic
was emitted as an error. Notes and hard errors do not carry warning suffixes.
GCC's base format checker uses its parameter-family spelling, so those
diagnostics end in `[-Wformat=]` or `[-Werror=format=]`.

## Format checking

`-Wformat` type-checks literal formats for the known `printf`, `scanf`,
`strftime`, and `strfmon` families after default argument promotions have
been applied. It validates flags, widths, precisions, length modifiers,
positional operands, scansets, POSIX allocation conversions, wide conversions,
and missing or excess arguments. Direct calls are recognized by external
name plus a compatible rough signature, including every fixed parameter; an
incompatible user declaration or an internal-linkage function with the same
name is not treated as a builtin.

`-Wformat=2` additionally enables nonliteral, security, and two-digit-year
diagnostics. The y2k check covers explicit two-digit years and the
locale-dependent `strftime` `%c`/`%x` forms. The contained-NUL, extra-argument,
and zero-length checks are part of level 1 and can be disabled individually.
Same-width integer-sign mismatches are checked only by
`-Wformat-signedness`. A null format argument is covered by the `-Wnonnull`
implication of `-Wformat`.

Cgfried also provides `-Wformat-unbounded-scanf`, off by default, which warns
when an unbounded `%s` or `%[` writes into a fixed-size array. This is a
Cgfried safety extension rather than GCC 8 behavior. Value-range sizing for
`-Wformat-overflow` and `-Wformat-truncation`, format attributes, and wide
stdio functions are outside v0.1.0.

## Flow checking

Flow warnings run over a clone of the unoptimized IR through one fixed
analysis pipeline. The pipeline performs definite-assignment classification,
mem2reg, and CFG simplification independently of the user-selected `-O` level.
Consequently, the warning stream is identical at `-O0`, `-O1`, `-O2`, `-O3`,
and `-Os`; unlike GCC 8, `-Wuninitialized` is useful at `-O0`.

`-Wuninitialized` diagnoses a reachable read with no defining assignment on
any path. `-Wmaybe-uninitialized` diagnoses a read for which defined and
undefined paths meet, and includes a note identifying a decisive branch when
one is available. Both attach a declaration note. Address-taken and volatile
locals are excluded because mem2reg cannot prove their writes; functions that
call `setjmp` are excluded because `longjmp` changes the applicable dataflow
rules. Static-storage objects are zero-initialized and are never candidates.

The default maybe-uninitialized mode is false-positive-averse. It suppresses
same-predicate initialization/use shapes, loop-carried first-iteration shapes,
and cases whose path search exceeds its bounded proof budget. Use
`-Wmaybe-uninitialized=strict` to report those undecided cases. `-Winit-self`
is separate and remains off by default.

The CFG half of `-Wreturn-type` warns when a reachable path falls off the end
of a non-void function. `main`, proven infinite loops, `_Noreturn` calls, the
standard noreturn library functions, and the trap/unreachable builtins cut
such paths.

Cgfried also provides two off-by-default flow extensions:

- `-Wunreachable-code` reports the first statement in each unreachable source
  region. It suppresses macro/target configuration branches and defensive
  switch-arm breaks. GCC 8 accepts this option as a no-op.
- `-Winfinite-recursion` reports direct recursion only when every reachable
  path calls the function itself. Mutual recursion is intentionally outside
  this check.

The reusable CFG reachability, dominance, and path-note boundary lives in
`src/warn/flow.h`. Memory-safety policy does not live in the flow-warning
module.

## Memory checking

`-Wmem` runs a read-only analysis over a dedicated IR module at every
optimization level, including under `-fsyntax-only`. It diagnoses only proven
intraprocedural use-after-free, double-free, leak-without-escape,
constant/affine out-of-bounds access, uninitialized heap read, and free of a
must-nonheap or proven interior pointer. Imprecise aliases, lost path
correlation, and unknown byte ranges suppress a diagnostic rather than turn a
may-bug into a default warning.

Realloc results remain correlated with the old allocation until a null check:
success frees the old site, while failure leaves it live. Unknown calls escape
ownership in the default tier. `-Wmem-strict` additionally enables
`-Wmem-use-after-free-unknown`, which treats passing a freed pointer to an
unknown callee as a possible dereference.

Each memory warning is followed by ordered allocation, release, branch,
escape, or return notes from the proof path. The full per-check contract,
false-positive budget, musl coverage baseline, and demotion procedure are in
`doc/memsafe.md`.

## Source-level control

Cgfried supports GCC-compatible diagnostic pragmas with lexical,
location-sensitive scope:

```c
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
/* code compiled with -Wpragmas ignored */
#pragma GCC diagnostic pop
```

The available actions are:

```c
#pragma GCC diagnostic push
#pragma GCC diagnostic pop
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic warning "-Wpragmas"
#pragma GCC diagnostic error "-Wpragmas"
```

`ignored`, `warning`, and `error` set the classification from that source
location onward. In particular, `error` makes the named diagnostic active
even when its command-line state is disabled. `push` saves all current pragma
classifications, and `pop` restores the saved set. Following GCC 8, an
unmatched `pop` emits no diagnostic and restores the command-line baseline.
Diagnostic pragmas accept only the canonical positive spelling of a registry
warning, such as `"-Wpragmas"`; negative, parameterized, or unknown names emit
`-Wpragmas` and leave warning state unchanged.

The equivalent `_Pragma` form works inside macros:

```c
#define CGF_IGNORE_PRAGMAS \
    _Pragma("GCC diagnostic ignored \"-Wpragmas\"")
```

For a diagnostic triggered by macro-expanded code, Cgfried consults the
pragma state at the macro expansion site, not at the macro definition. Put the
pragma around the macro use when the intent is to control diagnostics caused
by that use.

## System headers

Cgfried suppresses a warning by default when the primary diagnostic span's
spelling provenance is in a system header, including a system-header macro.
Headers found through `-isystem` are system headers; a header can also mark
the lines after this directive as system-header content:

```c
#pragma GCC system_header
```

The directive is meaningful only in an included file. In the primary source
file it is ignored with a `-Wpragmas` diagnostic.

Suppression is applied before warning-to-error promotion, so `-Werror` and
`-Werror=foo` do not make a suppressed system-header diagnostic visible. Use
`-Wsystem-headers` when those diagnostics are wanted.
