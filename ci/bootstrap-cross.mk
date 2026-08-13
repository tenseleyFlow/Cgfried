# Sprint 58 host-independent ARM64 bootstrap stream.
#
# Both host compilers consume the same source tree and the same archived
# native ARM64 header sysroot.  The compiler owns C -> assembly; objects and
# final containers are deliberately produced later by one native toolchain.

BOOTSTRAP_CROSS_SYSROOT ?= build/boot/cross/sysroot
BOOTSTRAP_CROSS_OUTPUT ?= build/boot/cross/output
BOOTSTRAP_CGF ?= build/cgfried
BOOTSTRAP_LEVEL ?= O2

BOOTSTRAP_CROSS_COMPILER_FLAGS := -std=c11 -pedantic -Wall -Wextra -Werror \
    -$(BOOTSTRAP_LEVEL) -D_POSIX_C_SOURCE=200809L -Isrc \
    --target=arm64-linux --sysroot=$(BOOTSTRAP_CROSS_SYSROOT)
BOOTSTRAP_CROSS_RUNTIME_FLAGS := -std=c11 -Wall -Wextra -Werror \
    -$(BOOTSTRAP_LEVEL) -fno-strict-aliasing -Isrc \
    --target=arm64-linux --sysroot=$(BOOTSTRAP_CROSS_SYSROOT)

BOOTSTRAP_CROSS_COMPILER_SRC := $(shell find src -name '*.c' \
    -not -path 'src/rt/*' | LC_ALL=C sort)
BOOTSTRAP_CROSS_RUNTIME_SRC := $(shell { find src/rt -name '*.c'; \
    printf '%s\n' src/util/bigint.c src/util/softfp.c; } | LC_ALL=C sort)
BOOTSTRAP_CROSS_COMPILER_ASM := $(patsubst %.c,\
    $(BOOTSTRAP_CROSS_OUTPUT)/compiler/%.s,$(BOOTSTRAP_CROSS_COMPILER_SRC))
BOOTSTRAP_CROSS_RUNTIME_ASM := $(patsubst %.c,\
    $(BOOTSTRAP_CROSS_OUTPUT)/runtime/%.s,$(BOOTSTRAP_CROSS_RUNTIME_SRC))

.PHONY: assembly

assembly: $(BOOTSTRAP_CROSS_COMPILER_ASM) $(BOOTSTRAP_CROSS_RUNTIME_ASM)

$(BOOTSTRAP_CROSS_OUTPUT)/compiler/%.s: %.c
	@mkdir -p $(dir $@)
	@$(BOOTSTRAP_CGF) $(BOOTSTRAP_CROSS_COMPILER_FLAGS) -S -o $@ $<

$(BOOTSTRAP_CROSS_OUTPUT)/runtime/%.s: %.c
	@mkdir -p $(dir $@)
	@$(BOOTSTRAP_CGF) $(BOOTSTRAP_CROSS_RUNTIME_FLAGS) -S -o $@ $<
