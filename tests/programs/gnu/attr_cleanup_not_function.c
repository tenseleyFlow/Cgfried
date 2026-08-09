// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: cleanup argument not a function
// Three shapes, ONE message, which is gcc's own conflation and worth keeping:
// in this position the only thing a name can legally be is a function, so an
// undeclared identifier reports this rather than the ordinary
// undeclared-identifier error.
//
// The function POINTER is the row that matters. Accepting it would emit an
// indirect call through whatever the pointer held at scope exit, which is not
// what the attribute means -- and it is the one shape a careless
// implementation accepts, because a pointer-to-function does have a callable
// type.
void (*fp)(int *);
int not_a_function;

void a_pointer(void)
{
    int a __attribute__((cleanup(fp))) = 1;

    (void)a;
}

void undeclared(void)
{
    int a __attribute__((cleanup(nope))) = 1;

    (void)a;
}

void an_object(void)
{
    int a __attribute__((cleanup(not_a_function))) = 1;

    (void)a;
}
