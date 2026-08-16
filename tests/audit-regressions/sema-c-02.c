// XFAIL(audit): SEMA-C-02 AAPCS64 zero-width bitfields fail to raise record alignment
struct aligned_by_zero_width_bitfield {
    long : 0;
    int value;
};
