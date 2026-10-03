// Private, versioned source-buffer format. No process pointers, edits or
// catalogue identities are serialized. Pixel data is losslessly compressed
// by OfflineStore; RAW remains mosaic data with its original camera metadata.
#include "common/dng_opcode.h"
#include "develop/imageop_math.h"
#define SMART_FIELDS(X) \
    X(exif_inited) X(orientation) X(exif_exposure) X(exif_exposure_bias) \
    X(exif_aperture) X(exif_iso) X(exif_focal_length) X(exif_focus_distance) \
    X(exif_crop) X(exif_highlight_preservation) X(exif_correction_type) X(exif_correction_data) \
    X(camera_maker) X(camera_model) X(camera_alias) X(camera_makermodel) X(camera_missing_sample) \
    X(width) X(height) X(p_width) X(p_height) X(crop_x) X(crop_y) X(crop_right) X(crop_bottom) \
    X(loader) X(buf_dsc) X(d65_color_matrix) X(colorspace) X(raw_black_level) \
    X(raw_black_level_separate) X(raw_white_point) X(fuji_rotation_pos) X(pixel_aspect_ratio) \
    X(linear_response_limit) X(wb_coeffs) X(adobe_XYZ_to_CAM) X(usercrop)
#define SMART_MEMBER(field) __typeof__(((dt_image_t *)0)->field) field;
typedef struct {
    char magic[24];
    uint32_t bytes, preview_width, preview_height, bpp, profile_bytes, maps;
    float iscale;
    uint32_t source_flags;
    SMART_FIELDS(SMART_MEMBER)
} oma_smart_header;
#undef SMART_MEMBER
static const char smart_magic[24] = "OMARAW-SMART-281f60c-v1";
#define SMART_FLAGS (DT_IMAGE_LDR | DT_IMAGE_RAW | DT_IMAGE_HDR | DT_IMAGE_4BAYER | DT_IMAGE_S_RAW | DT_IMAGE_HAS_ADDITIONAL_EXIF_TAGS)
static int smart_loader(dt_image_t *image, dt_mipmap_buffer_t *buffer, const char *path) {
    if (!g_str_has_suffix(path, ".omsp")) return 0;
    void *blob = NULL; size_t size = 0;
    if (!oma_smart_read(path, &blob, &size)) return -1;
    oma_smart_header h;
    if (size < sizeof h) { free(blob); return -1; }
    memcpy(&h, blob, sizeof h);
    const size_t pixels = (size_t)h.preview_width * h.preview_height * h.bpp;
    if (memcmp(h.magic, smart_magic, sizeof h.magic) || h.bytes != sizeof h
        || !h.preview_width || !h.preview_height || h.preview_width > 2560 || h.preview_height > 2560
        || h.width < (int)h.preview_width || h.height < (int)h.preview_height
        || h.width > 100000 || h.height > 100000
        || (h.bpp != 2 && h.bpp != 4 && h.bpp != 16)
        || (h.buf_dsc.channels != 1 && h.buf_dsc.channels != 4)
        || (h.buf_dsc.datatype != TYPE_UINT16 && h.buf_dsc.datatype != TYPE_FLOAT)
        || h.bpp != dt_iop_buffer_dsc_to_bpp(&h.buf_dsc)
        || (h.buf_dsc.filters && h.buf_dsc.channels != 1)
        || (!h.buf_dsc.filters && h.bpp != 16)
        || fabsf(h.iscale - (float)h.width / h.preview_width) > .001f
        || !isfinite(h.iscale) || h.iscale < 1.f || h.iscale > 1000.f
        || h.profile_bytes > 16u * 1024u * 1024u || h.maps > 64
        || size < sizeof h + pixels + h.profile_bytes) { free(blob); return -1; }
    // Validate the variable-size gain maps before changing the live image.
    const unsigned char *cursor = (const unsigned char *)blob + sizeof h + pixels + h.profile_bytes;
    const unsigned char *end = (const unsigned char *)blob + size;
    GList *maps = NULL;
    for (uint32_t i = 0; i < h.maps; ++i) {
        uint32_t bytes = 0;
        if ((size_t)(end - cursor) < sizeof bytes) goto invalid;
        memcpy(&bytes, cursor, sizeof bytes); cursor += sizeof bytes;
        if (bytes < sizeof(dt_dng_gain_map_t) || bytes > 16u * 1024u * 1024u || (size_t)(end - cursor) < bytes) goto invalid;
        dt_dng_gain_map_t head; memcpy(&head, cursor, sizeof head);
        const uint64_t count = (uint64_t)head.map_points_v * head.map_points_h * head.map_planes;
        if (count > 4000000 || sizeof head + count * sizeof(float) != bytes) goto invalid;
        maps = g_list_append(maps, g_memdup2(cursor, bytes)); cursor += bytes;
    }
    if (cursor != end) goto invalid;
    dt_image_t allocation = *image;
    allocation.width = h.preview_width; allocation.height = h.preview_height; allocation.buf_dsc = h.buf_dsc;
    void *out = dt_mipmap_cache_alloc(buffer, &allocation);
    if (!out) goto invalid;
    memcpy(out, (const unsigned char *)blob + sizeof h, pixels);
#define SMART_RESTORE(field) memcpy(&image->field, &h.field, sizeof h.field);
    SMART_FIELDS(SMART_RESTORE)
#undef SMART_RESTORE
    image->flags = (image->flags & ~SMART_FLAGS) | (h.source_flags & SMART_FLAGS);
    g_free(image->profile);
    image->profile_size = h.profile_bytes;
    image->profile = h.profile_bytes ? g_memdup2((const unsigned char *)blob + sizeof h + pixels, h.profile_bytes) : NULL;
    g_list_free_full(image->dng_gain_maps, g_free); image->dng_gain_maps = maps;
    buffer->width = h.preview_width; buffer->height = h.preview_height; buffer->iscale = h.iscale;
    free(blob); return 1;
invalid:
    g_list_free_full(maps, g_free); free(blob); return -1;
}
int oma_engine_using_smart_preview(int imgid) {
    if (!g_inited) return 0;
    char path[PATH_MAX] = {0}; gboolean cached = TRUE;
    dt_image_full_path(imgid, path, sizeof path, &cached);
    return g_str_has_suffix(path, ".omsp");
}
int oma_engine_smart_data(int imgid, void **out, size_t *size, int *width, int *height) {
    g_error[0] = 0;
    if (!out || !size || !width || !height || !g_inited) return -1;
    *out = NULL; *size = 0; *width = *height = 0;
    if (source_available(imgid) || oma_engine_using_smart_preview(imgid)) {
        set_error("Reconnect the original or its full offline copy to build a Smart Preview", NULL); return -1;
    }
    dt_mipmap_buffer_t input;
    dt_mipmap_cache_get(&input, imgid, DT_MIPMAP_FULL, DT_MIPMAP_BLOCKING, 'r');
    const dt_image_t *image = dt_image_cache_get(imgid, 'r');
    if (!input.buf || input.width < 1 || input.height < 1 || image->load_status != DT_IMAGEIO_OK) {
        switch (image->load_status) {
        case DT_IMAGEIO_CACHE_FULL:
            set_error("Not enough memory to decode the original for a Smart Preview; try again after closing other photos", NULL); break;
        case DT_IMAGEIO_FILE_NOT_FOUND: case DT_IMAGEIO_IOERROR:
            set_error("The original could not be read; check its drive and file permissions", NULL); break;
        default:
            set_error("The RAW data could not be decoded. This camera or compression mode may be unsupported; a display preview can still be available", NULL); break;
        }
        goto failed;
    }
    if (image->flags & DT_IMAGE_4BAYER) {
        set_error("Smart Previews do not yet support this four-colour sensor; use a full offline copy instead", NULL); goto failed;
    }
    const size_t bpp = dt_iop_buffer_dsc_to_bpp(&image->buf_dsc);
    if ((image->buf_dsc.filters && bpp != 2 && bpp != 4) || (!image->buf_dsc.filters && bpp != 16)) goto failed;
    const float scale = fminf(1.f, 2560.f / MAX(input.width, input.height));
    const int w = MAX(1, (int)(input.width * scale)), h = MAX(1, (int)(input.height * scale));
    const size_t pixels = (size_t)w * h * bpp;
    size_t total = sizeof(oma_smart_header) + pixels + image->profile_size;
    if (image->profile_size > 16u * 1024u * 1024u || g_list_length(image->dng_gain_maps) > 64) goto failed;
    for (GList *l = image->dng_gain_maps; l; l = l->next) {
        const dt_dng_gain_map_t *map = l->data;
        const uint64_t count = (uint64_t)map->map_points_v * map->map_points_h * map->map_planes;
        if (count > 4000000) goto failed;
        total += sizeof(uint32_t) + sizeof(*map) + count * sizeof(float);
    }
    if (total > 128u * 1024u * 1024u) goto failed;
    // Align temporary pixels for SIMD downsampling; the stored blob itself
    // has no alignment contract and is copied into the engine's aligned cache.
    void *small = dt_alloc_aligned(pixels);
    if (!small) goto failed;
    dt_iop_roi_t in = {0, 0, input.width, input.height, 1.f}, dst = {0, 0, w, h, scale};
    if (scale == 1.f) memcpy(small, input.buf, pixels);
    else if (!image->buf_dsc.filters) dt_iop_clip_and_zoom(small, (const float *)input.buf, &dst, &in, image->colorspace != DT_IMAGE_COLORSPACE_NONE);
    else if (image->buf_dsc.filters == 9u && bpp == 2) dt_iop_clip_and_zoom_mosaic_third_size_xtrans(small, (const uint16_t *)input.buf, &dst, &in, w, input.width, image->buf_dsc.xtrans);
    else if (image->buf_dsc.filters == 9u) dt_iop_clip_and_zoom_mosaic_third_size_xtrans_f(small, (const float *)input.buf, &dst, &in, w, input.width, image->buf_dsc.xtrans);
    else if (bpp == 2) dt_iop_clip_and_zoom_mosaic_half_size(small, (const uint16_t *)input.buf, &dst, &in, w, input.width, image->buf_dsc.filters);
    else dt_iop_clip_and_zoom_mosaic_half_size_f(small, (const float *)input.buf, &dst, &in, w, input.width, image->buf_dsc.filters);
    unsigned char *blob = calloc(1, total);
    if (!blob) { dt_free_align(small); goto failed; }
    oma_smart_header header = {0};
    memcpy(header.magic, smart_magic, sizeof smart_magic);
    header.bytes = sizeof header; header.preview_width = w; header.preview_height = h; header.bpp = bpp;
    header.profile_bytes = image->profile_size; header.maps = g_list_length(image->dng_gain_maps);
    header.iscale = (float)input.width / w; header.source_flags = image->flags & SMART_FLAGS;
#define SMART_SAVE(field) memcpy(&header.field, &image->field, sizeof header.field);
    SMART_FIELDS(SMART_SAVE)
#undef SMART_SAVE
    memcpy(blob, &header, sizeof header);
    memcpy(blob + sizeof header, small, pixels); dt_free_align(small);
    unsigned char *cursor = blob + sizeof header + pixels;
    if (image->profile_size) { memcpy(cursor, image->profile, image->profile_size); cursor += image->profile_size; }
    for (GList *l = image->dng_gain_maps; l; l = l->next) {
        const dt_dng_gain_map_t *map = l->data;
        const uint32_t bytes = sizeof(*map) + (uint64_t)map->map_points_v * map->map_points_h * map->map_planes * sizeof(float);
        memcpy(cursor, &bytes, sizeof bytes); cursor += sizeof bytes;
        memcpy(cursor, map, bytes); cursor += bytes;
    }
    *out = blob; *size = total; *width = w; *height = h;
    dt_image_cache_read_release(image); dt_mipmap_cache_release(&input); note_source(imgid); return 0;
failed:
    dt_image_cache_read_release(image); dt_mipmap_cache_release(&input);
    if (!g_error[0]) set_error("Cannot prepare a Smart Preview from this source", NULL);
    return -1;
}
#undef SMART_FIELDS
#undef SMART_FLAGS
