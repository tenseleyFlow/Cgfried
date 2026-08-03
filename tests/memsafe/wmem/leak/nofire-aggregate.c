// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
struct box {
    void *item;
};
void take_box(struct box *);
void nofire_aggregate(void)
{
    struct box b;
    b.item = malloc(8);
    take_box(&b);
}
