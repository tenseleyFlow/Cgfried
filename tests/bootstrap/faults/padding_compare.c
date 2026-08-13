struct Record {
    char tag;
    int value;
};

int compare_object_representation(const struct Record *a,
                                  const struct Record *b)
{
    return memcmp(a, b, sizeof(*a));
}
