// FLAGS: -std=c17
// ENV: CGF_VERIFY_AFTER_EACH=1
// OPT_EQ: all

int main(void)
{
    unsigned i;
    unsigned safe = 0;
    unsigned overshoot = 0;

    for (i = 0; i < 12; i += 3) {
        if (i + 3 <= 12)
            safe++;
    }
    for (i = 0; i < 10; i += 3) {
        if (i + 3 < 10)
            overshoot++;
    }

    return safe == 4 && overshoot == 3 ? 0 : 1;
}
