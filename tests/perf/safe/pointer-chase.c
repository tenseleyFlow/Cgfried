void *malloc(unsigned long);
void free(void *);

typedef struct Node {
    struct Node *next;
    unsigned long value;
} Node;

int main(void)
{
    Node *nodes = malloc(4096 * sizeof(Node));
    unsigned long round;
    unsigned long i;
    unsigned long sum = 0;
    Node *p;

    if (!nodes)
        return 2;
    for (i = 0; i < 4096; i++) {
        nodes[i].next = &nodes[(i + 1) & 4095];
        nodes[i].value = i;
    }
    p = nodes;
    for (round = 0; round < 10000; round++)
        for (i = 0; i < 4096; i++) {
            sum += p->value;
            p = p->next;
        }
    free(nodes);
    return sum == 83865600000UL ? 0 : 3;
}
