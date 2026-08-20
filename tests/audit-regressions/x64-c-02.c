// RESOLVED(audit): X64-C-02 large static frame sizes are truncated to 32 bits
int large_static_frame(void) {
    volatile char frame[4294967296ULL];
    frame[0] = 1;
    return frame[0];
}
