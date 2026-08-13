struct Record { int value; char padding[3]; };
unsigned long fwrite(const void *, unsigned long, unsigned long, void *);

void emit_object_representation(void *stream)
{
    struct Record record = { 1, { 0, 0, 0 } };
    fwrite(&record, sizeof(record), 1, stream);
}
