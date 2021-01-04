#ifndef __SDS_H__
#define __SDS_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SDS_MAX_PREALLOC (1024 * 1024)
extern const char *SDS_NOINIT;

#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>

typedef char *sds;

size_t sdslen(const sds s);
sds sdsempty(void);
sds sdsnewlen(const void *init, size_t initlen);
sds sdsMakeRoomFor(sds s, size_t addlen);
void sdsIncrLen(sds s, ssize_t incr);
void sdsfree(sds s);

#ifdef __cplusplus
}
#endif

#endif  //__SDS_H__