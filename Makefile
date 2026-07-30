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
SRC := $(shell find src -name '*.c' | sort)
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
# The frontend fuzzer: same shape, drives -fsyntax-only over pp+lex+parse.
FEFUZZ_OBJ := $(BUILD)/tests/fuzz/fuzz_frontend.o \
              $(BUILD)/tests/runner/spawn.o $(LIB_OBJ)

# Unit harness: explicit registry generated at build time (strict C11 — no
# constructor attributes). The registry depends on every test_*.c, or a
# stale registry would silently drop new tests.
UNIT_TEST_SRC := $(sort $(wildcard tests/unit/test_*.c))
UNIT_OBJ := $(BUILD)/tests/unit/unit_main.o \
            $(UNIT_TEST_SRC:%.c=$(BUILD)/%.o) \
            $(BUILD)/gen/unit_registry.o \
            $(BUILD)/tests/runner/directive.o \
            $(LIB_OBJ)

DIRS := $(sort $(dir $(OBJ) $(RUNNER_OBJ) $(UNIT_OBJ) $(PPDIFF_OBJ) $(FUZZ_OBJ) $(FEFUZZ_OBJ)) $(BUILD)/gen/)

.PHONY: all test test-san test-ppdiff fuzz-smoke fuzz-frontend-smoke fuzz \
        pp-bench clean tools bootstrap install asan ubsan

all: $(BUILD)/cgfried $(BUILD)/cgf

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

$(BUILD)/fuzz_frontend: $(sort $(FEFUZZ_OBJ))
	$(CC) $(CFLAGS) -o $@ $(sort $(FEFUZZ_OBJ))

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
test: all $(BUILD)/unit_tests $(BUILD)/cgf-test
	$(BUILD)/unit_tests
	CGF_TEST_CC=$(BUILD)/cgfried \
	    $(BUILD)/cgf-test --profile linux-x86_64 tests/programs \
	    > $(BUILD)/programs.log 2>&1; s=$$?; \
	    cat $(BUILD)/programs.log; exit $$s
	sh ci/check_skips.sh linux-x86_64 $(BUILD)/programs.log
	sh tests/runner/meta/run_meta.sh $(BUILD)/cgf-test
	$(MAKE) BUILD=$(BUILD) CC='$(CC)' test-ppdiff
	sh scripts/pp_dm_check.sh $(BUILD)/cgfried
	sh scripts/lex_diff.sh $(BUILD)/cgfried
	sh scripts/parse_diff.sh $(BUILD)/cgfried
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
	sh scripts/check_fuzz_crashes.sh
	sh scripts/check_bans.sh
	sh scripts/check_pp_seams.sh
	sh scripts/check_format.sh

# Preprocessor differential: token-level vs gcc AND clang over fixtures
# and imported corpora, at both std flavors.
PPDIFF_FILES := $(sort $(wildcard tests/ppdiff/*.c) $(wildcard tests/fixtures/imported/tinycc-pp/*.c) $(wildcard tests/fixtures/imported/chibicc/*.c))

test-ppdiff: $(BUILD)/cgfried $(BUILD)/cgf-ppdiff
	$(BUILD)/cgf-ppdiff --std -std=c17 -I tests/fixtures/imported/chibicc --xfail tests/fixtures/imported/ppdiff-xfail.txt $(BUILD)/cgfried $(PPDIFF_FILES) > $(BUILD)/ppdiff.log 2>&1; s=$$?; cat $(BUILD)/ppdiff.log; [ $$s -eq 0 ]
	$(BUILD)/cgf-ppdiff --std -std=gnu17 -I tests/fixtures/imported/chibicc --xfail tests/fixtures/imported/ppdiff-xfail.txt $(BUILD)/cgfried $(PPDIFF_FILES) >> $(BUILD)/ppdiff.log 2>&1; s=$$?; tail -1 $(BUILD)/ppdiff.log; [ $$s -eq 0 ]
	sh ci/check_skips.sh ppdiff $(BUILD)/ppdiff.log

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

-include $(sort $(OBJ:.o=.d) $(RUNNER_OBJ:.o=.d) $(UNIT_OBJ:.o=.d))
