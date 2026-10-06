/* Native-Apple-only boundary: <mach/message.h> brackets these ABI records in
 * #pragma pack(push, 4).  The header's own xnu_static_assert_struct_size
 * checks must remain enabled; these explicit assertions make the two layouts
 * that first exposed the compiler gap visible in the fixture as well. */
#include <mach/message.h>

_Static_assert(sizeof(mach_msg_context_trailer_t) == 60,
               "mach_msg_context_trailer_t ABI size");
_Static_assert(sizeof(mach_msg_mac_trailer_t) == 68,
               "mach_msg_mac_trailer_t ABI size");

struct cgf_after_mach_message {
    char lead;
    long long value;
};
_Static_assert(sizeof(struct cgf_after_mach_message) == 16,
               "mach/message.h restores default packing");
_Static_assert(_Alignof(struct cgf_after_mach_message) == 8,
               "mach/message.h restores default alignment");

int cgf_arm64_macos_pragma_pack_sdk(void)
{
    return 0;
}
