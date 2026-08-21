#ifndef CGF_RT_CGF_SAFE_INTERNAL_H
#define CGF_RT_CGF_SAFE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

_Noreturn void cgf_safe_fail_free(const char *reason, uint32_t site,
                                  uint64_t size);
_Noreturn void cgf_safe_fail_null(size_t access_size, int kind,
                                  uint32_t site_id);
_Noreturn void cgf_safe_fail_access(const char *reason, uintptr_t addr,
                                    uintptr_t base, size_t access_size,
                                    int kind, uint32_t access_site,
                                    uint32_t alloc_site, uint64_t alloc_size,
                                    int freed, int bad_canary);
_Noreturn void cgf_safe_fail_transform(const char *reason, uint32_t access_site,
                                       uint32_t alloc_site, uint64_t alloc_size,
                                       int freed, int bad_canary);

void cgf_safe_check_derive(const void *origin, int64_t offset,
                           const void *derived, uint32_t site_id);
void cgf_safe_check_round_trip(const void *origin, const void *derived,
                               uint32_t site_id);
void cgf_safe_check_index(const void *origin, uint64_t index,
                          uint64_t element_size, uint32_t flags,
                          const void *derived, uint32_t site_id);

#endif
