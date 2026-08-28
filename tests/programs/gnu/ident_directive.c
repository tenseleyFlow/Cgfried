// FLAGS: -std=gnu17
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): {{\.section[ \t]+\.comment,"",@progbits}}
// ASM_CHECK(x86_64-linux-gnu): {{\.byte[ \t]+67}}
// ASM_CHECK(x86_64-linux-gnu): {{\.byte[ \t]+71}}
// ASM_CHECK(arm64-linux): {{\.section[ \t]+\.comment,"",@progbits}}
// ASM_CHECK(arm64-linux): {{\.byte[ \t]+67}}
// ASM_CHECK(arm64-linux): {{\.byte[ \t]+71}}
#define IDENT_TEXT "CGF ident"
#ident IDENT_TEXT
#sccs "second ident"

int main(void)
{
    return 0;
}
