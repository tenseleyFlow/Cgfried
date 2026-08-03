char *strcpy(char *, const char *);
int snprintf(char *, unsigned long, const char *, ...);

#define snprintf(...) ((void)0)

void macro_replacement_api(const char *source)
{
    char destination[8];

    strcpy(destination, source);
}
