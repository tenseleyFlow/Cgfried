// FLAGS: -fsyntax-only
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <cgfried/memsafe.h>

CGF_RETURNS_OWNED void *make_owned(void);
void take_owned(void *) CGF_TAKES_OWNERSHIP(1);
void borrow_value(const void *) CGF_BORROWS(1);
CGF_RETURNS_BORROWED(1) void *borrowed_result(void *);
void retain_nothing(void *) CGF_NO_ESCAPE(1);
