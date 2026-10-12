// FLAGS: --target=arm64-macos -framework CoreServices -fsyntax-only
/* Darwin build systems use the paired framework spelling rather than a
 * `-l` library name. Parsing it must consume the framework name and retain
 * the pair for the Mach-O link plan. */
int macos_framework;
