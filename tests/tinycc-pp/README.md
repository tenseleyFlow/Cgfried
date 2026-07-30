Imported from TinyCC (tests/pp/), LGPL-2.1 — attribution: the TinyCC
authors, https://repo.or.cz/tinycc.git. Used as a preprocessor conformance
corpus: scripts/tinycc_pp_smoke.sh runs `cgf -E` over each NN.c and
token-compares against NN.expect (whitespace-insensitive). Known-fail cases
are listed in xfail.txt with reasons (each is tracked debt).
