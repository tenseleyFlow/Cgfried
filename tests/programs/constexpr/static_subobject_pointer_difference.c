// FLAGS: -std=gnu17
// EXIT_CODE: 0

struct Record {
    char head;
    char member;
    char tail[3];
};

static struct Record record;
static long decayed_member_delta = record.tail - &record.member;
static long explicit_member_delta = (char *)&record.tail[2] - (char *)&record;
static int pooled_string_delta = &"Cgfried"[5] - &"Cgfried"[1];

int main(void)
{
    if (decayed_member_delta != 1)
        return 1;
    if (explicit_member_delta != 4)
        return 2;
    if (pooled_string_delta != 4)
        return 3;
    return 0;
}
