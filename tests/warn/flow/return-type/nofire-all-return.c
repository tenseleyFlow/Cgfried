// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_all_return(int condition)
{
    if (condition)
        return 1;
    return 2;
}
