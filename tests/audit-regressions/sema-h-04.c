// RESOLVED(audit): SEMA-H-04 no-linkage and external-linkage declarations are merged
void automatic_then_external(void) {
    int value;
    extern int value;
    value = 1;
}

void external_then_automatic(void) {
    extern int value;
    int value;
    value = 2;
}
