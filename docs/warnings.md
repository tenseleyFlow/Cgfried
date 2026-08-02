# Warning controls

Cgfried keeps warning names, default states, and group membership in one
registry. The registry drives option lookup, diagnostic suffixes, and
`--help=warnings`, so the command is the authoritative list of recognized
warning flags:

```sh
cgf --help=warnings
```

Sprint 37 provides the control machinery and preprocessor diagnostics.
Sprints 38–40 add the frontend, format, and flow warning checkers.

## Command-line options

| Option | Effect |
| --- | --- |
| `-Wfoo` / `-Wno-foo` | Enable or disable one warning. |
| `-Wall`, `-Wextra`, and other groups | Enable or disable the warnings in that group. `-Wall` does not mean every warning. |
| `-Wformat`, `-Wformat=1`, `-Wformat=2`, `-Wformat=0` | Select the base format group, add the level-2 format checks, or disable both levels. Other levels and parameterized negative forms are rejected. |
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
