// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_all_paths_define(int condition)
{
    int x;
    if (condition)
        x = 1;
    else
        x = 2;
    return x;
}
