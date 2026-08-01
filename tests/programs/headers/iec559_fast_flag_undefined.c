// FLAGS: -ffast-math -fsyntax-only
#ifdef __STDC_IEC_559__
#error "fast math must not claim Annex F conformance"
#endif

int probe(void)
{
    return 0;
}
