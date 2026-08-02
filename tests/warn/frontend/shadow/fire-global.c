// FLAGS: -fsyntax-only -Wshadow
// WARN_COUNT: 1
int shadowed_global;
void shadow_global(void)
{
    // WARN_CHECK: shadow declaration of 'shadowed_global' shadows
    int shadowed_global = 1;
    (void)shadowed_global;
}
