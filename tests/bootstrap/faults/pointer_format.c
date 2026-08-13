int fprintf(void *, const char *, ...);

void emit_pointer(void *stream, void *pointer)
{
    fprintf(stream, "%p", pointer);
}
