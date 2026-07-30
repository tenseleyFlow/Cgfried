// FLAGS: -fsyntax-only
// TU-BREAK
int main(void) { return 0; }
// TU-BREAK
int main(int argc, char **argv) { (void)argc; (void)argv; return 0; }
// TU-BREAK
int main(int argc, char *argv[]) { (void)argc; (void)argv; return 0; }
