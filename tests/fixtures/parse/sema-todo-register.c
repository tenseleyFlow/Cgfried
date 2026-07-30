/* A function definition may not be `register` (6.9.1p4). We PARSE it;
   the constraint is sema. Kept out of the accept corpus so parse_diff
   compares like with like until Sprint 12. */
register int fn(int q) { return q; }
