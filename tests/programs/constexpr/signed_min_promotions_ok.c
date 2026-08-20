// FLAGS: -fsyntax-only
_Static_assert((signed char)-128 / -1 == 128,
               "signed char minimum promotes before division");
_Static_assert((signed char)-128 % -1 == 0,
               "signed char minimum remainder promotes too");
_Static_assert((short)-32768 / -1 == 32768,
               "short minimum promotes before division");
_Static_assert((short)-32768 % -1 == 0, "short minimum remainder promotes too");
