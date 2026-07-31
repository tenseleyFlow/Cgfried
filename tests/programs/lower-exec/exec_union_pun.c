// C17 6.5.2.3 fn.95: reading another union member reinterprets the
// bytes — Sprint 32's alias analysis must treat union memory as one
// blob. (Becomes an OPT_EQ tripwire the moment Sprint 30 lands the
// directive: re-tag this fixture `// OPT_EQ: all` then.)
// EXIT_CODE: 63
union P {
    float f;
    unsigned u;
};
int main(void)
{
    union P p;
    p.f = 1.0f;
    return (int)(p.u >> 24); /* 0x3f800000 >> 24 = 0x3f = 63 */
}
