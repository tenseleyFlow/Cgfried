# Sprint 59 zlib campaign descriptor.  The source archive is cached once and
# verified on every use, so an already-populated CI cache needs no network.

include ci/campaigns/common.mk

ZLIB_REF := 1.3.1
ZLIB_SHA256 := 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
ZLIB_SRC := fetch:https://github.com/madler/zlib/releases/download/v$(ZLIB_REF)/zlib-$(ZLIB_REF).tar.gz
ZLIB_URL := $(patsubst fetch:%,%,$(ZLIB_SRC))
CGF ?= build/cgfried
CGF_CAMPAIGN_ZLIB_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/zlib-$(ZLIB_REF).tar.gz
CGF_CAMPAIGN_ZLIB_WORK ?= $(CGF_CAMPAIGN_BUILD)/zlib
CGF_CAMPAIGN_ZLIB_EXPECTED ?= ci/campaigns/zlib.expected
CGF_CAMPAIGN_ZLIB_ACTUAL ?= $(CGF_CAMPAIGN_ZLIB_WORK)/results.txt
CGF_CAMPAIGN_ZLIB_RUNNER ?= scripts/campaigns/zlib.sh
CGF_CAMPAIGN_ZLIB_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_ZLIB_PRODUCER ?= zlib-validate

.PHONY: campaign-zlib campaign-zlib-fetch campaign-zlib-verify-source \
	zlib-configure zlib-build zlib-validate zlib-expected

campaign-zlib: zlib-expected

campaign-zlib-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_ZLIB_ARCHIVE))"; \
	if test -f "$(CGF_CAMPAIGN_ZLIB_ARCHIVE)" && \
	   printf '%s  %s\n' "$(ZLIB_SHA256)" "$(CGF_CAMPAIGN_ZLIB_ARCHIVE)" | \
	       sha256sum -c - >/dev/null 2>&1; then \
		:; \
	else \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo 'zlib archive is absent or invalid in the offline cache: $(CGF_CAMPAIGN_ZLIB_ARCHIVE)' >&2; \
			exit 1; \
		}; \
		tmp="$(CGF_CAMPAIGN_ZLIB_ARCHIVE).tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$(ZLIB_URL)"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$(ZLIB_URL)"; \
		else \
			echo 'zlib fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$(ZLIB_SHA256)" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$(CGF_CAMPAIGN_ZLIB_ARCHIVE)"; \
		trap - EXIT HUP INT TERM; \
	fi

campaign-zlib-verify-source: campaign-zlib-fetch
	@set -eu; \
	got=$$(sha256sum "$(CGF_CAMPAIGN_ZLIB_ARCHIVE)" | awk '{print $$1}'); \
	test "$$got" = "$(ZLIB_SHA256)" || { \
		echo "zlib archive checksum mismatch: expected $(ZLIB_SHA256), got $$got" >&2; \
		exit 1; \
	}

zlib-configure: campaign-zlib-verify-source build/cgfried build/timeit
	CGF_CAMPAIGN_ZLIB_ARCHIVE="$(abspath $(CGF_CAMPAIGN_ZLIB_ARCHIVE))" \
	CGF_CAMPAIGN_ZLIB_WORK="$(abspath $(CGF_CAMPAIGN_ZLIB_WORK))" \
	CGF_CAMPAIGN_ZLIB_CGF="$(abspath $(CGF))" \
	CGF_CAMPAIGN_ZLIB_TIMEIT="$(abspath build/timeit)" \
		$(CGF_CAMPAIGN_ZLIB_RUNNER) configure

zlib-build: zlib-configure
	CGF_CAMPAIGN_ZLIB_ARCHIVE="$(abspath $(CGF_CAMPAIGN_ZLIB_ARCHIVE))" \
	CGF_CAMPAIGN_ZLIB_WORK="$(abspath $(CGF_CAMPAIGN_ZLIB_WORK))" \
	CGF_CAMPAIGN_ZLIB_CGF="$(abspath $(CGF))" \
	CGF_CAMPAIGN_ZLIB_TIMEIT="$(abspath build/timeit)" \
		$(CGF_CAMPAIGN_ZLIB_RUNNER) build

zlib-validate: zlib-build
	CGF_CAMPAIGN_ZLIB_ARCHIVE="$(abspath $(CGF_CAMPAIGN_ZLIB_ARCHIVE))" \
	CGF_CAMPAIGN_ZLIB_WORK="$(abspath $(CGF_CAMPAIGN_ZLIB_WORK))" \
	CGF_CAMPAIGN_ZLIB_CGF="$(abspath $(CGF))" \
	CGF_CAMPAIGN_ZLIB_TIMEIT="$(abspath build/timeit)" \
		$(CGF_CAMPAIGN_ZLIB_RUNNER) validate

zlib-expected: $(CGF_CAMPAIGN_ZLIB_PRODUCER)
	$(CGF_CAMPAIGN_ZLIB_CHECK) "$(CGF_CAMPAIGN_ZLIB_EXPECTED)" \
		"$(CGF_CAMPAIGN_ZLIB_ACTUAL)"
