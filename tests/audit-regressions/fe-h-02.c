// RESOLVED(audit): FE-H-02 nested K&R identifier list escapes the definition-only constraint
typedef int T;

void f(void)
{
    T T, (*p)(T);
}
