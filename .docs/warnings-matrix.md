# GCC 8 warning parity matrix

This is the Phase 8 burn-down ledger. It was extracted from the raw `Warning`
records in GCC tag `releases/gcc-8.5.0`: C-enabled records from
`gcc/c-family/c.opt`, plus language-independent records from `gcc/common.opt`.
The legacy aliases `-all-warnings` and `-extra-warnings` are not `-W` flags
and are the only excluded `Warning` records. The resulting 222 rows retain
aliases, parameter-taking forms, and duplicate raw entries across the two files.

A Clang cell of `—` means that this ledger does not claim an exact Clang 7
equivalent. Planned and out-of-scope rows have no fixture until implemented;
every `done` row names its firing fixture.

| gcc-8 flag | clang-7 equivalent (or —) | cgf status | fixture path |
| --- | --- | --- | --- |
| -Wabi | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wabi= | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wpsabi | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Waddress | — | planned-s38 | — |
| -Wall | — | planned-s38 | — |
| -Walloca | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Walloc-size-larger-than= | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Walloc-zero | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Walloca-larger-than= | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wbad-function-cast | — | planned-s38 | — |
| -Wbool-compare | — | planned-s38 | — |
| -Wbool-operation | — | planned-s38 | — |
| -Wframe-address | — | planned-s38 | — |
| -Wbuiltin-declaration-mismatch | — | planned-s38 | — |
| -Wbuiltin-macro-redefined | — | planned-s38 | — |
| -Wc90-c99-compat | — | planned-s38 | — |
| -Wc99-c11-compat | — | planned-s38 | — |
| -Wc++-compat | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wcast-function-type | — | planned-s38 | — |
| -Wcast-qual | — | planned-s38 | — |
| -Wchar-subscripts | — | planned-s38 | — |
| -Wchkp | — | out-of-scope: the corresponding optional language or runtime feature is not in v0.1.0 | — |
| -Wclobbered | — | out-of-scope: setjmp dataflow is post-v0.1.0 (XD-S37-CLOBBERED) | — |
| -Wcomment | — | planned-s38 | — |
| -Wcomments | — | planned-s38 | — |
| -Wconversion | — | planned-s38 | — |
| -Wdangling-else | — | planned-s38 | — |
| -Wdate-time | — | planned-s38 | — |
| -Wdeclaration-after-statement | — | planned-s38 | — |
| -Wdeprecated | — | planned-s38 | — |
| -Wdesignated-init | — | planned-s38 | — |
| -Wdiscarded-array-qualifiers | — | planned-s38 | — |
| -Wdiscarded-qualifiers | — | planned-s38 | — |
| -Wdiv-by-zero | — | planned-s38 | — |
| -Wduplicated-branches | — | planned-s38 | — |
| -Wduplicated-cond | — | planned-s38 | — |
| -Wempty-body | — | planned-s38 | — |
| -Wendif-labels | — | planned-s38 | — |
| -Wenum-compare | — | planned-s38 | — |
| -Werror-implicit-function-declaration | — | planned-s38 | — |
| -Wextra | — | planned-s38 | — |
| -Wfloat-conversion | — | planned-s38 | — |
| -Wfloat-equal | — | planned-s38 | — |
| -Wformat | — | planned-s39 | — |
| -Wformat-contains-nul | — | planned-s39 | — |
| -Wformat-extra-args | — | planned-s39 | — |
| -Wformat-nonliteral | — | planned-s39 | — |
| -Wformat-overflow | — | out-of-scope: value-range format sizing is deferred beyond v0.1.0 (Sprint 39) | — |
| -Wformat-security | — | planned-s39 | — |
| -Wformat-signedness | — | planned-s39 | — |
| -Wformat-truncation | — | out-of-scope: value-range format sizing is deferred beyond v0.1.0 (Sprint 39) | — |
| -Wformat-y2k | — | planned-s39 | — |
| -Wformat-zero-length | — | planned-s39 | — |
| -Wformat= | — | planned-s39 | — |
| -Wformat-overflow= | — | out-of-scope: value-range format sizing is deferred beyond v0.1.0 (Sprint 39) | — |
| -Wformat-truncation= | — | out-of-scope: value-range format sizing is deferred beyond v0.1.0 (Sprint 39) | — |
| -Wif-not-aligned | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wignored-qualifiers | — | planned-s38 | — |
| -Wignored-attributes | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wincompatible-pointer-types | — | planned-s38 | — |
| -Winit-self | — | planned-s40 | — |
| -Wimplicit | — | planned-s38 | — |
| -Wdouble-promotion | — | planned-s38 | — |
| -Wexpansion-to-defined | — | planned-s38 | — |
| -Wimplicit-function-declaration | — | planned-s38 | — |
| -Wimplicit-int | — | planned-s38 | — |
| -Wint-conversion | — | planned-s38 | — |
| -Wint-in-bool-context | — | planned-s38 | — |
| -Wint-to-pointer-cast | — | planned-s38 | — |
| -Winvalid-pch | — | out-of-scope: gcov and precompiled headers are not supported | — |
| -Wjump-misses-init | — | planned-s38 | — |
| -Wlogical-op | — | planned-s38 | — |
| -Wlogical-not-parentheses | — | planned-s38 | — |
| -Wlong-long | — | planned-s38 | — |
| -Wmain | — | planned-s38 | — |
| -Wmemset-transposed-args | — | planned-s38 | — |
| -Wmemset-elt-size | — | planned-s38 | — |
| -Wmisleading-indentation | — | planned-s38 | — |
| -Wmissing-braces | — | planned-s38 | — |
| -Wmissing-declarations | — | planned-s38 | — |
| -Wmissing-field-initializers | — | planned-s38 | — |
| -Wmultistatement-macros | — | planned-s38 | — |
| -Wpacked-not-aligned | — | out-of-scope: layout-quality diagnostics are post-v0.1.0 | — |
| -Wsizeof-pointer-div | — | planned-s38 | — |
| -Wsizeof-pointer-memaccess | — | planned-s38 | — |
| -Wsizeof-array-argument | — | planned-s38 | — |
| -Wstringop-overflow | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wstringop-overflow= | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wstringop-truncation | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wsuggest-attribute=format | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wswitch | — | planned-s38 | — |
| -Wswitch-default | — | planned-s38 | — |
| -Wswitch-enum | — | planned-s38 | — |
| -Wswitch-bool | — | planned-s38 | — |
| -Wmissing-attributes | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wmissing-format-attribute | — | planned-s38 | — |
| -Wmissing-include-dirs | — | planned-s38 | — |
| -Wmissing-parameter-type | — | planned-s38 | — |
| -Wmissing-prototypes | — | planned-s38 | — |
| -Wmultichar | — | planned-s38 | — |
| -Wnarrowing | — | planned-s38 | — |
| -Wnested-externs | — | planned-s38 | — |
| -Wnonnull | — | planned-s38 | — |
| -Wnormalized | — | out-of-scope: Unicode normalization diagnostics are post-v0.1.0 | — |
| -Wnormalized= | — | out-of-scope: Unicode normalization diagnostics are post-v0.1.0 | — |
| -Wold-style-declaration | — | planned-s38 | — |
| -Wold-style-definition | — | planned-s38 | — |
| -Wopenmp-simd | — | out-of-scope: the corresponding optional language or runtime feature is not in v0.1.0 | — |
| -Woverlength-strings | — | planned-s38 | — |
| -Woverride-init | — | planned-s38 | — |
| -Woverride-init-side-effects | — | planned-s38 | — |
| -Wpacked-bitfield-compat | — | out-of-scope: layout-quality diagnostics are post-v0.1.0 | — |
| -Wparentheses | — | planned-s38 | — |
| -Wpedantic | — | planned-s38 | — |
| -Wpointer-arith | — | planned-s38 | — |
| -Wpointer-sign | — | planned-s38 | — |
| -Wpointer-compare | — | planned-s38 | — |
| -Wpointer-to-int-cast | — | planned-s38 | — |
| -Wpragmas | -Wpragmas | done | tests/warn/pragma/malformed.c |
| -Wredundant-decls | — | planned-s38 | — |
| -Wreturn-type | — | planned-s40 | — |
| -Wscalar-storage-order | — | planned-s38 | — |
| -Wsequence-point | — | out-of-scope: requires alias or value analysis beyond Phase 8 | — |
| -Wshift-overflow | — | planned-s38 | — |
| -Wshift-overflow= | — | planned-s38 | — |
| -Wshift-count-negative | — | planned-s38 | — |
| -Wshift-count-overflow | — | planned-s38 | — |
| -Wshift-negative-value | — | planned-s38 | — |
| -Wsign-compare | — | planned-s38 | — |
| -Wsign-conversion | — | planned-s38 | — |
| -Wstrict-prototypes | — | planned-s38 | — |
| -Wsync-nand | — | planned-s38 | — |
| -Wsystem-headers | — | planned-s38 | — |
| -Wtautological-compare | — | planned-s38 | — |
| -Wtraditional | — | planned-s38 | — |
| -Wtraditional-conversion | — | planned-s38 | — |
| -Wtrigraphs | — | planned-s38 | — |
| -Wundef | — | planned-s38 | — |
| -Wunknown-pragmas | -Wunknown-pragmas | done | tests/warn/pragma/unknown.c |
| -Wunsuffixed-float-constants | — | planned-s38 | — |
| -Wunused-local-typedefs | — | planned-s38 | — |
| -Wunused-macros | — | planned-s38 | — |
| -Wunused-result | — | planned-s38 | — |
| -Wunused-const-variable | — | planned-s38 | — |
| -Wunused-const-variable= | — | planned-s38 | — |
| -Wvariadic-macros | — | planned-s38 | — |
| -Wvarargs | — | planned-s38 | — |
| -Wvla | — | planned-s38 | — |
| -Wvla-larger-than= | — | planned-s38 | — |
| -Wvolatile-register-var | — | planned-s38 | — |
| -Wwrite-strings | — | planned-s38 | — |
| -Wduplicate-decl-specifier | — | planned-s38 | — |
| -Wrestrict | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -W | — | planned-s38 | — |
| -Waggregate-return | — | out-of-scope: resource and stack-size diagnostics are post-v0.1.0 | — |
| -Waggressive-loop-optimizations | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |
| -Warray-bounds | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Warray-bounds= | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wattributes | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wattribute-alias | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wcast-align | — | planned-s38 | — |
| -Wcast-align=strict | — | planned-s38 | — |
| -Wcpp | -W#warnings | done | tests/warn/pragma/cpp.c |
| -Wdeprecated-declarations | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wdisabled-optimization | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |
| -Wextra | — | planned-s38 | — |
| -Wframe-larger-than= | — | planned-s38 | — |
| -Wfree-nonheap-object | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Whsa | — | out-of-scope: the corresponding optional language or runtime feature is not in v0.1.0 | — |
| -Wimplicit-fallthrough | — | planned-s38 | — |
| -Wimplicit-fallthrough= | — | planned-s38 | — |
| -Winline | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |
| -Winvalid-memory-model | — | planned-s38 | — |
| -Wlarger-than- | — | planned-s38 | — |
| -Wlarger-than= | — | planned-s38 | — |
| -Wnonnull-compare | — | planned-s38 | — |
| -Wnull-dereference | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wunsafe-loop-optimizations | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |
| -Wmissing-noreturn | — | planned-s38 | — |
| -Wodr | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Woverflow | — | planned-s38 | — |
| -Wlto-type-mismatch | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wpacked | — | out-of-scope: layout-quality diagnostics are post-v0.1.0 | — |
| -Wpadded | — | out-of-scope: layout-quality diagnostics are post-v0.1.0 | — |
| -Wpedantic | — | planned-s38 | — |
| -Wreturn-local-addr | — | planned-s40 | — |
| -Wshadow | — | planned-s38 | — |
| -Wshadow=global | — | planned-s38 | — |
| -Wshadow=local | — | planned-s38 | — |
| -Wshadow-local | — | planned-s38 | — |
| -Wshadow=compatible-local | — | planned-s38 | — |
| -Wshadow-compatible-local | — | planned-s38 | — |
| -Wstack-protector | — | out-of-scope: resource and stack-size diagnostics are post-v0.1.0 | — |
| -Wstack-usage= | — | planned-s38 | — |
| -Wstrict-aliasing | — | out-of-scope: requires alias or value analysis beyond Phase 8 | — |
| -Wstrict-aliasing= | — | out-of-scope: requires alias or value analysis beyond Phase 8 | — |
| -Wstrict-overflow | — | out-of-scope: requires alias or value analysis beyond Phase 8 | — |
| -Wstrict-overflow= | — | out-of-scope: requires alias or value analysis beyond Phase 8 | — |
| -Wsuggest-attribute=cold | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wsuggest-attribute=const | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wsuggest-attribute=pure | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wsuggest-attribute=noreturn | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wsuggest-attribute=malloc | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wsuggest-final-types | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wsuggest-final-methods | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wswitch-unreachable | — | planned-s40 | — |
| -Wsystem-headers | — | planned-s38 | — |
| -Wtrampolines | — | out-of-scope: the underlying C++ or GNU nested-function feature is not in v0.1.0 | — |
| -Wtype-limits | — | planned-s38 | — |
| -Wuninitialized | — | planned-s40 | — |
| -Wmaybe-uninitialized | — | planned-s40 | — |
| -Wunreachable-code | — | planned-s40 | — |
| -Wunused | — | planned-s38 | — |
| -Wunused-but-set-parameter | — | planned-s38 | — |
| -Wunused-but-set-variable | — | planned-s38 | — |
| -Wunused-function | — | planned-s38 | — |
| -Wunused-label | — | planned-s38 | — |
| -Wunused-parameter | — | planned-s38 | — |
| -Wunused-value | — | planned-s38 | — |
| -Wunused-variable | — | planned-s38 | — |
| -Wcoverage-mismatch | — | out-of-scope: gcov and precompiled headers are not supported | — |
| -Wvector-operation-performance | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |

