# Post-v0.1.0 large-FOSS campaign: PCRE2's release archive is cached once and
# verified on every use. The required validation is offline from that cache.

include ci/campaigns/common.mk

PCRE2_REF := 7978954dbd2efc6f2196869290553cf1871b4ce6
PCRE2_VERSION := 10.48
PCRE2_SHA256 := ebcc25aadf2a51fa1fefa9b8bc9e7a79b3dae86870a0f1152a22e42befd46888
PCRE2_SRC := fetch:https://github.com/PCRE2Project/pcre2/releases/download/pcre2-$(PCRE2_VERSION)/pcre2-$(PCRE2_VERSION).tar.gz
PCRE2_URL := $(patsubst fetch:%,%,$(PCRE2_SRC))
CGF ?= build/cgfried
CGF_CAMPAIGN_PCRE2_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/pcre2-$(PCRE2_VERSION).tar.gz
CGF_CAMPAIGN_PCRE2_WORK ?= $(CGF_CAMPAIGN_BUILD)/pcre2
CGF_CAMPAIGN_PCRE2_EXPECTED ?= ci/campaigns/pcre2.expected
CGF_CAMPAIGN_PCRE2_ACTUAL ?= $(CGF_CAMPAIGN_PCRE2_WORK)/results.txt
CGF_CAMPAIGN_PCRE2_RUNNER ?= scripts/campaigns/pcre2.sh
CGF_CAMPAIGN_PCRE2_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_PCRE2_PRODUCER ?= pcre2-validate

.PHONY: campaign-pcre2 campaign-pcre2-fetch campaign-pcre2-verify-source \
	pcre2-configure pcre2-build pcre2-validate pcre2-expected

campaign-pcre2: pcre2-expected

campaign-pcre2-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_PCRE2_ARCHIVE))"; \
	if test -f "$(CGF_CAMPAIGN_PCRE2_ARCHIVE)" && \
	   printf '%s  %s\n' "$(PCRE2_SHA256)" "$(CGF_CAMPAIGN_PCRE2_ARCHIVE)" | \
	       sha256sum -c - >/dev/null 2>&1; then \
		:; \
	else \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo 'PCRE2 archive is absent or invalid in the offline cache: $(CGF_CAMPAIGN_PCRE2_ARCHIVE)' >&2; \
			exit 1; \
		}; \
		tmp="$(CGF_CAMPAIGN_PCRE2_ARCHIVE).tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$(PCRE2_URL)"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$(PCRE2_URL)"; \
		else \
			echo 'PCRE2 fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$(PCRE2_SHA256)" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$(CGF_CAMPAIGN_PCRE2_ARCHIVE)"; \
		trap - EXIT HUP INT TERM; \
	fi

campaign-pcre2-verify-source: campaign-pcre2-fetch
	@set -eu; \
	got=$$(sha256sum "$(CGF_CAMPAIGN_PCRE2_ARCHIVE)" | awk '{print $$1}'); \
	test "$$got" = "$(PCRE2_SHA256)" || { \
		echo "PCRE2 archive checksum mismatch: expected $(PCRE2_SHA256), got $$got" >&2; \
		exit 1; \
	}

pcre2-configure: campaign-pcre2-verify-source build/cgfried
	CGF_CAMPAIGN_PCRE2_ARCHIVE="$(abspath $(CGF_CAMPAIGN_PCRE2_ARCHIVE))" \
	CGF_CAMPAIGN_PCRE2_WORK="$(abspath $(CGF_CAMPAIGN_PCRE2_WORK))" \
	CGF_CAMPAIGN_PCRE2_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_PCRE2_RUNNER) configure

pcre2-build: pcre2-configure
	CGF_CAMPAIGN_PCRE2_ARCHIVE="$(abspath $(CGF_CAMPAIGN_PCRE2_ARCHIVE))" \
	CGF_CAMPAIGN_PCRE2_WORK="$(abspath $(CGF_CAMPAIGN_PCRE2_WORK))" \
	CGF_CAMPAIGN_PCRE2_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_PCRE2_RUNNER) build

pcre2-validate: pcre2-build
	CGF_CAMPAIGN_PCRE2_ARCHIVE="$(abspath $(CGF_CAMPAIGN_PCRE2_ARCHIVE))" \
	CGF_CAMPAIGN_PCRE2_WORK="$(abspath $(CGF_CAMPAIGN_PCRE2_WORK))" \
	CGF_CAMPAIGN_PCRE2_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_PCRE2_RUNNER) validate

pcre2-expected: $(CGF_CAMPAIGN_PCRE2_PRODUCER)
	$(CGF_CAMPAIGN_PCRE2_CHECK) "$(CGF_CAMPAIGN_PCRE2_EXPECTED)" \
		"$(CGF_CAMPAIGN_PCRE2_ACTUAL)"
