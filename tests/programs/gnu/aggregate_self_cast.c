// FLAGS: -std=gnu17
/* GNU aggregate self-casts are identity conversions. Exercise both record
 * kinds and typedef spellings through whole-object return and assignment. */
struct Pair {
    int x;
    int y;
};
typedef struct Pair Pair;

union Word {
    unsigned value;
    int signed_value;
};
typedef union Word Word;

static Pair cast_pair(Pair value)
{
    return (struct Pair)value;
}

static Word cast_word(Word value)
{
    return (Word)value;
}

int main(void)
{
    Pair pair = {17, 29};
    Word word;

    pair = (Pair)cast_pair(pair);
    word.value = 0x12345678u;
    word = (union Word)cast_word(word);
    return pair.x == 17 && pair.y == 29 && word.value == 0x12345678u ? 0 : 1;
}
