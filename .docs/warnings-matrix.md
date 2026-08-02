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

`-Wreturn-type` is split deliberately: Sprint 38 completes the syntactic
return/value mismatch diagnostics recorded below; Sprint 40 still owns the CFG
half, “control reaches end of non-void function.”

| gcc-8 flag | clang-7 equivalent (or —) | cgf status | fixture path |
| --- | --- | --- | --- |
| -Wabi | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wabi= | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wpsabi | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Waddress | — | done | tests/warn/frontend/address/fire_function_designator.c |
| -Wall | — | done | tests/unit/test_s38_warn_defaults.c |
| -Walloca | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Walloc-size-larger-than= | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Walloc-zero | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Walloca-larger-than= | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wbad-function-cast | — | planned-s39 | — |
| -Wbool-compare | — | done | tests/warn/frontend/bool-compare/fire_out_of_range.c |
| -Wbool-operation | — | planned-s39 | — |
| -Wframe-address | — | planned-s39 | — |
| -Wbuiltin-declaration-mismatch | — | planned-s39 | — |
| -Wbuiltin-macro-redefined | — | planned-s39 | — |
| -Wc90-c99-compat | — | planned-s39 | — |
| -Wc99-c11-compat | — | planned-s39 | — |
| -Wc++-compat | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wcast-function-type | — | planned-s39 | — |
| -Wcast-qual | — | planned-s39 | — |
| -Wchar-subscripts | — | done | tests/warn/frontend/char-subscripts/fire_plain_char.c |
| -Wchkp | — | out-of-scope: the corresponding optional language or runtime feature is not in v0.1.0 | — |
| -Wclobbered | — | out-of-scope: setjmp dataflow is post-v0.1.0 (XD-S37-CLOBBERED) | — |
| -Wcomment | — | planned-s39 | — |
| -Wcomments | — | planned-s39 | — |
| -Wconversion | — | done | tests/warn/frontend/conversion/fire-narrowing.c |
| -Wdangling-else | — | planned-s39 | — |
| -Wdate-time | — | planned-s39 | — |
| -Wdeclaration-after-statement | — | planned-s39 | — |
| -Wdeprecated | — | planned-s39 | — |
| -Wdesignated-init | — | planned-s39 | — |
| -Wdiscarded-array-qualifiers | — | planned-s39 | — |
| -Wdiscarded-qualifiers | — | planned-s39 | — |
| -Wdiv-by-zero | — | planned-s39 | — |
| -Wduplicated-branches | — | planned-s39 | — |
| -Wduplicated-cond | — | planned-s39 | — |
| -Wempty-body | — | planned-s39 | — |
| -Wendif-labels | — | planned-s39 | — |
| -Wenum-compare | — | planned-s39 | — |
| -Werror-implicit-function-declaration | — | planned-s39 | — |
| -Wextra | — | done | tests/unit/test_s38_warn_defaults.c |
| -Wfloat-conversion | — | planned-s39 | — |
| -Wfloat-equal | — | planned-s39 | — |
| -Wformat | — | done | tests/warn/format/grammar/printf_mismatch.c |
| -Wformat-contains-nul | — | done | tests/warn/format/levels/contains_nul.c |
| -Wformat-extra-args | — | done | tests/warn/format/levels/extra_args.c |
| -Wformat-nonliteral | — | done | tests/warn/format/levels/nonliteral_level2.c |
| -Wformat-overflow | — | out-of-scope: value-range format sizing is deferred beyond v0.1.0 (Sprint 39 §Defer) | — |
| -Wformat-security | — | done | tests/warn/format/levels/security_level2.c |
| -Wformat-signedness | — | done | tests/warn/format/levels/signedness_on.c |
| -Wformat-truncation | — | out-of-scope: value-range format sizing is deferred beyond v0.1.0 (Sprint 39 §Defer) | — |
| -Wformat-y2k | — | done | tests/warn/format/levels/y2k_on.c |
| -Wformat-zero-length | — | done | tests/warn/format/levels/zero_length.c |
| -Wformat= | — | done | tests/warn/format/levels/security_level1.c |
| -Wformat-overflow= | — | out-of-scope: value-range format sizing is deferred beyond v0.1.0 (Sprint 39 §Defer) | — |
| -Wformat-truncation= | — | out-of-scope: value-range format sizing is deferred beyond v0.1.0 (Sprint 39 §Defer) | — |
| -Wif-not-aligned | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wignored-qualifiers | — | planned-s39 | — |
| -Wignored-attributes | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wincompatible-pointer-types | — | planned-s39 | — |
| -Winit-self | — | planned-s40 | — |
| -Wimplicit | — | done | tests/unit/test_warn.c |
| -Wdouble-promotion | — | planned-s39 | — |
| -Wexpansion-to-defined | — | planned-s39 | — |
| -Wimplicit-function-declaration | — | done | tests/warn/frontend/implicit-function-declaration/fire-c99.c |
| -Wimplicit-int | — | done | tests/warn/frontend/implicit-int/fire-c99.c |
| -Wint-conversion | — | planned-s39 | — |
| -Wint-in-bool-context | — | planned-s39 | — |
| -Wint-to-pointer-cast | — | planned-s39 | — |
| -Winvalid-pch | — | out-of-scope: gcov and precompiled headers are not supported | — |
| -Wjump-misses-init | — | planned-s39 | — |
| -Wlogical-op | — | planned-s39 | — |
| -Wlogical-not-parentheses | — | done | tests/warn/frontend/logical-not-parentheses/fire_not_compared_to_three.c |
| -Wlong-long | — | planned-s39 | — |
| -Wmain | — | planned-s39 | — |
| -Wmemset-transposed-args | — | planned-s39 | — |
| -Wmemset-elt-size | — | planned-s39 | — |
| -Wmisleading-indentation | — | done | tests/warn/frontend/misleading-indentation/fire_if_body_column.c |
| -Wmissing-braces | — | planned-s39 | — |
| -Wmissing-declarations | — | planned-s39 | — |
| -Wmissing-field-initializers | — | planned-s39 | — |
| -Wmultistatement-macros | — | planned-s39 | — |
| -Wpacked-not-aligned | — | out-of-scope: layout-quality diagnostics are post-v0.1.0 | — |
| -Wsizeof-pointer-div | — | planned-s39 | — |
| -Wsizeof-pointer-memaccess | — | planned-s39 | — |
| -Wsizeof-array-argument | — | planned-s39 | — |
| -Wstringop-overflow | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wstringop-overflow= | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wstringop-truncation | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wsuggest-attribute=format | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wswitch | — | done | tests/warn/frontend/switch/fire_missing_enumerator.c |
| -Wswitch-default | — | done | tests/warn/frontend/switch-default/fire_missing_default.c |
| -Wswitch-enum | — | done | tests/warn/frontend/switch-enum/fire_default_does_not_suppress.c |
| -Wswitch-bool | — | planned-s39 | — |
| -Wmissing-attributes | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wmissing-format-attribute | — | out-of-scope: format attribute semantics land in Sprint 55 | — |
| -Wmissing-include-dirs | — | planned-s39 | — |
| -Wmissing-parameter-type | — | done | tests/warn/frontend/missing-parameter-type/fire_untyped_knr_parameter.c |
| -Wmissing-prototypes | — | done | tests/warn/frontend/missing-prototypes/fire_external_definition.c |
| -Wmultichar | — | planned-s39 | — |
| -Wnarrowing | — | planned-s39 | — |
| -Wnested-externs | — | planned-s39 | — |
| -Wnonnull | — | done | tests/warn/format/levels/nonnull_implied.c |
| -Wnormalized | — | out-of-scope: Unicode normalization diagnostics are post-v0.1.0 | — |
| -Wnormalized= | — | out-of-scope: Unicode normalization diagnostics are post-v0.1.0 | — |
| -Wold-style-declaration | — | done | tests/warn/frontend/old-style-declaration/fire_storage_class_order.c |
| -Wold-style-definition | — | done | tests/warn/frontend/old-style-definition/fire_knr_definition.c |
| -Wopenmp-simd | — | out-of-scope: the corresponding optional language or runtime feature is not in v0.1.0 | — |
| -Woverlength-strings | — | planned-s39 | — |
| -Woverride-init | — | planned-s39 | — |
| -Woverride-init-side-effects | — | planned-s39 | — |
| -Wpacked-bitfield-compat | — | out-of-scope: layout-quality diagnostics are post-v0.1.0 | — |
| -Wparentheses | — | done | tests/warn/frontend/parentheses/fire-assignment.c |
| -Wpedantic | — | planned-s39 | — |
| -Wpointer-arith | — | done | tests/warn/frontend/pointer-arith/fire-void-add.c |
| -Wpointer-sign | — | planned-s39 | — |
| -Wpointer-compare | — | planned-s39 | — |
| -Wpointer-to-int-cast | — | planned-s39 | — |
| -Wpragmas | -Wpragmas | done | tests/warn/pragma/malformed.c |
| -Wredundant-decls | — | planned-s39 | — |
| -Wreturn-type | — | done | tests/warn/frontend/return-type/fire_missing_value.c |
| -Wscalar-storage-order | — | planned-s39 | — |
| -Wsequence-point | — | out-of-scope: requires alias or value analysis beyond Phase 8 | — |
| -Wshift-overflow | — | planned-s39 | — |
| -Wshift-overflow= | — | planned-s39 | — |
| -Wshift-count-negative | — | planned-s39 | — |
| -Wshift-count-overflow | — | planned-s39 | — |
| -Wshift-negative-value | — | planned-s39 | — |
| -Wsign-compare | — | done | tests/warn/frontend/sign-compare/fire-variable.c |
| -Wsign-conversion | — | done | tests/warn/frontend/sign-conversion/fire-negative.c |
| -Wstrict-prototypes | — | done | tests/warn/frontend/strict-prototypes/fire_empty_parameters.c |
| -Wsync-nand | — | planned-s39 | — |
| -Wsystem-headers | — | planned-s39 | — |
| -Wtautological-compare | — | done | tests/warn/frontend/tautological-compare/fire_self_compare.c |
| -Wtraditional | — | planned-s39 | — |
| -Wtraditional-conversion | — | planned-s39 | — |
| -Wtrigraphs | — | planned-s39 | — |
| -Wundef | — | planned-s39 | — |
| -Wunknown-pragmas | -Wunknown-pragmas | done | tests/warn/pragma/unknown.c |
| -Wunsuffixed-float-constants | — | planned-s39 | — |
| -Wunused-local-typedefs | — | planned-s39 | — |
| -Wunused-macros | — | planned-s39 | — |
| -Wunused-result | — | planned-s39 | — |
| -Wunused-const-variable | — | planned-s39 | — |
| -Wunused-const-variable= | — | planned-s39 | — |
| -Wvariadic-macros | — | planned-s39 | — |
| -Wvarargs | — | planned-s39 | — |
| -Wvla | — | done | tests/warn/frontend/vla/fire_runtime_bound.c |
| -Wvla-larger-than= | — | planned-s39 | — |
| -Wvolatile-register-var | — | planned-s39 | — |
| -Wwrite-strings | — | planned-s39 | — |
| -Wduplicate-decl-specifier | — | planned-s39 | — |
| -Wrestrict | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -W | — | planned-s39 | — |
| -Waggregate-return | — | out-of-scope: resource and stack-size diagnostics are post-v0.1.0 | — |
| -Waggressive-loop-optimizations | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |
| -Warray-bounds | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Warray-bounds= | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wattributes | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wattribute-alias | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wcast-align | — | planned-s39 | — |
| -Wcast-align=strict | — | planned-s39 | — |
| -Wcpp | -W#warnings | done | tests/warn/pragma/cpp.c |
| -Wdeprecated-declarations | — | out-of-scope: attribute semantics land in Sprint 55 | — |
| -Wdisabled-optimization | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |
| -Wextra | — | done | tests/unit/test_s38_warn_defaults.c |
| -Wframe-larger-than= | — | planned-s39 | — |
| -Wfree-nonheap-object | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Whsa | — | out-of-scope: the corresponding optional language or runtime feature is not in v0.1.0 | — |
| -Wimplicit-fallthrough | — | done | tests/warn/frontend/implicit-fallthrough/fire-level3.c |
| -Wimplicit-fallthrough= | — | done | tests/warn/frontend/implicit-fallthrough/fire-level4-lower.c |
| -Winline | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |
| -Winvalid-memory-model | — | planned-s39 | — |
| -Wlarger-than- | — | planned-s39 | — |
| -Wlarger-than= | — | planned-s39 | — |
| -Wnonnull-compare | — | planned-s39 | — |
| -Wnull-dereference | — | out-of-scope: memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wunsafe-loop-optimizations | — | out-of-scope: optimizer-quality diagnostics are post-v0.1.0 | — |
| -Wmissing-noreturn | — | planned-s39 | — |
| -Wodr | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Woverflow | — | done | tests/warn/frontend/overflow/fire-char.c |
| -Wlto-type-mismatch | — | out-of-scope: C++ or ABI compatibility diagnostics are outside Phase 8 | — |
| -Wpacked | — | out-of-scope: layout-quality diagnostics are post-v0.1.0 | — |
| -Wpadded | — | out-of-scope: layout-quality diagnostics are post-v0.1.0 | — |
| -Wpedantic | — | planned-s39 | — |
| -Wreturn-local-addr | — | planned-s40 | — |
| -Wshadow | — | done | tests/warn/frontend/shadow/fire-global.c |
| -Wshadow=global | — | planned-s39 | — |
| -Wshadow=local | — | planned-s39 | — |
| -Wshadow-local | — | planned-s39 | — |
| -Wshadow=compatible-local | — | planned-s39 | — |
| -Wshadow-compatible-local | — | planned-s39 | — |
| -Wstack-protector | — | out-of-scope: resource and stack-size diagnostics are post-v0.1.0 | — |
| -Wstack-usage= | — | planned-s39 | — |
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
| -Wsystem-headers | — | planned-s39 | — |
| -Wtrampolines | — | out-of-scope: the underlying C++ or GNU nested-function feature is not in v0.1.0 | — |
| -Wtype-limits | — | done | tests/warn/frontend/type-limits/fire_unsigned_zero.c |
| -Wuninitialized | — | planned-s40 | — |
| -Wmaybe-uninitialized | — | planned-s40 | — |
| -Wunreachable-code | — | planned-s40 | — |
| -Wunused | — | done | tests/unit/test_s38_warn_defaults.c |
| -Wunused-but-set-parameter | — | done | tests/warn/frontend/unused-but-set-parameter/fire.c |
| -Wunused-but-set-variable | — | done | tests/warn/frontend/unused-but-set-variable/fire.c |
| -Wunused-function | — | done | tests/warn/frontend/unused-function/fire-static.c |
| -Wunused-label | — | done | tests/warn/frontend/unused-label/fire.c |
| -Wunused-parameter | — | done | tests/warn/frontend/unused-parameter/fire.c |
| -Wunused-value | — | done | tests/warn/frontend/unused-value/fire-arithmetic.c |
| -Wunused-variable | — | done | tests/warn/frontend/unused-variable/fire-local.c |
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
| -Wcompare-distinct-pointer-types | -Wcompare-distinct-pointer-types | planned-s39 | — |
| -Wempty-declaration | -Wempty-declaration | done | tests/warn/pragma/migrated_policy.c |
| -Wfatal-errors | -Wfatal-errors | out-of-scope: accepted for driver compatibility; early-stop policy is post-Sprint 37 | — |
| -Wformat-unbounded-scanf | — | done | tests/warn/format/grammar/unbounded_scanf_on.c |
| -Wframe-larger-than | — | out-of-scope: normalized parameter spelling; resource diagnostics are post-v0.1.0 | — |
| -Winitializer-string-too-long | -Wexcess-initializers | done | tests/warn/pragma/initializer_string.c |
| -Winvalid-function-specifier | -Wignored-attributes | done | tests/warn/pragma/migrated_policy.c |
| -Wlarger-than | — | out-of-scope: normalized parameter spelling; resource diagnostics are post-v0.1.0 | — |
| -Wmacro-redefined | -Wmacro-redefined | done | tests/warn/pragma/macro_redefined.c |
| -Wnewline-eof | -Wnewline-eof | planned-s39 | — |
| -Wnull-character | -Wnull-character | done | tests/programs/pp/fuzz_embedded_nul.c |
| -Wpointer-compared-to-zero-with-relational | — | planned-s39 | — |
| -Wstatic-in-inline | -Wstatic-in-inline | done | tests/programs/sema16/inline_constraints.c |
| -Wtentative-definition-array | — | done | tests/warn/pragma/migrated_policy.c |
| -Wtypedef-redefinition | -Wtypedef-redefinition | done | tests/warn/pragma/migrated_policy.c |
| -Wunknown-warning-option | -Wunknown-warning-option | done | tests/programs/driver/wbogus_warns.c |
| -Wunused-command-line-argument | — | out-of-scope: CGF driver diagnostic absent from the GCC 8 raw warning inventory | — |
| -Wvisibility | -Wvisibility | done | tests/warn/pragma/migrated_policy.c |
| -Wvla-larger-than | — | out-of-scope: normalized parameter spelling; memory analysis is owned by Phase 9 (Sprint 42) | — |
| -Wzero-length-array | -Wzero-length-array | done | tests/warn/pragma/migrated_policy.c |

