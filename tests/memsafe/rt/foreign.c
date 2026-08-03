typedef struct ForeignFile ForeignFile;

void *mmap(void *, unsigned long, int, int, int, long);
int munmap(void *, unsigned long);
char *getenv(const char *);
ForeignFile *tmpfile(void);
int fputs(const char *, ForeignFile *);
int fclose(ForeignFile *);

int main(void)
{
    char *page = mmap((void *)0, 4096, 3, 0x22, -1, 0);
    char *path;
    ForeignFile *stream;

    if (page == (void *)-1)
        return 2;
    page[0] = 41;
    if (page[0] != 41)
        return 3;
    if (munmap(page, 4096) != 0)
        return 4;

    path = getenv("PATH");
    if (path && path[0] == '\0')
        return 5;

    stream = tmpfile();
    if (!stream)
        return 6;
    if (fputs("foreign FILE buffer\n", stream) < 0)
        return 7;
    if (fclose(stream) != 0)
        return 8;
    return 0;
}
