# Fifth post-v0.1.0 large-FOSS campaign: checksum-pinned zstd is built as a
# complete static portable-C closure and runs its upstream CTest suite against
# a separate host-compiler build of the identically patched exported tree.

include ci/campaigns/common.mk

ZSTD_REF := f8745da6ff1ad1e7bab384bd1f9d742439278e99
ZSTD_VERSION := 1.5.7
ZSTD_SHA256 := eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
ZSTD_SRC := fetch:https://github.com/facebook/zstd/releases/download/v$(ZSTD_VERSION)/zstd-$(ZSTD_VERSION).tar.gz
ZSTD_URL := $(patsubst fetch:%,%,$(ZSTD_SRC))
CGF ?= build/cgfried
CGF_CAMPAIGN_ZSTD_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/zstd-$(ZSTD_VERSION).tar.gz
CGF_CAMPAIGN_ZSTD_WORK ?= $(CGF_CAMPAIGN_BUILD)/zstd
CGF_CAMPAIGN_ZSTD_EXPECTED ?= ci/campaigns/zstd.expected
CGF_CAMPAIGN_ZSTD_ACTUAL ?= $(CGF_CAMPAIGN_ZSTD_WORK)/results.txt
CGF_CAMPAIGN_ZSTD_RUNNER ?= scripts/campaigns/zstd.sh
CGF_CAMPAIGN_ZSTD_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_ZSTD_PRODUCER ?= zstd-validate

.PHONY: campaign-zstd campaign-zstd-fetch campaign-zstd-verify-source \
	zstd-configure zstd-build zstd-validate zstd-expected

campaign-zstd: zstd-expected

campaign-zstd-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_ZSTD_ARCHIVE))"; \
	if test -f "$(CGF_CAMPAIGN_ZSTD_ARCHIVE)" && \
	   printf '%s  %s\n' "$(ZSTD_SHA256)" "$(CGF_CAMPAIGN_ZSTD_ARCHIVE)" | \
	       sha256sum -c - >/dev/null 2>&1; then \
		:; \
	else \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo 'zstd archive is absent or invalid in the offline cache: $(CGF_CAMPAIGN_ZSTD_ARCHIVE)' >&2; \
			exit 1; \
		}; \
		tmp="$(CGF_CAMPAIGN_ZSTD_ARCHIVE).tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$(ZSTD_URL)"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$(ZSTD_URL)"; \
		else \
			echo 'zstd fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$(ZSTD_SHA256)" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$(CGF_CAMPAIGN_ZSTD_ARCHIVE)"; \
		trap - EXIT HUP INT TERM; \
	fi

campaign-zstd-verify-source: campaign-zstd-fetch
	@set -eu; \
	got=$$(sha256sum "$(CGF_CAMPAIGN_ZSTD_ARCHIVE)" | awk '{print $$1}'); \
	test "$$got" = "$(ZSTD_SHA256)" || { \
		echo "zstd archive checksum mismatch: expected $(ZSTD_SHA256), got $$got" >&2; \
		exit 1; \
	}

zstd-configure: campaign-zstd-verify-source build/cgfried
	CGF_CAMPAIGN_ZSTD_ARCHIVE="$(abspath $(CGF_CAMPAIGN_ZSTD_ARCHIVE))" \
	CGF_CAMPAIGN_ZSTD_WORK="$(abspath $(CGF_CAMPAIGN_ZSTD_WORK))" \
	CGF_CAMPAIGN_ZSTD_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_ZSTD_RUNNER) configure

zstd-build: zstd-configure
	CGF_CAMPAIGN_ZSTD_ARCHIVE="$(abspath $(CGF_CAMPAIGN_ZSTD_ARCHIVE))" \
	CGF_CAMPAIGN_ZSTD_WORK="$(abspath $(CGF_CAMPAIGN_ZSTD_WORK))" \
	CGF_CAMPAIGN_ZSTD_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_ZSTD_RUNNER) build

zstd-validate: zstd-build
	CGF_CAMPAIGN_ZSTD_ARCHIVE="$(abspath $(CGF_CAMPAIGN_ZSTD_ARCHIVE))" \
	CGF_CAMPAIGN_ZSTD_WORK="$(abspath $(CGF_CAMPAIGN_ZSTD_WORK))" \
	CGF_CAMPAIGN_ZSTD_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_ZSTD_RUNNER) validate

zstd-expected: $(CGF_CAMPAIGN_ZSTD_PRODUCER)
	$(CGF_CAMPAIGN_ZSTD_CHECK) "$(CGF_CAMPAIGN_ZSTD_EXPECTED)" \
		"$(CGF_CAMPAIGN_ZSTD_ACTUAL)"
