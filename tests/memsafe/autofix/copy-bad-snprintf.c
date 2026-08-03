char *strcpy(char *, const char *);
int snprintf(char *, int);

void incompatible_replacement_api(const char *source)
{
    char destination[8];

    strcpy(destination, source);
}
