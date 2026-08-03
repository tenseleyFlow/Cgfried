char *strcpy(char *, const char *);
char *strncpy(char *, const char *, unsigned long);
char *strcat(char *, const char *);
int sprintf(char *, const char *, ...);
int snprintf(char *, unsigned long, const char *, ...);

void bounded_copies(const char *src)
{
    char name[64];

    strcpy(name, src);
    sprintf(name, "%s", src);
    strcat(name, src);
    strncpy(name, src, sizeof name);
}

void unknown_copy(char *dst, const char *src)
{
    strcpy(dst, src);
}
