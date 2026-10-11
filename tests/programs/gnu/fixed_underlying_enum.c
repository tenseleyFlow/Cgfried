// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 2
// EXIT_CODE: 0
// OPT_EQ: all

/* CAMP-ZSTD-002: current Apple SDK headers publish this C23 definition form
 * unconditionally. The fixed type controls layout, conversions, and ABI even
 * when every enumerator would otherwise fit in int. */
typedef unsigned long u64;

typedef enum PacketKind : u64 {
    PACKET_EMPTY,
    PACKET_LARGE = 1UL << 40,
    PACKET_NEXT
} PacketKind;

enum Flag : _Bool { FLAG_CLEAR, FLAG_SET };

_Static_assert(sizeof(PacketKind) == sizeof(u64), "fixed enum size");
_Static_assert(_Alignof(PacketKind) == _Alignof(u64), "fixed enum alignment");
_Static_assert(__builtin_types_compatible_p(PacketKind, u64),
               "fixed compatible type");
_Static_assert(sizeof(enum Flag) == sizeof(_Bool), "bool fixed enum size");

static PacketKind round_trip(PacketKind kind)
{
    return kind;
}

int main(void)
{
    PacketKind value = round_trip(PACKET_NEXT);

    if ((u64)value != (1UL << 40) + 1)
        return 1;
    if ((enum Flag)7 != FLAG_SET)
        return 2;
    return 0;
}
