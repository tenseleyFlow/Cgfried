#if 1 + 2 * 3 == 7
c1_yes
#endif
#if -1 < 0u
c2_wrong
#else
c2_unsigned_infection
#endif
#if 'A' == 65 && 'ab' == (('a' << 8) | 'b')
c3_chars
#endif
#if (1 << 10) == 1024 && (0xFF & 0x0F) == 0x0F
c4_bits
#endif
#if 0
#elif 3 > 2
c5_elif
#else
c5_else
#endif
#ifndef NEVER_DEF
c6_ifndef
#endif
#if defined(NEVER_DEF) || (10 % 3 == 1 ? 1 : 0)
c7_ternary
#endif
#if -7 / 2 == -3 && -7 % 2 == -1
c8_trunc_division
#endif
#line 500
#if 1
c9_after_line
#endif
