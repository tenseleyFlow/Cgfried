/* cgfried freestanding <stdalign.h> (C17 7.15). */
#ifndef _CGF_STDALIGN_H
#define _CGF_STDALIGN_H

#ifndef __cplusplus
#define alignas _Alignas
#define alignof _Alignof
#endif

#define __alignas_is_defined 1
#define __alignof_is_defined 1

#endif
