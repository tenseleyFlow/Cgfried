#ifndef CGFRIED_MEMSAFE_H
#define CGFRIED_MEMSAFE_H

/* Ownership contracts understood by Cgfried's memory-safety analysis.
 * They intentionally disappear under other compilers so annotated public
 * headers remain portable to GCC, Clang, and other C implementations. */
#if defined(__CGFRIED__)
#define CGF_RETURNS_OWNED __attribute__((cgf_returns_owned))
#define CGF_TAKES_OWNERSHIP(n) __attribute__((cgf_takes_ownership(n)))
#define CGF_BORROWS(n) __attribute__((cgf_borrows(n)))
#define CGF_RETURNS_BORROWED(n) __attribute__((cgf_returns_borrowed(n)))
#define CGF_NO_ESCAPE(n) __attribute__((cgf_no_escape(n)))
#else
#define CGF_RETURNS_OWNED
#define CGF_TAKES_OWNERSHIP(n)
#define CGF_BORROWS(n)
#define CGF_RETURNS_BORROWED(n)
#define CGF_NO_ESCAPE(n)
#endif

#endif
