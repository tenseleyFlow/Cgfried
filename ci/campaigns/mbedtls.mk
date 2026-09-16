# Post-v0.1.0 large-FOSS campaign: the checksum-pinned Mbed TLS release is
# cached once, then the complete symmetric-only static closure runs offline.

include ci/campaigns/common.mk

MBEDTLS_REF := 068ff080b369adfac81509f9b57b2afabaf82dc5
MBEDTLS_VERSION := 3.6.7
MBEDTLS_SHA256 := a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
MBEDTLS_SRC := fetch:https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$(MBEDTLS_VERSION)/mbedtls-$(MBEDTLS_VERSION).tar.bz2
MBEDTLS_URL := $(patsubst fetch:%,%,$(MBEDTLS_SRC))
CGF ?= build/cgfried
CGF_CAMPAIGN_MBEDTLS_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/mbedtls-$(MBEDTLS_VERSION).tar.bz2
CGF_CAMPAIGN_MBEDTLS_WORK ?= $(CGF_CAMPAIGN_BUILD)/mbedtls
CGF_CAMPAIGN_MBEDTLS_EXPECTED ?= ci/campaigns/mbedtls.expected
CGF_CAMPAIGN_MBEDTLS_ACTUAL ?= $(CGF_CAMPAIGN_MBEDTLS_WORK)/results.txt
CGF_CAMPAIGN_MBEDTLS_RUNNER ?= scripts/campaigns/mbedtls.sh
CGF_CAMPAIGN_MBEDTLS_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_MBEDTLS_PRODUCER ?= mbedtls-validate

.PHONY: campaign-mbedtls campaign-mbedtls-fetch campaign-mbedtls-verify-source \
	mbedtls-configure mbedtls-build mbedtls-validate mbedtls-expected

campaign-mbedtls: mbedtls-expected

campaign-mbedtls-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_MBEDTLS_ARCHIVE))"; \
	if test -f "$(CGF_CAMPAIGN_MBEDTLS_ARCHIVE)" && \
	   printf '%s  %s\n' "$(MBEDTLS_SHA256)" "$(CGF_CAMPAIGN_MBEDTLS_ARCHIVE)" | \
	       sha256sum -c - >/dev/null 2>&1; then \
		:; \
	else \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo 'Mbed TLS archive is absent or invalid in the offline cache: $(CGF_CAMPAIGN_MBEDTLS_ARCHIVE)' >&2; \
			exit 1; \
		}; \
		tmp="$(CGF_CAMPAIGN_MBEDTLS_ARCHIVE).tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$(MBEDTLS_URL)"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$(MBEDTLS_URL)"; \
		else \
			echo 'Mbed TLS fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$(MBEDTLS_SHA256)" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$(CGF_CAMPAIGN_MBEDTLS_ARCHIVE)"; \
		trap - EXIT HUP INT TERM; \
	fi

campaign-mbedtls-verify-source: campaign-mbedtls-fetch
	@set -eu; \
	got=$$(sha256sum "$(CGF_CAMPAIGN_MBEDTLS_ARCHIVE)" | awk '{print $$1}'); \
	test "$$got" = "$(MBEDTLS_SHA256)" || { \
		echo "Mbed TLS archive checksum mismatch: expected $(MBEDTLS_SHA256), got $$got" >&2; \
		exit 1; \
	}

mbedtls-configure: campaign-mbedtls-verify-source build/cgfried
	CGF_CAMPAIGN_MBEDTLS_ARCHIVE="$(abspath $(CGF_CAMPAIGN_MBEDTLS_ARCHIVE))" \
	CGF_CAMPAIGN_MBEDTLS_WORK="$(abspath $(CGF_CAMPAIGN_MBEDTLS_WORK))" \
	CGF_CAMPAIGN_MBEDTLS_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_MBEDTLS_RUNNER) configure

mbedtls-build: mbedtls-configure
	CGF_CAMPAIGN_MBEDTLS_ARCHIVE="$(abspath $(CGF_CAMPAIGN_MBEDTLS_ARCHIVE))" \
	CGF_CAMPAIGN_MBEDTLS_WORK="$(abspath $(CGF_CAMPAIGN_MBEDTLS_WORK))" \
	CGF_CAMPAIGN_MBEDTLS_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_MBEDTLS_RUNNER) build

mbedtls-validate: mbedtls-build
	CGF_CAMPAIGN_MBEDTLS_ARCHIVE="$(abspath $(CGF_CAMPAIGN_MBEDTLS_ARCHIVE))" \
	CGF_CAMPAIGN_MBEDTLS_WORK="$(abspath $(CGF_CAMPAIGN_MBEDTLS_WORK))" \
	CGF_CAMPAIGN_MBEDTLS_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_MBEDTLS_RUNNER) validate

mbedtls-expected: $(CGF_CAMPAIGN_MBEDTLS_PRODUCER)
	$(CGF_CAMPAIGN_MBEDTLS_CHECK) "$(CGF_CAMPAIGN_MBEDTLS_EXPECTED)" \
		"$(CGF_CAMPAIGN_MBEDTLS_ACTUAL)"
