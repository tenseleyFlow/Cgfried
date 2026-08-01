# Test-program corpus

Directive-annotated C programs driven by `build/cgf-test`. Empty until
Sprint 3 starts feeding it; the compile-and-run flow goes live in Sprint 25.

Directives are `// NAME: value` comments at the start of a line, scanned from
the whole file. Any `// ALL_CAPS:` comment that is not a known directive is a
hard configuration error (so a typo'd directive can never silently become a
plain comment — and consequently `// TODO:`-style comments are not allowed in
test sources). Examples:

```c
// CHECK: hello
// EXIT_CODE: 0
```

```c
// ERROR_EXPECTED: expected ';'
```

```c
// XFAIL(x86_64-freebsd): XF-0001 kqueue fixture flake
```

```c
// SKIP(arm64-macos): needs codesigning fixture
// TIMEOUT: 30
```

```c
// FLAGS: -E -trigraphs
// ENV: CGF_PP_DUMP_TOKENS=1
```

`FLAGS` adds space-separated arguments to the compile step (one FLAGS line
per test). When it contains `-E` the pipeline stops at the compiler:
`CHECK`/`EXIT_CODE` then apply to the *compiler's* stdout and exit code —
that is how preprocessor fixtures assert on token dumps. `ENV` (repeatable)
sets `NAME=VALUE` in the compile step's environment.

`OPT_EQ` repeats an end-to-end C test at two or more optimization levels and
requires every run to satisfy its normal `CHECK`/`EXIT_CODE` contract. Runtime
stdout must also be byte-identical, and exit status identical, across all
levels. The allowed levels are `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, and
`-Ofast`; duplicates are configuration errors. `all` expands to that canonical
list.

```c
// OPT_EQ: -O0 -O1
// CHECK: result=42
```

Target selectors are the closed target-name set (`x86_64-linux-gnu`,
`arm64-linux`, `arm64-macos`, `x86_64-linux-musl`, `x86_64-freebsd`) plus
`*`. Unknown selectors are configuration errors. Every `XFAIL` cites an
`XF-NNNN` id from `.docs/audits/xfail-debt.md`; XPASS is a hard failure.

`ASM_CHECK(<arch>)` checks assembly text, and `IR_CHECK` checks textual IR.
