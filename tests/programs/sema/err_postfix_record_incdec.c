// ERROR_EXPECTED: invalid operand of type 'struct P' to '++'
// ERROR_EXPECTED: invalid operand of type 'struct P' to '--'
struct P {
    int member;
};

void reject_record_postfix(void)
{
    struct P value;

    value++;
    value--;
}
