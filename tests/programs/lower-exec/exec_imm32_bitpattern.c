// EXIT_CODE: 0
// A 32-bit bitfield mask may be represented by the folder as a signed value
// outside the mathematical imm32 range even though its low 32 bits are a
// perfectly legal x86 immediate. QBE's Ref initializer exposed this.
typedef struct Ref {
    unsigned type : 3;
    unsigned value : 29;
} Ref;

static unsigned pack(unsigned value)
{
    Ref r = {0};
    r.value = value;
    return *(unsigned *)&r;
}

int main(void)
{
    return pack(7) == 56 ? 0 : 1;
}
