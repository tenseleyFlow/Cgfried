char *strcpy(char *, const char *);
int snprintf(char *, unsigned long, const char *, ...);

void shadowed_replacement_api(const char *source)
{
    char destination[8];
    int snprintf = 0;

    strcpy(destination, source);
    (void)snprintf;
}
