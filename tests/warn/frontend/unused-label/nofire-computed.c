// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void unused_label_conditional(int p)
{
    if (p)
        goto used;
used:
    return;
}
