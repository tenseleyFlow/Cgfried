# Sprint 59 curl descriptor.  The required campaign is offline: populate the
# content-addressed download cache explicitly with campaign-curl-fetch.

include ci/campaigns/common.mk

CURL_VERSION := 8.9.1
CURL_REF := $(CURL_VERSION)
CURL_ARCHIVE := curl-$(CURL_VERSION).tar.xz
CURL_SHA256 := f292f6cc051d5bbabf725ef85d432dfeacc8711dd717ea97612ae590643801e5
CURL_URL := https://curl.se/download/$(CURL_ARCHIVE)
CURL_SRC := fetch:$(CURL_URL)
CGF_CAMPAIGN_CURL_CACHE ?= $(CGF_CAMPAIGN_BUILD)/dl/$(CURL_ARCHIVE)
CGF_CAMPAIGN_CURL_WORK ?= $(CGF_CAMPAIGN_BUILD)/curl
CGF_CAMPAIGN_CURL_EXPECTED ?= ci/campaigns/curl.expected
CGF_CAMPAIGN_CURL_ACTUAL ?= $(CGF_CAMPAIGN_CURL_WORK)/results.txt
CGF_CAMPAIGN_CURL_RUNNER ?= scripts/campaigns/curl.sh
CGF_CAMPAIGN_CURL_FETCH ?= curl
CGF_CAMPAIGN_CURL_CHECK ?= scripts/campaign-check.sh
CGF_CAMPAIGN_CURL_PRODUCER ?= curl-validate

.PHONY: campaign-curl campaign-curl-bootstrap campaign-curl-fetch \
	campaign-curl-run campaign-curl-gate campaign-curl-verify-cache \
	curl-configure curl-build curl-validate curl-expected
campaign-curl: curl-expected

# Convenience entry point for a connected developer machine.  Required CI can
# invoke campaign-curl directly after restoring build/campaigns/dl.
campaign-curl-bootstrap: campaign-curl-fetch campaign-curl-gate

campaign-curl-fetch:
	@set -eu; \
	archive='$(CGF_CAMPAIGN_CURL_CACHE)'; \
	mkdir -p "$$(dirname "$$archive")"; \
	if [ -f "$$archive" ] && \
	   printf '%s  %s\n' '$(CURL_SHA256)' "$$archive" | sha256sum -c - >/dev/null 2>&1; then \
		echo "campaign-curl-fetch: cached $(CURL_ARCHIVE)"; \
		exit 0; \
	fi; \
	[ "$${CGF_CAMPAIGN_OFFLINE:-0}" != 1 ] || { \
		echo "curl archive is absent or invalid in the offline cache: $$archive" >&2; \
		exit 1; \
	}; \
	partial="$$archive.part.$$$$"; \
	trap 'rm -f "$$partial"' EXIT HUP INT TERM; \
	case '$(CGF_CAMPAIGN_CURL_FETCH)' in \
		curl) curl -fL --retry 3 --proto '=https' -o "$$partial" '$(CURL_URL)' ;; \
		wget) wget --https-only -O "$$partial" '$(CURL_URL)' ;; \
		*) echo 'CGF_CAMPAIGN_CURL_FETCH must be curl or wget' >&2; exit 2 ;; \
	esac; \
	printf '%s  %s\n' '$(CURL_SHA256)' "$$partial" | sha256sum -c -; \
	mv "$$partial" "$$archive"; \
	trap - EXIT HUP INT TERM; \
	echo "campaign-curl-fetch: stored $$archive"

campaign-curl-verify-cache:
	@set -eu; \
	archive='$(CGF_CAMPAIGN_CURL_CACHE)'; \
	[ -f "$$archive" ] || { \
		echo "curl source cache is missing: $$archive" >&2; \
		echo 'run campaign-curl-fetch on a connected machine, then restore the cache offline' >&2; \
		exit 1; \
	}; \
	printf '%s  %s\n' '$(CURL_SHA256)' "$$archive" | sha256sum -c -; \
	members="$(CGF_CAMPAIGN_BUILD)/curl-members.$$$$"; \
	trap 'rm -f "$$members"' EXIT HUP INT TERM; \
	tar -tJf "$$archive" >"$$members"; \
	awk -v root='curl-$(CURL_VERSION)' '\
		BEGIN { bad = 0 } \
		{ \
			name = $$0; \
			if (substr(name, 1, 1) == "/" || index(name, "\\") != 0 || \
			    (name != root && name != root "/" && index(name, root "/") != 1)) { bad = 1; next } \
			sub(/\/$$/, "", name); \
			count = split(name, component, "/"); \
			for (i = 1; i <= count; i++) \
				if (component[i] == "" || component[i] == "." || component[i] == "..") bad = 1; \
		} \
		END { exit bad }' "$$members" || { \
		echo 'curl archive contains a member outside curl-$(CURL_VERSION)' >&2; exit 1; \
	}; \
	tar -tvJf "$$archive" | awk '\
		substr($$1, 1, 1) != "-" && substr($$1, 1, 1) != "d" { bad = 1 } \
		END { exit bad }' || { \
		echo 'curl archive contains a link or special-file member' >&2; exit 1; \
	}; \
	rm -f "$$members"; \
	trap - EXIT HUP INT TERM

campaign-curl-run: campaign-curl-verify-cache build/cgfried
	CGF_CAMPAIGN_CURL_CACHE="$(abspath $(CGF_CAMPAIGN_CURL_CACHE))" \
	CGF_CAMPAIGN_CURL_WORK="$(abspath $(CGF_CAMPAIGN_CURL_WORK))" \
	CGF_CAMPAIGN_CURL_CGF="$(abspath build/cgfried)" \
	CGF_CAMPAIGN_CURL_EXPECTED="$(CGF_CAMPAIGN_CURL_EXPECTED)" \
	CGF_CAMPAIGN_CHECK="$(CGF_CAMPAIGN_CHECK)" \
		$(CGF_CAMPAIGN_CURL_RUNNER)

campaign-curl-gate: campaign-curl-run
	$(CGF_CAMPAIGN_CHECK) "$(CGF_CAMPAIGN_CURL_EXPECTED)" \
		"$(CGF_CAMPAIGN_CURL_ACTUAL)"

# curl.sh retains the complete configure/build/validation artifact set in one
# invocation.  The public stage targets expose the format contract while their
# postconditions ensure a stale or partial work tree cannot satisfy a stage.
curl-configure: campaign-curl-run
	@test -s "$(CGF_CAMPAIGN_CURL_WORK)/logs/cgfried/config.log"

curl-build: curl-configure
	@test -x "$(CGF_CAMPAIGN_CURL_WORK)/cgfried-src/src/curl"

curl-validate: curl-build
	@test -s "$(CGF_CAMPAIGN_CURL_ACTUAL)"

curl-expected: $(CGF_CAMPAIGN_CURL_PRODUCER)
	$(CGF_CAMPAIGN_CURL_CHECK) "$(CGF_CAMPAIGN_CURL_EXPECTED)" \
		"$(CGF_CAMPAIGN_CURL_ACTUAL)"
