// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void unused_label_goto(void)
{
    goto used;
used:
    return;
}
