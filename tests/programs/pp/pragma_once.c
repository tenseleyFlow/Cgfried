// FLAGS: -E -Itests/fixtures/once
// CHECK: once_body
// CHECK: main_end
// (exactly one once_body: the 2nd/3rd/4th includes are the same (dev,ino)
//  reached via a repeat, a symlink, and a hardlink)
#include <oh.h>
#include <oh.h>
#include <oh_sym.h>
#include <oh_hard.h>
main_end
