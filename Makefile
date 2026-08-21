# Cgfried — the one hand-written GNU Makefile
# Any C11 compiler bootstraps stage0: never hardcode gcc.
CC     ?= cc
BUILD  ?= build
PREFIX ?= /usr/local
HOSTCC ?= $(CC)
BOOTSTRAP_JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
BOOTSTRAP_WORK ?= $(BUILD)/boot
BOOTSTRAP_HOST ?=
BOOTSTRAP_HOST_CLASS ?= $(if $(BOOTSTRAP_HOST),,local)
BOOTSTRAP_CONTROL_FILE ?=
BOOTSTRAP_SYSROOT ?=
BOOTSTRAP_CROSS_SYSROOT ?= $(BOOTSTRAP_WORK)/cross/sysroot
BOOTSTRAP_CROSS_OUTPUT ?= $(BOOTSTRAP_WORK)/cross/output
BOOTSTRAP_CROSS_X86 ?= $(BOOTSTRAP_WORK)/cross/x86
BOOTSTRAP_CROSS_NATIVE ?= $(BOOTSTRAP_WORK)/cross/native
BOOTSTRAP_CROSS_CGF ?= $(BOOTSTRAP_WORK)/O2/stage1/cgfried
BOOTSTRAP_CROSS_X86_BOOTSTRAP ?= $(BOOTSTRAP_WORK)/O2
BOOTSTRAP_REPRO_OUTPUT ?= $(BOOTSTRAP_WORK)/O2-repro-j1

# -MMD not -MD: system-header deps churn without value. -MP: survive header
# deletion. No -D with absolute build paths — that breaks the byte-identical
# stage1 == stage2 bootstrap (Sprint 58) and reproducible dists (Sprint 62).
# _POSIX_C_SOURCE: we are C11 + POSIX (spawn, poll, isatty); strict -std=c11
# alone hides POSIX declarations behind feature guards on glibc.
EXTRA_CFLAGS ?=
CFLAGS := -std=c11 -pedantic -Wall -Wextra -Werror -g -O2 \
          -fno-strict-overflow -D_POSIX_C_SOURCE=200809L -MMD -MP -Isrc \
          $(EXTRA_CFLAGS)

# Sorted: raw find order varies by filesystem and would leak into link order,
# making binaries nondeterministic.
# src/rt/ is the RUNTIME, not the compiler: it ships as libcgf_rt.a,
# builds with its own flags (it must stay buildable by a plain
# toolchain, and it needs __int128 which -pedantic rejects), and must
# never be linked into cgfried.
SRC := $(shell find src -name '*.c' -not -path 'src/rt/*' | sort)
OBJ := $(SRC:%.c=$(BUILD)/%.o)
COMPILER_PROVENANCE_INPUTS := Makefile \
    $(shell find src include -type f | sort) scripts/torture-provenance.sh
COMPILER_PROVENANCE_RECEIPT := $(BUILD)/cgfried.provenance
# A removed source/header disappears from the dynamic prerequisite list, which
# ordinary make timestamp logic cannot notice.  Keep one content-addressed
# state file and update its mtime only when the current path set changes; unlike
# permanent digest-named stamps, this also catches A -> B -> C -> B transitions.
# The cryptographic receipt remains the exact safety check, so cksum is only a
# rebuild trigger.
COMPILER_SOURCE_SET_ID := $(shell { printf '%s\n' Makefile; \
    find src include -type f -print; } | LC_ALL=C sort -u | cksum | \
    awk '{ print $$1 "-" $$2 }')
COMPILER_SOURCE_SET_STAMP := $(BUILD)/gen/compiler-source-set

# Everything but the driver entry point, shared by test binaries.
LIB_OBJ := $(filter-out $(BUILD)/src/main.o,$(OBJ))

