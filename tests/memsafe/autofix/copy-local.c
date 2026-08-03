static char *strcpy(char *destination, const char *source)
{
    char *result = destination;

    while ((*destination++ = *source++) != 0)
        ;
    return result;
}

void call_project_function(const char *source)
{
    char destination[8];

    strcpy(destination, source);
}
