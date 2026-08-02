# Cgfried — the one hand-written GNU Makefile
# Any C11 compiler bootstraps stage0: never hardcode gcc.
CC     ?= cc
BUILD  ?= build
PREFIX ?= /usr/local

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
OBJDIFF_OBJ := $(BUILD)/tests/tools/objdiff.o
# The float differential's printer: softfp only, no compiler, no host FPU.
FPDIFF_OBJ := $(BUILD)/tests/tools/fpdiff.o $(BUILD)/src/util/softfp.o \
              $(BUILD)/src/util/bigint.o

# The frontend fuzzer: same shape, drives -fsyntax-only over pp+lex+parse.
FEFUZZ_OBJ := $(BUILD)/tests/fuzz/fuzz_frontend.o \
              $(BUILD)/tests/runner/spawn.o $(LIB_OBJ)
IRFUZZ_OBJ := $(BUILD)/tests/fuzz/ir_fuzz.o $(LIB_OBJ)

# Unit harness: explicit registry generated at build time (strict C11 — no
# constructor attributes). The registry depends on every test_*.c, or a
# stale registry would silently drop new tests.
UNIT_TEST_SRC := $(sort $(wildcard tests/unit/test_*.c))
UNIT_OBJ := $(BUILD)/tests/unit/unit_main.o \
            $(UNIT_TEST_SRC:%.c=$(BUILD)/%.o) \
            $(BUILD)/gen/unit_registry.o \
            $(BUILD)/tests/runner/directive.o \
            $(LIB_OBJ)

DIRS := $(sort $(dir $(OBJ) $(RUNNER_OBJ) $(UNIT_OBJ) $(PPDIFF_OBJ) $(FUZZ_OBJ) $(FEFUZZ_OBJ) $(GENLAYOUT_OBJ) $(FPDIFF_OBJ)) $(BUILD)/gen/)

.PHONY: all test test-san test-ppdiff test-warndiff test-musl-warnings check-warn-matrix fuzz-smoke \
        fuzz-frontend-smoke fuzz pp-bench clean tools bootstrap install asan ubsan

# libcgf_rt.a: the runtime the Sprint 27 link line reserves a slot for.
# Built by the HOST cc (RT_CC) until Sprint 58 flips it to cgf — the
# flip is part of the self-host DoD. Its own flags are separate from
# CFLAGS: the runtime is not the compiler and must stay buildable with
# a plain toolchain. `ar rcsD` for a DETERMINISTIC archive (no
# timestamps, uids or modes) — two clean builds must be byte-equal.
RT_CC ?= $(CC)
RT_CFLAGS ?= -std=c11 -Wall -Wextra -O2 -fno-strict-aliasing
RT_TARGET := $(shell $(BUILD)/cgfried -dumpmachine 2>/dev/null || \
                     echo x86_64-linux-gnu)
RT_SRC := $(sort $(wildcard src/rt/*.c))
RT_OBJ := $(patsubst src/rt/%.c,$(BUILD)/rt/%.o,$(RT_SRC))
RT_LIB := $(BUILD)/$(RT_TARGET)/libcgf_rt.a

all: $(BUILD)/cgfried $(BUILD)/cgf rt

.PHONY: rt
rt: $(RT_LIB)

$(BUILD)/rt/%.o: src/rt/%.c | $(BUILD)/rt/
	$(RT_CC) $(RT_CFLAGS) -c -o $@ $<

$(RT_LIB): $(RT_OBJ)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcsD $@ $(RT_OBJ)

$(BUILD)/rt/:
	mkdir -p $@

$(BUILD)/cgfried: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

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

$(BUILD)/cgf-objdiff: $(sort $(OBJDIFF_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(OBJDIFF_OBJ))

$(BUILD)/fpdiff: $(sort $(FPDIFF_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(FPDIFF_OBJ))

$(BUILD)/ppfuzz: $(sort $(FUZZ_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(FUZZ_OBJ))

$(BUILD)/unit_tests: $(sort $(UNIT_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(UNIT_OBJ))

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
	$(BUILD)/unit_tests
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
	$(MAKE) BUILD=$(BUILD) test-musl-warnings
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
	sh scripts/check_posix_sh.sh
	sh scripts/check_bans.sh
	sh scripts/check_warn_seams.sh
	sh scripts/check_pp_seams.sh
	sh scripts/check_sema_target.sh
	sh scripts/check_verify_coverage.sh
	sh scripts/check_no_host_fpu.sh
	$(MAKE) check-warn-matrix
	$(MAKE) BUILD=$(BUILD) test-warndiff
	sh scripts/check_format.sh

check-warn-matrix:
	sh scripts/check_warn_matrix.sh

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

test-musl-warnings: $(BUILD)/cgfried
	CGF_MUSL_WARN_WORK=$(BUILD)/musl-warn sh scripts/musl_warn_dryrun.sh \
	    $(BUILD)/cgfried > $(BUILD)/musl-warn.log 2>&1; s=$$?; \
	    cat $(BUILD)/musl-warn.log; exit $$s
	@if grep -q '^HARNESS_SKIP ' $(BUILD)/musl-warn.log; then p=muslwarn-norefs; \
	    else p=muslwarn; fi; \
	    sh ci/check_skips.sh $$p $(BUILD)/musl-warn.log

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

bootstrap:
	@echo "error: 'make bootstrap' is not available yet: Sprint 58 (self-host)" >&2
	@exit 1

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BUILD)/cgfried $(DESTDIR)$(PREFIX)/bin/cgfried
	ln -sf cgfried $(DESTDIR)$(PREFIX)/bin/cgf
	@if [ -x afs-as/target/release/afs-as ]; then \
	    install -m 755 afs-as/target/release/afs-as \
	        $(DESTDIR)$(PREFIX)/bin/afs-as; \
	fi

clean:
	rm -rf $(BUILD)

-include $(sort $(OBJ:.o=.d) $(RUNNER_OBJ:.o=.d) $(UNIT_OBJ:.o=.d) \
               $(PPDIFF_OBJ:.o=.d) $(FUZZ_OBJ:.o=.d) $(FEFUZZ_OBJ:.o=.d) \
               $(IRFUZZ_OBJ:.o=.d) $(GENLAYOUT_OBJ:.o=.d) \
               $(OBJDIFF_OBJ:.o=.d) $(FPDIFF_OBJ:.o=.d))
