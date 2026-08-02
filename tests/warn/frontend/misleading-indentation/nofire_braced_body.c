// FLAGS: -fsyntax-only -Wmisleading-indentation
// WARN_COUNT: 0
void braced_if(int condition, int *value)
{
    if (condition) {
        *value = 1;
        *value = 2;
    }
}
