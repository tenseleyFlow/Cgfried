// MEMORY-class argument: the pointee is copied onto the outgoing stack
// (inline word copy until Sprint 24's memcpy).
// FLAGS: -emit-mir
// MIR_CHECK: store.q rax, [rsp]
// MIR_CHECK: store.q rax, [rsp+24]
// MIR_CHECK: call [rip @eat]
struct Big { long a, b, c, d; };
long eat(struct Big s);
long f(void) { struct Big s = {1, 2, 3, 4}; return eat(s); }
