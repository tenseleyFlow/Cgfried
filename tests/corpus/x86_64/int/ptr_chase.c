// Pointer loads through a hand-built list in an array.
// EXIT_CODE: 15
struct Node {
    int val;
    struct Node *next;
};
int main(void)
{
    struct Node n[5];
    int i, s = 0;
    struct Node *p;
    for (i = 0; i < 5; i++) {
        n[i].val = i + 1;
        n[i].next = i < 4 ? &n[i + 1] : 0;
    }
    for (p = &n[0]; p; p = p->next)
        s += p->val;
    return s;
}
