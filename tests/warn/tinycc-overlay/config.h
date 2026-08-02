/* Minimal deterministic configure output for the Sprint 39 warning pass. */
#define TCC_VERSION "0.9.28rc"
#define CC_NAME CC_gcc
#define GCC_MAJOR 8
#define GCC_MINOR 5
#if !(TCC_TARGET_I386 || TCC_TARGET_X86_64 || TCC_TARGET_ARM ||              \
      TCC_TARGET_ARM64 || TCC_TARGET_RISCV64 || TCC_TARGET_C67)
#define TCC_TARGET_X86_64 1
#endif
#define CONFIG_TCC_PREDEFS 0
