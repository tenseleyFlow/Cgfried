// FLAGS: -fPIC -O1 -S
// Full PIC: an EXTERNAL object goes through the GOT whether or not this
// module defines it, because a definition here can still be interposed by
// another shared object -- reading the local copy when the program means the
// interposed one is a silent wrong answer. Internal linkage is never
// preemptible and stays direct.
//
// Calls are the same rule with a different mechanism: @PLT, including for a
// function this module DEFINES. gcc emits exactly that, and only
// -fno-semantic-interposition relaxes it.
//
// All three spellings were measured against gcc: -fPIC, -fPIE and the
// default each match it (the default matches `gcc -fno-pie`, since upstream
// gcc-8 is no-pie by default and only distro builds flip it).
// ASM_CHECK matching is IN ORDER, so these follow emission order.
// ASM_CHECK: stat_var(%rip)
// ASM_CHECK: ext_var@GOTPCREL(%rip)
// ASM_CHECK: def_var@GOTPCREL(%rip)
// ASM_CHECK: call ext_fn@PLT
// ASM_CHECK: call def_fn@PLT
extern int ext_var;
int def_var = 7;
static int stat_var = 9;
extern int ext_fn(void);

int def_fn(void)
{
    return stat_var;
}

int read_all(void)
{
    return ext_var + def_var + ext_fn() + def_fn();
}
