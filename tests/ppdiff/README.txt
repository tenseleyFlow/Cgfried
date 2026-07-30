Note for Sprint 6: gcc -E respells extended identifier characters as UCNs
(caf\U000000e9) while cgf -E passes raw UTF-8 through. The Sprint 6
harness must normalize UCN spellings before comparing; until then UTF-8
identifier coverage lives in tests/programs/pp/utf8_ident.c (our output).
