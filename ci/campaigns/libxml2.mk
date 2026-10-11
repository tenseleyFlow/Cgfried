# Sixth post-v0.1.0 large-FOSS campaign: checksum-pinned libxml2 is built as a
# complete static C closure and runs its upstream CTest suite against a
# separate host-compiler build of the identically supplemented exported tree.

include ci/campaigns/common.mk

LIBXML2_REF := 9fac185ccc0b719eb647db902d5c13c3aaa96263
LIBXML2_VERSION := 2.15.4
LIBXML2_SHA256 := 98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821
LIBXML2_SRC := fetch:https://download.gnome.org/sources/libxml2/2.15/libxml2-$(LIBXML2_VERSION).tar.xz
LIBXML2_URL := $(patsubst fetch:%,%,$(LIBXML2_SRC))
CGF ?= build/cgfried
CGF_CAMPAIGN_LIBXML2_ARCHIVE ?= $(CGF_CAMPAIGN_BUILD)/dl/libxml2-$(LIBXML2_VERSION).tar.xz
CGF_CAMPAIGN_LIBXML2_WORK ?= $(CGF_CAMPAIGN_BUILD)/libxml2
CGF_CAMPAIGN_LIBXML2_EXPECTED ?= ci/campaigns/libxml2.expected
CGF_CAMPAIGN_LIBXML2_ACTUAL ?= $(CGF_CAMPAIGN_LIBXML2_WORK)/results.txt
CGF_CAMPAIGN_LIBXML2_RUNNER ?= scripts/campaigns/libxml2.sh
CGF_CAMPAIGN_LIBXML2_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_LIBXML2_PRODUCER ?= libxml2-validate

.PHONY: campaign-libxml2 campaign-libxml2-fetch campaign-libxml2-verify-source \
	libxml2-configure libxml2-build libxml2-validate libxml2-expected

campaign-libxml2: libxml2-expected

campaign-libxml2-fetch:
	@set -eu; \
	mkdir -p "$(dir $(CGF_CAMPAIGN_LIBXML2_ARCHIVE))"; \
	if test -f "$(CGF_CAMPAIGN_LIBXML2_ARCHIVE)" && \
	   printf '%s  %s\n' "$(LIBXML2_SHA256)" "$(CGF_CAMPAIGN_LIBXML2_ARCHIVE)" | \
	       sha256sum -c - >/dev/null 2>&1; then \
		:; \
	else \
		test "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 || { \
			echo 'libxml2 archive is absent or invalid in the offline cache: $(CGF_CAMPAIGN_LIBXML2_ARCHIVE)' >&2; \
			exit 1; \
		}; \
		tmp="$(CGF_CAMPAIGN_LIBXML2_ARCHIVE).tmp.$$$$"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
		if command -v curl >/dev/null 2>&1; then \
			curl -fL --retry 3 -o "$$tmp" "$(LIBXML2_URL)"; \
		elif command -v wget >/dev/null 2>&1; then \
			wget -O "$$tmp" "$(LIBXML2_URL)"; \
		else \
			echo 'libxml2 fetch needs curl or wget' >&2; exit 1; \
		fi; \
		printf '%s  %s\n' "$(LIBXML2_SHA256)" "$$tmp" | sha256sum -c -; \
		mv "$$tmp" "$(CGF_CAMPAIGN_LIBXML2_ARCHIVE)"; \
		trap - EXIT HUP INT TERM; \
	fi

campaign-libxml2-verify-source: campaign-libxml2-fetch
	@set -eu; \
	got=$$(sha256sum "$(CGF_CAMPAIGN_LIBXML2_ARCHIVE)" | awk '{print $$1}'); \
	test "$$got" = "$(LIBXML2_SHA256)" || { \
		echo "libxml2 archive checksum mismatch: expected $(LIBXML2_SHA256), got $$got" >&2; \
		exit 1; \
	}

libxml2-configure: campaign-libxml2-verify-source build/cgfried
	CGF_CAMPAIGN_LIBXML2_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBXML2_ARCHIVE))" \
	CGF_CAMPAIGN_LIBXML2_WORK="$(abspath $(CGF_CAMPAIGN_LIBXML2_WORK))" \
	CGF_CAMPAIGN_LIBXML2_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBXML2_RUNNER) configure

libxml2-build: libxml2-configure
	CGF_CAMPAIGN_LIBXML2_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBXML2_ARCHIVE))" \
	CGF_CAMPAIGN_LIBXML2_WORK="$(abspath $(CGF_CAMPAIGN_LIBXML2_WORK))" \
	CGF_CAMPAIGN_LIBXML2_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBXML2_RUNNER) build

libxml2-validate: libxml2-build
	CGF_CAMPAIGN_LIBXML2_ARCHIVE="$(abspath $(CGF_CAMPAIGN_LIBXML2_ARCHIVE))" \
	CGF_CAMPAIGN_LIBXML2_WORK="$(abspath $(CGF_CAMPAIGN_LIBXML2_WORK))" \
	CGF_CAMPAIGN_LIBXML2_CGF="$(abspath $(CGF))" \
		$(CGF_CAMPAIGN_LIBXML2_RUNNER) validate

libxml2-expected: $(CGF_CAMPAIGN_LIBXML2_PRODUCER)
	$(CGF_CAMPAIGN_LIBXML2_CHECK) "$(CGF_CAMPAIGN_LIBXML2_EXPECTED)" \
		"$(CGF_CAMPAIGN_LIBXML2_ACTUAL)"
