/* libcgf_rt: the ELF executable's DSO identity token.
 *
 * glibc implements atexit() in libc_nonshared.a as a call to
 * __cxa_atexit(..., &__dso_handle).  GCC normally supplies this object from
 * crtbegin.o, but Cgfried intentionally does not import GCC's constructor,
 * TM-clone, and frame-registration runtime.  Supply the one plain-C ABI
 * object that hosted libc actually requires instead.  The self-pointer is
 * GCC's executable convention; hidden visibility matches libc's hidden
 * reference and prevents preemption from changing the identity token.
 *
 * The archive is lazy.  The driver groups libcgf_rt with libc so the
 * undefined reference introduced by libc_nonshared causes this member to be
 * selected on the next archive scan. */
#if defined(__ELF__)
/* ELF visibility has no ISO C spelling.  This is an ABI property of the
 * runtime object, not a compiler implementation convenience. */
__attribute__((visibility("hidden"))) /* check_bans allow: ELF ABI */
void *__dso_handle = &__dso_handle;
#endif
