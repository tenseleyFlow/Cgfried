// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
union Value {
    void *pointer;
    unsigned long bits;
};

int consume(union Value value)
{
    return value.bits != 0;
}
