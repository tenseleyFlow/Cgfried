struct Record { int live; char padding[3]; };
int compare(const void *, const void *);
void seeded_sins(void *base, unsigned long count, struct Record *record,
                 void *pointer, void *stream)
{
    qsort(base, count, sizeof(void *), compare);
    fprintf(stream, "%p", pointer);
    fprintf(stream, "%lu", (uintptr_t)pointer);
    fwrite(&record, sizeof(record), 1, stream);
    srand(58);
    strcoll("a", "b");
    readdir((void *)0);
}
