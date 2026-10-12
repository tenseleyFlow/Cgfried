// FLAGS: --target=arm64-linux -framework CoreServices -fsyntax-only
// ERROR_EXPECTED: option '-framework CoreServices' requires target 'arm64-macos'
/* Treating the framework name as an ELF filename is both misleading and
 * target-incorrect; the paired option is deliberately Mach-O-only. */
int macos_framework_nonapple;
