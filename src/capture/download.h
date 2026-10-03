#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Persist and SHA-256 verify camera bytes, then publish without replacing an
// existing file. 0 = durable, 1 = saved but directory sync failed (keep camera
// copy), -1 = failed. A nonempty out_path always names a verified saved file.
int oma_capture_save(const void *bytes, size_t size, const char *directory,
                     const char *stem, const char *extension, char *out_path, int out_size,
                     char *error, int error_size);
#ifdef __cplusplus
}
#endif