## Registry spellings

These supplemental rows cover CGF/Clang-compatible extensions and the normalized
spellings used by `warnings.def` for GCC parameter-taking records. They are not
additional raw GCC records and therefore are not part of the 222-row source count.

| gcc-8 flag | clang-7 equivalent (or —) | cgf status | fixture path |
| --- | --- | --- | --- |
| -Wbackslash-newline-escape | -Wbackslash-newline-escape | done | tests/unit/test_pp_lex.c |
| -Wbundled-only-option | — | done | tests/programs/driver/fast_math_component_warns.c |
| -Wc11-extensions | -Wc11-extensions | done | tests/warn/pragma/migrated_policy.c |
| -Wc23-extensions | -Wc23-extensions | done | tests/warn/pragma/migrated_policy.c |
| -Wcompare-distinct-pointer-types | -Wcompare-distinct-pointer-types | planned-s38 | — |
| -Wempty-declaration | -Wempty-declaration | done | tests/warn/pragma/migrated_policy.c |
| -Wfatal-errors | -Wfatal-errors | out-of-scope: accepted for driver compatibility; early-stop policy is post-Sprint 37 | — |
| -Wframe-larger-than | — | out-of-scope: normalized parameter spelling; resource diagnostics are post-v0.1.0 | — |
| -Winitializer-string-too-long | -Wexcess-initializers | done | tests/warn/pragma/initializer_string.c |
| -Winvalid-function-specifier | -Wignored-attributes | done | tests/warn/pragma/migrated_policy.c |
| -Wlarger-than | — | out-of-scope: normalized parameter spelling; resource diagnostics are post-v0.1.0 | — |
| -Wmacro-redefined | -Wmacro-redefined | done | tests/warn/pragma/macro_redefined.c |
| -Wnewline-eof | -Wnewline-eof | planned-s38 | — |
| -Wnull-character | -Wnull-character | done | tests/programs/pp/fuzz_embedded_nul.c |
| -Wpointer-compared-to-zero-with-relational | — | planned-s38 | — |
| -Wstatic-in-inline | -Wstatic-in-inline | done | tests/programs/sema16/inline_constraints.c |
| -Wtentative-definition-array | — | done | tests/warn/pragma/migrated_policy.c |
| -Wtypedef-redefinition | -Wtypedef-redefinition | done | tests/warn/pragma/migrated_policy.c |
| -Wunknown-warning-option | -Wunknown-warning-option | done | tests/programs/driver/wbogus_warns.c |
| -Wunused-command-line-argument | — | out-of-scope: CGF driver diagnostic absent from the GCC 8 raw warning inventory | — |
| -Wvisibility | -Wvisibility | done | tests/warn/pragma/migrated_policy.c |
| -Wvla-larger-than | — | out-of-scope: normalized parameter spelling; memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wzero-length-array | -Wzero-length-array | done | tests/warn/pragma/migrated_policy.c |
