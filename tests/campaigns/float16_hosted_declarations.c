#ifdef CGF_APPLE_SDK
#include <math.h>
#endif

typedef _Float16 cgf_hosted_half;

extern _Float16 cgf_hosted_function(_Float16);
extern _Float16 cgf_hosted_object;
static _Float16 *cgf_hosted_pointer;

_Static_assert(sizeof(cgf_hosted_half) == 2, "_Float16 size");
_Static_assert(_Alignof(cgf_hosted_half) == 2, "_Float16 alignment");
_Static_assert(!__builtin_types_compatible_p(cgf_hosted_half, float),
               "_Float16 is distinct from float");

int cgf_float16_hosted_declarations(void)
{
    return cgf_hosted_pointer == 0;
}
