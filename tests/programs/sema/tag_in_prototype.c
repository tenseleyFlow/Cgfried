// FLAGS: -fsyntax-only
// WARNING_EXPECTED: 'struct Hidden' declared inside parameter list will not be visible outside of this definition or declaration
// A tag first named inside a parameter list gets PROTOTYPE scope and dies
// at the ')', so no caller can ever name that type — the parameter is
// unusable. gcc warns rather than erroring because the code still has a
// meaning; matching the wording matters because this is a real bug in
// real code.
void g(struct Hidden *p);
int x = 0;
