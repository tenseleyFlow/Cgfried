// FLAGS: --target=arm64-linux -m64 -fsyntax-only
// ERROR_EXPECTED: option '-m64' requires an x86_64 target
/* Silently accepting an x86 ABI selector for ARM would make target selection
 * command-line-order dependent and let configure checks lie. */
int m64_arm64_rejected;