# cgf-test runner: consumer of src/util, never a fork of it.
RUNNER_SRC := $(filter-out tests/runner/ppdiff.c,$(sort $(wildcard tests/runner/*.c)))
RUNNER_OBJ := $(RUNNER_SRC:%.c=$(BUILD)/%.o) $(LIB_OBJ)

# The differential harness shares the runner's spawn layer and the
# compiler's own pp lexer (it tokenizes -E output, never text-diffs).
PPDIFF_OBJ := $(BUILD)/tests/runner/ppdiff.o $(BUILD)/tests/runner/spawn.o $(LIB_OBJ)

# The PP fuzzer: zero deps, deterministic seeds, forks the real binary.
FUZZ_OBJ := $(BUILD)/tests/fuzz/ppfuzz.o $(BUILD)/tests/runner/spawn.o $(LIB_OBJ)
# The layout differential's generator: emits random structs whose layout
# gcc then certifies via _Static_assert built from OUR numbers.
GENLAYOUT_OBJ := $(BUILD)/tests/tools/gen_layout.o $(LIB_OBJ)
# Sprint 60 optimizer-audit tools stay outside the default test suite: the
# generated campaign is deliberately bounded but comparatively expensive, and
# the alias oracle records the currently expected OPT-H-04 proof failure.
OPTGEN_OBJ := $(BUILD)/tests/tools/optgen.o
ALIAS_ORACLE_OBJ := $(BUILD)/tests/tools/alias_oracle.o $(LIB_OBJ)
OBJDIFF_OBJ := $(BUILD)/tests/tools/objdiff.o
# The ABI differential's generator. Host-only and dependency-free: it emits
# C sources, it is never itself a compilation target.
ABIGEN_OBJ := $(BUILD)/tests/tools/abigen.o
A64_OBJBYTES_OBJ := $(BUILD)/tests/tools/a64_objbytes.o
A64MIR_OBJ := $(BUILD)/tests/tools/a64mir.o $(LIB_OBJ)
A64_LOGIMM_GEN_OBJ := $(BUILD)/tests/tools/a64_logimm_gen.o $(LIB_OBJ)
# The float differential's printer: softfp only, no compiler, no host FPU.
FPDIFF_OBJ := $(BUILD)/tests/tools/fpdiff.o $(BUILD)/src/util/softfp.o \
              $(BUILD)/src/util/bigint.o

# The frontend fuzzer: same shape, drives -fsyntax-only over pp+lex+parse.
FEFUZZ_OBJ := $(BUILD)/tests/fuzz/fuzz_frontend.o \
              $(BUILD)/tests/runner/spawn.o $(LIB_OBJ)
IRFUZZ_OBJ := $(BUILD)/tests/fuzz/ir_fuzz.o $(LIB_OBJ)

# Sprint 52's host measurement tool is deliberately standalone. The second
# object compiles the same pure statistics helpers without main so the unit
# harness can pin median/MAD arithmetic instead of timing the scheduler.
TIMEIT_OBJ := $(BUILD)/tests/bench/timeit.o
TIMEIT_LIB_OBJ := $(BUILD)/tests/bench/timeit_lib.o
TIMEIT_MATH_TEST_OBJ := $(BUILD)/tests/bench/timeit_math_test.o

# Corpus identity is part of every performance number. A source or flag bump
# must change this pin and land with a separately reviewed baseline update.
SQLITE_AMALGAMATION_VERSION := 3500400
SQLITE_AMALGAMATION_SHA3 := 9145255e83da6529e70121ee4d7a4c88fe83ca4511da0c9ed13d10842df36782
SQLITE_AMALGAMATION_CKSUM := 2703132855:9282866

BENCH_SKIP_TIME ?= 0
BENCH_RUNS ?= $(if $(filter 1,$(BENCH_SKIP_TIME)),1,10)
BENCH_WARMUP ?= $(if $(filter 1,$(BENCH_SKIP_TIME)),0,1)
BENCH_RESULTS ?= $(BUILD)/bench/results.txt
BENCH_HOST_CLASS ?= ci
BENCH_BASELINE ?= .benchmarks/baseline-$(RT_HOST_TARGET).$(BENCH_HOST_CLASS).txt
KERNEL_X86_RESULT ?= $(BUILD)/bench/kernels-x86_64-linux-gnu.txt
KERNEL_A64_RESULT ?= $(BUILD)/bench/kernels-arm64-linux.txt
KERNEL_X86_GOLDEN := .benchmarks/golden/kernels-x86_64-linux-gnu.txt
KERNEL_A64_GOLDEN := .benchmarks/golden/kernels-arm64-linux.txt
KERNEL_DASHBOARD ?= .benchmarks/kernels-vs-gcc.md
SIZE_X86_RESULT ?= $(BUILD)/bench/size-x86_64-linux-gnu.txt
SIZE_A64_RESULT ?= $(BUILD)/bench/size-arm64-linux.txt
SIZE_X86_BASELINE := .benchmarks/baseline-size-x86_64-linux-gnu.ci.txt
SIZE_A64_BASELINE := .benchmarks/baseline-size-arm64-linux.ci.txt
PERF_CONFIG_DIR := ci/gates.d
PERF_REPORT_VERSION ?= 0.0.x
PERF_REPORT ?= .benchmarks/report-$(PERF_REPORT_VERSION).md
PERF_REPORT_LATEST ?=
PERF_REPORT_PREVIOUS ?=

# Unit harness: explicit registry generated at build time (strict C11 — no
# constructor attributes). The registry depends on every test_*.c, or a
# stale registry would silently drop new tests.
UNIT_TEST_SRC := $(sort $(wildcard tests/unit/test_*.c))
UNIT_OBJ := $(BUILD)/tests/unit/unit_main.o \
            $(UNIT_TEST_SRC:%.c=$(BUILD)/%.o) \
            $(BUILD)/gen/unit_registry.o \
            $(BUILD)/tests/runner/directive.o \
            $(TIMEIT_LIB_OBJ) \
            $(LIB_OBJ)

DIRS := $(sort $(dir $(OBJ) $(RUNNER_OBJ) $(UNIT_OBJ) $(PPDIFF_OBJ) $(FUZZ_OBJ) $(FEFUZZ_OBJ) $(GENLAYOUT_OBJ) $(OPTGEN_OBJ) $(ALIAS_ORACLE_OBJ) $(FPDIFF_OBJ) $(A64_OBJBYTES_OBJ) $(A64MIR_OBJ) $(A64_LOGIMM_GEN_OBJ) $(TIMEIT_OBJ) $(TIMEIT_LIB_OBJ) $(TIMEIT_MATH_TEST_OBJ)) $(BUILD)/gen/)

.PHONY: FORCE all test test-san test-ppdiff test-warndiff test-flow-warnings \
        test-memsafe-foundation test-mem-warnings test-mem-interproc \
        test-mem-runtime test-mem-autofix test-safe-mode safe-dogfood \
        test-mem-fanalyzer bench-safe \
        test-bench test-perf-gates test-kernels kernels kernel-compare \
        bench bench-gate perf-size-x86 perf-size-arm64 perf-static-x86 \
        perf-static-arm64 perf-x86 perf-arm64 perf-summary-x86 \
        perf-summary-arm64 perf-report fleet-perf \
        musl-sweep test-musl-warnings test-tinycc-warnings \
        check-warn-matrix check-format-matrix fuzz-smoke \
        check-ub-division test-a64-asm-diff test-a64-mir test-a64-debug \
        test-a64-corpus test-audit-fixtures test-audit-sample \
        test-closeout-gate check-closeouts test-posix-sh \
        audit-opt-generated \
        audit-opt-alias \
        test-a64-spill-all test-a64-char-sign test-abi-diff \
        fuzz-frontend-smoke fuzz pp-bench clean tools bootstrap \
        bootstrap-O0 bootstrap-O2 bootstrap-repro-O2 \
        bootstrap-cross-emit \
        bootstrap-cross-compare test-bootstrap install \
        asan ubsan

# libcgf_rt.a: the runtime the Sprint 27 link line reserves a slot for.
# Built by the HOST cc (RT_CC) until Sprint 58 flips it to cgf — the
# flip is part of the self-host DoD. Its own flags are separate from
# CFLAGS: the runtime is not the compiler and must stay buildable with
# a plain toolchain. `ar rcsD` for a DETERMINISTIC archive (no
# timestamps, uids or modes) — two clean builds must be byte-equal.
RT_CC ?= $(CC)
RT_CFLAGS ?= -std=c11 -Wall -Wextra -O2 -fno-strict-aliasing -Isrc
# RT_TARGET is evaluated when this file is PARSED, which on a fresh tree is
# before $(BUILD)/cgfried exists -- so the fallback is what a clean checkout
# actually uses, and it must describe the HOST. A hardcoded x86_64 fallback
# filed the arm64 runtime under x86_64-linux-gnu/ on the native arm64 runner,
# where the driver never looks; the corpus lane's guard caught it, but only
# after CI went red. The names are cgf_target_names[] in src/target.c, and
# for a NATIVE build the host IS the target (there is no --target until
# Sprint 51). A cross build must still name it: `make RT_TARGET=arm64-linux`.
#
# Make conditionals rather than a shell `case`: an unescaped `)` inside
# $(shell ...) closes the function call, so `Linux/arm64)` truncated the
# whole expansion and RT_TARGET came out EMPTY.
RT_HOST_SYS := $(shell uname -s)
RT_HOST_MACHINE := $(shell uname -m)
RT_HOST_TARGET := x86_64-linux-gnu
ifeq ($(RT_HOST_SYS),Linux)
  ifneq (,$(filter aarch64 arm64,$(RT_HOST_MACHINE)))
    RT_HOST_TARGET := arm64-linux
  endif
endif
ifeq ($(RT_HOST_SYS),Darwin)
  ifneq (,$(filter aarch64 arm64,$(RT_HOST_MACHINE)))
    RT_HOST_TARGET := arm64-macos
  endif
endif
ifeq ($(RT_HOST_SYS),FreeBSD)
  RT_HOST_TARGET := x86_64-freebsd
endif
RT_TARGET := $(shell $(BUILD)/cgfried -dumpmachine 2>/dev/null || \
                     echo $(RT_HOST_TARGET))
RT_SRC := $(sort $(wildcard src/rt/*.c))
# Sprint 49: the arm64 fp128 entry points ARE softfp. Sprint 15 kept that
# core library-clean (no arena, no diagnostics, no sema) exactly so it could
# link in here; this is the payoff, and it is why the runtime never grows a
# second float implementation to drift against the compiler's.
RT_SHARED_SRC := src/util/softfp.c src/util/bigint.c
RT_OBJ := $(patsubst src/rt/%.c,$(BUILD)/rt/%.o,$(RT_SRC)) \
          $(patsubst src/util/%.c,$(BUILD)/rt/shared_%.o,$(RT_SHARED_SRC))
RT_LIB := $(BUILD)/$(RT_TARGET)/libcgf_rt.a

all: $(BUILD)/cgfried $(COMPILER_PROVENANCE_RECEIPT) $(BUILD)/cgf $(BUILD)/timeit rt

.PHONY: rt
rt: $(RT_LIB)

$(BUILD)/rt/%.o: src/rt/%.c | $(BUILD)/rt/
	$(RT_CC) $(RT_CFLAGS) -c -o $@ $<

$(BUILD)/rt/shared_%.o: src/util/%.c | $(BUILD)/rt/
	$(RT_CC) $(RT_CFLAGS) -c -o $@ $<

$(RT_LIB): $(RT_OBJ)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcsD $@ $(RT_OBJ)

$(BUILD)/rt/:
	mkdir -p $@

$(BUILD)/cgfried: $(OBJ) $(COMPILER_PROVENANCE_INPUTS) \
    $(COMPILER_SOURCE_SET_STAMP)
	rm -f $(COMPILER_PROVENANCE_RECEIPT)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

$(COMPILER_PROVENANCE_RECEIPT): $(BUILD)/cgfried $(COMPILER_PROVENANCE_INPUTS)
	scripts/torture-provenance.sh --write-receipt $@ --compiler $(BUILD)/cgfried

FORCE:

$(COMPILER_SOURCE_SET_STAMP): FORCE
	@mkdir -p $(dir $@)
	@current='$(COMPILER_SOURCE_SET_ID)'; \
	if [ ! -f "$@" ] || [ "$$(cat "$@")" != "$$current" ]; then \
		tmp="$@.tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		printf '%s\n' "$$current" >"$$tmp"; \
		mv -f "$$tmp" "$@"; \
	fi

# The short alias from the locked decision.
$(BUILD)/cgf: $(BUILD)/cgfried
	ln -sf cgfried $@

$(BUILD)/cgf-test: $(sort $(RUNNER_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(RUNNER_OBJ))

$(BUILD)/cgf-ppdiff: $(sort $(PPDIFF_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(PPDIFF_OBJ))

$(BUILD)/tests/fuzz/ppfuzz.o: tests/fuzz/ppfuzz.c | $(DIRS)
	$(CC) $(CFLAGS) -Itests/runner -c -o $@ $<

$(BUILD)/tests/fuzz/fuzz_frontend.o: tests/fuzz/fuzz_frontend.c | $(DIRS)
	$(CC) $(CFLAGS) -Itests/runner -c -o $@ $<

$(BUILD)/tests/fuzz/ir_fuzz.o: tests/fuzz/ir_fuzz.c | $(DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/ir_fuzz: $(sort $(IRFUZZ_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(IRFUZZ_OBJ))

$(BUILD)/fuzz_frontend: $(sort $(FEFUZZ_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(FEFUZZ_OBJ))

$(BUILD)/gen_layout: $(sort $(GENLAYOUT_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(GENLAYOUT_OBJ))

$(BUILD)/optgen: $(OPTGEN_OBJ)
	$(CC) $(CFLAGS) -o $@ $(OPTGEN_OBJ)

$(BUILD)/alias_oracle: $(sort $(ALIAS_ORACLE_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(ALIAS_ORACLE_OBJ))

$(BUILD)/cgf-objdiff: $(sort $(OBJDIFF_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(OBJDIFF_OBJ))

$(BUILD)/abigen: $(sort $(ABIGEN_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(ABIGEN_OBJ))

$(BUILD)/a64_objbytes: $(A64_OBJBYTES_OBJ)
	$(CC) $(CFLAGS) -o $@ $(A64_OBJBYTES_OBJ)

$(BUILD)/a64mir: $(sort $(A64MIR_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(A64MIR_OBJ))

$(BUILD)/a64_logimm_gen: $(sort $(A64_LOGIMM_GEN_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(A64_LOGIMM_GEN_OBJ))

$(BUILD)/fpdiff: $(sort $(FPDIFF_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(FPDIFF_OBJ))

$(BUILD)/ppfuzz: $(sort $(FUZZ_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(FUZZ_OBJ))

$(BUILD)/unit_tests: $(sort $(UNIT_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(UNIT_OBJ))

$(BUILD)/timeit: $(TIMEIT_OBJ)
	$(CC) $(CFLAGS) -o $@ $(TIMEIT_OBJ)

$(BUILD)/timeit-math-test: $(TIMEIT_MATH_TEST_OBJ) $(TIMEIT_LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $(TIMEIT_MATH_TEST_OBJ) $(TIMEIT_LIB_OBJ)

$(TIMEIT_LIB_OBJ): tests/bench/timeit.c tests/bench/timeit.h | $(DIRS)
	$(CC) $(CFLAGS) -DCGF_TIMEIT_NO_MAIN -c -o $@ $<

$(BUILD)/gen/unit_registry.c: scripts/gen_unit_registry.sh $(UNIT_TEST_SRC) \
                              | $(BUILD)/gen/
	sh scripts/gen_unit_registry.sh $@ $(UNIT_TEST_SRC)

$(BUILD)/gen/unit_registry.o: $(BUILD)/gen/unit_registry.c
	$(CC) $(CFLAGS) -Itests/unit -c -o $@ $<

# Order-only prerequisites: a normal dir prereq would rebuild on every dir
# mtime change.
$(BUILD)/%.o: %.c | $(DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(DIRS):
	mkdir -p $@

# Stage composition: every stage must pass; logs are captured so check_skips
# can assert the exact HARNESS_SKIP set. The toolchain profile is picked by
# whether afs-as is built: with it, zero skips are tolerated; without it, the
# skip must appear exactly (never silent either way).
#
# F-S25-RUSTFREE: compiler tests stay Rust-free (the Sprint 2 law). When
# bundled afs-as is unbuilt, the lanes that assemble/link route to the
# system assembler via CGF_AS=0 — loudly, never silently. Unskipping the
# exec fixtures broke the CI test/test-san jobs (no Rust there) with
# tool-not-found exit 3s until this fallback landed. The afs-as lane is still proven by the
# objdiff toolchain profile and the CI toolchain job wherever the tool
# exists. Recipe-expanded (=, not :=) so a same-invocation `make tools test`
# sees the fresh binary.
AS_LANE = $(shell test -x afs-as/target/release/afs-as || echo CGF_AS=0)
test: all $(BUILD)/unit_tests $(BUILD)/cgf-test
	@if [ ! -x afs-as/target/release/afs-as ]; then \
	    echo "test: afs-as unbuilt; exec lanes use system gas (CGF_AS=0)"; fi
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-bootstrap
	$(BUILD)/unit_tests
	sh scripts/check_unit_registry.sh $(BUILD)/gen/unit_registry.c
	$(MAKE) BUILD=$(BUILD) test-audit-fixtures
	$(MAKE) BUILD=$(BUILD) test-audit-sample
	$(MAKE) test-closeout-gate
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-bench
	$(AS_LANE) CGF_TEST_CC=$(BUILD)/cgfried \
	    $(BUILD)/cgf-test --profile linux-x86_64 tests/programs \
	    > $(BUILD)/programs.log 2>&1; s=$$?; \
	    cat $(BUILD)/programs.log; exit $$s
	sh ci/check_skips.sh linux-x86_64 $(BUILD)/programs.log
	sh scripts/spill_all_lane.sh $(BUILD)/cgfried
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' $(BUILD)/cgf-objdiff
	CGF_OBJDIFF_WORK=$(BUILD)/objdiff-work sh scripts/objdiff_lane.sh \
	    $(BUILD)/cgfried $(BUILD)/cgf-objdiff > $(BUILD)/objdiff.log 2>&1; \
	    s=$$?; cat $(BUILD)/objdiff.log; exit $$s
	@if [ -x afs-as/target/release/afs-as ]; then p=objdiff; \
	    else p=objdiff-gasonly; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/objdiff.log
	sh scripts/check_as_fault.sh $(BUILD)/cgfried
	$(AS_LANE) CGF_TEST_CC=$(BUILD)/cgfried \
	    $(BUILD)/cgf-test --profile linux-x86_64 tests/corpus \
	    > $(BUILD)/corpus.log 2>&1; s=$$?; \
	    cat $(BUILD)/corpus.log; exit $$s
	sh scripts/opt_driver.sh $(BUILD)/cgfried
	$(AS_LANE) sh scripts/s33_ipo_driver.sh $(BUILD)/cgfried
	$(AS_LANE) sh scripts/s34_loop_driver.sh $(BUILD)/cgfried
	$(AS_LANE) sh scripts/s35_loop_driver.sh $(BUILD)/cgfried \
	    $(BUILD)/cgf-test
	$(AS_LANE) sh scripts/s36_vector_driver.sh $(BUILD)/cgfried \
	    $(BUILD)/cgf-test
	sh scripts/s36_isa_driver.sh $(BUILD)/cgfried ci/check_isa.sh
	$(MAKE) BUILD=$(BUILD) check-ub-division
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-a64-mir
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-a64-debug
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-a64-asm-diff
	$(AS_LANE) sh scripts/strict_alias_diff.sh $(BUILD)/cgfried
	$(AS_LANE) CGF_SPILL_ALL=1 CGF_TEST_CC=$(BUILD)/cgfried \
	    $(BUILD)/cgf-test --profile linux-x86_64 tests/corpus \
	    > $(BUILD)/corpus-spill.log 2>&1; s=$$?; \
	    tail -1 $(BUILD)/corpus-spill.log; exit $$s
	$(AS_LANE) sh scripts/e2e_gcc_diff.sh $(BUILD)/cgfried
	$(AS_LANE) CGF_DRIVER_MATRIX_WORK=$(BUILD)/driver-matrix \
	    sh scripts/driver_matrix.sh $(BUILD)/cgfried
	$(AS_LANE) CGF_HEADER_WORK=$(BUILD)/header-diff \
	    sh scripts/header_diff.sh $(BUILD)/cgfried \
	    > $(BUILD)/header.log 2>&1; s=$$?; \
	    cat $(BUILD)/header.log; exit $$s
	sh ci/check_skips.sh headerdiff $(BUILD)/header.log
	CGF_HEADER_PORTABILITY_WORK=$(BUILD)/header-portability BUILD=$(BUILD) \
	    CC='$(CC)' sh scripts/header_portability.sh
	CGF_RT_WORK=$(BUILD)/rt-diff BUILD=$(BUILD) \
	    sh scripts/rt_diff.sh $(BUILD)/cgfried \
	    > $(BUILD)/rt.log 2>&1; s=$$?; \
	    cat $(BUILD)/rt.log; exit $$s
	sh ci/check_skips.sh rtdiff $(BUILD)/rt.log
	$(AS_LANE) CGF_AFSLD_WORK=$(BUILD)/afsld-lane \
	    sh scripts/afsld_lane.sh $(BUILD)/cgfried \
	    > $(BUILD)/afsld.log 2>&1; s=$$?; \
	    cat $(BUILD)/afsld.log; exit $$s
	@if [ -x afs-ld/target/release/afs-ld ]; then p=afsld; \
	    else p=afsld-notools; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/afsld.log
	CGF_DEBUG_WORK=$(BUILD)/debug-info-lane \
	    sh scripts/debug_info_lane.sh $(BUILD)/cgfried \
	    > $(BUILD)/debug-info.log 2>&1; s=$$?; \
	    cat $(BUILD)/debug-info.log; exit $$s
	@p=debug; \
	    if grep -q 'suite=debug-info test=afs-' $(BUILD)/debug-info.log; then \
	        p=debug-notools; fi; \
	    if grep -q 'suite=debug-info test=gdb ' $(BUILD)/debug-info.log; then \
	        p=$$p-nogdb; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/debug-info.log
	sh tests/runner/meta/run_meta.sh $(BUILD)/cgf-test
	CGF_TEST_CC=$(BUILD)/cgfried \
	    $(BUILD)/cgf-test --profile linux-x86_64 tests/warn
	$(MAKE) BUILD=$(BUILD) test-flow-warnings
	$(MAKE) BUILD=$(BUILD) test-memsafe-foundation
	$(MAKE) BUILD=$(BUILD) test-mem-warnings
	$(MAKE) BUILD=$(BUILD) test-mem-interproc
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-mem-runtime
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-mem-autofix
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-safe-mode
	$(MAKE) BUILD=$(BUILD) check-format-matrix
	$(MAKE) BUILD=$(BUILD) test-musl-warnings
	$(MAKE) BUILD=$(BUILD) test-tinycc-warnings
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-ppdiff
	sh scripts/pp_dm_check.sh $(BUILD)/cgfried
	sh scripts/lex_diff.sh $(BUILD)/cgfried
	sh scripts/parse_diff.sh $(BUILD)/cgfried
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' $(BUILD)/gen_layout
	CGF_LAYOUT_GEN=$(BUILD)/gen_layout CGF_LAYOUT_WORK=$(BUILD)/layout-diff \
	    sh scripts/layout_diff.sh $(BUILD)/cgfried > $(BUILD)/layout.log 2>&1; \
	    s=$$?; cat $(BUILD)/layout.log; exit $$s
	sh ci/check_skips.sh layout $(BUILD)/layout.log
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' $(BUILD)/fpdiff
	CGF_FP_WORK=$(BUILD)/fp-diff sh scripts/fp_diff.sh $(BUILD)/fpdiff \
	    > $(BUILD)/fp.log 2>&1; s=$$?; cat $(BUILD)/fp.log; exit $$s
	sh ci/check_skips.sh fpdiff $(BUILD)/fp.log
	CGF_INIT_WORK=$(BUILD)/init-diff sh scripts/init_diff.sh $(BUILD)/cgfried \
	    > $(BUILD)/init.log 2>&1; s=$$?; cat $(BUILD)/init.log; exit $$s
	sh ci/check_skips.sh initdiff $(BUILD)/init.log
	CGF_INLINE_WORK=$(BUILD)/inline-diff sh scripts/inline_diff.sh \
	    $(BUILD)/cgfried > $(BUILD)/inline.log 2>&1; s=$$?; \
	    cat $(BUILD)/inline.log; exit $$s
	sh ci/check_skips.sh inlinediff $(BUILD)/inline.log
	sh tests/scripts/ctestsuite_diff_test.sh
	sh scripts/ctestsuite_diff.sh $(BUILD)/cgfried > $(BUILD)/ctestsuite.log 2>&1; s=$$?; \
	    cat $(BUILD)/ctestsuite.log; exit $$s
	@if [ -d .docs/refs/c-testsuite/tests/single-exec ]; then p=ctestsuite; \
	    else p=ctestsuite-norefs; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/ctestsuite.log
	sh scripts/tinycc_pp_smoke.sh $(BUILD)/cgfried
	sh scripts/toolchain_smoke.sh > $(BUILD)/toolchain.log 2>&1; s=$$?; \
	    cat $(BUILD)/toolchain.log; exit $$s
	@if [ -x afs-as/target/release/afs-as ]; then p=toolchain; \
	    else p=toolchain-notools; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/toolchain.log
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' fuzz-smoke
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' fuzz-frontend-smoke
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' fuzz-ir-smoke
	sh scripts/check_fuzz_crashes.sh
	$(MAKE) test-posix-sh
	# TI-M-01: keep the POSIX-shell producer connected to its exact skip
	# profile so losing dash cannot silently disable this harness gate.
	sh scripts/check_posix_sh.sh > $(BUILD)/posixsh.log 2>&1; s=$$?; \
	    cat $(BUILD)/posixsh.log; exit $$s
	@if grep -q '^HARNESS_SKIP ' $(BUILD)/posixsh.log; then \
	    sh ci/check_skips.sh posixsh-nodash $(BUILD)/posixsh.log; \
	else sh ci/check_skips.sh posixsh $(BUILD)/posixsh.log; fi
	$(MAKE) campaign-expected-meta
	sh scripts/check_bans.sh
	$(MAKE) torture-import-verify
	sh scripts/check_warn_seams.sh
	sh scripts/check_pp_seams.sh
	sh scripts/check_sema_target.sh
	sh scripts/check_target_seam.sh
	sh scripts/check_deferrals.sh
	sh scripts/check_gnu_tiers.sh
	sh scripts/check_verify_coverage.sh
	sh scripts/check_no_host_fpu.sh
	CGF_CROSS_WORK=$(BUILD)/cross-determinism \
	    sh tests/cross/determinism.sh $(BUILD)/cgfried >/dev/null
	CGF_MUSL_CROSS_WORK=$(BUILD)/musl-cross \
	    sh scripts/musl_cross_lane.sh $(BUILD)/cgfried
	CGF_FREEBSD_WORK=$(BUILD)/freebsd-cross \
	    sh scripts/freebsd_cross_lane.sh $(BUILD)/cgfried
	sh scripts/a64_va_list_diff.sh
	$(MAKE) BUILD=$(BUILD) $(BUILD)/a64mir
	sh scripts/a64_exec_lane.sh $(BUILD)/a64mir
	CGF_A64_OBJDIFF_WORK=$(BUILD)/a64-objdiff \
	    sh scripts/a64_objdiff_lane.sh $(BUILD)/a64mir
	sh scripts/char_sign_oracle.sh
	sh scripts/fp128_diff.sh
	sh scripts/x86_atomics_lane.sh $(BUILD)/cgfried
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' $(BUILD)/abigen
	CGF_ABIGEN=$(BUILD)/abigen CGF_ABI_DIFF_WORK=$(BUILD)/abi-differential \
	    CGF_ABI_DIFF_COUNT=24 \
	    sh scripts/abi_differential_lane.sh $(BUILD)/cgfried
	$(MAKE) check-warn-matrix
	$(MAKE) BUILD=$(BUILD) test-warndiff
	sh scripts/check_format.sh

check-warn-matrix:
	sh scripts/check_warn_matrix.sh

check-ub-division:
	CGF_UB_DIV_WORK=$(BUILD)/ub-division-lint \
	    sh scripts/check_ub_division.sh --self-test

# Sprint 51 DoD 6: generated signatures, compiled half by us and half by the
# reference compiler, linked together and run. In `make test` at a small seed
# count because a disagreement here is an ABI bug and those are silent; the
# soak runs with CGF_ABI_DIFF_COUNT cranked up.
test-abi-diff: all $(BUILD)/abigen
	CGF_ABIGEN=$(BUILD)/abigen CGF_ABI_DIFF_WORK=$(BUILD)/abi-differential \
	    sh scripts/abi_differential_lane.sh $(BUILD)/cgfried
	CGF_ABIGEN=$(BUILD)/abigen \
	    CGF_ABI_DIFF_WORK=$(BUILD)/abi-differential-a64 \
	    CGF_ABI_DIFF_TARGET=arm64-linux \
	    sh scripts/abi_differential_lane.sh $(BUILD)/cgfried

# Not in `make test`: it cross-builds the whole compiler and runs two levels
# of emulation. CI runs it; locally it is one command away.
test-a64-corpus: $(BUILD)/cgf-test
	CGF_A64_CORPUS_WORK=$(BUILD)/a64-corpus \
	    sh scripts/a64_corpus_lane.sh "" $(BUILD)/cgf-test

# The same lane with every interval forced to memory. Separate ledger,
# because the spill rewrite is the one place an instruction's index moves
# and natural pressure barely reaches it -- `9c98698` was a stale NZCV
# producer index that exactly one fixture at one level caught.
test-a64-spill-all: $(BUILD)/cgf-test
	CGF_A64_SPILL_ALL=1 \
	    CGF_A64_CORPUS_WORK=$(BUILD)/a64-corpus-spill-all \
	    CGF_A64_LEDGER=ci/expected_a64_spill_all_failures.txt \
	    sh scripts/a64_corpus_lane.sh "" $(BUILD)/cgf-test

# Sprint 49 DoD 4: the char-sign fixtures carry one expectation PER ARCH, and
# only this runs the arm64 half through OUR compiler -- char_sign_oracle.sh
# proves the EXPECTATIONS with gcc, which is a different claim.
test-a64-char-sign: $(BUILD)/cgf-test
	CGF_A64_CORPUS_DIR=tests/corpus/char_sign \
	    CGF_A64_CORPUS_WORK=$(BUILD)/a64-char-sign \
	    CGF_A64_LEDGER=/dev/null \
	    sh scripts/a64_corpus_lane.sh "" $(BUILD)/cgf-test

test-a64-mir: $(BUILD)/a64mir
	CGF_A64_MIR_WORK=$(BUILD)/a64-mir-lane \
	    sh scripts/a64_mir_lane.sh $(BUILD)/a64mir

# Cross tools only -- it reads objects and never runs one -- so unlike the
# corpus lane this belongs in `make test` on an x86 host.
test-a64-debug: $(BUILD)/cgfried
	CGF_A64_DEBUG_WORK=$(BUILD)/a64-debug-lane \
	    sh scripts/a64_debug_lane.sh $(BUILD)/cgfried

# Sprint 60 findings start OPEN/XFAIL. Sprint 61 repairs move atomically to
# PASS; XPASS and resolved-fixture regressions remain red.
test-audit-fixtures: $(BUILD)/cgfried
	sh tests/scripts/audit_fixture_lifecycle_test.sh
	sh tests/scripts/burndown_test.sh
	sh scripts/check-burndown.sh
	sh scripts/check-audit-fixtures.sh $(BUILD)/cgfried

# Sprint 61's fresh-context sample is reproducible from its recorded seed.
test-audit-sample:
	sh tests/scripts/audit_sample_test.sh

# The validator's meta-test belongs in ordinary CI even while an honest
# NOT READY closeout keeps the Sprint 62 entry gate red.
test-closeout-gate:
	sh tests/scripts/closeout_gate_test.sh

test-posix-sh:
	sh tests/scripts/posix_sh_test.sh

# Sprint 61's gate-of-gates: run this before Sprint 62 starts.
check-closeouts: test-closeout-gate
	sh ci/check-closeouts.sh

# Sprint 60 F05 evidence targets.  They are intentionally opt-in during the
# audit: a generated failure needs inspection and a durable finding, rather
# than making ordinary contributor test runs non-diagnostic or expensive.
audit-opt-generated: $(BUILD)/cgfried $(BUILD)/cgf-test $(BUILD)/optgen
	CGF_OPT_AUDIT_RUNNER=$(BUILD)/cgf-test \
	    sh scripts/opt_generated_diff.sh $(BUILD)/cgfried $(BUILD)/optgen

audit-opt-alias: $(BUILD)/alias_oracle
	$(BUILD)/alias_oracle

test-a64-asm-diff: $(BUILD)/a64_objbytes $(BUILD)/a64_logimm_gen
	CGF_A64_OBJBYTES=$(BUILD)/a64_objbytes \
	    CGF_A64_LOGIMM_GEN=$(BUILD)/a64_logimm_gen \
	    CGF_A64_DIFF_WORK=$(BUILD)/a64-asm-diff \
	    sh scripts/a64_asm_diff.sh > $(BUILD)/a64-asm-diff.log 2>&1; \
	    s=$$?; cat $(BUILD)/a64-asm-diff.log; exit $$s
	@p=a64asmdiff; \
	    if grep -q 'test=afs-as ' $(BUILD)/a64-asm-diff.log; then \
	        p=a64asmdiff-notools; \
	    elif grep -q 'test=gnu-as ' $(BUILD)/a64-asm-diff.log; then \
	        p=a64asmdiff-clang; \
	    elif grep -q 'test=elf-assembler ' $(BUILD)/a64-asm-diff.log; then \
	        p=a64asmdiff-noelf; \
	    fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/a64-asm-diff.log

# Preprocessor differential: token-level vs gcc AND clang over fixtures
# and imported corpora, at both std flavors.
PPDIFF_FILES := $(sort $(wildcard tests/ppdiff/*.c) $(wildcard tests/fixtures/imported/tinycc-pp/*.c) $(wildcard tests/fixtures/imported/chibicc/*.c))

test-ppdiff: $(BUILD)/cgfried $(BUILD)/cgf-ppdiff
	$(BUILD)/cgf-ppdiff --std -std=c17 -I tests/fixtures/imported/chibicc --xfail tests/fixtures/imported/ppdiff-xfail.txt $(BUILD)/cgfried $(PPDIFF_FILES) > $(BUILD)/ppdiff.log 2>&1; s=$$?; cat $(BUILD)/ppdiff.log; [ $$s -eq 0 ]
	$(BUILD)/cgf-ppdiff --std -std=gnu17 -I tests/fixtures/imported/chibicc --xfail tests/fixtures/imported/ppdiff-xfail.txt $(BUILD)/cgfried $(PPDIFF_FILES) >> $(BUILD)/ppdiff.log 2>&1; s=$$?; tail -1 $(BUILD)/ppdiff.log; [ $$s -eq 0 ]
	sh ci/check_skips.sh ppdiff $(BUILD)/ppdiff.log

test-warndiff: $(BUILD)/cgfried
	CGF_WARN_DIFF_WORK=$(BUILD)/warn-diff sh scripts/warn_diff.sh \
	    $(BUILD)/cgfried > $(BUILD)/warn-diff.log 2>&1; s=$$?; \
	    cat $(BUILD)/warn-diff.log; exit $$s
	@if grep -q '^HARNESS_SKIP ' $(BUILD)/warn-diff.log; then p=warndiff-nogcc8; \
	    else p=warndiff; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/warn-diff.log

test-flow-warnings: $(BUILD)/cgfried
	CGF_FLOW_LEVEL_WORK=$(BUILD)/flow-levels \
	    sh scripts/warn_flow_levels.sh $(BUILD)/cgfried

test-memsafe-foundation: $(BUILD)/cgfried
	CGF_MEMSAFE_WORK=$(BUILD)/memsafe-foundation \
	    sh scripts/memsafe_foundation.sh $(BUILD)/cgfried \
	    tests/memsafe/foundation

test-mem-warnings: $(BUILD)/cgfried $(BUILD)/cgf-test
	CGF_TEST_CC=$(BUILD)/cgfried $(BUILD)/cgf-test \
	    --profile linux-x86_64 tests/memsafe/wmem
	CGF_MEM_WARN_WORK=$(BUILD)/mem-warnings \
	    sh scripts/memsafe_warn.sh $(BUILD)/cgfried tests/memsafe/wmem
	sh tests/memsafe/musl_status_meta.sh

test-mem-interproc: $(BUILD)/cgfried $(BUILD)/cgf-test
	CGF_TEST_CC=$(BUILD)/cgfried $(BUILD)/cgf-test \
	    --profile linux-x86_64 tests/memsafe/interproc
	CGF_MEM_WARN_WORK=$(BUILD)/mem-interproc-warnings \
	    sh scripts/memsafe_warn.sh $(BUILD)/cgfried \
	    tests/memsafe/interproc

test-mem-runtime: $(BUILD)/cgfried rt
	CGF_SAFE_WORK=$(BUILD)/safe-runtime \
	    sh scripts/safe_runtime.sh $(BUILD)/cgfried $(BUILD)

test-mem-autofix: $(BUILD)/cgfried
	CGF_AUTOFIX_WORK=$(BUILD)/autofix-transforms \
	    sh scripts/autofix_transforms.sh $(BUILD)/cgfried $(BUILD)

test-safe-mode: $(BUILD)/cgfried $(BUILD)/cgf-test rt
	$(AS_LANE) CGF_TEST_CC=$(BUILD)/cgfried $(BUILD)/cgf-test \
	    --profile linux-x86_64 tests/memsafe/safe-mode
	$(AS_LANE) CGF_SAFE_MODE_WORK=$(BUILD)/safe-mode \
	    sh scripts/safe_mode.sh $(BUILD)/cgfried $(BUILD)

safe-dogfood: $(BUILD)/cgfried rt
	CC='$(CC)' CGF_SAFE_DOGFOOD_WORK=$(BUILD)/safe-dogfood \
	    sh scripts/safe_dogfood.sh $(BUILD)/cgfried $(BUILD)

bench-safe: $(BUILD)/cgfried rt
	CC='$(CC)' CGF_SAFE_WORK=$(BUILD)/safe-bench \
	    sh scripts/safe_runtime.sh $(BUILD)/cgfried $(BUILD) bench

# Fast, deterministic infrastructure checks belong in `make test`; the actual
# benchmark protocol stays explicit so normal tests never depend on host load.
test-bench: $(BUILD)/cgfried $(BUILD)/timeit $(BUILD)/timeit-math-test
	sh tests/bench/timeit_test.sh $(BUILD)/timeit
	$(BUILD)/timeit-math-test
	sh tests/bench/benchmark_gate_test.sh
	sh tests/bench/kernel_static_test.sh
	sh tests/bench/kernel_compare_test.sh
	CGF_STATS_WORK=$(BUILD)/stats-smoke \
	    sh scripts/stats_smoke.sh $(BUILD)/cgfried
	CGF_BENCH_TEST_WORK=$(BUILD)/bench-test sh tests/bench/corpus_test.sh
	CGF_SCOPE_TEST_WORK=$(BUILD)/scope-index-test \
	    sh tests/bench/scope_index_test.sh $(BUILD)/cgfried
	$(MAKE) BUILD=$(BUILD) test-perf-gates

test-perf-gates:
	sh tests/scripts/gates/bench_control_test.sh
	sh tests/scripts/gates/size_gate_test.sh
	sh tests/scripts/gates/runtime_gate_test.sh
	sh tests/scripts/gates/musl_full_build_test.sh
	sh tests/scripts/gates/reporting_test.sh
	sh tests/scripts/gates/bench_policy_test.sh
	sh tests/scripts/gates/perf_gate_test.sh
	sh tests/scripts/gates/fleet_perf_test.sh
	sh tests/scripts/gates/fleet_nightly_test.sh
	sh tests/scripts/gates/perf_config_test.sh
	sh scripts/check_perf_configs.sh

bench: $(BUILD)/cgfried $(BUILD)/timeit
	BUILD=$(abspath $(BUILD)) \
	CGF_BENCH_RUNS=$(BENCH_RUNS) CGF_BENCH_WARMUP=$(BENCH_WARMUP) \
	BENCH_SKIP_TIME=$(BENCH_SKIP_TIME) \
	CGF_BENCH_HOST_CLASS="$${CGF_BENCH_HOST_CLASS:-$(if $(filter 1,$(BENCH_SKIP_TIME)),$(BENCH_HOST_CLASS),)}" \
	CGF_BENCH_RESULTS=$(abspath $(BENCH_RESULTS)) \
	CGF_SQLITE_VERSION=$(SQLITE_AMALGAMATION_VERSION) \
	CGF_SQLITE_SHA3=$(SQLITE_AMALGAMATION_SHA3) \
	CGF_SQLITE_CKSUM=$(SQLITE_AMALGAMATION_CKSUM) sh scripts/bench.sh

bench-gate: bench
	BENCH_GATE_KIND=rss sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/max-rss.conf -- scripts/benchmark_gate.sh \
	    $(BENCH_BASELINE) $(BENCH_RESULTS)
	@if [ "$(BENCH_SKIP_TIME)" != 1 ]; then \
	    BENCH_GATE_KIND=time sh scripts/perf_gate.sh \
	        $(PERF_CONFIG_DIR)/compile-time.conf -- \
	        scripts/benchmark_gate.sh $(BENCH_BASELINE) $(BENCH_RESULTS); \
	else \
	    echo 'bench-gate: shared-CI timing comparison disabled'; \
	fi

perf-size-x86: $(BUILD)/cgfried
	@mkdir -p $(BUILD)/bench
	CGF_SIZE_WORK=$(BUILD)/size-gate/x86_64-linux-gnu \
	    sh scripts/size_gate.sh --measure $(BUILD)/cgfried \
	    x86_64-linux-gnu $(SIZE_X86_RESULT)
	CGF_SIZE_GATE_KIND=program sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/corpus-binary-size.conf -- \
	    scripts/size_gate.sh --gate $(SIZE_X86_BASELINE) $(SIZE_X86_RESULT)
	CGF_SIZE_GATE_KIND=self sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/cgf-self-size.conf -- \
	    scripts/size_gate.sh --gate $(SIZE_X86_BASELINE) $(SIZE_X86_RESULT)

perf-size-arm64: $(BUILD)/cgfried
	@mkdir -p $(BUILD)/bench
	CGF_SIZE_WORK=$(BUILD)/size-gate/arm64-linux \
	    sh scripts/size_gate.sh --measure $(BUILD)/cgfried \
	    arm64-linux $(SIZE_A64_RESULT)
	CGF_SIZE_GATE_KIND=program sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/corpus-binary-size.conf -- \
	    scripts/size_gate.sh --gate $(SIZE_A64_BASELINE) $(SIZE_A64_RESULT)
	CGF_SIZE_GATE_KIND=self sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/cgf-self-size.conf -- \
	    scripts/size_gate.sh --gate $(SIZE_A64_BASELINE) $(SIZE_A64_RESULT)

perf-static-x86: $(BUILD)/cgfried
	@mkdir -p $(BUILD)/bench
	CGF_KERNEL_MEASURE_ONLY=1 \
	    CGF_KERNEL_WORK=$(BUILD)/kernel-static/x86_64-linux-gnu \
	    sh scripts/kernel-static.sh $(BUILD)/cgfried x86_64-linux-gnu \
	    $(KERNEL_X86_RESULT) $(KERNEL_X86_GOLDEN)
	CGF_KERNEL_GATE_KIND=icount sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/kernel-icount.conf -- \
	    scripts/kernel-static.sh --gate $(KERNEL_X86_GOLDEN) \
	    $(KERNEL_X86_RESULT)
	CGF_KERNEL_GATE_KIND=text sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/kernel-text.conf -- \
	    scripts/kernel-static.sh --gate $(KERNEL_X86_GOLDEN) \
	    $(KERNEL_X86_RESULT)

perf-static-arm64: $(BUILD)/cgfried
	@mkdir -p $(BUILD)/bench
	CGF_KERNEL_MEASURE_ONLY=1 \
	    CGF_KERNEL_WORK=$(BUILD)/kernel-static/arm64-linux \
	    sh scripts/kernel-static.sh $(BUILD)/cgfried arm64-linux \
	    $(KERNEL_A64_RESULT) $(KERNEL_A64_GOLDEN)
	CGF_KERNEL_GATE_KIND=icount sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/kernel-icount.conf -- \
	    scripts/kernel-static.sh --gate $(KERNEL_A64_GOLDEN) \
	    $(KERNEL_A64_RESULT)
	CGF_KERNEL_GATE_KIND=text sh scripts/perf_gate.sh \
	    $(PERF_CONFIG_DIR)/kernel-text.conf -- \
	    scripts/kernel-static.sh --gate $(KERNEL_A64_GOLDEN) \
	    $(KERNEL_A64_RESULT)

perf-x86: perf-size-x86 perf-static-x86

perf-arm64: perf-size-arm64 perf-static-arm64

perf-summary-x86:
	@status=0; scripts/bench-summary.sh --target x86_64-linux-gnu \
	    --class ci --bench $(BENCH_BASELINE) $(BENCH_RESULTS) \
	    --size $(SIZE_X86_BASELINE) $(SIZE_X86_RESULT) \
	    --static $(KERNEL_X86_GOLDEN) $(KERNEL_X86_RESULT) || status=$$?; \
	    [ $$status -eq 0 ] || [ $$status -eq 1 ]

perf-summary-arm64:
	@status=0; scripts/bench-summary.sh --target arm64-linux --class arm64-ci \
	    --bench $(BENCH_BASELINE) $(BENCH_RESULTS) \
	    --size $(SIZE_A64_BASELINE) $(SIZE_A64_RESULT) \
	    --static $(KERNEL_A64_GOLDEN) $(KERNEL_A64_RESULT) || status=$$?; \
	    [ $$status -eq 0 ] || [ $$status -eq 1 ]

perf-report:
	@set -eu; \
	set -- scripts/perf-report.sh --version $(PERF_REPORT_VERSION) \
	    --output $(PERF_REPORT); \
	for baseline in $$(find .benchmarks -maxdepth 1 -type f \
	    -name 'baseline-*.txt' -print | sort); do \
	    set -- "$$@" --baseline "$$baseline"; \
	done; \
	if [ -n "$(PERF_REPORT_LATEST)" ]; then \
	    for latest in $(PERF_REPORT_LATEST); do \
	        set -- "$$@" --latest "$$latest"; \
	    done; \
	else \
	    for suffix in kasumi.txt kasumi-kernels.txt hasu.txt \
	        hasu-kernels.txt nomad-1.txt nomad-1-kernels.txt \
	        ci-x86_64-linux-gnu-bench.txt \
	        ci-x86_64-linux-gnu-size.txt \
	        ci-arm64-linux-bench.txt ci-arm64-linux-size.txt; do \
	        latest=$$(find .benchmarks/runs -maxdepth 1 -type f \
	            -name "*-$$suffix" -print 2>/dev/null | sort | tail -n 1); \
	        [ -n "$$latest" ] || { \
	            echo "perf-report: no committed latest artifact for *-$$suffix" >&2; \
	            exit 3; \
	        }; \
	        set -- "$$@" --latest "$$latest"; \
	    done; \
	    for bootstrap_host in kasumi hasu; do \
	        baseline=.benchmarks/baseline-bootstrap-O2-x86_64-linux-gnu.$$bootstrap_host.txt; \
	        if [ -r "$$baseline" ]; then \
	            latest=$$(find .benchmarks/runs -maxdepth 1 -type f \
	                -name "*-$$bootstrap_host-bootstrap.txt" -print 2>/dev/null | \
	                sort | tail -n 1); \
	            [ -n "$$latest" ] || { \
	                echo "perf-report: no committed bootstrap artifact for $$bootstrap_host" >&2; \
	                exit 3; \
	            }; \
	            set -- "$$@" --latest "$$latest"; \
	        fi; \
	    done; \
	    for musl_host in kasumi hasu; do \
	        baseline=.benchmarks/baseline-musl-full-build-x86_64-linux-musl.$$musl_host.txt; \
	        if [ -r "$$baseline" ]; then \
	            latest=$$(find .benchmarks/runs -maxdepth 1 -type f \
	                -name "*-$$musl_host-musl-full-build.txt" -print 2>/dev/null | \
	                sort | tail -n 1); \
	            [ -n "$$latest" ] || { \
	                echo "perf-report: no committed musl artifact for $$musl_host" >&2; \
	                exit 3; \
	            }; \
	            set -- "$$@" --latest "$$latest"; \
	        fi; \
	    done; \
	fi; \
	set -- "$$@" --golden $(KERNEL_X86_GOLDEN) \
	    --golden $(KERNEL_A64_GOLDEN) --dashboard $(KERNEL_DASHBOARD); \
	if [ -n "$(PERF_REPORT_PREVIOUS)" ]; then \
	    set -- "$$@" --previous $(PERF_REPORT_PREVIOUS); \
	fi; \
	"$$@"

fleet-perf: $(BUILD)/cgfried $(BUILD)/timeit
	BUILD=$(abspath $(BUILD)) sh scripts/fleet-perf.sh

# Static kernel counts are deterministic and therefore safe on shared hosts.
# Runtime comparisons remain an explicit fleet-only visibility lane.
kernels: $(BUILD)/cgfried
	@mkdir -p $(BUILD)/bench
	CGF_KERNEL_WORK=$(BUILD)/kernel-static/x86_64-linux-gnu \
	    sh scripts/kernel-static.sh $(BUILD)/cgfried x86_64-linux-gnu \
	    $(KERNEL_X86_RESULT) $(KERNEL_X86_GOLDEN)
	CGF_KERNEL_WORK=$(BUILD)/kernel-static/arm64-linux \
	    sh scripts/kernel-static.sh $(BUILD)/cgfried arm64-linux \
	    $(KERNEL_A64_RESULT) $(KERNEL_A64_GOLDEN)

test-kernels: $(BUILD)/cgfried $(BUILD)/cgf-test
	sh tests/bench/kernel_static_test.sh
	$(MAKE) kernels
	CGF_AS=0 CGF_TEST_CC=$(BUILD)/cgfried \
	    CGF_TEST_WORK=$(BUILD)/kernel-opt-eq $(BUILD)/cgf-test \
	    --profile linux-x86_64 tests/bench/kernels
	CGF_KERNEL_A64_EXEC_WORK=$(BUILD)/kernel-arm64-exec \
	    sh scripts/kernel-arm64-exec.sh $(BUILD)/cgfried $(BUILD)/cgf-test

kernel-compare: $(BUILD)/cgfried
	CGF_KERNEL_COMPARE_WORK=$(BUILD)/kernel-compare \
	    sh scripts/kernel-compare.sh $(KERNEL_DASHBOARD)

# Optional local comparison: records both verdicts without treating GCC's
# analyzer as an oracle for Cgfried's narrower default policy.
test-mem-fanalyzer: $(BUILD)/cgfried
	CGF_FANALYZER_WORK=$(BUILD)/mem-fanalyzer \
	    sh scripts/fanalyzer_mem_compare.sh $(BUILD)/cgfried \
	    tests/memsafe/fanalyzer

musl-sweep: $(BUILD)/cgfried
	timeout 90 env CGF_MUSL_MEM_WORK=$(BUILD)/musl-mem-warn \
	    sh scripts/musl_mem_warn.sh $(BUILD)/cgfried

test-musl-warnings: $(BUILD)/cgfried
	CGF_MUSL_WARN_WORK=$(BUILD)/musl-warn sh scripts/musl_warn_dryrun.sh \
	    $(BUILD)/cgfried > $(BUILD)/musl-warn.log 2>&1; s=$$?; \
	    cat $(BUILD)/musl-warn.log; exit $$s
	@if grep -q '^HARNESS_SKIP ' $(BUILD)/musl-warn.log; then p=muslwarn-norefs; \
	    else p=muslwarn; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/musl-warn.log

test-tinycc-warnings: $(BUILD)/cgfried
	CGF_TINYCC_WARN_WORK=$(BUILD)/tinycc-warn sh scripts/tinycc_warn_dryrun.sh \
	    $(BUILD)/cgfried > $(BUILD)/tinycc-warn.log 2>&1; s=$$?; \
	    cat $(BUILD)/tinycc-warn.log; exit $$s
	@if grep -q '^HARNESS_SKIP ' $(BUILD)/tinycc-warn.log; then p=tinyccwarn-norefs; \
	    else p=tinyccwarn; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/tinycc-warn.log

# Fuzz smoke: both modes, fixed seeds, must be clean. Long runs are
# manual/nightly (--iters). Findings reproduce from their seed alone.
FUZZ_CORPUS := tests/programs/pp tests/fixtures/imported

fuzz-smoke: $(BUILD)/cgfried $(BUILD)/ppfuzz
	$(BUILD)/ppfuzz --iters 2000 $(BUILD)/cgfried $(FUZZ_CORPUS)
	$(BUILD)/ppfuzz -diff --iters 2000 $(BUILD)/cgfried $(FUZZ_CORPUS)

# Frontend fuzz corpus: our own fixtures plus every parse-corpus program.
FE_FUZZ_CORPUS := tests/fixtures tests/programs

# The smoke run is small enough for `make test`; CI runs FUZZ_ITERS=100000
# under sanitizers. The digest gate is the determinism proof: if a mutator
# changes, the sequence a given seed produces changes, and a pinned digest
# is the only way to notice that a "green" fuzz run is testing something
# else than it used to.
# The IR round-trip fuzzer is IN-PROCESS (no spawn per iteration), which
# is what makes the 10^6-iteration local run honest. The smoke slice
# rides `make test`; CI gets the same slice.
fuzz-ir-smoke: $(BUILD)/ir_fuzz
	$(BUILD)/ir_fuzz --iters=5000 tests/programs/ir tests/programs/ir/bad

fuzz-frontend-smoke: $(BUILD)/cgfried $(BUILD)/fuzz_frontend
	$(BUILD)/fuzz_frontend --iters 2000 $(BUILD)/cgfried $(FE_FUZZ_CORPUS)
	@got=$$($(BUILD)/fuzz_frontend --hash --iters 5000 $(BUILD)/cgfried \
	    $(FE_FUZZ_CORPUS) | awk '{print $$4}'); \
	    want=$$(cat ci/fuzz_sequence_digest.txt); \
	    if [ "$$got" != "$$want" ]; then \
	        echo "fuzz digest changed: got $$got want $$want" >&2; \
	        echo "  (a mutator changed; re-pin ci/fuzz_sequence_digest.txt" >&2; \
	        echo "   only when that change is intended)" >&2; \
	        exit 1; \
	    fi; \
	    echo "fuzz_frontend: mutation sequence digest $$got matches"

# Long runs: `make fuzz FUZZ_ITERS=100000 FUZZ_SEED=1`. The seed is always
# printed so a finding reproduces from the log alone.
FUZZ_ITERS ?= 100000
FUZZ_SEED  ?= 1

fuzz: $(BUILD)/cgfried $(BUILD)/fuzz_frontend
	$(BUILD)/fuzz_frontend --iters $(FUZZ_ITERS) --seed $(FUZZ_SEED) \
	    $(BUILD)/cgfried $(FE_FUZZ_CORPUS)
	sh scripts/check_fuzz_crashes.sh

# Sanitizer build flavors — dev and CI tooling, never release flags.
asan:
	$(MAKE) BUILD=build-asan \
	    EXTRA_CFLAGS="-fsanitize=address -fno-omit-frame-pointer -O1 -g" all

ubsan:
	$(MAKE) BUILD=build-ubsan \
	    EXTRA_CFLAGS="-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -O1 -g" all

# Include-guard fast-path benchmark (see the script for the ceiling math).
pp-bench: $(BUILD)/cgfried
	sh tests/perf/pp_include_bench.sh $(BUILD)/cgfried

# The whole suite under ASan+UBSan (host sanitizers are dev tooling, never a
# dependency). Separate build tree so it composes with the normal one.
test-san:
	$(MAKE) BUILD=build-san \
	    EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
	    test

# Rust/cargo is a build-time dependency of the bundled assembler/linker ONLY.
# The compiler is C11 + POSIX; it builds and runs without a Rust toolchain,
# using system as/ld. `tools` is never a prerequisite of `all` or the
# compiler's own tests.
tools:
	@command -v cargo >/dev/null 2>&1 || { \
	    echo "tools: cargo not found - Rust is required to build afs-as/afs-ld (tools only); the compiler itself builds without Rust" >&2; \
	    exit 1; }
	cd afs-as && cargo build --release
	cd afs-ld && cargo build --release

bootstrap: bootstrap-O2

bootstrap-O0: scripts/bootstrap.sh ci/bootstrap.mk scripts/bisect-nondet.sh
	CGF_BOOTSTRAP_WORK='$(BOOTSTRAP_WORK)/O0' \
	    CGF_BOOTSTRAP_JOBS='$(BOOTSTRAP_JOBS)' HOSTCC='$(HOSTCC)' \
	    CGF_BOOTSTRAP_HOST='$(BOOTSTRAP_HOST)' \
	    CGF_BOOTSTRAP_HOST_CLASS='$(BOOTSTRAP_HOST_CLASS)' \
	    CGF_BOOTSTRAP_CONTROL_FILE='$(BOOTSTRAP_CONTROL_FILE)' \
	    CGF_BOOTSTRAP_SYSROOT='$(BOOTSTRAP_SYSROOT)' \
	    sh scripts/bootstrap.sh O0

bootstrap-O2: scripts/bootstrap.sh ci/bootstrap.mk scripts/bisect-nondet.sh
	CGF_BOOTSTRAP_WORK='$(BOOTSTRAP_WORK)/O2' \
	    CGF_BOOTSTRAP_JOBS='$(BOOTSTRAP_JOBS)' HOSTCC='$(HOSTCC)' \
	    CGF_BOOTSTRAP_HOST='$(BOOTSTRAP_HOST)' \
	    CGF_BOOTSTRAP_HOST_CLASS='$(BOOTSTRAP_HOST_CLASS)' \
	    CGF_BOOTSTRAP_CONTROL_FILE='$(BOOTSTRAP_CONTROL_FILE)' \
	    CGF_BOOTSTRAP_SYSROOT='$(BOOTSTRAP_SYSROOT)' \
	    sh scripts/bootstrap.sh O2

bootstrap-repro-O2: scripts/bootstrap-repro.sh
	$(MAKE) BOOTSTRAP_JOBS=8 bootstrap-O2
	HOSTCC='$(HOSTCC)' sh scripts/bootstrap-repro.sh O2 \
	    '$(BOOTSTRAP_WORK)/O2' '$(BOOTSTRAP_REPRO_OUTPUT)'

bootstrap-cross-emit: scripts/bootstrap-cross.sh ci/bootstrap-cross.mk
	CGF_BOOTSTRAP_JOBS='$(BOOTSTRAP_JOBS)' \
	    sh scripts/bootstrap-cross.sh emit O2 \
	    '$(BOOTSTRAP_CROSS_CGF)' '$(BOOTSTRAP_CROSS_SYSROOT)' \
	    '$(BOOTSTRAP_CROSS_OUTPUT)' '$(BOOTSTRAP_CROSS_X86_BOOTSTRAP)'

bootstrap-cross-compare: scripts/bootstrap-cross.sh ci/bootstrap.mk \
        scripts/bisect-nondet.sh
	CGF_BOOTSTRAP_JOBS='$(BOOTSTRAP_JOBS)' HOSTCC='$(HOSTCC)' \
	    sh scripts/bootstrap-cross.sh compare O2 \
	    '$(BOOTSTRAP_CROSS_X86)' '$(BOOTSTRAP_CROSS_NATIVE)' \
	    '$(BOOTSTRAP_CROSS_X86_BOOTSTRAP)'

test-bootstrap: $(BUILD)/cgfried
	CGF_TEST_CC=$(BUILD)/cgfried sh tests/bootstrap/run.sh
	sh scripts/audit-determinism.sh .
	CGF_TEST_CC=$(BUILD)/cgfried sh tests/rt/int128_abi.sh

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BUILD)/cgfried $(DESTDIR)$(PREFIX)/bin/cgfried
	ln -sf cgfried $(DESTDIR)$(PREFIX)/bin/cgf
	install -d $(DESTDIR)$(PREFIX)/lib/cgfried/include
	cp -R include/. $(DESTDIR)$(PREFIX)/lib/cgfried/include/
	install -d $(DESTDIR)$(PREFIX)/lib/cgfried/$(RT_TARGET)
	install -m 644 $(RT_LIB) \
	    $(DESTDIR)$(PREFIX)/lib/cgfried/$(RT_TARGET)/libcgf_rt.a
	@if [ -x afs-as/target/release/afs-as ]; then \
	    install -m 755 afs-as/target/release/afs-as \
	        $(DESTDIR)$(PREFIX)/bin/afs-as; \
	fi

clean:
	rm -rf $(BUILD)

-include $(sort $(OBJ:.o=.d) $(RUNNER_OBJ:.o=.d) $(UNIT_OBJ:.o=.d) \
               $(PPDIFF_OBJ:.o=.d) $(FUZZ_OBJ:.o=.d) $(FEFUZZ_OBJ:.o=.d) \
               $(IRFUZZ_OBJ:.o=.d) $(GENLAYOUT_OBJ:.o=.d) \
               $(OBJDIFF_OBJ:.o=.d) $(A64_OBJBYTES_OBJ:.o=.d) \
               $(A64MIR_OBJ:.o=.d) \
               $(A64_LOGIMM_GEN_OBJ:.o=.d) \
               $(FPDIFF_OBJ:.o=.d) $(TIMEIT_OBJ:.o=.d) \
               $(TIMEIT_LIB_OBJ:.o=.d) $(TIMEIT_MATH_TEST_OBJ:.o=.d))

check-format-matrix:
	CGF_FORMAT_MATRIX_WORK=$(BUILD)/format-matrix-check \
	    sh scripts/check_format_matrix.sh

# Sprint 56's imported-corpus matrix is kept in a standalone fragment so its
# infrastructure self-test can exercise the orchestration without rebuilding
# the compiler.  Including it last preserves `all` as the default goal.
include ci/torture.mk

# Sprint 57 and Sprint 59's compile-the-world campaigns are isolated fragments
# with one exact-results gate per upstream project. Keep them at the end so
# `all` remains the default goal while each public campaign stage is available
# both locally and in CI. ci/campaigns/ladder.yml is the audited inventory.
include ci/campaigns/musl.mk
include ci/campaigns/chibicc.mk
include ci/campaigns/tinycc.mk
include ci/campaigns/qbe.mk
include ci/campaigns/zlib.mk
include ci/campaigns/lua.mk
include ci/campaigns/sqlite.mk
include ci/campaigns/curl.mk
