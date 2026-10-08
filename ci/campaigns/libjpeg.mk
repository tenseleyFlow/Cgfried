# Fourth post-v0.1.0 large-FOSS campaign: checksum-pinned libjpeg-turbo is
# built as a complete static, non-SIMD C closure and runs its upstream CTest
# suite against a separate pristine host-compiler build.

include ci/campaigns/common.mk

LIBJPEG_REF := c85e6b905bf237038faa936dab160ebfc5da0344
LIBJPEG_VERSION := 3.2.0
LIBJPEG_SHA256 := 6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e
LIBJPEG_SRC := fetch:https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$(LIBJPEG_VERSION)/libjpeg-turbo-$(LIBJPEG_VERSION).tar.gz
LIBJPEG_URL := $(patsubst fetch:%,%,$(LIBJPEG_SRC))
CGF ?= build/cgfried
CGF_CAMPAIGN_LIBJPEG_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/libjpeg-turbo-$(LIBJPEG_VERSION).tar.gz
CGF_CAMPAIGN_LIBJPEG_WORK ?= $(CGF_CAMPAIGN_BUILD)/libjpeg
CGF_CAMPAIGN_LIBJPEG_EXPECTED ?= ci/campaigns/libjpeg.expected
CGF_CAMPAIGN_LIBJPEG_ACTUAL ?= $(CGF_CAMPAIGN_LIBJPEG_WORK)/results.txt
CGF_CAMPAIGN_LIBJPEG_RUNNER ?= scripts/campaigns/libjpeg.sh
CGF_CAMPAIGN_LIBJPEG_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_LIBJPEG_PRODUCER ?= libjpeg-validate

.PHONY: campaign-libjpeg campaign-libjpeg-fetch campaign-libjpeg-verify-source \
	libjpeg-configure libjpeg-build libjpeg-validate libjpeg-expected

campaign-libjpeg: libjpeg-expected

campaign-libjpeg-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_LIBJPEG_ARCHIVE))"; \
	if test -f "$(CGF_CAMPAIGN_LIBJPEG_ARCHIVE)" && \
	   printf '%s  %s\n' "$(LIBJPEG_SHA256)" "$(CGF_CAMPAIGN_LIBJPEG_ARCHIVE)" | \
	       sha256sum -c - >/dev/null 2>&1; then \
		:; \
	else \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo 'libjpeg-turbo archive is absent or invalid in the offline cache: $(CGF_CAMPAIGN_LIBJPEG_ARCHIVE)' >&2; \
			exit 1; \
		}; \
		tmp="$(CGF_CAMPAIGN_LIBJPEG_ARCHIVE).tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$(LIBJPEG_URL)"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$(LIBJPEG_URL)"; \
		else \
			echo 'libjpeg-turbo fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$(LIBJPEG_SHA256)" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$(CGF_CAMPAIGN_LIBJPEG_ARCHIVE)"; \
		trap - EXIT HUP INT TERM; \
	fi

campaign-libjpeg-verify-source: campaign-libjpeg-fetch
	@set -eu; \
	got=$$(sha256sum "$(CGF_CAMPAIGN_LIBJPEG_ARCHIVE)" | awk '{print $$1}'); \
	test "$$got" = "$(LIBJPEG_SHA256)" || { \
		echo "libjpeg-turbo archive checksum mismatch: expected $(LIBJPEG_SHA256), got $$got" >&2; \
		exit 1; \
	}

libjpeg-configure: campaign-libjpeg-verify-source build/cgfried
	CGF_CAMPAIGN_LIBJPEG_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBJPEG_ARCHIVE))" \
	CGF_CAMPAIGN_LIBJPEG_WORK="$(abspath $(CGF_CAMPAIGN_LIBJPEG_WORK))" \
	CGF_CAMPAIGN_LIBJPEG_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBJPEG_RUNNER) configure

libjpeg-build: libjpeg-configure
	CGF_CAMPAIGN_LIBJPEG_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBJPEG_ARCHIVE))" \
	CGF_CAMPAIGN_LIBJPEG_WORK="$(abspath $(CGF_CAMPAIGN_LIBJPEG_WORK))" \
	CGF_CAMPAIGN_LIBJPEG_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBJPEG_RUNNER) build

libjpeg-validate: libjpeg-build
	CGF_CAMPAIGN_LIBJPEG_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBJPEG_ARCHIVE))" \
	CGF_CAMPAIGN_LIBJPEG_WORK="$(abspath $(CGF_CAMPAIGN_LIBJPEG_WORK))" \
	CGF_CAMPAIGN_LIBJPEG_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBJPEG_RUNNER) validate

libjpeg-expected: $(CGF_CAMPAIGN_LIBJPEG_PRODUCER)
	$(CGF_CAMPAIGN_LIBJPEG_CHECK) "$(CGF_CAMPAIGN_LIBJPEG_EXPECTED)" \
		"$(CGF_CAMPAIGN_LIBJPEG_ACTUAL)"