## GCC 8 differential divergence ledger

The whole-tree differential requires exact `(line, warning-group)` parity unless
the fixture records one of these intentional policy or diagnostic-name differences.

| fixture | intentional difference |
| --- | --- |
| tests/warn/frontend/conversion/fire-double-literal-to-float.c | GCC 8 uses `-Wfloat-conversion`; CGF's group lands in Sprint 39. |
| tests/warn/frontend/conversion/fire-double-variable-to-float.c | GCC 8 uses `-Wfloat-conversion`; CGF's group lands in Sprint 39. |
| tests/warn/frontend/return-type/fire_missing_value.c | GCC 8 emits the warning without a diagnostic-group tag. |
| tests/warn/frontend/shadow/nofire-macro-declaration.c | CGF suppresses macro-originated shadow declarations. |
| tests/warn/frontend/type-limits/nofire_macro_guard.c | CGF suppresses range diagnostics for macro-originated operands. |
| tests/warn/frontend/unused-variable/nofire-macro-declaration.c | CGF suppresses macro-originated unused declarations. |
| tests/warn/frontend/unused-but-set-parameter/fire-increment.c | GCC 8 counts a discarded increment as a meaningful read. |
| tests/warn/frontend/unused-but-set-variable/fire-compound-assignment.c | GCC 8 counts discarded compound assignment as a meaningful read. |
| tests/warn/pragma/error_implies_enable.c | A CGF diagnostic-error pragma enables its warning group. |
| tests/warn/pragma/initializer_string.c | CGF uses its Clang-compatible name and dump-only mode. |
| tests/warn/pragma/macro_definition_state.c | CGF binds state at macro expansion rather than definition. |
| tests/warn/pragma/macro_redefined.c | CGF exposes Clang-compatible `-Wmacro-redefined`. |
| tests/warn/pragma/migrated_policy.c | CGF retains Clang-compatible migrated diagnostic names. |
| tests/warn/pragma/nul_system_enabled.c | CGF exposes Clang-compatible `-Wnull-character`. |
| tests/warn/pragma/primary_system_header.c | CGF diagnoses primary-file `system_header` and the following pragma. |
| tests/warn/pragma/system_enabled.c | CGF's system-header policy includes its own header fixture. |
| tests/warn/pragma/system_pragma_threshold.c | CGF applies its `system_header` pragma threshold. |
| tests/warn/pragma/unknown.c | CGF uses its own unknown-pragma spelling and policy. |
