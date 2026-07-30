# Cgfried — the one hand-written GNU Makefile (locked decision; CLAUDE.md).
# Any C11 compiler bootstraps stage0: never hardcode gcc.
CC     ?= cc
BUILD  ?= build
PREFIX ?= /usr/local

# -MMD not -MD: system-header deps churn without value. -MP: survive header
# deletion. No -D with absolute build paths — that breaks the byte-identical
# stage1 == stage2 bootstrap (Sprint 58) and reproducible dists (Sprint 62).
CFLAGS := -std=c11 -pedantic -Wall -Wextra -Werror -g -O2 \
          -fno-strict-overflow -MMD -MP -Isrc

# Sorted: raw find order varies by filesystem and would leak into link order,
# making binaries nondeterministic.
SRC := $(shell find src -name '*.c' | sort)
OBJ := $(SRC:%.c=$(BUILD)/%.o)

UNIT_SRC := $(shell find tests/unit -name '*.c' | sort)
UNIT_OBJ := $(UNIT_SRC:%.c=$(BUILD)/%.o) \
            $(filter-out $(BUILD)/src/main.o,$(OBJ))

# Order-only prerequisites: a normal dir prereq would rebuild on every dir
# mtime change.
DIRS := $(sort $(dir $(OBJ) $(UNIT_OBJ)))

.PHONY: all test clean tools bootstrap install

all: $(BUILD)/cgfried $(BUILD)/cgf

$(BUILD)/cgfried: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

# The short alias from the locked decision.
$(BUILD)/cgf: $(BUILD)/cgfried
	ln -sf cgfried $@

$(BUILD)/unit_tests: $(UNIT_OBJ)
	$(CC) $(CFLAGS) -o $@ $(UNIT_OBJ)

$(BUILD)/%.o: %.c | $(DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(DIRS):
	mkdir -p $@

test: all $(BUILD)/unit_tests
	$(BUILD)/unit_tests
	sh scripts/smoke.sh $(BUILD)/cgfried

tools:
	@echo "error: 'make tools' is not available yet: Sprint 2 (toolchain submodules)" >&2
	@exit 1

bootstrap:
	@echo "error: 'make bootstrap' is not available yet: Sprint 58 (self-host)" >&2
	@exit 1

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BUILD)/cgfried $(DESTDIR)$(PREFIX)/bin/cgfried
	ln -sf cgfried $(DESTDIR)$(PREFIX)/bin/cgf

clean:
	rm -rf $(BUILD)

-include $(sort $(OBJ:.o=.d) $(UNIT_OBJ:.o=.d))
