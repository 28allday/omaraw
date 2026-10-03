#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int oma_offline_resolve(const char *original, char *out, size_t size);
int oma_smart_read(const char *file, void **data, size_t *size);
void oma_offline_configure(const char *library);
#ifdef __cplusplus
}
#endif
