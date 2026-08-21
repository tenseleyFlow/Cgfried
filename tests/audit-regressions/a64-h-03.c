// RESOLVED(audit): A64-H-03 large GOT addends trigger an internal compiler error
extern char external_arena[16777217];

char *large_got_addend(void) {
    return external_arena + 16777216;
}
