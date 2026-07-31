/* cgfried freestanding <limits.h> (C17 5.2.4.2.1).
 *
 * Complete and final: our include dir precedes the system paths, so this
 * header WINS over libc's and must therefore define everything C17 asks
 * for. No #include_next games.
 *
 * Divergence documented on purpose: MB_LEN_MAX is 4 here (the UTF-8
 * maximum). glibc says 16 to cover stateful legacy encodings its own
 * multibyte functions accept; nothing the compiler owns needs more than
 * 4, and a bigger value would only inflate user buffers. */
#ifndef _CGF_LIMITS_H
#define _CGF_LIMITS_H

#define CHAR_BIT __CHAR_BIT__
#define SCHAR_MIN (-__SCHAR_MAX__ - 1)
#define SCHAR_MAX __SCHAR_MAX__
#define UCHAR_MAX (__SCHAR_MAX__ * 2 + 1)

/* Plain char follows the target's signedness (AAPCS64 Linux: unsigned). */
#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN 0
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#endif

#define SHRT_MIN (-__SHRT_MAX__ - 1)
#define SHRT_MAX __SHRT_MAX__
#define USHRT_MAX (__SHRT_MAX__ * 2 + 1)
#define INT_MIN (-__INT_MAX__ - 1)
#define INT_MAX __INT_MAX__
#define UINT_MAX (__INT_MAX__ * 2U + 1U)
#define LONG_MIN (-__LONG_MAX__ - 1L)
#define LONG_MAX __LONG_MAX__
#define ULONG_MAX (__LONG_MAX__ * 2UL + 1UL)
#define LLONG_MIN (-__LONG_LONG_MAX__ - 1LL)
#define LLONG_MAX __LONG_LONG_MAX__
#define ULLONG_MAX (__LONG_LONG_MAX__ * 2ULL + 1ULL)

#define MB_LEN_MAX 4

#endif
