#define CGF_RETURNS_OWNED banana
#define CGF_TAKES_OWNERSHIP(n) banana

void *malloc(unsigned long);
void *fake_contract(void);

void *fake_contract(void)
{
    return malloc(8);
}
