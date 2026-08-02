// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 1
void unused_label_fire(void)
{
    // WARN_CHECK: unused-label label 'unused' defined but not used
unused:
    return;
}
