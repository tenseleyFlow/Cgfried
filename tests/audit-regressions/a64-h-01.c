// XFAIL(audit): A64-H-01 TLS tentative definitions are emitted as COMMON symbols
_Thread_local int tls_counter;

int read_tls_counter(void) {
    return tls_counter;
}
