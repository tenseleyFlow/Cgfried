// -MT is verbatim, -MQ is Make-quoted; both may repeat and join with
// spaces in the target position.
// FLAGS: -E -MM -MT plain -MQ dollar$sign
// IR_CHECK: plain dollar$$sign: tests/programs/driver/dep_mq_quote.c
int main(void)
{
    return 0;
}
