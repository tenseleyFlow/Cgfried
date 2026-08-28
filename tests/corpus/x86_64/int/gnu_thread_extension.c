// ENV: CGF_AS=0
// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 7 42 11 1
/* `__thread` and `__extension__`, executed. gcc-verified.
 *
 * `__thread` IS AN ALIAS, and the alias-ness is what this proves: it
 * produces the same AST_SC_THREAD_LOCAL as `_Thread_local`, so nothing
 * downstream can tell which spelling was written. Verified directly by
 * compiling both spellings to assembly and diffing -- BYTE-IDENTICAL, with
 * `.section .tdata,"awT"` and `@tls_object` in each.
 *
 * There are deliberately NO THREADS here. Sprint 51 already proved the
 * threading semantics for `_Thread_local` with a four-thread test, and
 * since the two spellings emit identical code that proof carries over.
 * Spawning threads in tests/corpus would add a pthread dependency to a
 * lane that cross-compiles and runs under qemu, risking a red lane for a
 * reason unrelated to the feature.
 *
 * `__extension__` is SWALLOWED. Its only job is suppressing the pedwarns
 * its operand would provoke, and a construct that pedwarns is one we accept
 * or reject on its own merits. Both declaration and expression positions
 * appear. The expression cases also pin both ambiguity boundaries: a block
 * item beginning directly with the marker is not a declaration, and a `(`
 * followed by the marker is not the start of a cast type-name.
 *
 * In tests/corpus rather than tests/programs on purpose -- tests/corpus is
 * outside FE_FUZZ_CORPUS, so an executable fixture here costs no digest
 * repin and no sanitized 100k run.
 *
 * CGF_AS=0 because the BUNDLED assembler has neither the %fs: segment
 * override nor @tpoff on x86 (TLS-004); the driver refuses by name rather
 * than letting afs-as reject correct assembly. It is INERT on arm64, where
 * the corpus lane routes through CGF_AS_PATH and an explicit path beats a
 * mode -- so this fixture still executes through the bundled assembler
 * there, which is where arm64 TLS relocations get their coverage. */
extern int printf(const char *, ...);

__thread int slot = 7;

struct box {
    int value;
};

__extension__ static int read_slot(void)
{
    return slot;
}

int main(void)
{
    int first = read_slot();
    long long wide = __extension__ 1LL;
    int e;

    slot = 42;
    e = __extension__(first + 1);
    (__extension__(e += 1));
    __extension__(e += 1);
    e += (__extension__ ((struct box){1}).value);
    printf("%d %d %d %lld\n", first, slot, e, wide);
    return 0;
}
