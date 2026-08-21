// FLAGS: -fsyntax-only
_Static_assert(-(-2147483647) == 2147483647,
               "one above int minimum remains representable");
_Static_assert(-(-9223372036854775807LL) == 9223372036854775807LL,
               "one above long long minimum remains representable");
_Static_assert(-((signed char)-128) == 128,
               "narrow signed operands promote before unary minus");
_Static_assert(-(1u << 31) == (1u << 31),
               "unsigned unary minus wraps modulo the target width");
_Static_assert(~0 == -1, "adjacent unary complement remains valid");
_Static_assert(+(-2147483647 - 1) == (-2147483647 - 1),
               "adjacent unary plus does not overflow");
