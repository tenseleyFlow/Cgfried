void *malloc(unsigned long);

typedef struct Item {
    int value;
} Item;

void allocation_sizes(unsigned long count)
{
    Item *p = malloc(sizeof(p));
    Item **q = malloc(count * sizeof(Item));
    Item *exact = malloc(sizeof(Item));
    Item *many = malloc(sizeof(Item[4]));
    Item *with_tail = malloc(sizeof(Item[4]) + count);
    void *opaque = malloc(sizeof(opaque));
    char *bytes = malloc(count);

    (void)p;
    (void)q;
    (void)exact;
    (void)many;
    (void)with_tail;
    (void)opaque;
    (void)bytes;
}
