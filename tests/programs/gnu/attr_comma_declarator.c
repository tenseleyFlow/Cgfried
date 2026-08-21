// FLAGS: -fsyntax-only -std=gnu17
/* A prefix attribute after a comma belongs only to the declarator that
 * follows it. The declaration-wide prefix still reaches every sibling. */
int a, __attribute__((weak)) b, c;
__attribute__((used)) int x, y;

int main(void)
{
    return a + b + c + x + y;
}
