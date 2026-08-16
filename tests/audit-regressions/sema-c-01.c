// XFAIL(audit): SEMA-C-01 signed-minimum division by minus one crashes constant evaluation
enum { DIV_OVERFLOW = (-9223372036854775807LL - 1) / -1 };
