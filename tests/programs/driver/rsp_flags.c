// @file response files feed real flags: FROM_RSP comes from
// tests/fixtures/driver/flags.rsp (which also passes -O2 — stored only,
// pipelines are Phase 7).
// FLAGS: @tests/fixtures/driver/flags.rsp
// EXIT_CODE: 42
int main(void)
{
    return FROM_RSP;
}
