// FLAGS: -fsyntax-only -Wno-not-a-real-warning -Wbogus
// WARNING_EXPECTED: may have been intended to silence earlier diagnostics
// WARNING_EXPECTED: [-Wunknown-warning-option]
int deferred_negative_warning_option;
