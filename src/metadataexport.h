#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Encoder metadata, prepared before AVIF/JXL are written. Buffers are malloc
// allocated and freed by the C bridge after the writer returns.
int oma_export_metadata(const void *exif, int exif_size, const char *xmp, int flags,
                        const char *description_json, void **out_exif, int *out_size,
                        char **out_xmp, char *error, int error_size);
#ifdef __cplusplus
}
#endif
