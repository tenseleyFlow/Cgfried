// Mixing bare and target-qualified CHECKs makes the in-order match sequence
// depend on which target is running, so it is a CONFIG ERROR.
// CHECK: always
// CHECK(arm64-linux): sometimes
int main(void)
{
    return 0;
}
