// FLAGS: -fsyntax-only
// WARNING_EXPECTED: reserved for the implementation
// WARN_COUNT: 2
// Priorities 0..100 are reserved for the implementation. gcc WARNS and honours
// the request rather than rejecting it, so this compiles.
//
// A REGISTRY SUPPLEMENT, and the reason is worth keeping: gcc 8 emits this
// warning unconditionally, with no flag to silence it, and gcc 9 gave it the
// `-Wprio-ctor-dtor` spelling used here. Every controllable diagnostic in this
// compiler needs a registry id, so taking the modern name is what makes
// `-Wno-prio-ctor-dtor` work at all. The gcc 8 differential sees a warning
// with no group where we carry one; that is the documented divergence.
//
// WARN_CHECK is LINE-ANCHORED -- the warning must land on the line directly
// below the directive -- so each sits above its own site.
// WARN_CHECK: prio-ctor-dtor constructor priorities from 0 to 100 are reserved
__attribute__((constructor(50))) static void early(void)
{
}

// WARN_CHECK: prio-ctor-dtor destructor priorities from 0 to 100 are reserved
__attribute__((destructor(0))) static void late(void)
{
}

/* 101 is the first unreserved priority and must stay silent -- WARN_COUNT
 * above is what makes that a real assertion rather than an absence nobody
 * checks. */
__attribute__((constructor(101))) static void fine(void)
{
}
