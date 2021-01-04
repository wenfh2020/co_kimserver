#ifndef __KIM_HASH_H__
#define __KIM_HASH_H__

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t hash_fnv1_64(const char* key, size_t len);
uint32_t hash_fnv1a_64(const char* key, size_t len);
uint32_t murmur3_32(const char* key, uint32_t len, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif  //__KIM_HASH_H__