# Post-v0.1.0 large-FOSS campaign: checksum-pinned libpng is built against
# checksum-pinned zlib. Both archives are cached once and every required
# validation runs offline from that cache.

include ci/campaigns/common.mk

LIBPNG_REF := cd952f49f722c8ef3d2097b7fd0078399e2c4b2e
LIBPNG_VERSION := 1.6.59
LIBPNG_SHA256 := d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389
LIBPNG_SRC := fetch:https://downloads.sourceforge.net/project/libpng/libpng16/$(LIBPNG_VERSION)/libpng-$(LIBPNG_VERSION).tar.xz
LIBPNG_URL := $(patsubst fetch:%,%,$(LIBPNG_SRC))
LIBPNG_ZLIB_VERSION := 1.3.1
LIBPNG_ZLIB_SHA256 := 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
LIBPNG_ZLIB_URL := https://github.com/madler/zlib/releases/download/v$(LIBPNG_ZLIB_VERSION)/zlib-$(LIBPNG_ZLIB_VERSION).tar.gz
CGF ?= build/cgfried
CGF_CAMPAIGN_LIBPNG_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/libpng-$(LIBPNG_VERSION).tar.xz
CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/zlib-$(LIBPNG_ZLIB_VERSION).tar.gz
CGF_CAMPAIGN_LIBPNG_WORK ?= $(CGF_CAMPAIGN_BUILD)/libpng
CGF_CAMPAIGN_LIBPNG_EXPECTED ?= ci/campaigns/libpng.expected
CGF_CAMPAIGN_LIBPNG_ACTUAL ?= $(CGF_CAMPAIGN_LIBPNG_WORK)/results.txt
CGF_CAMPAIGN_LIBPNG_RUNNER ?= scripts/campaigns/libpng.sh
CGF_CAMPAIGN_LIBPNG_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_LIBPNG_PRODUCER ?= libpng-validate

.PHONY: campaign-libpng campaign-libpng-fetch campaign-libpng-verify-source \
	libpng-configure libpng-build libpng-validate libpng-expected

campaign-libpng: libpng-expected

campaign-libpng-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_LIBPNG_ARCHIVE))"; \
	for specification in \
	  "$(CGF_CAMPAIGN_LIBPNG_ARCHIVE)|$(LIBPNG_SHA256)|$(LIBPNG_URL)|libpng" \
	  "$(CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE)|$(LIBPNG_ZLIB_SHA256)|$(LIBPNG_ZLIB_URL)|zlib"; do \
		archive=$${specification%%|*}; rest=$${specification#*|}; \
		digest=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; label=$${rest#*|}; \
		if test -f "$$archive" && \
		   printf '%s  %s\n' "$$digest" "$$archive" | sha256sum -c - >/dev/null 2>&1; then \
			continue; \
		fi; \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo "$$label archive is absent or invalid in the offline cache: $$archive" >&2; \
			exit 1; \
		}; \
		tmp="$$archive.tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$$url"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$$url"; \
		else \
			echo 'libpng fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$$digest" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$$archive"; \
		trap - EXIT HUP INT TERM; \
	done

campaign-libpng-verify-source: campaign-libpng-fetch
	@set -eu; \
	libpng_got=$$(sha256sum "$(CGF_CAMPAIGN_LIBPNG_ARCHIVE)" | awk '{print $$1}'); \
	test "$$libpng_got" = "$(LIBPNG_SHA256)" || { \
		echo "libpng archive checksum mismatch: expected $(LIBPNG_SHA256), got $$libpng_got" >&2; \
		exit 1; \
	}; \
	zlib_got=$$(sha256sum "$(CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE)" | awk '{print $$1}'); \
	test "$$zlib_got" = "$(LIBPNG_ZLIB_SHA256)" || { \
		echo "zlib archive checksum mismatch: expected $(LIBPNG_ZLIB_SHA256), got $$zlib_got" >&2; \
		exit 1; \
	}

libpng-configure: campaign-libpng-verify-source build/cgfried
	CGF_CAMPAIGN_LIBPNG_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBPNG_ARCHIVE))" \
	CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE))" \
	CGF_CAMPAIGN_LIBPNG_WORK="$(abspath $(CGF_CAMPAIGN_LIBPNG_WORK))" \
	CGF_CAMPAIGN_LIBPNG_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBPNG_RUNNER) configure

libpng-build: libpng-configure
	CGF_CAMPAIGN_LIBPNG_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBPNG_ARCHIVE))" \
	CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE))" \
	CGF_CAMPAIGN_LIBPNG_WORK="$(abspath $(CGF_CAMPAIGN_LIBPNG_WORK))" \
	CGF_CAMPAIGN_LIBPNG_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBPNG_RUNNER) build

libpng-validate: libpng-build
	CGF_CAMPAIGN_LIBPNG_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBPNG_ARCHIVE))" \
	CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBPNG_ZLIB_ARCHIVE))" \
	CGF_CAMPAIGN_LIBPNG_WORK="$(abspath $(CGF_CAMPAIGN_LIBPNG_WORK))" \
	CGF_CAMPAIGN_LIBPNG_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBPNG_RUNNER) validate

libpng-expected: $(CGF_CAMPAIGN_LIBPNG_PRODUCER)
	$(CGF_CAMPAIGN_LIBPNG_CHECK) "$(CGF_CAMPAIGN_LIBPNG_EXPECTED)" \
		"$(CGF_CAMPAIGN_LIBPNG_ACTUAL)"
