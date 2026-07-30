// FLAGS: -E
// ERROR_EXPECTED: #elif after #else
#if 0
#else
#elif 1
#endif
