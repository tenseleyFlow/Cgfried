// RESOLVED(audit): A64-H-02 large TLS addends are emitted as unencodable immediates
static _Thread_local char tls_arena[8192];

char *tls_offset(void) {
    return tls_arena + 5000;
}
