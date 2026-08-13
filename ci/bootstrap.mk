# Sprint 58 fixed-point stage graph.
#
# Cgfried owns C -> assembly.  One explicitly resolved external assembler,
# archiver, and host linker own the container/toolchain boundary for both
# stages.  Keeping that boundary fixed lets a mismatch name its cause:
# assembly is the compiler, objects are the assembler, and the archive/final
# executable are the archiver/linker.

BOOTSTRAP_ROOT ?= build/boot/stage
BOOTSTRAP_CGF ?= build/cgfried
BOOTSTRAP_HOSTCC ?= cc
BOOTSTRAP_AS ?= as
BOOTSTRAP_AR ?= ar
BOOTSTRAP_LEVEL ?= O2
BOOTSTRAP_TARGET ?= x86_64-linux-gnu
BOOTSTRAP_TARGET_FLAG ?=
BOOTSTRAP_FROZEN_ASSEMBLY ?= 0

BOOTSTRAP_COMPILER_FLAGS := -std=c11 -pedantic -Wall -Wextra -Werror \
    -$(BOOTSTRAP_LEVEL) -D_POSIX_C_SOURCE=200809L -Isrc \
    $(BOOTSTRAP_TARGET_FLAG)
BOOTSTRAP_RUNTIME_FLAGS := -std=c11 -Wall -Wextra -Werror \
    -$(BOOTSTRAP_LEVEL) -fno-strict-aliasing -Isrc \
    $(BOOTSTRAP_TARGET_FLAG)

# Discovery and every downstream list are sorted.  A newly added TU enters
# the experiment automatically; filesystem enumeration order never does.
BOOTSTRAP_COMPILER_SRC := $(shell find src -name '*.c' \
    -not -path 'src/rt/*' | LC_ALL=C sort)
BOOTSTRAP_RUNTIME_SRC := $(shell { find src/rt -name '*.c'; \
    printf '%s\n' src/util/bigint.c src/util/softfp.c; } | LC_ALL=C sort)

BOOTSTRAP_COMPILER_ASM := $(patsubst %.c,$(BOOTSTRAP_ROOT)/compiler/%.s,\
    $(BOOTSTRAP_COMPILER_SRC))
BOOTSTRAP_COMPILER_OBJ := $(BOOTSTRAP_COMPILER_ASM:.s=.o)
BOOTSTRAP_RUNTIME_ASM := $(patsubst %.c,$(BOOTSTRAP_ROOT)/runtime/%.s,\
    $(BOOTSTRAP_RUNTIME_SRC))
BOOTSTRAP_RUNTIME_OBJ := $(BOOTSTRAP_RUNTIME_ASM:.s=.o)
BOOTSTRAP_RT_LIB := $(BOOTSTRAP_ROOT)/$(BOOTSTRAP_TARGET)/libcgf_rt.a
BOOTSTRAP_BINARY := $(BOOTSTRAP_ROOT)/cgfried

.PHONY: all assembly objects runtime link

all: $(BOOTSTRAP_BINARY)

assembly: $(BOOTSTRAP_COMPILER_ASM) $(BOOTSTRAP_RUNTIME_ASM)

objects: $(BOOTSTRAP_COMPILER_OBJ) $(BOOTSTRAP_RUNTIME_OBJ)

runtime: $(BOOTSTRAP_RT_LIB)

link: $(BOOTSTRAP_BINARY)

ifneq ($(BOOTSTRAP_FROZEN_ASSEMBLY),1)
$(BOOTSTRAP_ROOT)/compiler/%.s: %.c
	@mkdir -p $(dir $@)
	@$(BOOTSTRAP_CGF) $(BOOTSTRAP_COMPILER_FLAGS) -S -o $@ $<

$(BOOTSTRAP_ROOT)/runtime/%.s: %.c
	@mkdir -p $(dir $@)
	@$(BOOTSTRAP_CGF) $(BOOTSTRAP_RUNTIME_FLAGS) -S -o $@ $<
endif

$(BOOTSTRAP_ROOT)/compiler/%.o: $(BOOTSTRAP_ROOT)/compiler/%.s
	@$(BOOTSTRAP_AS) -o $@ $<

$(BOOTSTRAP_ROOT)/runtime/%.o: $(BOOTSTRAP_ROOT)/runtime/%.s
	@$(BOOTSTRAP_AS) -o $@ $<

$(BOOTSTRAP_RT_LIB): $(BOOTSTRAP_RUNTIME_OBJ)
	@mkdir -p $(dir $@)
	@rm -f $@
	@$(BOOTSTRAP_AR) rcsD $@ $(BOOTSTRAP_RUNTIME_OBJ)

# Cgfried currently emits the non-PIE relocation model on both Linux targets.
# The host compiler is deliberately only the fixed linker frontend here; it
# compiles no stage1/stage2 source.
$(BOOTSTRAP_BINARY): $(BOOTSTRAP_COMPILER_OBJ) $(BOOTSTRAP_RT_LIB)
	@$(BOOTSTRAP_HOSTCC) -no-pie -o $@ $(BOOTSTRAP_COMPILER_OBJ) \
	    $(BOOTSTRAP_RT_LIB)
