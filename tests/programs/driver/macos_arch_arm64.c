// FLAGS: --target=arm64-macos -arch arm64 -fsyntax-only
/* Darwin CMake emits the native Apple driver spelling even when
 * CMAKE_OSX_ARCHITECTURES is empty.  Cgfried accepts the one architecture it
 * can truthfully produce for its arm64-macos target. */
int main(void)
{
    return 0;
}
