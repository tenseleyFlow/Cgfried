# Seventh post-v0.1.0 large-FOSS campaign: checksum-pinned OpenSSL is built as
# a complete static C closure and runs every upstream recipe against a separate
# host-compiler build of the identically supplemented exported tree.

include ci/campaigns/common.mk

OPENSSL_REF := 45e844fa2a14ec92d146bd8f5778ac130b6625fb
OPENSSL_VERSION := 3.5.9
OPENSSL_SHA256 := 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
OPENSSL_SRC := fetch:https://github.com/openssl/openssl/releases/download/openssl-$(OPENSSL_VERSION)/openssl-$(OPENSSL_VERSION).tar.gz
OPENSSL_URL := $(patsubst fetch:%,%,$(OPENSSL_SRC))
CGF ?= build/cgfried
CGF_CAMPAIGN_OPENSSL_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/openssl-$(OPENSSL_VERSION).tar.gz
CGF_CAMPAIGN_OPENSSL_WORK ?= $(CGF_CAMPAIGN_BUILD)/openssl
CGF_CAMPAIGN_OPENSSL_EXPECTED ?= ci/campaigns/openssl.expected
CGF_CAMPAIGN_OPENSSL_ACTUAL ?= $(CGF_CAMPAIGN_OPENSSL_WORK)/results.txt
CGF_CAMPAIGN_OPENSSL_RUNNER ?= scripts/campaigns/openssl.sh
CGF_CAMPAIGN_OPENSSL_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_OPENSSL_PRODUCER ?= openssl-validate

.PHONY: campaign-openssl campaign-openssl-fetch campaign-openssl-verify-source \
	openssl-configure openssl-build openssl-validate openssl-expected

campaign-openssl: openssl-expected

campaign-openssl-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_OPENSSL_ARCHIVE))"; \
	if test -f "$(CGF_CAMPAIGN_OPENSSL_ARCHIVE)" && \
	   printf '%s  %s\n' "$(OPENSSL_SHA256)" "$(CGF_CAMPAIGN_OPENSSL_ARCHIVE)" | \
	       sha256sum -c - >/dev/null 2>&1; then \
		:; \
	else \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo 'OpenSSL archive is absent or invalid in the offline cache: $(CGF_CAMPAIGN_OPENSSL_ARCHIVE)' >&2; \
			exit 1; \
		}; \
		tmp="$(CGF_CAMPAIGN_OPENSSL_ARCHIVE).tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$(OPENSSL_URL)"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$(OPENSSL_URL)"; \
		else \
			echo 'OpenSSL fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$(OPENSSL_SHA256)" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$(CGF_CAMPAIGN_OPENSSL_ARCHIVE)"; \
		trap - EXIT HUP INT TERM; \
	fi

campaign-openssl-verify-source: campaign-openssl-fetch
	@set -eu; \
	got=$$(sha256sum "$(CGF_CAMPAIGN_OPENSSL_ARCHIVE)" | awk '{print $$1}'); \
	test "$$got" = "$(OPENSSL_SHA256)" || { \
		echo "OpenSSL archive checksum mismatch: expected $(OPENSSL_SHA256), got $$got" >&2; \
		exit 1; \
	}

openssl-configure: campaign-openssl-verify-source build/cgfried rt
	CGF_CAMPAIGN_OPENSSL_ARCHIVE="$(abspath $(CGF_CAMPAIGN_OPENSSL_ARCHIVE))" \
	CGF_CAMPAIGN_OPENSSL_WORK="$(abspath $(CGF_CAMPAIGN_OPENSSL_WORK))" \
	CGF_CAMPAIGN_OPENSSL_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_OPENSSL_RUNNER) configure

openssl-build: openssl-configure
	CGF_CAMPAIGN_OPENSSL_ARCHIVE="$(abspath $(CGF_CAMPAIGN_OPENSSL_ARCHIVE))" \
	CGF_CAMPAIGN_OPENSSL_WORK="$(abspath $(CGF_CAMPAIGN_OPENSSL_WORK))" \
	CGF_CAMPAIGN_OPENSSL_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_OPENSSL_RUNNER) build

openssl-validate: openssl-build
	CGF_CAMPAIGN_OPENSSL_ARCHIVE="$(abspath $(CGF_CAMPAIGN_OPENSSL_ARCHIVE))" \
	CGF_CAMPAIGN_OPENSSL_WORK="$(abspath $(CGF_CAMPAIGN_OPENSSL_WORK))" \
	CGF_CAMPAIGN_OPENSSL_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_OPENSSL_RUNNER) validate

openssl-expected: $(CGF_CAMPAIGN_OPENSSL_PRODUCER)
	$(CGF_CAMPAIGN_OPENSSL_CHECK) "$(CGF_CAMPAIGN_OPENSSL_EXPECTED)" \
		"$(CGF_CAMPAIGN_OPENSSL_ACTUAL)"
