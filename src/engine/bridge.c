#include "bridge.h"
#include "../imagematchmath.h"
#include "../creativeprofiledata.h"
#include "dng_gain.h"
#include "../colourbridge.h"
#include "../texturebridge.h"
#include "../metadataexport.h"
#include "../offlinebridge.h"
#include "common/exif.h"

#include "common/darktable.h"
#include "common/film.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/introspection.h"
#include "common/history.h"
#include "common/database.h"
#include "common/mipmap_cache.h"
#include "common/iop_order.h"
#include "common/ras2vect.h"
#include <sqlite3.h>
#include "common/colorspaces.h"
#include "control/conf.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/blend.h"
#include "develop/masks.h"
#include "develop/pixelpipe_hb.h"
#include "develop/tiling.h"
#include "imageio/imageio_common.h"
#include "imageio/imageio_module.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include "../chainbridge.h"

static gboolean g_inited = FALSE;
static void (*g_set_export_xmp)(const char *) = NULL;
static dt_develop_t g_dev;
static int g_dev_imgid = -1;
static void cleanup_comparisons(void);
static void clear_comparison_preview(int imgid);
static char g_error[512];
static char *g_argv_store[32];
static GMutex g_preview_mutex;
static dt_dev_pixelpipe_t *g_preview_pipe = NULL;
static int (*g_preview_cancelled)(void *) = NULL;
static void *g_preview_request = NULL;
static void clear_preview_cache(void);
static gboolean g_reduced_previews = FALSE;
// darktable's export default: downsample right after demosaic so the rest of
// the pipe runs at output size, as the fitted preview and the reference
// renderer already do. Full resolution processes everything at source size.
static gboolean g_export_full_resolution = FALSE;
static gboolean g_export_overwrite = FALSE;
// Set by the patched engine's approximate-demosaic hook, when present.
static void (*g_set_approx_demosaic)(gboolean) = NULL;
// Set by the patched engine's colour-chain hook, when present.
static void (*g_set_chain)(oma_chain_stage_fn) = NULL;
// Set by oma_engine_begin_edit: the next step recorded is a step of its own
// (see oma_add_history_item); the engine function that forces one, if any.
static gboolean g_new_step = FALSE;
static void (*g_add_new_step)(dt_develop_t *, dt_iop_module_t *, gboolean) = NULL;
static gboolean g_chain_enabled = FALSE;
static void clear_overviews(int imgid);
// Decoded full images, bounded by bytes rather than the engine's entry count
// (see "decoded sources" below).
static GQueue g_sources = G_QUEUE_INIT;
static size_t g_source_budget = 0;
static void trim_sources(int keep_imgid);
static void note_source(int imgid);
static size_t resident_source_bytes(int imgid);
static char *preview_source_key(int imgid);
static GHashTable *g_source_versions = NULL;
static int texture_preview(const float *, float *, int, int, float, float, double);
static int source_available(int imgid);
static int render_preview(int, int, int, int, int, float **, int *, int *);
static int smart_loader(dt_image_t *, dt_mipmap_buffer_t *, const char *);
static int geometry_param_set(int imgid, const char *field, float value);

static void set_error(const char *fmt, const char *a) {
    snprintf(g_error, sizeof g_error, fmt, a ? a : "");
}

const char *oma_engine_error(void) { return g_error; }
const char *oma_engine_version(void) { return darktable_package_version; }
void oma_engine_free(void *p) { free(p); }

// Internal pixelpipes use the native profile at full runtime precision. All
// outward-facing floats use the same serialized linear profile as Qt. Reading
// these colourants directly avoids mislabelling through ICC rounding first.
static int interchange_image(float *pixels, int w, int h) {
    const dt_colorspaces_color_profile_t *profile =
        dt_colorspaces_get_profile(DT_COLORSPACE_LIN_REC709, "", DT_PROFILE_DIRECTION_OUT);
    if (!profile || !profile->profile) { set_error("missing native Rec.709 profile", NULL); return -1; }
    const cmsCIEXYZ *r = cmsReadTag(profile->profile, cmsSigRedColorantTag);
    const cmsCIEXYZ *g = cmsReadTag(profile->profile, cmsSigGreenColorantTag);
    const cmsCIEXYZ *b = cmsReadTag(profile->profile, cmsSigBlueColorantTag);
    if (!r || !g || !b) { set_error("missing native Rec.709 matrix", NULL); return -1; }
    const double matrix[] = {r->X,g->X,b->X, r->Y,g->Y,b->Y, r->Z,g->Z,b->Z};
    return oma_colour_interchange(pixels, w, h, matrix, g_error, sizeof g_error);
}

// The engine's rasterfile module looks for OmaRAW's managed masks where the
// service says (an engine without the hook uses its own library's folder).
static void set_masks_dir_for(const char *library) {
    void (*set_masks)(const char *) = dlsym(RTLD_DEFAULT, "dt_omaraw_set_masks_dir");
    gchar *dir = g_path_get_dirname(library);
    gchar *masks = g_build_filename(dir, "masks", NULL);
    if(set_masks) set_masks(masks);
    gchar *repairs=g_build_filename(dir,"repairs",NULL);
    dt_conf_set_string("plugins/darkroom/omarawrepair/directory",repairs);
    g_free(repairs);
    g_free(masks); g_free(dir);
}

int oma_engine_init(const char *prefix, const char *configdir, const char *cachedir, const char *library) {
    if (g_inited) {
#ifdef _OPENMP
        // OpenMP's team size belongs to the calling thread. A replacement
        // engine worker must inherit the count chosen by dt_init: per-thread
        // scratch buffers use that count even when this thread has a larger
        // OpenMP default (for example after a profiling --threads override).
        omp_set_num_threads(darktable.num_openmp_threads);
#endif
        // A later service in the same process may own a different library:
        // its offline copies and managed masks live beside that one.
        oma_offline_configure(library);
        set_masks_dir_for(library);
        return 0;
    }
    g_error[0] = 0;
    char *datadir = g_strdup_printf("%s/share/darktable", prefix);
    char *moduledir = g_strdup_printf("%s/lib/darktable", prefix);
    g_mkdir_with_parents(configdir, 0700);
    g_mkdir_with_parents(cachedir, 0700);
    // Keep owning pointers separate: dt_init compacts/NULLs its argv and
    // retains argv[0] as the process name until engine shutdown.
    const char *args[] = {
        "omaraw",
        "--datadir", datadir,
        "--moduledir", moduledir,
        "--configdir", configdir,
        "--cachedir", cachedir,
        "--library", library,
        // Never write darktable-style sidecars next to the user's files;
        // OmaRAW owns sidecar policy (brief §11).
        "--conf", "write_sidecar_files=never",
        "--conf", "ui_last/import_apply_metadata=false",
        "--conf", "opencl=false",
        "--conf", "resourcelevel=default",
        // The reduced mosaic that serves fitted previews of large sources
        // (see render_preview): darktable's 4096x2560 box covers a fitted
        // view on a 4K display. Needs the preview-demosaic engine patch.
        "--conf", "omaraw_preview_mip=6",
        "--disable-opencl",
        NULL
    };
    int argc = 0;
    while (args[argc]) { g_argv_store[argc] = g_strdup(args[argc]); ++argc; }
    // Process-only profiling override; the engine otherwise chooses its
    // normal thread count. dt_init validates against available processors.
    const char *level = g_getenv("OMARAW_ENGINE_RESOURCES");
    if (level && *level) {
        for (int i = 0; i < argc; ++i)
            if (g_str_has_prefix(g_argv_store[i], "resourcelevel=")) {
                g_free(g_argv_store[i]);
                g_argv_store[i] = g_strdup_printf("resourcelevel=%s", level);
                break;
            }
    }
    const char *threads = g_getenv("OMARAW_ENGINE_THREADS");
    if (threads && *threads) {
        char *end = NULL;
        const gint64 count = g_ascii_strtoll(threads, &end, 10);
        if (end != threads && !*end && count > 0 && count <= 1024) {
            g_argv_store[argc++] = g_strdup("--threads");
            g_argv_store[argc++] = g_strdup(threads);
        }
    }
    g_argv_store[argc] = NULL;
    char *argv[G_N_ELEMENTS(g_argv_store)];
    memcpy(argv, g_argv_store, sizeof argv);
    const int rc = dt_init(argc, argv, FALSE, TRUE, NULL);
    g_free(datadir);
    g_free(moduledir);
    if (rc) { set_error("dt_init failed (%s)", "see stderr"); return -1; }
    // Opt-in per-module timings for reproducible Develop profiling.
    if (g_strcmp0(g_getenv("OMARAW_ENGINE_PROFILE"), "1") == 0)
        darktable.unmuted |= DT_DEBUG_PIPE | DT_DEBUG_PERF;
    void (*set_colour)(oma_colour_stage_fn) = dlsym(RTLD_DEFAULT, "dt_omaraw_set_ocio_transform");
    void (*set_texture)(oma_texture_stage_fn) = dlsym(RTLD_DEFAULT, "dt_omaraw_set_texture");
    g_set_export_xmp = dlsym(RTLD_DEFAULT, "dt_omaraw_set_export_xmp");
    // Optional: an engine without it simply renders previews at full quality.
    g_set_approx_demosaic = dlsym(RTLD_DEFAULT, "dt_omaraw_set_approx_demosaic");
    // Optional: an engine without it runs every module natively.
    g_set_chain = dlsym(RTLD_DEFAULT, "dt_omaraw_set_chain");
    // Optional: an engine without it folds an on/off switch into the step before.
    g_add_new_step = dlsym(RTLD_DEFAULT, "dt_omaraw_add_history_item_new");
    void (*set_source)(int (*)(const char *, char *, size_t)) = dlsym(RTLD_DEFAULT, "dt_omaraw_set_source_resolver");
    void (*set_smart)(int (*)(dt_image_t *, dt_mipmap_buffer_t *, const char *)) = dlsym(RTLD_DEFAULT, "dt_omaraw_set_smart_loader");
    if (!set_smart || !set_colour || !set_texture || !g_set_export_xmp || !set_source) {
        dt_cleanup();
        set_error("The engine needs OmaRAW's OCIO patch; rebuild with bin/package", NULL); return -1;
    }
    set_colour(oma_colour_stage);
    set_texture(texture_preview);
    oma_engine_set_chain_enabled(g_strcmp0(g_getenv("OMARAW_CHAIN"), "0") != 0);
    oma_offline_configure(library);
    set_masks_dir_for(library);
    set_source(oma_offline_resolve);
    set_smart(smart_loader);
    // Engine colour boundaries stay on CPU. Shared Vulkan processing owns
    // validated acceleration; do not initialise unused OpenCL/CUDA drivers.
    dt_conf_set_bool("opencl", FALSE);
    darktable.prefer_library_history = TRUE;
    dt_conf_set_string("write_sidecar_files", "never");
    dt_dev_init(&g_dev, FALSE);
    // A ceiling on decoded source held behind the current photo. The engine
    // bounds these by entry count, so sixteen large RAWs would retain far
    // more than sixteen small ones; this bounds them by bytes instead.
    // Deliberately generous: a mixed RAW set measured 951 MiB, so this does
    // not bind on ordinary work, and tighter values cost real time (a 64-MiB
    // ceiling moved first renders from 192 to 268-337 ms). Zero disables it.
    g_source_budget = (size_t)2048 << 20;
    g_reduced_previews = g_strcmp0(g_getenv("OMARAW_REDUCED_PREVIEWS"), "1") == 0;
    g_export_full_resolution = g_strcmp0(g_getenv("OMARAW_EXPORT_FULL_RESOLUTION"), "1") == 0;
    const char *budget = g_getenv("OMARAW_SOURCE_CACHE");
    if (budget && *budget) {
        char *end = NULL;
        const gint64 mib = g_ascii_strtoll(budget, &end, 10);
        if (end != budget && !*end && mib >= 0 && mib <= 65536) g_source_budget = (size_t)mib << 20;
    }
    g_inited = TRUE;
    cleanup_comparisons();
    // LUTs are addressed by absolute path: darktable joins its LUT folder
    // and the field, and GLib collapses "/" + "/abs/file" to the file.
    dt_conf_set_string("plugins/darkroom/lut3d/def_path", "/");
    return 0;
}

void oma_engine_cache_stats(size_t *thumb_bytes, size_t *thumb_quota,
                            int *full_entries, int *full_quota,
                            int *f_entries, int *f_quota) {
    if (thumb_bytes) *thumb_bytes = 0;
    if (thumb_quota) *thumb_quota = 0;
    if (full_entries) *full_entries = 0;
    if (full_quota) *full_quota = 0;
    if (f_entries) *f_entries = 0;
    if (f_quota) *f_quota = 0;
    if (!g_inited || !darktable.mipmap_cache) return;
    const dt_mipmap_cache_t *c = darktable.mipmap_cache;
    // The thumbnail cache counts bytes; the full and reduced-float caches
    // count entries (see dt_mipmap_cache_print).
    if (thumb_bytes) *thumb_bytes = c->mip_thumbs.cache.cost;
    if (thumb_quota) *thumb_quota = c->mip_thumbs.cache.cost_quota;
    if (full_entries) *full_entries = (int)c->mip_full.cache.cost;
    if (full_quota) *full_quota = (int)c->mip_full.cache.cost_quota;
    if (f_entries) *f_entries = (int)c->mip_f.cache.cost;
    if (f_quota) *f_quota = (int)c->mip_f.cache.cost_quota;
}

void oma_engine_set_reduced_previews(int reduced) {
    const gboolean want = reduced ? TRUE : FALSE;
    if (want == g_reduced_previews) return;
    g_reduced_previews = want;
    // The pipes carry the flag, and cached overview pixels were produced
    // under the old setting.
    if (g_inited) { clear_preview_cache(); clear_overviews(-1); }
}
int oma_engine_reduced_previews(void) { return g_reduced_previews ? 1 : 0; }
void oma_engine_set_chain_enabled(int enabled) {
    g_chain_enabled = enabled && g_set_chain ? TRUE : FALSE;
    if (g_set_chain) g_set_chain(g_chain_enabled ? oma_chain_stage : NULL);
}
int oma_engine_chain_available(void) { return g_set_chain ? 1 : 0; }
int oma_engine_chain_enabled(void) { return g_chain_enabled ? 1 : 0; }
void oma_engine_set_export_full_resolution(int full) { g_export_full_resolution = full ? TRUE : FALSE; }
void oma_engine_set_export_overwrite(int overwrite) { g_export_overwrite = overwrite ? TRUE : FALSE; }
int oma_engine_export_full_resolution(void) { return g_export_full_resolution ? 1 : 0; }

void oma_engine_set_source_ceiling(size_t bytes) { g_source_budget = bytes; }
size_t oma_engine_source_ceiling(void) { return g_source_budget; }

size_t oma_engine_source_bytes(void) {
    if (!g_inited || !darktable.mipmap_cache) return 0;
    size_t total = 0;
    for (GList *link = g_sources.head; link; link = link->next)
        total += resident_source_bytes(GPOINTER_TO_INT(link->data) - 1);
    return total;
}

void oma_engine_cleanup(void) {
    if (!g_inited) return;
    cleanup_comparisons();
    clear_preview_cache();
    clear_overviews(-1);
    g_queue_clear(&g_sources);
    g_clear_pointer(&g_source_versions, g_hash_table_destroy);
    if (g_dev_imgid >= 0) dt_dev_cleanup(&g_dev);
    g_dev_imgid = -1;
    dt_cleanup();
    for (size_t i = 0; i < G_N_ELEMENTS(g_argv_store); ++i) g_clear_pointer(&g_argv_store[i], g_free);
    g_inited = FALSE;
}

static dt_imgid_t id_for_version(dt_filmid_t fid, const char *filename, int version) {
    sqlite3_stmt *stmt = NULL;
    dt_imgid_t id = NO_IMGID;
    if (sqlite3_prepare_v2(dt_database_get(darktable.db),
                           "SELECT id FROM main.images WHERE film_id = ?1 AND filename = ?2 AND version = ?3",
                           -1, &stmt, NULL) != SQLITE_OK) return NO_IMGID;
    sqlite3_bind_int(stmt, 1, fid);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, version);
    if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return id;
}

// The engine's id for a version it already knows, or -1: nothing is
// imported, so asking about a photo never edited costs nothing.
int oma_engine_known_version(const char *path, int version) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    gchar *dir = g_path_get_dirname(path), *base = g_path_get_basename(path);
    sqlite3_stmt *stmt = NULL;
    int id = -1;
    if (sqlite3_prepare_v2(dt_database_get(darktable.db),
                           "SELECT i.id FROM main.images i JOIN main.film_rolls f ON f.id = i.film_id"
                           " WHERE f.folder = ?1 AND i.filename = ?2 AND i.version = ?3",
                           -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, dir, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, base, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, version);
        if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    g_free(dir); g_free(base);
    return id;
}

int oma_engine_open_version(const char *path, int version) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    char resolved[PATH_MAX];
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR) && !oma_offline_resolve(path, resolved, sizeof resolved)) {
        set_error("original unavailable; no verified offline working copy: %s", path); return -1;
    }
    gchar *dir = g_path_get_dirname(path);
    dt_film_t film;
    const dt_filmid_t fid = dt_film_new(&film, dir);
    g_free(dir);
    if (!dt_is_valid_filmid(fid)) { set_error("cannot open folder of %s", path); return -1; }
    // Look up the existing image before import: its original drive may be
    // disconnected. The resolver preserves its identity, history and masks.
    gchar *base = g_path_get_basename(path);
    const dt_imgid_t known = id_for_version(fid, base, version);
    if (dt_is_valid_imgid(known)) { g_free(base); return (int)known; }
    // dt_image_import returns whichever version it finds first.
    const dt_imgid_t any = dt_image_import(fid, path, TRUE, FALSE);
    if (!dt_is_valid_imgid(any)) { g_free(base); set_error("cannot import %s", path); return -1; }
    const dt_imgid_t id = id_for_version(fid, base, version);
    g_free(base);
    if (!dt_is_valid_imgid(id)) {
        if (version == 0) return (int)any; // legacy library without a version 0 row
        set_error("no such version of %s", path);
        return -1;
    }
    return (int)id;
}

int oma_engine_open(const char *path) { return oma_engine_open_version(path, 0); }

static int source_available(int imgid) {
    char path[PATH_MAX] = {0}; gboolean cached = TRUE;
    dt_image_full_path(imgid, path, sizeof path, &cached);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        set_error("original unavailable; no verified offline working copy: %s", path);
        return -1;
    }
    if (!g_source_versions) g_source_versions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    char *key = preview_source_key(imgid);
    const char *previous = g_hash_table_lookup(g_source_versions, GINT_TO_POINTER(imgid));
    if (!g_strcmp0(previous, key)) { g_free(key); return 0; }
    // Full decoded buffers are keyed by image id, not file identity. Evict
    // them explicitly: dt_mipmap_cache_remove only removes LDR thumbnails.
    // First use is also invalidated in case import or an earlier context
    // decoded the image before its identity was recorded here.
    clear_preview_cache();
    clear_overviews(imgid);
    if (g_dev_imgid == imgid) {
        dt_dev_cleanup(&g_dev); dt_dev_init(&g_dev, FALSE); g_dev_imgid = -1;
    }
    dt_mipmap_cache_remove(imgid);
    dt_mipmap_cache_evict_at_size(imgid, DT_MIPMAP_F);
    dt_mipmap_cache_evict_at_size(imgid, DT_MIPMAP_FULL);
    // Keep source tracking bounded independently of catalogue size. A
    // forgotten identity takes the conservative first-use path next time.
    if (g_hash_table_size(g_source_versions) >= 512) g_hash_table_remove_all(g_source_versions);
    g_hash_table_replace(g_source_versions, GINT_TO_POINTER(imgid), key);
    return 0;
}

int oma_engine_image_version(int imgid) {
    if (!g_inited) return -1;
    const dt_image_t *img = dt_image_cache_get(imgid, 'r');
    if (!img) return -1;
    const int v = img->version;
    dt_image_cache_read_release(img);
    return v;
}

#include "smartpreview.inc"

// ── decoded sources ─────────────────────────────────────────────────────
// The engine holds decoded full images in a cache bounded by ENTRY COUNT —
// twice its worker threads, rounded up, so sixteen here — and not by bytes.
// A mixed set of large RAWs therefore retains gigabytes: thirteen resident
// images accounted for most of a 4 GiB resting footprint. Track what has
// been loaded and drop the least recently used beyond a byte budget.
//
// Sizes come from the engine itself through a non-blocking peek, so nothing
// is estimated. Eviction needs the entry's write lock and spins until it
// gets it, so it runs only between jobs, never on the image being rendered.
static size_t resident_source_bytes(int imgid) {
    if (!darktable.mipmap_cache) return 0;
    size_t bytes = 0;
    const dt_mipmap_size_t sizes[] = {DT_MIPMAP_FULL, DT_MIPMAP_F};
    for (size_t i = 0; i < G_N_ELEMENTS(sizes); ++i) {
        dt_mipmap_buffer_t buf = {0};
        dt_mipmap_cache_get(&buf, imgid, sizes[i], DT_MIPMAP_TESTLOCK, 'r');
        if (buf.cache_entry) {
            bytes += buf.cache_entry->data_size;
            dt_mipmap_cache_release(&buf);
        }
    }
    return bytes;
}

static void note_source(int imgid) {
    if (imgid < 0) return;
    gpointer tag = GINT_TO_POINTER(imgid + 1);
    g_queue_remove(&g_sources, tag);
    g_queue_push_tail(&g_sources, tag);
    while (g_queue_get_length(&g_sources) > 256) g_queue_pop_head(&g_sources);   // long gone by now
}

// Keep the most recently used decoded images that fit the budget and drop
// the rest. The current image is always kept, even when it alone is larger
// than the budget: a photo must be openable whatever its size.
static void trim_sources(int keep_imgid) {
    if (!g_source_budget || !darktable.mipmap_cache) return;
    size_t kept = 0;
    for (GList *link = g_sources.tail, *previous; link; link = previous) {
        previous = link->prev;
        const int imgid = GPOINTER_TO_INT(link->data) - 1;
        const size_t bytes = resident_source_bytes(imgid);
        // Nothing to count: evicted, or momentarily locked by a reader. The
        // entry stays listed so a locked source is counted next time rather
        // than dropping out of the budget for good; the list itself is
        // bounded below.
        if (!bytes) continue;
        if (imgid == keep_imgid || kept + bytes <= g_source_budget) {
            kept += bytes;
            continue;
        }
        // Only the decoded source goes; history, thumbnails and this
        // process's own overview cache are untouched, so revisiting the
        // photo costs a decode and nothing else.
        dt_mipmap_cache_evict_at_size(imgid, DT_MIPMAP_FULL);
        dt_mipmap_cache_evict_at_size(imgid, DT_MIPMAP_F);
        g_queue_delete_link(&g_sources, link);
    }
}

// A new step after an Undo replaces what was undone. The engine keeps one
// kind of step from above the end and makes it active again: that of an
// always-on module with no earlier step, which History must hold (drop it and
// the engine inserts it again on the next load, moving the end). Kept as it
// was, it brought an undone change back with the next, unrelated edit. So it
// stays, once, at the module's defaults; everything else above goes.
static void drop_redo_tail(dt_develop_t *dev) {
    GList *l = g_list_nth(dev->history, dev->history_end);
    GHashTable *kept = g_hash_table_new(g_direct_hash, g_direct_equal);
    while (l) {
        GList *next = l->next;
        dt_dev_history_item_t *h = l->data;
        dt_iop_module_t *m = h->module;
        gboolean earlier = FALSE;
        int i = 0;
        for (GList *p = dev->history; p && i < dev->history_end; p = p->next, ++i)
            if (((dt_dev_history_item_t *)p->data)->module->so == m->so) { earlier = TRUE; break; }
        if ((m->hide_enable_button || m->default_enabled) && !earlier && !g_hash_table_contains(kept, m->so)) {
            g_hash_table_add(kept, m->so);
            if (h->params && m->default_params) memcpy(h->params, m->default_params, m->params_size);
            if (h->blend_params && m->default_blendop_params) memcpy(h->blend_params, m->default_blendop_params, sizeof(dt_develop_blend_params_t));
            h->enabled = m->default_enabled;
        } else {
            dt_dev_free_history_item(h);
            dev->history = g_list_delete_link(dev->history, l);
        }
        l = next;
    }
    g_hash_table_destroy(kept);
}
// A step asked to be its own (g_new_step) is forced to be one: the engine's
// own merge compares parameters only, so switching a module on or off right
// after a step on that module folded into it. An older engine without the
// OmaRAW addition keeps that behaviour.
// True when the module already stands exactly as its last active step (or,
// with none, its defaults) left it: recording would add a step Undo takes
// back with nothing to see, as a reset pressed on something already reset.
static gboolean unchanged_step(dt_develop_t *dev, dt_iop_module_t *m, const gboolean enable) {
    const gboolean on = enable ? TRUE : m->enabled;
    const dt_dev_history_item_t *last = NULL;
    int i = 0;
    for (GList *l = dev->history; l && i < dev->history_end; l = l->next, ++i) {
        const dt_dev_history_item_t *h = l->data;
        if (h->module == m) last = h;
    }
    const void *params = last ? (const void *)last->params : (const void *)m->default_params;
    const gboolean was_on = last ? last->enabled : m->default_enabled;
    if (!params || was_on != on || memcmp(params, m->params, m->params_size)) return FALSE;
    if (m->blend_params) {
        const void *blend = last ? (const void *)last->blend_params : (const void *)m->default_blendop_params;
        if (!blend || memcmp(blend, m->blend_params, sizeof(dt_develop_blend_params_t))) return FALSE;
    }
    return TRUE;
}

static void oma_add_history_item(dt_develop_t *dev, dt_iop_module_t *m, const gboolean enable, const gboolean no_image) {
    if (unchanged_step(dev, m, enable)) return;
    drop_redo_tail(dev);
    if (g_new_step && g_add_new_step) { g_new_step = FALSE; g_add_new_step(dev, m, enable); return; }
    g_new_step = FALSE;
    dt_dev_add_history_item_ext(dev, m, enable, no_image);
}
static void oma_add_masks_history_item(dt_develop_t *dev, dt_iop_module_t *m, const gboolean enable, const gboolean no_image) {
    drop_redo_tail(dev);
    dt_dev_add_masks_history_item_ext(dev, m, enable, no_image);
}

// Load (or keep) the develop context for imgid.
static int load(int imgid) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    if (source_available(imgid)) return -1;
    if (g_dev_imgid == imgid) return 0;
    if (g_dev_imgid >= 0) { dt_dev_cleanup(&g_dev); dt_dev_init(&g_dev, FALSE); }
    trim_sources(imgid); // make room before this photo is decoded
    dt_dev_load_image(&g_dev, imgid);
    // Reading history fills dev->history; applying it to the module
    // instances (params + enabled) is a separate step the darkroom does.
    dt_dev_pop_history_items_ext(&g_dev, g_dev.history_end);
    g_dev_imgid = imgid;
    note_source(imgid);
    return 0;
}

// Re-read history from the database so the develop context matches what
// the export path (which loads its own) will see.
static void reload(void) {
    const int id = g_dev_imgid;
    if (id < 0) return;
    dt_dev_cleanup(&g_dev);
    dt_dev_init(&g_dev, FALSE);
    dt_dev_load_image(&g_dev, id);
    dt_dev_pop_history_items_ext(&g_dev, g_dev.history_end);
}

// The edit as darktable writes it into a sidecar (history, masks, module
// order), from what is stored, the open photo's latest edit included.
char *oma_engine_edit_xmp(int imgid) {
    if (!g_inited) { set_error("engine not initialised", NULL); return NULL; }
    if (g_dev_imgid == imgid) dt_dev_write_history(&g_dev);
    char *xmp = dt_exif_xmp_read_string(imgid);
    if (!xmp) set_error("the edit could not be read", NULL);
    return xmp;
}

// Replaces the photo's history with the one in an XMP file's darktable
// section: exactly the edit that was written there.
int oma_engine_apply_edit_xmp(int imgid, const char *xmp_path) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    dt_image_t *img = dt_image_cache_get(imgid, 'w');
    if (!img) { set_error("no such image in the engine", NULL); return -1; }
    const gboolean failed = dt_exif_xmp_read(img, xmp_path, TRUE);
    dt_image_cache_write_release(img, DT_IMAGE_CACHE_RELAXED);
    if (failed) { set_error("the sidecar's edit could not be read: %s", xmp_path); return -1; }
    if (g_dev_imgid == imgid) reload();
    return 0;
}

static int preset_schema(void);
static int copy_comparison_rows(const char *, const char *, int, int);
static int copy_import_baseline(int source, int target);

int oma_engine_duplicate(int imgid, int *version) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    // Flush the loaded develop so the copy sees the latest history.
    if (g_dev_imgid == imgid) dt_dev_write_history(&g_dev);
    const dt_imgid_t newid = dt_image_duplicate(imgid);
    if (!dt_is_valid_imgid(newid)) { set_error("duplicate failed", NULL); return -1; }
    // The row is copied, the history is not: paste it whole (modules,
    // order, masks) so the variant starts where the source is now.
    dt_history_copy_and_paste_on_image(imgid, newid, FALSE, NULL, TRUE, TRUE, TRUE);
    if (copy_import_baseline(imgid, newid) || preset_schema()
        || copy_comparison_rows("omaraw_preset_state", "imgid", imgid, newid)) {
        dt_image_remove(newid); set_error("could not copy the import baseline", NULL); return -1;
    }
    if (version) *version = oma_engine_image_version(newid);
    return (int)newid;
}

static int set_version(int imgid, int version) {
    // The image cache's write-back does not carry `version`: write the row
    // ourselves, then keep the cached struct in step.
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(dt_database_get(darktable.db), "UPDATE main.images SET version = ?1 WHERE id = ?2", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, version);
    sqlite3_bind_int(stmt, 2, imgid);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    dt_image_t *img = dt_image_cache_get(imgid, 'w');
    if (img) { img->version = version; dt_image_cache_write_release(img, DT_IMAGE_CACHE_RELAXED); }
    return 0;
}

int oma_engine_swap_versions(int a, int b) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    const int va = oma_engine_image_version(a), vb = oma_engine_image_version(b);
    if (va < 0 || vb < 0) { set_error("no such image", NULL); return -1; }
    // Two steps through a spare number: (film_id, filename, version) must stay unique.
    if (set_version(a, -1) || set_version(b, va) || set_version(a, vb)) { set_error("could not swap versions", NULL); return -1; }
    return 0;
}

int oma_engine_remove(int imgid) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    clear_overviews(imgid);
    if (g_dev_imgid == imgid) { dt_dev_cleanup(&g_dev); dt_dev_init(&g_dev, FALSE); g_dev_imgid = -1; }
    dt_image_remove(imgid);
    return 0;
}

// Copy the pinned engine's complete rows, including camera metadata and
// module blobs. A savepoint journals the disposable image atomically; no
// live image, grouping, history cursor or max_version is written here.
static int copy_comparison_rows(const char *table, const char *owner, int source, int target) {
    sqlite3 *db = dt_database_get(darktable.db);
    char *sql = sqlite3_mprintf("PRAGMA main.table_info(\"%w\")", table);
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_free(sql);
    if (rc != SQLITE_OK) return -1;
    GString *columns = g_string_new(NULL), *values = g_string_new(NULL);
    const gboolean image = !strcmp(table, "images");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *column = (const char *)sqlite3_column_text(stmt, 1);
        if (image && !strcmp(column, owner)) continue;
        char *quoted = sqlite3_mprintf("\"%w\"", column);
        if (columns->len) { g_string_append_c(columns, ','); g_string_append_c(values, ','); }
        g_string_append(columns, quoted);
        if (!strcmp(column, owner)) g_string_append(values, "?2");
        else if (image && !strcmp(column, "version")) g_string_append(values, "-2");
        else if (image && !strcmp(column, "group_id")) g_string_append(values, "NULL");
        else if (image && !strcmp(column, "flags")) g_string_append_printf(values, "flags & ~%d", DT_IMAGE_LOCAL_COPY);
        else g_string_append(values, quoted);
        sqlite3_free(quoted);
    }
    sqlite3_finalize(stmt);
    sql = sqlite3_mprintf("INSERT INTO main.\"%w\" (%s) SELECT %s FROM main.\"%w\" WHERE \"%w\"=?1",
                         table, columns->str, values->str, table, owner);
    g_string_free(columns, TRUE); g_string_free(values, TRUE);
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL); sqlite3_free(sql);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, source);
        if (!image) sqlite3_bind_int(stmt, 2, target);
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}
static int copy_import_baseline(int source, int target) {
    sqlite3_stmt *stmt = NULL;
    sqlite3 *db = dt_database_get(darktable.db);
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='omaraw_import_baselines'",
                         -1, &stmt, NULL) != SQLITE_OK) return -1;
    const gboolean exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    if (!exists) return 0;
    return copy_comparison_rows("omaraw_import_baselines", "imgid", source, target)
        || copy_comparison_rows("omaraw_import_history", "imgid", source, target) ? -1 : 0;
}

int oma_engine_comparison_create(int imgid) {
    if (!g_inited || source_available(imgid)) return -1;
    sqlite3 *db = dt_database_get(darktable.db);
    if (sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS main.omaraw_comparison_images"
                         " (imgid INTEGER PRIMARY KEY REFERENCES images(id) ON DELETE CASCADE)", NULL, NULL, NULL) != SQLITE_OK
        || sqlite3_exec(db, "SAVEPOINT omaraw_comparison", NULL, NULL, NULL) != SQLITE_OK) return -1;
    int id = -1;
    if (copy_comparison_rows("images", "id", imgid, 0) || sqlite3_changes(db) != 1) goto failed;
    id = (int)sqlite3_last_insert_rowid(db);
    char *sql = sqlite3_mprintf("INSERT INTO main.omaraw_comparison_images VALUES(%d);"
                               "UPDATE main.images SET group_id=id WHERE id=%d", id, id);
    const int rc = sqlite3_exec(db, sql, NULL, NULL, NULL); sqlite3_free(sql);
    if (rc != SQLITE_OK) goto failed;
    for (const char **table = (const char *[]){"history", "masks_history", "module_order", "history_hash", NULL}; *table; ++table)
        if (copy_comparison_rows(*table, "imgid", imgid, id)) goto failed;
    if (sqlite3_exec(db, "RELEASE omaraw_comparison", NULL, NULL, NULL) == SQLITE_OK) return id;
failed:
    set_error("could not prepare saved comparison: %s", sqlite3_errmsg(db));
    sqlite3_exec(db, "ROLLBACK TO omaraw_comparison; RELEASE omaraw_comparison", NULL, NULL, NULL);
    return -1;
}
void oma_engine_comparison_remove(int imgid) {
    if (!g_inited || imgid < 0) return;
    sqlite3_stmt *stmt = NULL;
    sqlite3 *db = dt_database_get(darktable.db);
    if (sqlite3_prepare_v2(db, "SELECT imgid FROM main.omaraw_comparison_images WHERE imgid=?1", -1, &stmt, NULL) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, imgid);
    const gboolean owned = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    if (!owned) return;
    clear_comparison_preview(imgid);
    // module_order has no foreign key in the pinned engine schema. Remove
    // it before the image so an interrupted cleanup remains journalled.
    if (sqlite3_prepare_v2(db, "DELETE FROM main.module_order WHERE imgid=?1", -1, &stmt, NULL) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, imgid);
    const int removed = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (removed != SQLITE_DONE) return;
    oma_engine_remove(imgid);
    dt_mipmap_cache_evict_at_size(imgid, DT_MIPMAP_FULL);
    dt_mipmap_cache_evict_at_size(imgid, DT_MIPMAP_F);
    if (g_source_versions) g_hash_table_remove(g_source_versions, GINT_TO_POINTER(imgid));
    g_queue_remove(&g_sources, GINT_TO_POINTER(imgid + 1));
}
static void cleanup_comparisons(void) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(dt_database_get(darktable.db), "SELECT imgid FROM main.omaraw_comparison_images", -1, &stmt, NULL) != SQLITE_OK) return;
    GList *ids = NULL;
    while (sqlite3_step(stmt) == SQLITE_ROW) ids = g_list_prepend(ids, GINT_TO_POINTER(sqlite3_column_int(stmt, 0)));
    sqlite3_finalize(stmt);
    for (GList *item = ids; item; item = item->next) oma_engine_comparison_remove(GPOINTER_TO_INT(item->data));
    g_list_free(ids);
}

int oma_engine_relink_folder(const char *old_dir, const char *new_dir) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    // Drop the loaded develop: its image may live in the moved roll.
    if (g_dev_imgid >= 0) { dt_dev_cleanup(&g_dev); dt_dev_init(&g_dev, FALSE); g_dev_imgid = -1; }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(dt_database_get(darktable.db),
                           "UPDATE main.film_rolls SET folder = ?1 || substr(folder, length(?2) + 1)"
                           " WHERE folder = ?2 OR substr(folder, 1, length(?2) + 1) = ?2 || '/'",
                           -1, &stmt, NULL) != SQLITE_OK) { set_error("relink: prepare failed", NULL); return -1; }
    sqlite3_bind_text(stmt, 1, new_dir, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, old_dir, -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { set_error("relink: update failed", NULL); return -1; }
    // Cached image structs remember nothing about the folder; the mipmap
    // cache keys on image id, so renders stay valid.
    return 0;
}

int oma_engine_move_image(const char *old_path, const char *new_path) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    gchar *old_dir = g_path_get_dirname(old_path), *old_base = g_path_get_basename(old_path);
    gchar *new_dir = g_path_get_dirname(new_path), *new_base = g_path_get_basename(new_path);
    int rc = 0;
    const dt_filmid_t old_fid = dt_film_get_id(old_dir);
    if (!dt_is_valid_filmid(old_fid)) goto out;   // never opened here: nothing to carry over
    dt_film_t film;
    const dt_filmid_t new_fid = dt_film_new(&film, new_dir);
    if (!dt_is_valid_filmid(new_fid)) { set_error("cannot make a film roll for %s", new_dir); rc = -1; goto out; }
    // Every version of the file: the row first, then the cached struct.
    {
        GList *ids = NULL;
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(dt_database_get(darktable.db), "SELECT id FROM main.images WHERE film_id = ?1 AND filename = ?2", -1, &stmt, NULL) != SQLITE_OK) { set_error("move: prepare failed", NULL); rc = -1; goto out; }
        sqlite3_bind_int(stmt, 1, old_fid);
        sqlite3_bind_text(stmt, 2, old_base, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) ids = g_list_prepend(ids, GINT_TO_POINTER(sqlite3_column_int(stmt, 0)));
        sqlite3_finalize(stmt);
        for (GList *l = ids; l; l = g_list_next(l)) {
            const int imgid = GPOINTER_TO_INT(l->data);
            if (g_dev_imgid == imgid) { dt_dev_cleanup(&g_dev); dt_dev_init(&g_dev, FALSE); g_dev_imgid = -1; }
            sqlite3_stmt *up = NULL;
            if (sqlite3_prepare_v2(dt_database_get(darktable.db), "UPDATE main.images SET film_id = ?1, filename = ?2 WHERE id = ?3", -1, &up, NULL) != SQLITE_OK) { rc = -1; continue; }
            sqlite3_bind_int(up, 1, new_fid);
            sqlite3_bind_text(up, 2, new_base, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(up, 3, imgid);
            if (sqlite3_step(up) != SQLITE_DONE) rc = -1;
            sqlite3_finalize(up);
            dt_image_t *img = dt_image_cache_get(imgid, 'w');
            if (img) {
                img->film_id = new_fid;
                g_strlcpy(img->filename, new_base, sizeof img->filename);
                dt_image_cache_write_release(img, DT_IMAGE_CACHE_RELAXED);
            }
        }
        g_list_free(ids);
        if (rc) set_error("move: could not update %s", old_base);
    }
out:
    g_free(old_dir); g_free(old_base); g_free(new_dir); g_free(new_base);
    return rc;
}

// ── render to memory, the mipmap-cache way ──────────────────────────────
typedef struct {
    dt_imageio_module_data_t head;
    uint8_t *buf;
    size_t cap;
} mem_data_t;

static int mem_bpp(dt_imageio_module_data_t *d) { (void)d; return 32; }
static int mem_levels(dt_imageio_module_data_t *d) { (void)d; return IMAGEIO_RGB | IMAGEIO_FLOAT; }
static int mem_flags(dt_imageio_module_data_t *d) { (void)d; return FORMAT_FLAGS_NO_TMPFILE; }
static const char *mem_mime(dt_imageio_module_data_t *d) { (void)d; return "memory"; }
static const char *metadata_mime(dt_imageio_module_data_t *d) { (void)d; return "image/x-omaraw-export"; }
static const char *mem_ext(dt_imageio_module_data_t *d) { (void)d; return ""; }
static int mem_write(dt_imageio_module_data_t *data, const char *filename, const void *in,
                     dt_colorspaces_color_profile_type_t over_type, const char *over_filename,
                     void *exif, int exif_len, dt_imgid_t imgid, int num, int total,
                     struct dt_dev_pixelpipe_t *pipe, const gboolean export_masks) {
    (void)filename; (void)over_type; (void)over_filename; (void)exif; (void)exif_len;
    (void)imgid; (void)num; (void)total; (void)pipe; (void)export_masks;
    mem_data_t *d = (mem_data_t *)data;
    const size_t pixels = (size_t)data->width * data->height;
    if (data->width <= 0 || data->height <= 0 || pixels > 268435456) return 1;
    const size_t need = pixels * 4 * sizeof(float);
    d->buf = malloc(need);
    if (!d->buf) return 1;
    memcpy(d->buf, in, need);
    if (interchange_image((float *)d->buf, data->width, data->height)) {
        free(d->buf); d->buf = NULL; return 1;
    }
    // Engine colour buffers use the fourth float as scratch/mask data.
    // Photographs are opaque at this boundary, never an undefined alpha.
    for (size_t i = 0; i < pixels; ++i) ((float *)d->buf)[4*i+3] = 1.0f;
    return 0;
}

int oma_engine_render(int imgid, int max_w, int max_h, float **out, int *w, int *h) {
    if(oma_engine_repair_check(imgid)) return -1;
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    if (source_available(imgid)) return -1;
    if (!out || !w || !h || max_w <= 0 || max_h <= 0) { set_error("invalid render size", NULL); return -1; }
    *out = NULL; *w = *h = 0;
    if (oma_engine_using_smart_preview(imgid)) return render_preview(imgid, 0, 0, max_w, max_h, out, w, h);
    oma_colour_engine_check(1, NULL, 0);
    dt_imageio_module_format_t format;
    memset(&format, 0, sizeof format);
    format.bpp = mem_bpp;
    format.levels = mem_levels;
    format.flags = mem_flags;
    format.mime = mem_mime;
    format.extension = mem_ext;
    format.write_image = mem_write;
    mem_data_t dat;
    memset(&dat, 0, sizeof dat);
    dat.head.max_width = max_w;
    dat.head.max_height = max_h;
    // ignore_exif, no display byte order, no hq, no upscale, no scaling,
    // scale 1, not a thumbnail export, no filter, no metadata, no masks.
    const gboolean fail = dt_imageio_export_with_flags(imgid, "unused", &format, (dt_imageio_module_data_t *)&dat,
                                                       TRUE, FALSE, FALSE, FALSE, FALSE, 1.0, FALSE, NULL,
                                                       FALSE, FALSE, DT_COLORSPACE_LIN_REC709, NULL, DT_INTENT_RELATIVE_COLORIMETRIC,
                                                       NULL, NULL, 1, 1, NULL, -1);
    if (oma_colour_engine_check(0, g_error, sizeof g_error)) { free(dat.buf); return -1; }
    if (fail || dat.head.width <= 0) { free(dat.buf); set_error("render failed", NULL); return -1; }
    *out = (float *)dat.buf;
    *w = dat.head.width;
    *h = dat.head.height;
    return 0;
}

// The worker owns the scope; the mutex protects its lifetime and the pipe
// pointer while the UI thread signals cancellation.
static gboolean preview_cancelled(void) {
    return g_preview_cancelled && g_preview_cancelled(g_preview_request);
}
static int texture_preview(const float *input, float *output, int w, int h,
                           float amount, float scale, double cpu_ms) {
    // Only the generation-scoped private overview/detail jobs use Vulkan.
    // Reference rendering, exports and unscoped original/mask jobs stay native.
    if (!g_preview_pipe || !g_preview_cancelled) return 0;
    return oma_texture_stage(input, output, w, h, amount, scale, cpu_ms,
                             g_preview_cancelled, g_preview_request);
}
void oma_engine_preview_begin(int (*cancelled)(void *), void *request) {
    g_mutex_lock(&g_preview_mutex);
    g_preview_cancelled = cancelled; g_preview_request = request;
    g_mutex_unlock(&g_preview_mutex);
}
void oma_engine_preview_end(void) {
    g_mutex_lock(&g_preview_mutex);
    g_preview_cancelled = NULL; g_preview_request = NULL;
    g_mutex_unlock(&g_preview_mutex);
}
void oma_engine_cancel_preview(void) {
    g_mutex_lock(&g_preview_mutex);
    if (g_preview_pipe && preview_cancelled()) {
        // set_shutdown only accepts a running pipe. STOP_NODES also survives
        // process startup, closing the race between publication and entry.
        dt_atomic_set_int(&g_preview_pipe->shutdown, DT_DEV_PIXELPIPE_STOP_NODES);
        oma_texture_cancel();
    }
    g_mutex_unlock(&g_preview_mutex);
}

// Two bounded contexts: current/crop and before. Keep module nodes and their
// ROI caches between adjacent tiles; reacquire the source for each job so no
// cache read lock survives into a rename, removal or another image operation.
#define PREVIEW_AREAS 4
typedef struct {
    int imgid, original, uncropped;
    // Reduced: fed from the engine's downsampled mosaic (DT_MIPMAP_F) with
    // its iscale, so the mosaic-stage modules and demosaic run on a few
    // megapixels instead of the whole sensor. Serves fitted overviews only.
    int reduced;
    int reference_width, reference_height;
    gboolean ready, reusable;
    char *key, *source_key, *resources_key;
    dt_develop_t dev;
    dt_dev_pixelpipe_t pipe;
    // Finished areas that native tiles are cut from while a mask is
    // feathered or blurred (see render_region). Valid for `key` only.
    struct { int x, y, width, height; double scale; char *key; float *pixels; guint64 used; } areas[PREVIEW_AREAS];
    guint64 area_clock, used;
} oma_preview_cache;
#define PREVIEW_SLOTS 5
static oma_preview_cache g_previews[PREVIEW_SLOTS];
static int repair_context_valid(oma_preview_cache *cache);
static inline int preview_slot(int original, int reduced) { return original == 3 ? 4 : (reduced ? 2 : 0) + (original ? 1 : 0); }
static inline dt_mipmap_size_t preview_mip(const oma_preview_cache *cache) { return cache->reduced ? DT_MIPMAP_F : DT_MIPMAP_FULL; }
static gboolean g_reusable_preview = FALSE;
static oma_preview_stats g_preview_stats;
// Retain final linear overviews across photo navigation without retaining
// each photo's full-resolution intermediate buffers or an engine cache lock.
typedef struct {
    int imgid, width, height;
    char *key;
    float *pixels;
    size_t bytes;
} oma_overview;
static GQueue g_overviews = G_QUEUE_INIT;
static size_t g_overview_budget = 64u * 1024u * 1024u;
static size_t g_pipe_budget = 256u * 1024u * 1024u;
static size_t g_area_budget = 256u * 1024u * 1024u;
static size_t g_context_budget = 0; // zero keeps standalone legacy defaults
static guint64 g_context_clock = 0;
static void remove_overview(GList *link) {
    oma_overview *frame = link->data;
    g_preview_stats.overview_bytes -= frame->bytes;
    g_free(frame->key); free(frame->pixels); g_free(frame);
    g_queue_delete_link(&g_overviews, link);
}
static void clear_overviews(int imgid) {
    for (GList *link = g_overviews.head, *next; link; link = next) {
        next = link->next;
        const oma_overview *frame = link->data;
        if (imgid < 0 || frame->imgid == imgid) remove_overview(link);
    }
}
static gboolean recall_overview(int imgid, const char *key, int width, int height, float **pixels) {
    for (GList *link = g_overviews.head; link; link = link->next) {
        const oma_overview *frame = link->data;
        if (frame->imgid != imgid || frame->width != width || frame->height != height || strcmp(frame->key, key)) continue;
        float *copy = malloc(frame->bytes);
        if (!copy) break; // allocation failure retains the normal renderer
        memcpy(copy, frame->pixels, frame->bytes);
        *pixels = copy;
        g_queue_unlink(&g_overviews, link); g_queue_push_tail_link(&g_overviews, link);
        ++g_preview_stats.overview_hits;
        return TRUE;
    }
    ++g_preview_stats.overview_misses;
    return FALSE;
}
static void remember_overview(int imgid, const char *key, int width, int height, const float *pixels) {
    clear_overviews(imgid); // one current size/edit per photo, including variants
    const size_t bytes = (size_t)width * height * 4 * sizeof(float);
    if (!bytes || bytes > g_overview_budget) return;
    while (g_overviews.length >= 16 || g_preview_stats.overview_bytes + bytes > g_overview_budget)
        remove_overview(g_overviews.head);
    float *copy = malloc(bytes);
    if (!copy) return; // this cache is optional
    memcpy(copy, pixels, bytes);
    oma_overview *frame = g_new0(oma_overview, 1);
    frame->imgid = imgid; frame->width = width; frame->height = height;
    frame->key = g_strdup(key); frame->pixels = copy; frame->bytes = bytes;
    g_queue_push_tail(&g_overviews, frame);
    g_preview_stats.overview_bytes += bytes;
}
static char *char_array_field(dt_iop_module_t *m, const char *field, int *len);
static void clear_preview_areas(oma_preview_cache *cache) {
    for (int i = 0; i < PREVIEW_AREAS; ++i) { g_free(cache->areas[i].key); free(cache->areas[i].pixels); }
    memset(cache->areas, 0, sizeof cache->areas);
}
static void clear_preview(oma_preview_cache *cache) {
    clear_preview_areas(cache);
    if (cache->ready) {
        dt_dev_pixelpipe_cleanup(&cache->pipe);
        dt_dev_cleanup(&cache->dev);
    }
    g_free(cache->key);
    g_free(cache->source_key);
    g_free(cache->resources_key);
    memset(cache, 0, sizeof *cache);
}
static void clear_preview_cache(void) {
    for (int i = 0; i < PREVIEW_SLOTS; ++i) clear_preview(&g_previews[i]);
}
static void trim_preview_pipe(oma_preview_cache *cache, size_t limit) {
    if (!cache->ready) return;
    dt_dev_pixelpipe_t *pipe = &cache->pipe;
    pipe->backbuf = NULL;
    while (pipe->cache.allmem > limit) {
        int largest = -1;
        for (int i = 2; i < pipe->cache.entries; ++i)
            if (pipe->cache.data[i] && (largest < 0 || pipe->cache.size[i] > pipe->cache.size[largest])) largest = i;
        if (largest < 0) { clear_preview(cache); break; }
        dt_dev_pixelpipe_clear_cacheline(pipe, pipe->cache.data[largest], "preview retained budget");
    }
}
static size_t preview_context_bytes(const oma_preview_cache *cache) {
    size_t bytes = cache->ready ? cache->pipe.cache.allmem : 0;
    for (int i = 0; i < PREVIEW_AREAS; ++i) if (cache->areas[i].pixels)
        bytes += (size_t)cache->areas[i].width * cache->areas[i].height * 4 * sizeof(float);
    return bytes;
}
static size_t preview_context_total(void) {
    size_t bytes = 0;
    for (int i = 0; i < PREVIEW_SLOTS; ++i) bytes += preview_context_bytes(&g_previews[i]);
    return bytes;
}
static void trim_preview_contexts(oma_preview_cache *keep) {
    if (!g_context_budget) return;
    size_t total = preview_context_total();
    while (total > g_context_budget) {
        oma_preview_cache *victim = NULL;
        for (int i = 0; i < PREVIEW_SLOTS; ++i) {
            oma_preview_cache *candidate = &g_previews[i];
            if (candidate != keep && preview_context_bytes(candidate)
                && (!victim || candidate->used < victim->used)) victim = candidate;
        }
        if (!victim) victim = keep;
        if (!victim) break;
        const size_t excess = total - g_context_budget;
        if (victim->ready && victim->pipe.cache.allmem) {
            const size_t pipe_bytes = victim->pipe.cache.allmem;
            trim_preview_pipe(victim, pipe_bytes > excess ? pipe_bytes - excess : 0);
        } else clear_preview_areas(victim);
        const size_t remaining = preview_context_total();
        if (remaining >= total) clear_preview(victim);
        total = preview_context_total();
    }
}
void oma_engine_set_memory_budget(size_t working, size_t sources, size_t contexts, size_t overviews) {
    if (!g_inited) return;
    g_source_budget = sources;
    // Explicit profiling overrides retain their existing meaning.
    const char *forced = g_getenv("OMARAW_SOURCE_CACHE");
    if (forced && *forced) {
        char *end = NULL; const gint64 mib = g_ascii_strtoll(forced, &end, 10);
        if (end != forced && !*end && mib >= 0 && mib <= 65536) g_source_budget = (size_t)mib << 20;
    }
    // Share bytes among active views rather than reserving equal slices for
    // five contexts that are rarely all in use. A complete native area can
    // then serve many tiles without repeatedly re-rendering the same pixels.
    g_context_budget = MAX((size_t)1 << 20, contexts);
    g_pipe_budget = g_context_budget;
    g_area_budget = g_context_budget;
    g_overview_budget = overviews;
    if (!g_getenv("OMARAW_ENGINE_RESOURCES")) {
        dt_conf_set_string("resourcelevel", "default");
        dt_get_sysresource_level();
        const size_t total = MAX((size_t)1, darktable.dtresources.total_memory);
        darktable.dtresources.fractions[4] = MAX(1, (int)(working * 1024 / total));
        darktable.dtresources.fractions[5] = MAX(1, (int)(working / 16 * 1024 / total));
    }
    oma_preview_cache *recent = &g_previews[0];
    for (int i = 0; i < PREVIEW_SLOTS; ++i) {
        clear_preview_areas(&g_previews[i]);
        trim_preview_pipe(&g_previews[i], g_pipe_budget);
        if (g_previews[i].used > recent->used) recent = &g_previews[i];
    }
    trim_preview_contexts(recent);
    while (g_preview_stats.overview_bytes > g_overview_budget && g_overviews.head) remove_overview(g_overviews.head);
    trim_sources(g_dev_imgid);
}
void oma_engine_memory_budget(size_t *working, size_t *sources, size_t *contexts, size_t *overviews) {
    if (working) *working = g_inited ? dt_get_available_mem() : 0;
    if (sources) *sources = g_source_budget;
    if (contexts) *contexts = g_context_budget ? g_context_budget : (g_pipe_budget + g_area_budget) * PREVIEW_SLOTS;
    if (overviews) *overviews = g_overview_budget;
}
static void clear_comparison_preview(int imgid) {
    for (int i = 0; i < PREVIEW_SLOTS; ++i)
        if (g_previews[i].ready && g_previews[i].imgid == imgid) clear_preview(&g_previews[i]);
}
static char *preview_source_key(int imgid) {
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    char path[PATH_MAX] = {0}; gboolean cached = TRUE;
    dt_image_full_path(imgid, path, sizeof path, &cached);
    g_checksum_update(sum, (const guchar *)path, strlen(path));
    GStatBuf st = {0};
    if (!g_stat(path, &st)) {
        g_checksum_update(sum, (const guchar *)&st.st_dev, sizeof st.st_dev);
        g_checksum_update(sum, (const guchar *)&st.st_ino, sizeof st.st_ino);
        g_checksum_update(sum, (const guchar *)&st.st_size, sizeof st.st_size);
        g_checksum_update(sum, (const guchar *)&st.st_mtim, sizeof st.st_mtim);
        g_checksum_update(sum, (const guchar *)&st.st_ctim, sizeof st.st_ctim);
    }
    char *key = g_strdup(g_checksum_get_string(sum)); g_checksum_free(sum); return key;
}
static char *preview_key(int imgid) {
    dt_history_hash_values_t hashes = {0};
    dt_history_hash_read(imgid, &hashes);
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    if (hashes.current && hashes.current_len > 0) g_checksum_update(sum, hashes.current, hashes.current_len);
    dt_history_hash_free(&hashes);
    // The pinned engine's hash includes num == history_end, although the
    // active history ends just before that item. Undoing one step can thus
    // leave its hash unchanged. Include the cursor so the previous geometry
    // and pixels cannot be recalled as if they were still current.
    sqlite3_stmt *cursor = NULL;
    if (sqlite3_prepare_v2(dt_database_get(darktable.db), "SELECT history_end FROM main.images WHERE id=?1", -1, &cursor, NULL) == SQLITE_OK) {
        sqlite3_bind_int(cursor, 1, imgid);
        if (sqlite3_step(cursor) == SQLITE_ROW) {
            const int end = sqlite3_column_int(cursor, 0);
            g_checksum_update(sum, (const guchar *)&end, sizeof end);
        }
    }
    sqlite3_finalize(cursor);
    char *source_key = preview_source_key(imgid);
    g_checksum_update(sum, (const guchar *)source_key, strlen(source_key));
    g_free(source_key);
    char *key = g_strdup(g_checksum_get_string(sum));
    g_checksum_free(sum);
    return key;
}
static void repair_resources_key(GChecksum *sum,const char *key);
static char *preview_resources_key(oma_preview_cache *cache) {
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    char *folder = dt_conf_get_string("plugins/darkroom/lut3d/def_path");
    for (GList *item = cache->dev.iop; item; item = item->next) {
        dt_iop_module_t *module = item->data;
        if(module->enabled && !strcmp(module->op,"omarawrepair")) {
            int length=0;const char *key=char_array_field(module,"file",&length);
            if(key && length) repair_resources_key(sum,key);
        }
        if (!module->enabled || (strcmp(module->op, "lut3d") && strcmp(module->op, "omarawprofile") && strcmp(module->op, "omarawprint"))) continue;
        int length = 0;
        const char *field = char_array_field(module, !strcmp(module->op, "omarawprint") ? "profile" : "filepath", &length);
        if (!field || !length || !field[0]) continue;
        char *relative = g_strndup(field, length), *path = g_path_is_absolute(relative) ? g_strdup(relative) : g_build_filename(folder, relative, NULL);
        g_checksum_update(sum, (const guchar *)path, strlen(path));
        GStatBuf st = {0};
        if (!g_stat(path, &st)) {
            g_checksum_update(sum, (const guchar *)&st.st_dev, sizeof st.st_dev);
            g_checksum_update(sum, (const guchar *)&st.st_ino, sizeof st.st_ino);
            g_checksum_update(sum, (const guchar *)&st.st_size, sizeof st.st_size);
            g_checksum_update(sum, (const guchar *)&st.st_mtim, sizeof st.st_mtim);
            g_checksum_update(sum, (const guchar *)&st.st_ctim, sizeof st.st_ctim);
        }
        g_free(path); g_free(relative);
    }
    g_free(folder);
    char *key = g_strdup(g_checksum_get_string(sum)); g_checksum_free(sum); return key;
}
static dt_iop_module_t *preview_module(dt_develop_t *dev, const dt_iop_module_t *source) {
    if (!source) return NULL;
    for (GList *item = dev->iop; item; item = item->next) {
        dt_iop_module_t *module = item->data;
        if (module->multi_priority == source->multi_priority && !strcmp(module->op, source->op)) return module;
    }
    return NULL;
}
static void preview_dimensions(oma_preview_cache *cache) {
    dt_dev_pixelpipe_t *pipe = &cache->pipe;
    for (GList *node = pipe->nodes; node; node = node->next) {
        dt_dev_pixelpipe_iop_t *piece = node->data;
        if (dt_iop_module_is_finalscale(piece->module)) piece->enabled = FALSE;
    }
    if (pipe->iscale > 1.f) {
        // Geometry only, with no original-size allocation. Each rounded
        // reduced buffer keeps its original crop/rotation reference frame.
        const int iw = pipe->iwidth, ih = pipe->iheight;
        const float iscale = pipe->iscale;
        pipe->iwidth = pipe->image.width; pipe->iheight = pipe->image.height; pipe->iscale = 1.f;
        for (GList *n = pipe->nodes; n; n = n->next) ((dt_dev_pixelpipe_iop_t *)n->data)->iscale = 1.f;
        dt_dev_pixelpipe_get_dimensions(pipe, &cache->dev, pipe->iwidth, pipe->iheight,
                                       &cache->reference_width, &cache->reference_height);
        for (GList *n = pipe->nodes; n; n = n->next) {
            dt_dev_pixelpipe_iop_t *piece = n->data;
            piece->omaraw_reference_in_width = piece->buf_in.width / (double)iscale;
            piece->omaraw_reference_in_height = piece->buf_in.height / (double)iscale;
            piece->omaraw_reference_out_width = piece->buf_out.width / (double)iscale;
            piece->omaraw_reference_out_height = piece->buf_out.height / (double)iscale;
            piece->iscale = iscale;
        }
        pipe->iwidth = iw; pipe->iheight = ih; pipe->iscale = iscale;
    }
    dt_dev_pixelpipe_get_dimensions(pipe, &cache->dev, pipe->iwidth, pipe->iheight,
                                    &pipe->processed_width, &pipe->processed_height);
    if (pipe->iscale == 1.f) {
        cache->reference_width = pipe->processed_width;
        cache->reference_height = pipe->processed_height;
    }
}
// Clone history onto existing private module instances. Changes to module
// topology (including enabling/disabling) rebuild the context; parameter
// and mask edits can retain nodes and cached results from unchanged earlier
// stages. Never mutate DB history.
static gboolean resync_preview(oma_preview_cache *cache) {
    if (load(cache->imgid)) return FALSE;
    GList *a = cache->dev.iop, *b = g_dev.iop;
    for (; a && b; a = a->next, b = b->next) {
        const dt_iop_module_t *old = a->data, *current = b->data;
        if (strcmp(old->op, current->op) || old->multi_priority != current->multi_priority
            || old->params_size != current->params_size || old->iop_order != current->iop_order
            || old->enabled != current->enabled) return FALSE;
    }
    if (a || b) return FALSE;
    GList *history = dt_history_duplicate(g_dev.history);
    for (GList *item = history; item; item = item->next) {
        dt_dev_history_item_t *step = item->data;
        step->module = preview_module(&cache->dev, step->module);
        if (!step->module || (step->module->params_size && !step->params) || !step->blend_params) {
            g_list_free_full(history, dt_dev_free_history_item); return FALSE;
        }
        if ((cache->uncropped && !strcmp(step->module->op, "crop"))
            || (cache->uncropped == 2 && (!strcmp(step->module->op, "ashift") || !strcmp(step->module->op, "flip")))) step->enabled = FALSE;
    }
    g_list_free_full(cache->dev.history, dt_dev_free_history_item);
    cache->dev.history = history;
    cache->dev.history_last_module = preview_module(&cache->dev, g_dev.history_last_module);
    // This private context owns its forms. Do not archive each discarded
    // copy in the interactive develop's undo pool.
    g_list_free_full(cache->dev.forms, (GDestroyNotify)dt_masks_free_form);
    cache->dev.forms = dt_masks_dup_forms_deep(g_dev.forms, NULL);
    dt_ioppr_iop_order_list_free(cache->dev.iop_order_list);
    cache->dev.iop_order_list = dt_ioppr_iop_order_copy_deep(g_dev.iop_order_list);
    cache->dev.iop_order_version = g_dev.iop_order_version;
    memcpy(&cache->dev.chroma, &g_dev.chroma, sizeof(dt_dev_chroma_t));
    cache->dev.chroma.temperature = preview_module(&cache->dev, g_dev.chroma.temperature);
    cache->dev.chroma.adaptation = preview_module(&cache->dev, g_dev.chroma.adaptation);
    cache->dev.history_end = g_dev.history_end;
    dt_dev_pop_history_items_ext(&cache->dev, g_dev.history_end);
    if (cache->uncropped == 2) for (GList *l = cache->dev.iop; l; l = l->next) {
        dt_iop_module_t *m = l->data;
        if (!strcmp(m->op, "ashift") || !strcmp(m->op, "flip") || !strcmp(m->op, "crop")) m->enabled = FALSE;
    }
    const gint64 synch_start = g_get_monotonic_time();
    dt_dev_pixelpipe_synch_all(&cache->pipe, &cache->dev);
    if (g_strcmp0(g_getenv("OMARAW_ENGINE_PROFILE"), "1") == 0)
        fprintf(stderr, "[omaraw resync %s] synch all %.1f ms\n", cache->reduced ? "reduced" : "full", (g_get_monotonic_time() - synch_start) / 1000.0);
    // The pinned engine's module hash looks up masks on its interactive
    // develop. Private mask geometry therefore needs explicit invalidation;
    // retain unaffected input/demosaic stages before the first drawn mask.
    int first_mask = INT_MAX;
    for (GList *node = cache->pipe.nodes; node; node = node->next) {
        dt_dev_pixelpipe_iop_t *piece = node->data;
        const dt_develop_blend_params_t *blend = piece->blendop_data;
        if (piece->enabled && blend && blend->mask_id > 0) first_mask = MIN(first_mask, piece->module->iop_order);
    }
    // A print below full strength blends with what the parked tone mapper
    // would have made, reading that switched-off module's settings. A
    // switched-off module is not part of the cached stages' key, so a change
    // to it alone would show the old blend: re-render from the print on.
    for (GList *node = cache->pipe.nodes; node; node = node->next) {
        dt_dev_pixelpipe_iop_t *piece = node->data;
        if (piece->enabled && !strcmp(piece->module->op, "omarawprint")) first_mask = MIN(first_mask, piece->module->iop_order);
    }
    if (first_mask != INT_MAX) dt_dev_pixelpipe_cache_invalidate_later(&cache->pipe, first_mask, "private mask geometry or print blend");
    preview_dimensions(cache);
    ++g_preview_stats.resyncs;
    return TRUE;
}
static oma_preview_cache *prepare_preview(int imgid, int original, int uncropped, int reduced) {
    if (source_available(imgid)) return NULL;
    oma_preview_cache *cache = &g_previews[preview_slot(original, reduced)];
    if (cache->ready) {
        char *resources_key = preview_resources_key(cache);
        // LUT contents can change without an edit/history change. Rebuild
        // their module data as well as invalidating downstream pixel buffers.
        if (g_strcmp0(cache->resources_key, resources_key)) clear_preview(cache);
        g_free(resources_key);
    }
    char *key = preview_key(imgid);
    if (cache->ready && cache->imgid == imgid && cache->original == original && cache->reduced == reduced
        && cache->uncropped == uncropped && (!g_reusable_preview || cache->reusable) && !g_strcmp0(cache->key, key)) {
        g_free(key); return cache;
    }
    char *source_key = preview_source_key(imgid);
    if (g_reusable_preview && cache->ready && cache->reusable && !original && cache->imgid == imgid
        && !cache->original && cache->reduced == reduced && cache->uncropped == uncropped && !g_strcmp0(cache->source_key, source_key)
        && resync_preview(cache)) {
        g_free(cache->key); cache->key = key; g_free(source_key);
        g_free(cache->resources_key); cache->resources_key = preview_resources_key(cache);
        return cache;
    }
    g_free(source_key);
    g_free(key); clear_preview(cache);
    const gboolean profile = g_strcmp0(g_getenv("OMARAW_ENGINE_PROFILE"), "1") == 0;
    gint64 t0 = profile ? g_get_monotonic_time() : 0;
#define PREPARE_MARK(what) do { if (profile) { const gint64 t1 = g_get_monotonic_time(); fprintf(stderr, "[omaraw prepare %s] %s %.1f ms\n", reduced ? "reduced" : "full", what, (t1 - t0) / 1000.0); t0 = t1; } } while (0)
    dt_dev_init(&cache->dev, FALSE);
    trim_sources(imgid); // make room before this photo is decoded
    dt_dev_load_image(&cache->dev, imgid);
    note_source(imgid);
    PREPARE_MARK("dev init+load");
    // Synching nodes reapplies the private history, so suppress crop in
    // that copy as well as the resulting module state.
    if (uncropped) for (GList *l = cache->dev.history; l; l = l->next) {
        dt_dev_history_item_t *item = l->data;
        if (!strcmp(item->module->op, "crop")
            || (uncropped == 2 && (!strcmp(item->module->op, "ashift") || !strcmp(item->module->op, "flip")))) item->enabled = FALSE;
    }
    dt_dev_pop_history_items_ext(&cache->dev, original == 1 ? 0 : cache->dev.history_end);
    if (uncropped == 2) for (GList *l = cache->dev.iop; l; l = l->next) {
        dt_iop_module_t *m = l->data;
        if (!strcmp(m->op, "ashift") || !strcmp(m->op, "flip") || !strcmp(m->op, "crop")) m->enabled = FALSE;
    }
    dt_ioppr_resync_modules_order(&cache->dev);
    PREPARE_MARK("history");
    dt_mipmap_buffer_t input;
    dt_mipmap_cache_get(&input, imgid, reduced ? DT_MIPMAP_F : DT_MIPMAP_FULL, DT_MIPMAP_BLOCKING, 'r');
    note_source(imgid);
    PREPARE_MARK("mipmap");
    if (!input.buf || input.width <= 0 || input.height <= 0 || (reduced && !(input.iscale > 1.0f))) {
        dt_mipmap_cache_release(&input); dt_dev_cleanup(&cache->dev);
        set_error(reduced ? "no reduced preview source" : "cannot load preview source", NULL); return NULL;
    }
    // Nine native tiles need more intermediate slots than a fitted view.
    // Storage remains lazy. OmaRAW trims these contexts against its shared
    // budget after each render; the engine's independent 1/64-working-memory
    // trim otherwise throws those same intermediates away before each edit.
    const gboolean allocated = g_reusable_preview
        ? dt_dev_pixelpipe_init_cached(&cache->pipe, 0, g_strcmp0(g_getenv("OMARAW_DETAIL_REUSE"), "0") ? 64 : 16, 0)
        : dt_dev_pixelpipe_init_export(&cache->pipe, 512, 512, IMAGEIO_RGB | IMAGEIO_FLOAT, FALSE);
    if (!allocated) {
        dt_mipmap_cache_release(&input); dt_dev_cleanup(&cache->dev);
        set_error("cannot allocate preview pixelpipe", NULL); return NULL;
    }
    cache->ready = TRUE; cache->imgid = imgid; cache->original = original; cache->uncropped = uncropped;
    cache->reduced = reduced;
    cache->reusable = g_reusable_preview;
    cache->pipe.type = DT_DEV_PIXELPIPE_EXPORT;
    cache->pipe.levels = IMAGEIO_RGB | IMAGEIO_FLOAT;
    ++g_preview_stats.rebuilds;
    dt_dev_pixelpipe_set_icc(&cache->pipe, DT_COLORSPACE_LIN_REC709, NULL, DT_INTENT_RELATIVE_COLORIMETRIC);
    dt_dev_pixelpipe_set_input(&cache->pipe, &cache->dev, (float *)input.buf, input.width, input.height, input.iscale);
    PREPARE_MARK("pipe init");
    dt_dev_pixelpipe_create_nodes(&cache->pipe, &cache->dev);
    PREPARE_MARK("create nodes");
    dt_dev_pixelpipe_synch_all(&cache->pipe, &cache->dev);
    PREPARE_MARK("synch all");
    preview_dimensions(cache);
    PREPARE_MARK("dimensions");
#undef PREPARE_MARK
    cache->pipe.input = NULL;
    dt_mipmap_cache_release(&input);
    // Loading a new image may have installed its initial history.
    cache->key = preview_key(imgid);
    cache->source_key = preview_source_key(imgid);
    cache->resources_key = preview_resources_key(cache);
    return cache;
}
// Reading on-picture coordinates must not replace the reusable render pipe
// with a two-buffer context. That discarded the native RAW stages on every
// parameter refresh whenever a photo contained masks or retouching.
static oma_preview_cache *prepare_geometry_preview(int imgid, int uncropped) {
    const gboolean previous = g_reusable_preview;
    g_reusable_preview = TRUE;
    oma_preview_cache *cache = prepare_preview(imgid, 0, uncropped, 0);
    g_reusable_preview = previous;
    return cache;
}
int oma_engine_preview_dimensions(int imgid, int original, int uncropped, int *width, int *height) {
    if (!g_inited || !width || !height) return -1;
    *width = *height = 0;
    oma_preview_cache *cache = prepare_preview(imgid, original, uncropped, 0);
    if (!cache) return -1;
    *width = cache->pipe.processed_width; *height = cache->pipe.processed_height;
    return 0;
}
int oma_engine_raw_clipping(int imgid, int uncropped, int width, int height, uint8_t **out) {
    if (!out) return -1;
    *out = NULL;
    if (width < 1 || height < 1 || width > 1600 || height > 1600) return -1;
    oma_preview_cache *cache = prepare_preview(imgid, 0, uncropped, 0);
    if (!cache) return -1;
    if (oma_engine_using_smart_preview(imgid)) return 2;
    const dt_image_t *im = &cache->dev.image_storage;
    if (im->buf_dsc.datatype != TYPE_UINT16 || !im->buf_dsc.filters || (im->flags & DT_IMAGE_4BAYER)) return 2;
    float white = im->raw_white_point;
    for (GList *l = cache->dev.iop; l; l = l->next) {
        dt_iop_module_t *m = l->data;
        if (!strcmp(m->op, "rawprepare") && m->so->get_p) {
            const uint16_t *value = m->so->get_p(m->params, "raw_white_point");
            if (value) white = *value;
        }
    }
    if (white <= 0) return 2;
    dt_mipmap_buffer_t input;
    dt_mipmap_cache_get(&input, imgid, DT_MIPMAP_FULL, DT_MIPMAP_BLOCKING, 'r');
    if (!input.buf) { dt_mipmap_cache_release(&input); return -1; }
    uint8_t *mask = calloc((size_t)width * height, 1);
    float *points = malloc(sizeof(float) * 2 * width);
    if (!mask || !points) { free(mask); free(points); dt_mipmap_cache_release(&input); return -1; }
    const uint16_t *raw = (const uint16_t *)input.buf;
    int result = 0;
    for (int y = 0; y < height; ++y) {
        if (preview_cancelled()) { result = OMA_ENGINE_CANCELLED; break; }
        for (int x = 0; x < width; ++x) {
            points[x*2] = (x + .5f) * cache->pipe.processed_width / width;
            points[x*2+1] = (y + .5f) * cache->pipe.processed_height / height;
        }
        dt_dev_distort_backtransform_plus(&cache->dev, &cache->pipe, 0, DT_DEV_TRANSFORM_DIR_ALL, points, width);
        for (int x = 0; x < width; ++x) {
            if (!isfinite(points[x*2]) || !isfinite(points[x*2+1])) continue;
            const float fx = points[x*2], fy = points[x*2+1];
            if (fx < 0 || fy < 0 || fx >= input.width || fy >= input.height) continue;
            const int rx = (int)fx, ry = (int)fy;
            // Include a CFA neighbourhood so downsampling cannot systematically
            // miss the clipped red or blue sites of a Bayer/X-Trans pattern.
            for (int dy=-2; dy<=2; ++dy) for (int dx=-2; dx<=2; ++dx) {
                const int xx=rx+dx, yy=ry+dy;
                if (xx>=0 && yy>=0 && xx<input.width && yy<input.height && raw[(size_t)yy*input.width+xx]>=white)
                    mask[y*width+x]=255;
            }
        }
    }
    free(points); dt_mipmap_cache_release(&input);
    if (result) free(mask); else *out = mask;
    return result;
}
// Reduced mosaics can round a crop down by less than one source pixel.
// Keep that boundary pixel available when fitting to the original geometry.
static int preview_scaled_extent(const oma_preview_cache *cache, int extent, double scale) {
    return (int)(cache->pipe.iscale > 1.f ? ceil(extent * scale) : floor(extent * scale));
}
static int render_region_direct(oma_preview_cache *cache, int x, int y, int width, int height, double scale,
                                float **out, int *w, int *h) {
    if(repair_context_valid(cache)) return -1;
    const int imgid = cache->imgid;
    dt_dev_pixelpipe_t *pipe = &cache->pipe;
    width = MIN(width, preview_scaled_extent(cache, pipe->processed_width, scale) - x);
    height = MIN(height, preview_scaled_extent(cache, pipe->processed_height, scale) - y);
    if (width <= 0 || height <= 0) { set_error("detail outside image", NULL); return -1; }
    dt_mipmap_buffer_t input;
    dt_mipmap_cache_get(&input, imgid, preview_mip(cache), DT_MIPMAP_BLOCKING, 'r');
    note_source(imgid);
    if (!input.buf) { dt_mipmap_cache_release(&input); set_error("cannot load detail source", NULL); return -1; }
    dt_dev_pixelpipe_set_input(pipe, &cache->dev, (float *)input.buf, input.width, input.height, input.iscale);
    g_mutex_lock(&g_preview_mutex);
    g_preview_pipe = pipe;
    const gboolean obsolete = preview_cancelled();
    g_mutex_unlock(&g_preview_mutex);
    // Only on-screen renders may demosaic approximately, and the engine's
    // own scale rule keeps anything at half size and above at full quality.
    // The original (before) view takes the same path as the edited view so a
    // comparison shows the edit, not two demosaicing methods. Exports run
    // their own pipes and never set it.
    const gboolean approximate = g_reduced_previews && g_set_approx_demosaic;
    if (approximate) g_set_approx_demosaic(TRUE);
    const uint64_t hits_before = pipe->cache.hits;
    const gboolean interrupted = obsolete || dt_dev_pixelpipe_process_no_gamma(pipe, &cache->dev, x, y, width, height, scale);
    ++g_preview_stats.region_renders;
    g_preview_stats.region_cache_hits += pipe->cache.hits - hits_before;
    if (approximate) g_set_approx_demosaic(FALSE);
    g_mutex_lock(&g_preview_mutex); g_preview_pipe = NULL; g_mutex_unlock(&g_preview_mutex);
    int result = preview_cancelled() ? OMA_ENGINE_CANCELLED : -1;
    if (result != OMA_ENGINE_CANCELLED && !interrupted && !oma_colour_engine_check(0, g_error, sizeof g_error)
        && pipe->backbuf && pipe->backbuf_width == width && pipe->backbuf_height == height) {
        float *pixels = malloc((size_t)width * height * 4 * sizeof(float));
        if (pixels) {
            memcpy(pixels, pipe->backbuf, (size_t)width * height * 4 * sizeof(float));
            if (!interchange_image(pixels, width, height)) {
                for (size_t i = 0; i < (size_t)width * height; ++i) pixels[4*i+3] = 1.0f;
                *out = pixels; *w = width; *h = height; result = 0;
            } else free(pixels);
        }
    }
    // The caller owns a copy now; the optional two-path blend must not
    // sit outside the retained-cache memory budget until another render.
    if ((void *)pipe->backbuf == (void *)pipe->omaraw_print_buffer) pipe->backbuf = NULL;
    dt_free_align(pipe->omaraw_print_buffer); pipe->omaraw_print_buffer = NULL;
    pipe->input = NULL;
    dt_mipmap_cache_release(&input);
    g_preview_stats.peak_bytes = MAX(g_preview_stats.peak_bytes, MAX(pipe->cache.allmem, pipe->cache.max_allmem));
    trim_sources(imgid);
    // The export backbuf aliases a cache line; the caller now owns a copy.
    // Share the configured context budget with other previews. A fixed
    // 256 MiB per-pipe cap discarded the native demosaic/denoise results
    // after every edit, even with gigabytes available in this budget.
    if (!result && cache->reusable) {
        trim_preview_pipe(cache, g_pipe_budget);
    }
    trim_preview_contexts(cache);
    if (result) {
        clear_preview(cache);
        if (result != OMA_ENGINE_CANCELLED) set_error("preview render failed", NULL);
    }
    return result;
}
// How far, in output pixels, a region's edge disturbs the pixels inside it.
// The engine takes the edge of a requested region for the edge of the picture:
// demosaicing falls back to its border interpolation there (measured on a
// 102 MP Bayer RAW: up to 0.11 linear within 9 sensor pixels, none from 16),
// and a mask is feathered or blurred without the pixels beyond it. Tiles
// rendered alone therefore meet at visible edges.
// The guided filter's window is the engine's own (_get_required_w) and 3x
// that is the overlap its internal tiling trusts; the blur is recursive, so
// six sigma limits its tail for bright RAWs and strong local corrections
// (five left 4.9e-4 between two areas in the D50 nativeTileReuse fixture).
static int region_margin(oma_preview_cache *cache, double scale) {
    if (!g_strcmp0(g_getenv("OMARAW_DETAIL_MARGIN"), "0")) return 0;
    const char *base = g_getenv("OMARAW_DETAIL_BASE_MARGIN"); // sensor pixels, for measurements
    int margin = (int)ceil((base && *base ? MAX(0, atoi(base)) : 16) * scale), refine = 0;
    for (GList *l = cache->dev.iop; l; l = l->next) {
        const dt_iop_module_t *m = l->data;
        const dt_develop_blend_params_t *bp = m->blend_params;
        if (!m->enabled || !bp || !(bp->mask_mode & DEVELOP_MASK_ENABLED)
            || !(bp->mask_mode & (DEVELOP_MASK_MASK | DEVELOP_MASK_CONDITIONAL | DEVELOP_MASK_RASTER))) continue;
        int reach = 0;
        if (bp->feathering_radius > 0.1f) reach += 16 * ((3 * MAX(1, (int)(2.0f * bp->feathering_radius * scale + 0.5f)) + 15) / 16);
        if (bp->blur_radius > 0.1f) reach += (int)ceil(6.0 * bp->blur_radius * scale);
        refine = MAX(refine, reach);
    }
    // Spatial modules that do not ask for a larger input themselves (glow,
    // bloom, local contrast, softening, texture…) only ever see the padded
    // area, so their reach must be in the margin or a bright source just
    // outside it never lights the area. Each says how far it reaches in its
    // tiling overlap; reaches of modules in sequence add up.
    int spatial = 0;
    const int full_w = MAX(1, preview_scaled_extent(cache, cache->pipe.processed_width, scale)), full_h = MAX(1, preview_scaled_extent(cache, cache->pipe.processed_height, scale));
    for (GList *node = cache->pipe.nodes; node; node = node->next) {
        dt_dev_pixelpipe_iop_t *piece = node->data;
        dt_iop_module_t *m = piece->module;
        if (!piece->enabled || !m || !m->tiling_callback || !m->modify_roi_in || !piece->data) continue;
        const dt_iop_roi_t out = {0, 0, full_w, full_h, (float)scale};
        dt_iop_roi_t in = out;
        m->modify_roi_in(m, piece, &out, &in);
        if (in.width > out.width || in.height > out.height) continue;   // it takes its own neighbours
        dt_develop_tiling_t tiling = {0};
        m->tiling_callback(m, piece, &in, &out, &tiling);
        if (tiling.overlap > 0) spatial += tiling.overlap;
        if (spatial >= 1024) break;
    }
    return MIN(margin + refine + spatial, 1024);
}
static float *copy_region(const float *from, int from_width, int x, int y, int width, int height) {
    float *pixels = malloc((size_t)width * height * 4 * sizeof(float));
    if (pixels) for (int row = 0; row < height; ++row)
        memcpy(pixels + (size_t)row * width * 4, from + ((size_t)(y + row) * from_width + x) * 4, (size_t)width * 4 * sizeof(float));
    return pixels;
}
static int render_region(oma_preview_cache *cache, int x, int y, int width, int height, double scale,
                         const int *hint, float **out, int *w, int *h) {
    cache->used = ++g_context_clock;
    const int full_w = preview_scaled_extent(cache, cache->pipe.processed_width, scale), full_h = preview_scaled_extent(cache, cache->pipe.processed_height, scale);
    width = MIN(width, full_w - x); height = MIN(height, full_h - y);
    // The whole picture has no neighbours to borrow from.
    const int margin = width > 0 && height > 0 && (x > 0 || y > 0 || width < full_w || height < full_h) ? region_margin(cache, scale) : 0;
    if (!margin) return render_region_direct(cache, x, y, width, height, scale, out, w, h);
    // Render a whole area with the margin around it and cut tiles from the
    // result, so the margin is paid once and not per tile. Any area that
    // holds the tile will do: each is exact, whichever rectangle it covers.
    for (int i = 0; i < PREVIEW_AREAS; ++i) {
        typeof(cache->areas[0]) *area = &cache->areas[i];
        if (!area->pixels || area->scale != scale || g_strcmp0(area->key, cache->key) || x < area->x || y < area->y
            || x + width > area->x + area->width || y + height > area->y + area->height) continue;
        float *pixels = copy_region(area->pixels, area->width, x - area->x, y - area->y, width, height);
        if (!pixels) { set_error("out of memory", NULL); return -1; }
        area->used = ++cache->area_clock;
        ++g_preview_stats.region_area_hits;
        *out = pixels; *w = width; *h = height;
        return 0;
    }
    // The caller's own block of tiles when it named one, else a fixed grid.
    const int side = margin <= 256 ? 1024 : 2048;
    int ax = x / side * side, ay = y / side * side, aw = MIN(side, full_w - ax), ah = MIN(side, full_h - ay);
    if (hint && hint[2] > 0 && hint[3] > 0) {
        const int hx = MAX(0, hint[0]), hy = MAX(0, hint[1]);
        const int hw = MIN(hint[2], full_w - hx), hh = MIN(hint[3], full_h - hy);
        if (x >= hx && y >= hy && x + width <= hx + hw && y + height <= hy + hh
            && (int64_t)(hw + 2 * margin) * (hh + 2 * margin) <= 32 * 1024 * 1024) { ax = hx; ay = hy; aw = hw; ah = hh; }
    }
    const gboolean shared = x >= ax && y >= ay && x + width <= ax + aw && y + height <= ay + ah;
    if (!shared) { ax = x; ay = y; aw = width; ah = height; }
    const int px = MAX(0, ax - margin), py = MAX(0, ay - margin);
    const int pw = MIN(full_w, ax + aw + margin) - px, ph = MIN(full_h, ay + ah + margin) - py;
    float *padded = NULL; int rw = 0, rh = 0;
    const int rc = render_region_direct(cache, px, py, pw, ph, scale, &padded, &rw, &rh);
    if (rc) return rc; // the failure already cleared the context
    if (rw != pw || rh != ph) { free(padded); set_error("preview render failed", NULL); return -1; }
    float *pixels = copy_region(padded, pw, x - px, y - py, width, height);
    // A request that is the whole area has nothing left to share.
    float *kept = cache->ready && shared && pixels && (aw > width || ah > height)
        && (size_t)aw * ah * 4 * sizeof(float) <= g_area_budget ? copy_region(padded, pw, ax - px, ay - py, aw, ah) : NULL;
    free(padded);
    if (!pixels) { set_error("out of memory", NULL); return -1; }
    if (kept) {
        int oldest = 0;
        for (int i = 0; i < PREVIEW_AREAS; ++i) {
            // Anything rendered under another edit can never be asked for again.
            if (cache->areas[i].pixels && g_strcmp0(cache->areas[i].key, cache->key)) {
                g_free(cache->areas[i].key); free(cache->areas[i].pixels);
                memset(&cache->areas[i], 0, sizeof cache->areas[i]);
            }
            if (cache->areas[i].used < cache->areas[oldest].used) oldest = i;
        }
        typeof(cache->areas[0]) *area = &cache->areas[oldest];
        g_free(area->key); free(area->pixels);
        area->x = ax; area->y = ay; area->width = aw; area->height = ah; area->scale = scale;
        area->key = g_strdup(cache->key); area->pixels = kept; area->used = ++cache->area_clock;
        // The tiles cut from an area live on in the viewer's cache, so older
        // areas are only worth 256 MiB. The newest always stays: it is being cut.
        for (;;) {
            size_t bytes = 0; int stale = -1;
            for (int i = 0; i < PREVIEW_AREAS; ++i) if (cache->areas[i].pixels) {
                bytes += (size_t)cache->areas[i].width * cache->areas[i].height * 4 * sizeof(float);
                if (&cache->areas[i] != area && (stale < 0 || cache->areas[i].used < cache->areas[stale].used)) stale = i;
            }
            if (bytes <= g_area_budget || stale < 0) break;
            g_free(cache->areas[stale].key); free(cache->areas[stale].pixels);
            memset(&cache->areas[stale], 0, sizeof cache->areas[stale]);
        }
    }
    trim_preview_contexts(cache);
    *out = pixels; *w = width; *h = height;
    return 0;
}
static int g_region_hint[4];
void oma_engine_region_hint(int x, int y, int width, int height) {
    g_region_hint[0] = x; g_region_hint[1] = y; g_region_hint[2] = width; g_region_hint[3] = height;
}
int oma_engine_render_region_mode(int imgid, int original, int uncropped,
                                  int x, int y, int width, int height, double scale,
                                  float **out, int *w, int *h) {
    int hint[4]; memcpy(hint, g_region_hint, sizeof hint); memset(g_region_hint, 0, sizeof g_region_hint);
    if (!g_inited || !out || !w || !h || !isfinite(scale) || scale <= 0 || scale > 1
        || x < 0 || y < 0 || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        set_error("invalid detail region", NULL); return -1;
    }
    *out = NULL; *w = *h = 0;
    if (preview_cancelled()) return OMA_ENGINE_CANCELLED;
    oma_colour_engine_check(1, NULL, 0);
    // Detail needs a reusable context even when the fitted overview uses
    // the separate reduced mosaic. Preserve a surrounding overview scope.
    const gboolean previous_reuse = g_reusable_preview;
    if (g_strcmp0(g_getenv("OMARAW_DETAIL_REUSE"), "0")) g_reusable_preview = TRUE;
    oma_preview_cache *cache = prepare_preview(imgid, original, uncropped, 0);
    g_reusable_preview = previous_reuse;
    if (preview_cancelled()) return OMA_ENGINE_CANCELLED;
    if (!cache) return -1;
    return render_region(cache, x, y, width, height, scale, hint, out, w, h);
}
int oma_engine_render_region(int imgid, int x, int y, int width, int height, double scale,
                             float **out, int *w, int *h) {
    return oma_engine_render_region_mode(imgid, 0, 0, x, y, width, height, scale, out, w, h);
}
static int render_preview(int imgid, int original, int uncropped, int max_w, int max_h, float **out, int *w, int *h) {
    if (out) *out = NULL;
    if (w) *w = 0;
    if (h) *h = 0;
    if (preview_cancelled()) return OMA_ENGINE_CANCELLED;
    int fw = 0, fh = 0;
    if (max_w <= 0 || max_h <= 0 || oma_engine_preview_dimensions(imgid, original, uncropped, &fw, &fh) || fw <= 0 || fh <= 0) return -1;
    const gboolean smart = oma_engine_using_smart_preview(imgid);
    oma_preview_cache *geometry = &g_previews[preview_slot(original, 0)];
    if (smart && geometry->ready) {
        fw = geometry->reference_width; fh = geometry->reference_height;
        max_w = MIN(max_w, geometry->pipe.processed_width);
        max_h = MIN(max_h, geometry->pipe.processed_height);
    }
    const double scale = fmin(1.0, fmin((double)MIN(max_w, 4096) / fw, (double)MIN(max_h, 4096) / fh));
    const int width = MAX(1, (int)floor(fw * scale)), height = MAX(1, (int)floor(fh * scale));
    char *overview_key = NULL;
    if (g_reusable_preview && !original && !uncropped && g_strcmp0(g_getenv("OMARAW_OVERVIEW_CACHE"), "0")) {
        // Preparing dimensions has verified source availability/identity,
        // synchronized history and checked the active external LUT files.
        const oma_preview_cache *cache = &g_previews[0];
        overview_key = g_strconcat(cache->key, ":", cache->resources_key, NULL);
        if (recall_overview(imgid, overview_key, width, height, out)) {
            *w = width; *h = height; g_free(overview_key); return 0;
        }
    }
    // A fitted overview of a source larger than the engine's reduced mosaic
    // renders from that mosaic instead: rawprepare, white balance, highlight
    // reconstruction and demosaic then run on a few megapixels. The full pipe
    // still answers dimensions, native detail tiles and exports. Convert the
    // original's physical scale with iscale; recomputing it from rounded
    // reduced crop dimensions moves edges and rotated images by a pixel.
    int result = -1;
    oma_preview_cache *full = &g_previews[preview_slot(original, 0)];
    const dt_mipmap_cache_t *mips = darktable.mipmap_cache;
    oma_preview_cache *reduced = NULL;
    if (g_reduced_previews && !oma_engine_using_smart_preview(imgid) && full->ready && mips
        && (full->pipe.iwidth > (int)mips->max_width[DT_MIPMAP_F] || full->pipe.iheight > (int)mips->max_height[DT_MIPMAP_F])) {
        reduced = prepare_preview(imgid, original, uncropped, 1);
        if (reduced && !(reduced->pipe.processed_width >= width && reduced->pipe.processed_height >= height)) reduced = NULL;
    }
    if (preview_cancelled()) return OMA_ENGINE_CANCELLED;
    if (reduced) {
        const double reduced_scale = fmin(1.0, scale * reduced->pipe.iscale);
        oma_colour_engine_check(1, NULL, 0);
        result = render_region(reduced, 0, 0, width, height, reduced_scale, NULL, out, w, h);
        if (!result) ++g_preview_stats.reduced_renders;
        else if (result != OMA_ENGINE_CANCELLED) { g_error[0] = 0; result = -1; }
    }
    if (result && result != OMA_ENGINE_CANCELLED) {
        const double source_scale = smart ? fmin(1.0, scale * geometry->pipe.iscale) : scale;
        result = oma_engine_render_region_mode(imgid, original, uncropped, 0, 0,
                                              width, height, source_scale, out, w, h);
    }
    if (!result && overview_key && !preview_cancelled()) remember_overview(imgid, overview_key, *w, *h, *out);
    g_free(overview_key);
    return result;
}
int oma_engine_render_cached(int imgid, int max_w, int max_h, float **out, int *w, int *h) {
    if (!g_inited || !out || !w || !h || max_w <= 0 || max_h <= 0) {
        set_error("invalid cached render request", NULL); return -1;
    }
    *out = NULL; *w = *h = 0;
    g_reusable_preview = TRUE;
    const int result = render_preview(imgid, 0, 0, max_w, max_h, out, w, h);
    g_reusable_preview = FALSE;
    if (result == OMA_ENGINE_CANCELLED || preview_cancelled()) {
        free(*out); *out = NULL; *w = *h = 0;
        return OMA_ENGINE_CANCELLED;
    }
    if (!result) return 0;
    ++g_preview_stats.fallbacks;
    clear_preview_cache();
    return oma_engine_render(imgid, max_w, max_h, out, w, h);
}
void oma_engine_preview_stats(oma_preview_stats *stats) {
    if (!stats) return;
    *stats = g_preview_stats; stats->retained_bytes = stats->largest_context_bytes = stats->region_area_bytes = 0;
    stats->overview_entries = g_overviews.length;
    for (int i = 0; i < PREVIEW_SLOTS; ++i) {
        if (g_previews[i].ready) {
            stats->retained_bytes += g_previews[i].pipe.cache.allmem;
            stats->largest_context_bytes = MAX(stats->largest_context_bytes, g_previews[i].pipe.cache.allmem);
        }
        for (int area = 0; area < PREVIEW_AREAS; ++area) if (g_previews[i].areas[area].pixels)
            stats->region_area_bytes += (size_t)g_previews[i].areas[area].width * g_previews[i].areas[area].height * 4 * sizeof(float);
    }
}

static dt_iop_module_t *find_instance(const char *op, int priority);
static dt_masks_form_t *instance_group(dt_iop_module_t *m);
#define OMA_STACK_MAX 16
static int stack_members(dt_iop_module_t *anchor, dt_iop_module_t **out, int max);
static float entry_opacity(const dt_masks_point_group_t *pt);
static void entry_set_opacity(dt_masks_point_group_t *pt, float v);

// Extract the engine's final drawn/parametric/refined mask, then transform
// it through the same downstream geometry as the photo. The private context
// uses CURRENT history, never rewinds or writes the catalog.
static int render_local_mask(int imgid, const char *op, int priority, int channel, int max_w, int max_h,
                              float **out, int *w, int *h) {
    if (!out || !w || !h || max_w <= 0 || max_h <= 0) return -1;
    *out = NULL; *w = *h = 0;
    oma_preview_cache *cache = prepare_preview(imgid, 3, 0, 0);
    if (!cache) return -1;
    dt_dev_pixelpipe_iop_t *source = NULL, *last = NULL;
    for (GList *n = cache->pipe.nodes; n; n = n->next) {
        dt_dev_pixelpipe_iop_t *piece = n->data;
        if (!strcmp(piece->module->op, op) && piece->module->multi_priority == priority) source = piece;
        if (piece->enabled) last = piece;
    }
    if (!source || !last) { clear_preview(cache); set_error("local mask unavailable", NULL); return -1; }
    source->enabled = TRUE; // a disabled adjustment's mask can still be inspected
    cache->pipe.omaraw_range_priority = priority;
    cache->pipe.omaraw_range_channel = channel;
    dt_dev_pixelpipe_cache_flush(&cache->pipe);
    const double scale = MIN(1.0, MIN((double)max_w / cache->pipe.processed_width,
                                     (double)max_h / cache->pipe.processed_height));
    const int width = MAX(1, (int)floor(cache->pipe.processed_width * scale));
    const int height = MAX(1, (int)floor(cache->pipe.processed_height * scale));
    float *render = NULL; int rw = 0, rh = 0;
    int rc = render_region_direct(cache, 0, 0, width, height, scale, &render, &rw, &rh);
    free(render);
    if (!rc) {
        gboolean free_mask = FALSE;
        float *mask = dt_dev_get_raster_mask(last, source->module, channel < 0 ? BLEND_RASTER_ID : -100, NULL, &free_mask);
        if (mask) {
            float *coverage = malloc((size_t)rw * rh * 4 * sizeof(float));
            if (coverage) {
                for (size_t i = 0; i < (size_t)rw * rh; ++i) {
                    coverage[4*i] = coverage[4*i+1] = coverage[4*i+2] = CLAMP(mask[i], 0.f, 1.f);
                    coverage[4*i+3] = 1.f;
                }
                *out = coverage; *w = rw; *h = rh;
            } else rc = -1;
            if (free_mask) dt_free_align(mask);
        } else { set_error("local mask unavailable", NULL); rc = -1; }
    }
    clear_preview(cache);
    return rc;
}

int oma_engine_render_mask(int imgid, const char *op, int priority, int max_w, int max_h,
                           float **out, int *w, int *h) {
    return render_local_mask(imgid, op, priority, -1, max_w, max_h, out, w, h);
}
static int compare_samples(const void *a, const void *b) {
    const float aa = *(const float *)a, bb = *(const float *)b;
    return (aa > bb) - (aa < bb);
}
int oma_engine_pick_range_area(int imgid, int priority, int channel, float x, float y, float x2, float y2,
                               float *value, float *spread) {
    if (!value || !spread || channel < 0 || channel > 2 || !isfinite(x) || !isfinite(y)
        || !isfinite(x2) || !isfinite(y2)) return -1;
    float *samples = NULL; int w = 0, h = 0;
    if (render_local_mask(imgid, "exposure", priority, channel, 1024, 1024, &samples, &w, &h)) return -1;
    int left = CLAMP((int)(MIN(x,x2)*w), 0, w-1), right = CLAMP((int)(MAX(x,x2)*w), 0, w-1);
    int top = CLAMP((int)(MIN(y,y2)*h), 0, h-1), bottom = CLAMP((int)(MAX(y,y2)*h), 0, h-1);
    if (right-left < 4) { left = MAX(0,left-2); right = MIN(w-1,right+2); }
    if (bottom-top < 4) { top = MAX(0,top-2); bottom = MIN(h-1,bottom+2); }
    const int step = MAX(1, (MAX(right-left+1,bottom-top+1)+127)/128);
    float values[128*128];
    double sum = 0, sine = 0, cosine = 0; int count = 0;
    for (int iy = top; iy <= bottom; iy += step)
        for (int ix = left; ix <= right; ix += step) {
            const float v = samples[4*((size_t)iy*w+ix)];
            if (!isfinite(v)) continue;
            values[count++] = v; sum += v; sine += sin(v*2*M_PI); cosine += cos(v*2*M_PI);
        }
    if (!count) { free(samples); set_error("no usable colour samples", NULL); return -1; }
    *value = channel == 1 ? fmod(atan2(sine, cosine)/(2*M_PI)+1, 1) : sum/count;
    for (int i = 0; i < count; ++i) {
        float distance = fabsf(values[i] - *value);
        values[i] = channel == 1 ? MIN(distance, 1.f-distance) : distance;
    }
    qsort(values, count, sizeof(float), compare_samples);
    *spread = values[MIN(count-1, (int)(count*.9))];
    free(samples); return 0;
}
int oma_engine_pick_range(int imgid, int priority, int channel, float x, float y, float *value) {
    float spread = 0;
    return oma_engine_pick_range_area(imgid, priority, channel, x, y, x, y, value, &spread);
}

int oma_engine_render_original(int imgid, int max_w, int max_h, float **out, int *w, int *h) {
    return render_preview(imgid, 1, 0, max_w, max_h, out, w, h);
}

int oma_engine_format_available(const char *name) {
    return g_inited && dt_imageio_get_format_by_name(name) != NULL;
}

const char *oma_engine_format_extension(const char *name) {
    dt_imageio_module_format_t *format = g_inited ? dt_imageio_get_format_by_name(name) : NULL;
    if (!format) return "";
    dt_imageio_module_data_t *d = format->get_params(format);
    const char *ext = d ? format->extension(d) : "";
    static char buf[16];
    g_strlcpy(buf, ext ? ext : "", sizeof buf);
    if (d) format->free_params(format, d);
    return buf;
}

int oma_engine_export_jpeg(int imgid, const char *path, int max_w, int max_h, int quality) {
    char out[PATH_MAX];
    return oma_engine_export(imgid, path, "jpeg", max_w, max_h, quality, 8, out, sizeof out);
}

int oma_engine_export(int imgid, const char *path, const char *name, int max_w, int max_h, int quality, int bpp,
                      char *out_path, int out_len) {
    return oma_engine_export_meta(imgid, path, name, max_w, max_h, quality, bpp, -1, out_path, out_len);
}

int oma_engine_export_meta(int imgid, const char *path, const char *name, int max_w, int max_h, int quality, int bpp,
                           int meta_flags, char *out_path, int out_len) {
    return oma_engine_export_profile(imgid, path, name, max_w, max_h, quality, bpp, meta_flags, 0, NULL, 0, 0, out_path, out_len);
}

// All bridge calls are serialized on the engine worker. This adapter asks the
// pixelpipe for float linear Rec.709, then hands the real writer its own bit
// depth and target ICC. Disk storage and metadata policy stay engine-owned.
static struct {
    dt_imageio_module_format_t *format;
    int profile, intent;
    dt_colorspaces_color_profile_type_t type;
    const char *filename;
    void *source_icc, *target_icc;
    cmsUInt32Number source_size, target_size;
    const char *description;
    char *source_xmp;
    int meta, native_metadata;
    oma_export_hook_fn finish;
    void *finish_data;
    int interchange_ready;
} g_output;

static void colour_finish(float *pixels, int w, int h, int bpp, void *unused) {
    (void)unused;
    g_output.interchange_ready = !interchange_image(pixels, w, h);
    if (g_output.interchange_ready && g_output.finish)
        g_output.finish(pixels, w, h, bpp, g_output.finish_data);
}

static void *profile_bytes(cmsHPROFILE profile, cmsUInt32Number *size) {
    if (!profile || !cmsSaveProfileToMem(profile, NULL, size) || !*size) return NULL;
    void *bytes = malloc(*size);
    if (!bytes || !cmsSaveProfileToMem(profile, bytes, size)) { free(bytes); return NULL; }
    return bytes;
}

static int colour_write(dt_imageio_module_data_t *data, const char *filename, const void *in,
                         dt_colorspaces_color_profile_type_t over_type, const char *over_filename,
                         void *exif, int exif_len, dt_imgid_t imgid, int num, int total,
                         struct dt_dev_pixelpipe_t *pipe, const gboolean export_masks) {
    (void)over_type; (void)over_filename;
    if (oma_colour_engine_check(0, g_error, sizeof g_error)) return 1;
    if (!g_output.interchange_ready) {
        if (!g_error[0]) set_error("export interchange conversion was not completed", NULL);
        return 1;
    }
    float *pixels = (float *)in;
    const int depth = g_output.format->bpp(data);
    if (!pixels || (depth != 8 && depth != 16 && depth != 32)) {
        set_error("unsupported colour output bit depth", NULL); return 1;
    }
    if (oma_colour_output(pixels, data->width, data->height, g_output.profile,
                           g_output.source_icc, g_output.source_size, g_output.target_icc, g_output.target_size,
                           g_output.intent, g_error, sizeof g_error)) return 1;
    const size_t count = (size_t)data->width * data->height;
    // Quantise only after OCIO. Writing towards the start is safe because
    // the float source is larger than either integer destination.
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float v = isfinite(pixels[4*i+c]) ? pixels[4*i+c] : 0.f;
            if (depth == 8) ((uint8_t *)pixels)[4*i+c] = (uint8_t)roundf(CLAMP(v, 0.f, 1.f) * 255.f);
            else if (depth == 16) ((uint16_t *)pixels)[4*i+c] = (uint16_t)roundf(CLAMP(v, 0.f, 1.f) * 65535.f);
            else pixels[4*i+c] = v;
        }
        if (depth == 8) ((uint8_t *)pixels)[4*i+3] = 255;
        else if (depth == 16) ((uint16_t *)pixels)[4*i+3] = 65535;
        else pixels[4*i+3] = 1.f;
    }
    void *encoded_exif = NULL;
    char *encoded_xmp = NULL;
    if (g_output.native_metadata) {
        int size = 0;
        if (oma_export_metadata(exif, exif_len, g_output.source_xmp, g_output.meta, g_output.description,
                                &encoded_exif, &size, &encoded_xmp, g_error, sizeof g_error)) return 1;
        exif = encoded_exif; exif_len = size;
        g_set_export_xmp(encoded_xmp);
    }
    const int result = g_output.format->write_image(data, filename, pixels, g_output.type, g_output.filename,
                                                   exif, exif_len, imgid, num, total, pipe, export_masks);
    if (g_output.native_metadata) g_set_export_xmp(NULL);
    free(encoded_exif); free(encoded_xmp);
    return result;
}

int oma_engine_export_profile(int imgid, const char *path, const char *name, int max_w, int max_h, int quality, int bpp,
                              int meta_flags, int profile, const void *icc, int icc_size, int intent, char *out_path, int out_len) {
    return oma_engine_export_described(imgid, path, name, max_w, max_h, quality, bpp, meta_flags, profile, icc, icc_size,
                                       intent, NULL, out_path, out_len);
}

void oma_engine_set_export_hook(oma_export_hook_fn hook, void *data) {
    dt_omaraw_export_hook = hook;
    dt_omaraw_export_hook_data = data;
}

// Validate the entire table container before export, including cached profiles.
// A missing/truncated external profile must not silently export the plain image.
static int camera_tables_valid(const char *path) {
    FILE *f = g_fopen(path, "rb");
    if (!f) return 0;
    char magic[4]; uint32_t head[3]; float values[17];
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "OMPT", 4)
        && fread(head, 4, 3, f) == 3 && head[0] == 3 && head[1] >= 2 && head[1] <= 256
        && head[2] >= 2 && head[2] <= 65536 && fread(values, 4, 17, f) == 17;
    if (ok) {
        for (int i = 0; i < 17; ++i) ok = ok && isfinite(values[i]);
        ok = ok && values[0] > 0 && values[3] > values[2] && values[5] > 0 && values[7] < 0;
        const uint64_t expected = 84 + 12ULL * ((uint64_t)head[1] * head[1] * head[1] + head[2]);
        ok = ok && !fseek(f, 0, SEEK_END) && ftell(f) >= 0 && (uint64_t)ftell(f) == expected;
    }
    fclose(f); return ok;
}

int oma_engine_export_described(int imgid, const char *path, const char *name, int max_w, int max_h, int quality, int bpp,
                                int meta_flags, int profile, const void *icc, int icc_size, int intent,
                                const char *description_json, char *out_path, int out_len) {
    if (!g_inited) { set_error("engine not initialised", NULL); return -1; }
    if (oma_engine_repair_check(imgid)) return -1;
    if (source_available(imgid)) return -1;
    if (oma_engine_using_smart_preview(imgid)) {
        set_error("Reconnect the original or its full offline copy to export; only a Smart Preview is available", NULL); return -1;
    }
    g_error[0] = 0;
    oma_param_info camera_stock = {0};
    oma_engine_param_get(imgid, "omarawprint", "stock", &camera_stock);
    if (camera_stock.found && camera_stock.enabled && camera_stock.value == 22) {
        char tables[1024] = {0};
        if (oma_engine_param_get_string(imgid, "omarawprint", "profile", tables, sizeof tables)
            || !camera_tables_valid(tables)) {
            set_error("Camera profile tables are missing or invalid; select or import the profile again before exporting", NULL);
            return -1;
        }
    }
    char dng_source[PATH_MAX]; gboolean dng_cache = FALSE;
    dt_image_full_path(imgid, dng_source, sizeof dng_source, &dng_cache);
    const char *dng_suffix = strrchr(dng_source, '.');
    if (dng_suffix && !g_ascii_strcasecmp(dng_suffix, ".dng")) {
        oma_dng_gain gain = {0};
        const int gain_status = oma_dng_gain_read(dng_source, &gain);
        oma_dng_gain_clear(&gain);
        if (gain_status < 0) { set_error("DNG profile gain metadata is malformed; export was stopped", NULL); return -1; }
    }
    oma_param_info creative = {0}, amount = {0};
    oma_engine_param_get(imgid, "omarawprofile", "look", &creative);
    oma_engine_param_get(imgid, "omarawprofile", "amount", &amount);
    if (creative.found && creative.enabled && creative.value == 7) {
        char resource[512] = {0}; oma_creative_data cube = {0};
        const int valid = oma_engine_param_get_string(imgid, "omarawprofile", "filepath", resource, sizeof resource) == 0
                          && oma_creative_read(resource, &cube);
        const int supported = !cube.enhanced || (oma_engine_is_scene_referred(imgid)>0 ? cube.scene : cube.output);
        oma_creative_clear(&cube);
        if (!supported) { set_error("Creative XMP profile does not support this photo type", NULL); return -1; }
        if (!valid) { set_error("Creative profile LUT is missing or invalid; import it again before exporting", NULL); return -1; }
    }
    oma_colour_engine_check(1, NULL, 0);
    if (profile < 0 || profile > 3 || intent < 0 || intent > 3) { set_error("invalid output colour settings", NULL); return -1; }
    const dt_colorspaces_color_profile_type_t types[] = {DT_COLORSPACE_SRGB, DT_COLORSPACE_ADOBERGB, DT_COLORSPACE_PROPHOTO_RGB, DT_COLORSPACE_FILE};
    const dt_colorspaces_color_profile_type_t output_type = types[profile];
    char profile_name[128] = {0};
    if (profile == 3) {
        if (!icc || icc_size <= 0) { set_error("missing output ICC profile", NULL); return -1; }
        gchar *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, icc, icc_size);
        snprintf(profile_name, sizeof profile_name, "/omaraw-profiles/%s.icc", hash);
        g_free(hash);
        if (!dt_colorspaces_get_profile(DT_COLORSPACE_FILE, profile_name, DT_PROFILE_DIRECTION_OUT)) {
            cmsHPROFILE handle = cmsOpenProfileFromMem(icc, icc_size);
            if (!handle || cmsGetColorSpace(handle) != cmsSigRgbData) {
                if (handle) cmsCloseProfile(handle);
                set_error("output ICC must be a readable RGB profile", NULL); return -1;
            }
            dt_colorspaces_color_profile_t *p = calloc(1, sizeof(*p));
            if (!p) { cmsCloseProfile(handle); set_error("cannot allocate output profile", NULL); return -1; }
            p->type = DT_COLORSPACE_FILE;
            p->profile = handle;
            g_strlcpy(p->filename, profile_name, sizeof(p->filename));
            cmsGetProfileInfoASCII(handle, cmsInfoDescription, "en", "US", p->name, sizeof(p->name));
            p->in_pos = p->display_pos = p->display2_pos = p->category_pos = p->work_pos = -1;
            p->out_pos = 0;
            darktable.color_profiles->profiles = g_list_append(darktable.color_profiles->profiles, p);
        }
    }
    const dt_colorspaces_color_profile_t *selected_profile = dt_colorspaces_get_profile(output_type, profile_name, DT_PROFILE_DIRECTION_OUT);
    if (!selected_profile) {
        set_error("output profile is unavailable", NULL); return -1;
    }
    if (!cmsIsIntentSupported(selected_profile->profile, intent, LCMS_USED_AS_OUTPUT)) {
        set_error("the output profile does not support this rendering intent", NULL); return -1;
    }
    dt_imageio_module_format_t *format = dt_imageio_get_format_by_name(name);
    dt_imageio_module_storage_t *storage = dt_imageio_get_storage_by_name("disk");
    if (!format || !storage) { set_error("format %s or disk storage missing", name); return -1; }
    // Each format reads its options from conf when get_params runs.
    char key[128];
    snprintf(key, sizeof key, "plugins/imageio/format/%s/quality", name);
    dt_conf_set_int(key, quality);
    snprintf(key, sizeof key, "plugins/imageio/format/%s/bpp", name);
    dt_conf_set_int(key, bpp);
    if (!strcmp(name, "tiff")) dt_conf_set_int("plugins/imageio/format/tiff/compress", 1);
    if (!strcmp(name, "png")) dt_conf_set_int("plugins/imageio/format/png/compression", 5);
    // 0 = a unique name beside an existing file, 1 = replace it (disk.c).
    dt_conf_set_int("plugins/imageio/storage/disk/overwrite", g_export_overwrite ? 1 : 0);
    dt_imageio_module_data_t *fdata = format->get_params(format);
    dt_imageio_module_data_t *sdata = storage->get_params(storage);
    if (!fdata || !sdata) {
        if (fdata) format->free_params(format, fdata);
        if (sdata) storage->free_params(storage, sdata);
        set_error("cannot get export params", NULL); return -1;
    }
    // The disk storage's params begin with the filename template; darktable-cli
    // writes the target path straight into it, and so do we. The storage
    // appends the format's extension itself, so hand it the stem. Callers
    // pass stems, and a stem may hold dots ("2026.09.26 Holiday"): only the
    // format's own extension is taken off ("look.jpg" for a JPEG).
    const char *fext = format->extension(fdata);
    char *stem = g_strdup(path);
    char *dot = strrchr(stem, '.');
    if (dot && !strchr(dot, '/') && dot[1]
        && ((fext && !g_ascii_strcasecmp(dot + 1, fext)) || !g_ascii_strcasecmp(dot + 1, name)
            || (!strcmp(name, "jpeg") && !g_ascii_strcasecmp(dot + 1, "jpg")) || (!strcmp(name, "tiff") && !g_ascii_strcasecmp(dot + 1, "tif"))))
        *dot = 0;
    g_strlcpy((char *)sdata, stem, DT_MAX_PATH_FOR_PARAMS);
    // An existing file is replaced only when the caller asked for that;
    // otherwise the storage picks "name_01.ext", and the caller is told the
    // name it really wrote (the same search the storage makes).
    char written[PATH_MAX];
    snprintf(written, sizeof written, "%s.%s", stem, fext ? fext : name);
    if (!g_export_overwrite)
        for (int seq = 1; g_file_test(written, G_FILE_TEST_EXISTS) && seq < 10000; ++seq)
            snprintf(written, sizeof written, "%s_%.2d.%s", stem, seq, fext ? fext : name);
    g_free(stem);
    fdata->max_width = max_w;
    fdata->max_height = max_h;
    fdata->style[0] = 0;
    fdata->style_append = 1;
    GList *ids = g_list_append(NULL, GINT_TO_POINTER(imgid));
    if (storage->initialize_store)
        storage->initialize_store(storage, sdata, &format, &fdata, &ids, TRUE, FALSE);
    dt_export_metadata_t metadata;
    metadata.flags = meta_flags < 0 ? dt_lib_export_metadata_default_flags() : (uint32_t)meta_flags;
    metadata.list = NULL;
    memset(&g_output, 0, sizeof g_output);
    g_output.format = format; g_output.profile = profile; g_output.intent = intent;
    g_output.type = output_type; g_output.filename = profile_name;
    g_output.description = description_json;
    g_output.meta = metadata.flags;
    g_output.native_metadata = !strcmp(name, "avif") || !strcmp(name, "jpegxl");
    if (g_output.native_metadata) g_output.source_xmp = dt_exif_xmp_read_string(imgid);
    int interchange_size = 0;
    const void *interchange = oma_colour_interchange_profile(&interchange_size);
    if (interchange && interchange_size > 0) {
        g_output.source_icc = malloc(interchange_size);
        if (g_output.source_icc) {
            memcpy(g_output.source_icc, interchange, interchange_size);
            g_output.source_size = interchange_size;
        }
    }
    g_output.target_icc = profile_bytes(selected_profile->profile, &g_output.target_size);
    dt_imageio_module_format_t adapter = *format;
    adapter.bpp = mem_bpp; adapter.levels = mem_levels; adapter.write_image = colour_write;
    // The engine normally suppresses all AVIF/JXL metadata unless every
    // metadata flag is set. Our writer filters the packets before encoding,
    // so request the ordinary source EXIF path and apply the chosen policy.
    if (g_output.native_metadata) adapter.mime = metadata_mime;
    int res = 1;
    g_output.finish = dt_omaraw_export_hook;
    g_output.finish_data = dt_omaraw_export_hook_data;
    dt_omaraw_export_hook = colour_finish;
    dt_omaraw_export_hook_data = NULL;
    if (g_output.source_icc && g_output.target_icc)
        res = storage->store(storage, sdata, imgid, &adapter, fdata, 1, 1, g_export_full_resolution, FALSE, FALSE, 1.0, FALSE,
                              DT_COLORSPACE_LIN_REC709, NULL, DT_INTENT_RELATIVE_COLORIMETRIC, &metadata);
    else set_error("cannot read output colour profiles", NULL);
    dt_omaraw_export_hook = g_output.finish;
    dt_omaraw_export_hook_data = g_output.finish_data;
    free(g_output.source_icc); free(g_output.target_icc); g_free(g_output.source_xmp);
    memset(&g_output, 0, sizeof g_output);
    if (storage->finalize_store) storage->finalize_store(storage, sdata);
    if (out_path && out_len > 0) g_strlcpy(out_path, written, (size_t)out_len);
    storage->free_params(storage, sdata);
    format->free_params(format, fdata);
    g_list_free(ids);
    if (res && !g_error[0]) set_error("export failed", NULL);
    return res ? -1 : 0;
}

// ── parameters ──────────────────────────────────────────────────────────
static dt_iop_module_t *find_module(const char *op) {
    for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
        dt_iop_module_t *m = (dt_iop_module_t *)l->data;
        if (!strcmp(m->op, op) && m->multi_priority == 0) return m;
    }
    for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
        dt_iop_module_t *m = (dt_iop_module_t *)l->data;
        if (!strcmp(m->op, op)) return m;
    }
    return NULL;
}

static int field_type(const dt_introspection_field_t *f) {
    switch (f->header.type) {
    case DT_INTROSPECTION_TYPE_FLOAT: return 0;
    case DT_INTROSPECTION_TYPE_INT: case DT_INTROSPECTION_TYPE_UINT: return 1;
    case DT_INTROSPECTION_TYPE_BOOL: return 2;
    case DT_INTROSPECTION_TYPE_ENUM: return 3;
    default: return 4;
    }
}

// darktable's generated accessors only name element zero. Walk array
// descriptors for the other elements, checking every dimension before access.
static void *numeric_field(dt_iop_module_t *m, const char *name, dt_introspection_field_t **field) {
    *field = NULL;
    if (!m || !m->so->get_f || !m->so->get_p || !name) return NULL;
    const char *bracket = strchr(name, '[');
    if (!bracket) {
        *field = m->so->get_f(name);
        return *field ? m->so->get_p(m->params, name) : NULL;
    }
    char base[128];
    const size_t n = bracket - name;
    if (!n || n >= sizeof base) return NULL;
    memcpy(base, name, n); base[n] = 0;
    dt_introspection_field_t *f = m->so->get_f(base);
    void *p = f ? m->so->get_p(m->params, base) : NULL;
    const char *s = bracket;
    while (p && *s == '[') {
        ++s;
        if (!g_ascii_isdigit(*s)) return NULL;
        char *end = NULL;
        const guint64 index = g_ascii_strtoull(s, &end, 10);
        if (!end || *end != ']' || index > G_MAXUINT) return NULL;
        p = dt_introspection_access_array(f, p, (unsigned)index, &f);
        s = end + 1;
    }
    if (p && *s == '.') {
        p = dt_introspection_get_child(f, p, s+1, &f);
        s += strlen(s);
    }
    if (*s || !p || (char *)p < (char *)m->params
        || (char *)p + f->header.size > (char *)m->params + m->params_size) return NULL;
    *field = f;
    return p;
}

static void append_numeric_fields(GString *names, dt_introspection_field_t *f, const char *path) {
    if (!f) return;
    if (f->header.type == DT_INTROSPECTION_TYPE_STRUCT) {
        for (dt_introspection_field_t **c = f->Struct.fields; c && *c; ++c) {
            char *name = *path ? g_strdup_printf("%s.%s", path, (*c)->header.field_name) : g_strdup((*c)->header.field_name);
            append_numeric_fields(names, *c, name); g_free(name);
        }
    } else if (f->header.type == DT_INTROSPECTION_TYPE_ARRAY) {
        for (size_t i = 0; i < f->Array.count; ++i) {
            char *name = g_strdup_printf("%s[%zu]", path, i);
            append_numeric_fields(names, f->Array.field, name); g_free(name);
        }
    } else if (field_type(f) < 4) { g_string_append(names, path); g_string_append_c(names, '\n'); }
}
// Only the three known tone mappers are serialized this way. Typed numeric
// rows keep snapshots readable and use the ordinary validated setters.
int oma_engine_tone_fields(int imgid, const char *op, char *out, int length) {
    if (!out || length <= 0 || (strcmp(op,"sigmoid") && strcmp(op,"filmicrgb") && strcmp(op,"basecurve"))) return -1;
    out[0] = 0;
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module(op);
    if (!m || !m->so->get_introspection) return -1;
    GString *names = g_string_new("");
    append_numeric_fields(names, m->so->get_introspection()->field, "");
    const int fits = names->len < (size_t)length;
    if (fits) g_strlcpy(out, names->str, length);
    g_string_free(names, TRUE); return fits ? 0 : -1;
}

static gboolean camera_look_op(const char *op) {
    return op && (!strcmp(op, "sigmoid") || !strcmp(op, "filmicrgb")
                  || !strcmp(op, "basecurve") || !strcmp(op, "colorbalancergb"));
}

int oma_engine_module_enabled(int imgid, const char *op) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module(op);
    return m ? !!m->enabled : -1;
}

char *oma_engine_match_signature(int imgid) {
    if (load(imgid)) return NULL;
    return preview_key(imgid);
}
int oma_engine_match_set(int imgid, const float *values, int count, int enabled) {
    if (!values || count != (int)(sizeof(oma_match_params)/sizeof(float))) {
        set_error("Invalid Image Match parameter layout", NULL); return -1;
    }
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("omarawmatch");
    if (!m || (size_t)m->params_size != (size_t)count * sizeof(float)) { set_error("Image Match needs the updated engine", NULL); return -1; }
    for (int i = 0; i < count; ++i) if (!isfinite(values[i])) return -1;
    if (!memcmp(m->params, values, count * sizeof(float)) && !!m->enabled == !!enabled) return 0;
    if (oma_engine_begin_edit(imgid)) return -1;
    memcpy(m->params, values, count * sizeof(float)); m->enabled = !!enabled;
    oma_add_history_item(&g_dev, m, FALSE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

int oma_engine_camera_look_fields(int imgid, const char *op, char *out, int length) {
    if (!out || length <= 0 || !camera_look_op(op) || load(imgid)) return -1;
    out[0] = 0;
    dt_iop_module_t *m = find_module(op);
    if (!m || !m->so->get_introspection) return -1;
    GString *names = g_string_new("");
    append_numeric_fields(names, m->so->get_introspection()->field, "");
    const int fits = names->len < (size_t)length;
    if (fits) g_strlcpy(out, names->str, length);
    g_string_free(names, TRUE);
    return fits ? 0 : -1;
}

char *oma_engine_camera_style_fields(int imgid, const char *op, int version, const char *encoded) {
    if (!camera_look_op(op) || !encoded || strlen(encoded) > 16384 || load(imgid)) return NULL;
    dt_iop_module_t *m = find_module(op);
    // The styles and engine are shipped at the same pinned revision. Refuse
    // incompatible data rather than guessing at a different module layout.
    if (!m || !m->so->get_introspection || version != m->version()) return NULL;
    int size = 0;
    unsigned char *blob = dt_exif_xmp_decode(encoded, strlen(encoded), &size);
    if (!blob || size != m->params_size) { g_free(blob); return NULL; }
    dt_iop_module_t copy = *m;
    copy.params = blob;
    GString *names = g_string_new(""), *rows = g_string_new("");
    append_numeric_fields(names, m->so->get_introspection()->field, "");
    gchar **fields = g_strsplit(names->str, "\n", -1);
    gboolean valid = TRUE;
    for (int i = 0; fields[i] && *fields[i]; ++i) {
        dt_introspection_field_t *field = NULL;
        void *p = numeric_field(&copy, fields[i], &field);
        if (!p) { valid = FALSE; break; }
        const int type = field_type(field);
        const double value = type == 0 ? *(float *)p : *(int *)p;
        if (!isfinite(value) || type > 3) { valid = FALSE; break; }
        char number[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(number, sizeof number, value);
        g_string_append_printf(rows, "%s\t%s\n", fields[i], number);
    }
    g_strfreev(fields); g_string_free(names, TRUE); g_free(blob);
    if (!valid) { g_string_free(rows, TRUE); return NULL; }
    return g_string_free(rows, FALSE);
}

int oma_engine_param_get(int imgid, const char *op, const char *field, oma_param_info *info) {
    memset(info, 0, sizeof *info);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module(op);
    if (!m || !m->so->get_f || !m->so->get_p) return 0;
    dt_introspection_field_t *f = NULL;
    void *p = numeric_field(m, field, &f);
    if (!p) return 0;
    info->found = 1;
    info->enabled = m->enabled;
    info->type = field_type(f);
    switch (info->type) {
    case 0: info->value = *(float *)p; info->min = f->Float.Min; info->max = f->Float.Max; info->def = f->Float.Default; break;
    case 1: info->value = (float)*(int *)p; info->min = (float)f->Int.Min; info->max = (float)f->Int.Max; info->def = (float)f->Int.Default; break;
    case 2: info->value = *(gboolean *)p ? 1.f : 0.f; info->min = 0; info->max = 1; info->def = 0; break;
    case 3: {
        info->value = (float)*(int *)p; info->def = (float)f->Enum.Default;
        int lo = 0, hi = 0, first = 1;
        for (dt_introspection_type_enum_tuple_t *t = f->Enum.values; t && t->name; ++t) {
            if (first || t->value < lo) lo = t->value;
            if (first || t->value > hi) hi = t->value;
            first = 0;
        }
        info->min = (float)lo; info->max = (float)hi;
        break;
    }
    default: break;
    }
    if (!strcmp(op, "omarawprint") && !strcmp(field, "base_tone"))
        info->value = dt_omaraw_print_base_tone(&g_dev, m);
    // Choices such as demosaic and input profile also have image-specific
    // defaults; the introspection enum's compile-time default is not enough.
    if (m->default_params && info->type <= 3) {
        const void *d = (char *)m->default_params + ((char *)p - (char *)m->params);
        info->def = info->type == 0 ? *(const float *)d : (float)*(const int *)d;
    }
    return 0;
}

static dt_introspection_field_t *enum_field(int imgid, const char *op, const char *field) {
    if (load(imgid)) return NULL;
    dt_iop_module_t *m = find_module(op);
    if (!m || !m->so->get_f) return NULL;
    dt_introspection_field_t *f = m->so->get_f(field);
    return f && f->header.type == DT_INTROSPECTION_TYPE_ENUM ? f : NULL;
}

int oma_engine_param_enum_count(int imgid, const char *op, const char *field) {
    dt_introspection_field_t *f = enum_field(imgid, op, field);
    return f ? (int)f->Enum.entries : 0;
}

const char *oma_engine_param_enum_option(int imgid, const char *op, const char *field, int index, int *value) {
    dt_introspection_field_t *f = enum_field(imgid, op, field);
    if (!f || index < 0 || index >= (int)f->Enum.entries) return NULL;
    const dt_introspection_type_enum_tuple_t *t = &f->Enum.values[index];
    if (value) *value = t->value;
    return t->description && *t->description ? t->description : t->name;
}

int oma_engine_begin_edit(int imgid) {
    if (load(imgid)) return -1;
    ++g_dev.focus_hash;
    g_new_step = TRUE;
    return 0;
}

// One value into one introspected field, the same way for every writer
// (the photo's modules, a local's instance, its stack members): finite,
// within the module's own limits, rounded for whole-number fields.
static int store_field(dt_introspection_field_t *f, void *p, const char *field, float value) {
    if (!isfinite(value)) { set_error("non-finite value for %s", field); return -1; }
    if ((field_type(f) == 1 || field_type(f) == 3) && (value < INT_MIN || value >= INT_MAX)) { set_error("value out of range for %s", field); return -1; }
    // Within the module's own limits, whoever asks: a hand-edited preset or
    // a scripted value must not store what the module was never built for.
    if (field_type(f) == 0 && f->Float.Max > f->Float.Min) value = fmaxf(f->Float.Min, fminf(f->Float.Max, value));
    if (f->header.type == DT_INTROSPECTION_TYPE_INT && f->Int.Max > f->Int.Min) value = fmaxf((float)f->Int.Min, fminf((float)f->Int.Max, value));
    if (f->header.type == DT_INTROSPECTION_TYPE_UINT && f->UInt.Max > f->UInt.Min) value = fmaxf((float)f->UInt.Min, fminf((float)f->UInt.Max, value));
    switch (field_type(f)) {
    case 0: *(float *)p = value; break;
    case 1: case 3: *(int *)p = (int)(value + (value >= 0 ? 0.5f : -0.5f)); break;
    case 2: *(gboolean *)p = value != 0; break;
    default: set_error("unsupported field type for %s", field); return -1;
    }
    return 0;
}

static void capture_print_baseline(dt_iop_module_t *m) {
    if (strcmp(m->op, "omarawprint") || m->enabled || !m->so->get_p) return;
    int *saved = m->so->get_p(m->params, "base_tone");
    if (!saved) return;
    *saved = 0;
    const char *ops[] = {"sigmoid", "filmicrgb", "basecurve"};
    for (int i = 0; i < 3; ++i) {
        dt_iop_module_t *tone = find_module(ops[i]);
        if (tone && tone->enabled) *saved |= 1 << i;
    }
}

int oma_engine_param_set(int imgid, const char *op, const char *field, float value) {
    if (!isfinite(value)) { set_error("non-finite value for %s", field); return -1; }
    if (!strcmp(op, "ashift") && (!strcmp(field, "rotation") || !strcmp(field, "lensshift_v") || !strcmp(field, "lensshift_h") || !strcmp(field, "cropmode"))
        && oma_engine_geometry_available()) return geometry_param_set(imgid, field, value);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module(op);
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no module %s", op); return -1; }
    dt_introspection_field_t *f = NULL;
    void *p = numeric_field(m, field, &f);
    if (!p) { set_error("no field %s", field); return -1; }
    if(!strcmp(op,"rgblevels") && !strncmp(field,"levels[",7)) {
        int channel=-1,index=-1,n=0;
        if(sscanf(field,"levels[%d][%d]%n",&channel,&index,&n)!=2 || field[n] || channel<0 || channel>2 || index<0 || index>2) return -1;
        float levels[3];
        for(int i=0;i<3;++i){char name[32];snprintf(name,sizeof name,"levels[%d][%d]",channel,i);dt_introspection_field_t *fi=NULL;float *v=numeric_field(m,name,&fi);if(!v)return -1;levels[i]=*v;}
        const float lo=index?levels[index-1]+.0001f:0, hi=index<2?levels[index+1]-.0001f:1;
        levels[index]=fmaxf(lo,fminf(hi,value));
        return oma_engine_levels_set(imgid,channel,levels[0],levels[1],levels[2]);
    }
    if((!strcmp(op,"denoiseprofile") || !strcmp(op,"atrous")) && !strncmp(field,"y[",2) && (value<0 || value>1)) return -1;
    if(!strcmp(op,"retouch") && ((!strcmp(field,"num_scales") && (value<0 || value>15)) || (!strcmp(field,"curr_scale") && (value<0 || value>16)))) return -1;
    if (strcmp(field, "base_tone")) capture_print_baseline(m);
    if (store_field(f, p, field, value)) return -1;
    // The native lens module otherwise renders its automatic defaults, even
    // when the stored scale/correction values have changed. Its GTK callbacks
    // set this flag; our controls and restored recipes must do the same.
    if (!strcmp(op, "lens")) {
        gboolean *configured = m->so->get_p(m->params, "has_been_set");
        if (configured) *configured = TRUE;
    }
    oma_add_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

// ── tone curves over rgbcurve ───────────────────────────────────────────
#include "common/curve_tools.h"
#define OMA_CURVE_CHANNELS 3

typedef struct { float x, y; } oma_curve_node;
typedef struct {
    dt_iop_module_t *m;
    oma_curve_node *nodes;   // [3][20]
    int *num, *type, *autoscale;
    gboolean *compensate;
} oma_curve_fields;

static int curve_fields(int imgid, oma_curve_fields *cf) {
    memset(cf, 0, sizeof *cf);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("rgbcurve");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no rgb curve module", NULL); return -1; }
    dt_introspection_field_t *fn = m->so->get_f("curve_nodes"), *fc = m->so->get_f("curve_num_nodes"),
                             *ft = m->so->get_f("curve_type"), *fa = m->so->get_f("curve_autoscale"),
                             *fg = m->so->get_f("compensate_middle_grey");
    if (!fn || !fc || !ft || !fa || !fg) { set_error("rgb curve fields missing", NULL); return -1; }
    char *base = (char *)m->params;
    cf->m = m;
    cf->nodes = (oma_curve_node *)(base + fn->header.offset);
    cf->num = (int *)(base + fc->header.offset);
    cf->type = (int *)(base + ft->header.offset);
    cf->autoscale = (int *)(base + fa->header.offset);
    cf->compensate = (gboolean *)(base + fg->header.offset);
    return 0;
}

int oma_engine_curve_get(int imgid, int channel, oma_curve_info *info) {
    memset(info, 0, sizeof *info);
    oma_curve_fields cf;
    if (curve_fields(imgid, &cf)) return -1;
    if (channel < 0 || channel >= OMA_CURVE_CHANNELS) { set_error("no such curve channel", NULL); return -1; }
    info->found = 1;
    info->enabled = cf.m->enabled;
    info->linked = *cf.autoscale == 0;
    info->type = cf.type[channel];
    int n = cf.num[channel];
    if (n < 0) n = 0;
    if (n > OMA_CURVE_MAX) n = OMA_CURVE_MAX;
    info->count = n;
    for (int i = 0; i < n; ++i) { info->x[i] = cf.nodes[channel * OMA_CURVE_MAX + i].x; info->y[i] = cf.nodes[channel * OMA_CURVE_MAX + i].y; }
    return 0;
}

static void curve_write(oma_curve_fields *cf, int channel, const float *x, const float *y, int count, int type) {
    // sorted by x, clamped to the unit square
    float xs[OMA_CURVE_MAX], ys[OMA_CURVE_MAX];
    for (int i = 0; i < count; ++i) { xs[i] = fminf(1.f, fmaxf(0.f, x[i])); ys[i] = fminf(1.f, fmaxf(0.f, y[i])); }
    for (int i = 1; i < count; ++i) {
        const float kx = xs[i], ky = ys[i]; int j = i - 1;
        while (j >= 0 && xs[j] > kx) { xs[j + 1] = xs[j]; ys[j + 1] = ys[j]; --j; }
        xs[j + 1] = kx; ys[j + 1] = ky;
    }
    for (int i = 0; i < count; ++i) { cf->nodes[channel * OMA_CURVE_MAX + i].x = xs[i]; cf->nodes[channel * OMA_CURVE_MAX + i].y = ys[i]; }
    cf->num[channel] = count;
    cf->type[channel] = type;
}

int oma_engine_curve_set(int imgid, int channel, const float *x, const float *y, int count, int type) {
    oma_curve_fields cf;
    if (curve_fields(imgid, &cf)) return -1;
    if (channel < 0 || channel >= OMA_CURVE_CHANNELS) { set_error("no such curve channel", NULL); return -1; }
    if (count < 2 || count > OMA_CURVE_MAX) { set_error("a curve needs 2 to 20 nodes", NULL); return -1; }
    if (type < CUBIC_SPLINE || type > MONOTONE_HERMITE) { set_error("unknown curve type", NULL); return -1; }
    // First touch of a fresh module: mid grey at 0.5, the way the editor draws it.
    if (!cf.m->enabled) *cf.compensate = TRUE;
    curve_write(&cf, channel, x, y, count, type);
    oma_add_history_item(&g_dev, cf.m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

int oma_engine_curve_set_linked(int imgid, int linked) {
    oma_curve_fields cf;
    if (curve_fields(imgid, &cf)) return -1;
    const int was = *cf.autoscale == 0;
    if (was == (linked != 0)) return 0;
    if (!linked) {
        // The shared curve becomes each channel's own.
        for (int ch = 1; ch < OMA_CURVE_CHANNELS; ++ch) {
            for (int i = 0; i < OMA_CURVE_MAX; ++i) cf.nodes[ch * OMA_CURVE_MAX + i] = cf.nodes[i];
            cf.num[ch] = cf.num[0]; cf.type[ch] = cf.type[0];
        }
    }
    *cf.autoscale = linked ? 0 : 1;
    if (!cf.m->enabled) *cf.compensate = TRUE;
    oma_add_history_item(&g_dev, cf.m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

int oma_engine_curve_sample(int imgid, int channel, float *y, int res) {
    oma_curve_info ci;
    if (oma_engine_curve_get(imgid, channel, &ci)) return -1;
    if (res <= 0 || !y) return -1;
    if (ci.count < 2) { for (int k = 0; k < res; ++k) y[k] = (float)k / (float)(res - 1); return 0; }
    float *tangents = interpolate_set(ci.count, ci.x, ci.y, (unsigned)ci.type);
    if (!tangents) { set_error("curve interpolation failed", NULL); return -1; }
    for (int k = 0; k < res; ++k) {
        const float x = res > 1 ? (float)k / (float)(res - 1) : 0.f;
        float v;
        if (x <= ci.x[0]) v = ci.y[0];
        else if (x >= ci.x[ci.count - 1]) v = ci.y[ci.count - 1];
        else v = interpolate_val(ci.count, ci.x, x, ci.y, tangents, (unsigned)ci.type);
        y[k] = fminf(1.f, fmaxf(0.f, v));
    }
    free(tangents);
    return 0;
}

// ── parametric curve over tonecurve (L channel) ─────────────────────────
typedef struct { dt_iop_module_t *m; oma_curve_node *nodes; int *num, *type, *autoscale; } oma_lcurve_fields;

static int lcurve_fields(int imgid, oma_lcurve_fields *lf) {
    memset(lf, 0, sizeof *lf);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("tonecurve");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no tone curve module", NULL); return -1; }
    dt_introspection_field_t *fn = m->so->get_f("tonecurve"), *fc = m->so->get_f("tonecurve_nodes"), *ft = m->so->get_f("tonecurve_type"),
                             *fa = m->so->get_f("tonecurve_autoscale_ab");
    if (!fn || !fc || !ft || !fa) { set_error("tone curve fields missing", NULL); return -1; }
    char *base = (char *)m->params;
    lf->m = m;
    lf->autoscale = (int *)(base + fa->header.offset);
    lf->nodes = (oma_curve_node *)(base + fn->header.offset);
    lf->num = (int *)(base + fc->header.offset);
    lf->type = (int *)(base + ft->header.offset);
    return 0;
}

int oma_engine_lcurve_get(int imgid, oma_curve_info *info) {
    memset(info, 0, sizeof *info);
    oma_lcurve_fields lf;
    if (lcurve_fields(imgid, &lf)) return -1;
    info->found = 1;
    info->enabled = lf.m->enabled;
    info->linked = 1;
    info->type = lf.type[0];
    int n = lf.num[0];
    if (n < 0) n = 0;
    if (n > OMA_CURVE_MAX) n = OMA_CURVE_MAX;
    info->count = n;
    for (int i = 0; i < n; ++i) { info->x[i] = lf.nodes[i].x; info->y[i] = lf.nodes[i].y; }
    return 0;
}

int oma_engine_lcurve_set(int imgid, const float *x, const float *y, int count) {
    oma_lcurve_fields lf;
    if (lcurve_fields(imgid, &lf)) return -1;
    if (count < 2 || count > OMA_CURVE_MAX) { set_error("a curve needs 2 to 20 nodes", NULL); return -1; }
    float xs[OMA_CURVE_MAX], ys[OMA_CURVE_MAX];
    for (int i = 0; i < count; ++i) { xs[i] = fminf(1.f, fmaxf(0.f, x[i])); ys[i] = fminf(1.f, fmaxf(0.f, y[i])); }
    for (int i = 1; i < count; ++i) {
        const float kx = xs[i], ky = ys[i]; int j = i - 1;
        while (j >= 0 && xs[j] > kx) { xs[j + 1] = xs[j]; ys[j + 1] = ys[j]; --j; }
        xs[j + 1] = kx; ys[j + 1] = ky;
    }
    for (int i = 0; i < count; ++i) { lf.nodes[i].x = xs[i]; lf.nodes[i].y = ys[i]; }
    lf.num[0] = count;
    lf.type[0] = MONOTONE_HERMITE;
    lf.m->enabled = TRUE;
    oma_add_history_item(&g_dev, lf.m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

// ── Lab colour curves over tonecurve (a and b channels) ─────────────────
enum { OMA_TONECURVE_LAB_INDEPENDENT = 0, OMA_TONECURVE_RGB_LINKED = 3 }; // dt_iop_tonecurve_autoscale_t

static gboolean labcurve_straight(const oma_lcurve_fields *lf, int channel) {
    const int n = lf->num[channel];
    for (int i = 0; i < n && i < OMA_CURVE_MAX; ++i)
        if (fabsf(lf->nodes[channel * OMA_CURVE_MAX + i].y - lf->nodes[channel * OMA_CURVE_MAX + i].x) > 1e-5f) return FALSE;
    return TRUE;
}

int oma_engine_labcurve_get(int imgid, int channel, oma_curve_info *info) {
    memset(info, 0, sizeof *info);
    oma_lcurve_fields lf;
    if (lcurve_fields(imgid, &lf)) return -1;
    if (channel < 1 || channel > 2) { set_error("no such Lab curve", NULL); return -1; }
    info->found = 1;
    info->enabled = lf.m->enabled;
    info->linked = *lf.autoscale != OMA_TONECURVE_LAB_INDEPENDENT;
    info->type = lf.type[channel];
    int n = lf.num[channel];
    if (n < 0) n = 0;
    if (n > OMA_CURVE_MAX) n = OMA_CURVE_MAX;
    info->count = n;
    for (int i = 0; i < n; ++i) { info->x[i] = lf.nodes[channel * OMA_CURVE_MAX + i].x; info->y[i] = lf.nodes[channel * OMA_CURVE_MAX + i].y; }
    return 0;
}

static int labcurve_write(oma_lcurve_fields *lf, int channel, const float *x, const float *y, int count, int type) {
    if (channel < 1 || channel > 2) { set_error("no such Lab curve", NULL); return -1; }
    if (count < 2 || count > OMA_CURVE_MAX) { set_error("a curve needs 2 to 20 nodes", NULL); return -1; }
    if (type < CUBIC_SPLINE || type > MONOTONE_HERMITE) { set_error("unknown curve type", NULL); return -1; }
    float xs[OMA_CURVE_MAX], ys[OMA_CURVE_MAX];
    for (int i = 0; i < count; ++i) { xs[i] = fminf(1.f, fmaxf(0.f, x[i])); ys[i] = fminf(1.f, fmaxf(0.f, y[i])); }
    for (int i = 1; i < count; ++i) {
        const float kx = xs[i], ky = ys[i]; int j = i - 1;
        while (j >= 0 && xs[j] > kx) { xs[j + 1] = xs[j]; ys[j + 1] = ys[j]; --j; }
        xs[j + 1] = kx; ys[j + 1] = ky;
    }
    for (int i = 0; i < count; ++i) { lf->nodes[channel * OMA_CURVE_MAX + i].x = xs[i]; lf->nodes[channel * OMA_CURVE_MAX + i].y = ys[i]; }
    lf->num[channel] = count;
    lf->type[channel] = type;
    return 0;
}
static void labcurve_commit(oma_lcurve_fields *lf) {
    *lf->autoscale = labcurve_straight(lf, 1) && labcurve_straight(lf, 2) ? OMA_TONECURVE_RGB_LINKED : OMA_TONECURVE_LAB_INDEPENDENT;
    lf->m->enabled = TRUE;
    oma_add_history_item(&g_dev, lf->m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
}

int oma_engine_labcurve_set(int imgid, int channel, const float *x, const float *y, int count, int type) {
    oma_lcurve_fields lf;
    if (lcurve_fields(imgid, &lf)) return -1;
    if (labcurve_write(&lf, channel, x, y, count, type)) return -1;
    labcurve_commit(&lf);
    return 0;
}

int oma_engine_labcurves_set(int imgid, const float *xa, const float *ya, int na, const float *xb, const float *yb, int nb, int type) {
    oma_lcurve_fields lf;
    if (lcurve_fields(imgid, &lf)) return -1;
    if (na < 2 || na > OMA_CURVE_MAX || nb < 2 || nb > OMA_CURVE_MAX) { set_error("a curve needs 2 to 20 nodes", NULL); return -1; }
    if (labcurve_write(&lf, 1, xa, ya, na, type) || labcurve_write(&lf, 2, xb, yb, nb, type)) return -1;
    labcurve_commit(&lf);
    return 0;
}

int oma_engine_labcurve_sample(int imgid, int channel, float *y, int res) {
    oma_curve_info ci;
    if (oma_engine_labcurve_get(imgid, channel, &ci)) return -1;
    if (res <= 0 || !y) return -1;
    if (ci.count < 2) { for (int k = 0; k < res; ++k) y[k] = (float)k / (float)(res - 1); return 0; }
    float *tangents = interpolate_set(ci.count, ci.x, ci.y, (unsigned)ci.type);
    if (!tangents) { set_error("curve interpolation failed", NULL); return -1; }
    for (int k = 0; k < res; ++k) {
        const float x = res > 1 ? (float)k / (float)(res - 1) : 0.f;
        float v;
        if (x <= ci.x[0]) v = ci.y[0];
        else if (x >= ci.x[ci.count - 1]) v = ci.y[ci.count - 1];
        else v = interpolate_val(ci.count, ci.x, x, ci.y, tangents, (unsigned)ci.type);
        y[k] = fminf(1.f, fmaxf(0.f, v));
    }
    free(tangents);
    return 0;
}

// ── crop ────────────────────────────────────────────────────────────────
static float *float_field(dt_iop_module_t *m, const char *field);
typedef struct { dt_iop_module_t *m; float *cx, *cy, *cw, *ch; int *rn, *rd; } oma_crop_fields;

static int crop_fields(int imgid, oma_crop_fields *cf) {
    memset(cf, 0, sizeof *cf);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("crop");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no crop module", NULL); return -1; }
    cf->m = m;
    cf->cx = float_field(m, "cx"); cf->cy = float_field(m, "cy"); cf->cw = float_field(m, "cw"); cf->ch = float_field(m, "ch");
    dt_introspection_field_t *fn = m->so->get_f("ratio_n"), *fd = m->so->get_f("ratio_d");
    cf->rn = fn ? (int *)m->so->get_p(m->params, "ratio_n") : NULL;
    cf->rd = fd ? (int *)m->so->get_p(m->params, "ratio_d") : NULL;
    if (!cf->cx || !cf->cy || !cf->cw || !cf->ch || !cf->rn || !cf->rd) { set_error("crop fields missing", NULL); return -1; }
    return 0;
}

int oma_engine_crop_get(int imgid, oma_crop_info *info) {
    memset(info, 0, sizeof *info);
    oma_crop_fields cf;
    if (crop_fields(imgid, &cf)) return -1;
    info->found = 1;
    info->enabled = cf.m->enabled;
    info->cx = *cf.cx; info->cy = *cf.cy; info->cw = *cf.cw; info->ch = *cf.ch;
    // A module never touched reports zero extents; the frame is the whole picture.
    if (!cf.m->enabled && info->cw <= info->cx) { info->cx = 0; info->cy = 0; info->cw = 1; info->ch = 1; }
    info->ratio_n = *cf.rn; info->ratio_d = *cf.rd;
    {
        const dt_image_t *img = &g_dev.image_storage;
        const int pw = img->p_width > 0 ? img->p_width : img->width, ph = img->p_height > 0 ? img->p_height : img->height;
        info->orientation = dt_image_orientation(img);
        dt_iop_module_t *flip = find_module("flip");
        if(flip) {
            const int *value = flip->so->get_p ? flip->so->get_p(flip->params, "orientation") : NULL;
            info->orientation = !flip->enabled ? ORIENTATION_NONE
                : value && *value != ORIENTATION_NULL ? *value : dt_image_orientation(img);
        }
        const int swap = (info->orientation & ORIENTATION_SWAP_XY) != 0;
        info->orig_w = swap ? ph : pw; info->orig_h = swap ? pw : ph;
    }
    return 0;
}

int oma_engine_crop_set(int imgid, float cx, float cy, float cw, float ch, int ratio_n, int ratio_d) {
    oma_crop_fields cf;
    if (crop_fields(imgid, &cf)) return -1;
    cx = fminf(1.f, fmaxf(0.f, cx)); cy = fminf(1.f, fmaxf(0.f, cy));
    cw = fminf(1.f, fmaxf(0.f, cw)); ch = fminf(1.f, fmaxf(0.f, ch));
    if (cw - cx < 0.01f || ch - cy < 0.01f) { set_error("crop too small", NULL); return -1; }
    *cf.cx = cx; *cf.cy = cy; *cf.cw = cw; *cf.ch = ch; *cf.rn = ratio_n; *cf.rd = ratio_d;
    const int identity = cx <= 0.f && cy <= 0.f && cw >= 1.f && ch >= 1.f;
    cf.m->enabled = identity ? FALSE : TRUE;
    oma_add_history_item(&g_dev, cf.m, identity ? FALSE : TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

int oma_engine_render_uncropped(int imgid, int max_w, int max_h, float **out, int *w, int *h) {
    return render_preview(imgid, 0, 1, max_w, max_h, out, w, h);
}

typedef int (*oma_geometry_fn)(void *, int, int, float *, int, int, int, int, const float *, int);
static oma_geometry_fn geometry_function(dt_iop_module_t *m) {
    oma_geometry_fn fn = NULL;
    if (m && m->so && m->so->module) g_module_symbol(m->so->module, "omaraw_geometry_v1", (gpointer *)&fn);
    return fn;
}
int oma_engine_geometry_available(void) {
    return g_inited && geometry_function(find_module("ashift")) != NULL;
}
int oma_engine_geometry_crop_mode(int imgid) {
    if (load(imgid)) return 0;
    dt_iop_module_t *m = find_module("ashift");
    const int *mode = m && m->so->get_p ? m->so->get_p(m->params, "cropmode") : NULL;
    return mode ? CLAMP(*mode, 0, 2) : 0;
}
static dt_dev_pixelpipe_iop_t *geometry_piece(oma_preview_cache *cache) {
    if (cache) for (GList *l = cache->pipe.nodes; l; l = l->next) {
        dt_dev_pixelpipe_iop_t *piece = l->data;
        if (!strcmp(piece->module->op, "ashift")) return piece;
    }
    return NULL;
}
static int geometry_error(int rc) {
    if (rc == 2) set_error("Not enough distinct lines. Draw two vertical or two horizontal guides, or try a different correction.", NULL);
    else if (rc == 3) set_error("No reliable correction found. Try drawing guides along clear edges.", NULL);
    else if (rc == 4) set_error("Could not find a usable crop. Reduce the perspective correction.", NULL);
    else set_error("Could not analyse the image geometry.", NULL);
    return -1;
}
int oma_engine_geometry(int imgid, int operation, int crop_mode, const float *guides, int count) {
    if (operation < 0 || operation > 6 || crop_mode < 0 || crop_mode > 2 || count < 0 || count > 4
        || (operation == 5 && (!guides || count < 2))) return geometry_error(2);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("ashift");
    oma_geometry_fn fn = geometry_function(m);
    if (!fn) { set_error("Geometry tools need the updated OmaRAW engine.", NULL); return -1; }
    // Use a private, uncropped context for coordinates. Never append temporary
    // history or write a database merely to analyse an image.
    oma_preview_cache *cache = prepare_preview(imgid, 0, 1, 0);
    dt_dev_pixelpipe_iop_t *piece = geometry_piece(cache);
    if (!piece || piece->buf_in.width < 8 || piece->buf_in.height < 8) return geometry_error(1);
    const int width = piece->buf_in.width, height = piece->buf_in.height;
    float lines[20] = {0};
    if (operation == 5) {
        for (int i = 0; i < count; ++i) {
            float points[4];
            for (int j = 0; j < 4; ++j) {
                if (!isfinite(guides[i*4+j]) || guides[i*4+j] < 0 || guides[i*4+j] > 1) return geometry_error(1);
                points[j] = guides[i*4+j] * (j % 2 ? cache->pipe.processed_height : cache->pipe.processed_width);
            }
            // Includes orientation AFTER ashift, current perspective and its
            // internal crop. Guides remain correct on portrait and rotated RAWs.
            if (!dt_dev_distort_backtransform_plus(&cache->dev, &cache->pipe, piece->module->iop_order,
                DT_DEV_TRANSFORM_DIR_FORW_INCL, points, 2)) return geometry_error(1);
            for (int j = 0; j < 4; ++j) lines[i*5+j] = points[j] / (j % 2 ? height : width);
            lines[i*5+4] = fabsf(points[3]-points[1]) > fabsf(points[2]-points[0]) ? 1.f : 0.f;
        }
    }
    if (operation == 2 || operation == 3) {
        float axis[4] = { cache->pipe.processed_width * .5f, cache->pipe.processed_height * .4f,
                          cache->pipe.processed_width * .5f, cache->pipe.processed_height * .6f };
        if (!dt_dev_distort_backtransform_plus(&cache->dev, &cache->pipe, piece->module->iop_order,
            DT_DEV_TRANSFORM_DIR_FORW_EXCL, axis, 2)) return geometry_error(1);
        if (fabsf(axis[2]-axis[0]) > fabsf(axis[3]-axis[1])) operation = operation == 2 ? 3 : 2;
    }
    void *params = g_memdup2(m->params, m->params_size);
    // Disabled modules can retain old parameters in history. The picture's
    // effective transform is neutral in that case.
    if (!m->enabled) {
        const char *fields[] = {"rotation", "lensshift_v", "lensshift_h", "shear", "cl", "ct"};
        for (int i = 0; i < 6; ++i) *(float *)m->so->get_p(params, fields[i]) = 0;
        *(float *)m->so->get_p(params, "cr") = *(float *)m->so->get_p(params, "cb") = 1;
    }
    float *buffer = NULL; int rw = 0, rh = 0;
    if (operation >= 1 && operation <= 4) {
        // Geometry-neutral, unrotated analysis image, bounded at 1600 pixels.
        const int rc = render_preview(imgid, 0, 2, 1600, 1600, &buffer, &rw, &rh);
        if (rc) { g_free(params); free(buffer); return rc; }
    }
    if (preview_cancelled()) { free(buffer); g_free(params); return OMA_ENGINE_CANCELLED; }
    const int rc = fn(params, width, height, buffer, rw, rh, operation, crop_mode, lines, count);
    free(buffer);
    if (preview_cancelled()) { g_free(params); return OMA_ENGINE_CANCELLED; }
    if (!rc) {
        memcpy(m->params, params, m->params_size);
        // Force a new undo boundary, including repeated geometry actions.
        ++g_dev.focus_hash;
        m->enabled = TRUE;
        oma_add_history_item(&g_dev, m, TRUE, TRUE);
        dt_dev_write_history(&g_dev);
    }
    g_free(params);
    if (operation == 1 && (rc == 2 || rc == 3)) {
        set_error("Auto level could not find reliable lines. The photo is unchanged; use Draw level line to straighten it.", NULL);
        return -1;
    }
    return rc ? geometry_error(rc) : 0;
}
static int geometry_param_set(int imgid, const char *field, float value) {
    oma_param_info info;
    if (oma_engine_param_get(imgid, "ashift", field, &info)) return -1;
    if (!isfinite(value) || !info.found || value < info.min || value > info.max) {
        set_error("Invalid geometry value for %s", field); return -1;
    }
    dt_iop_module_t *m = find_module("ashift");
    oma_geometry_fn fn = geometry_function(m);
    if (!fn) return geometry_error(1);
    oma_preview_cache *cache = prepare_preview(imgid, 0, 0, 0);
    dt_dev_pixelpipe_iop_t *piece = geometry_piece(cache);
    if (!piece) return geometry_error(1);
    void *params = g_memdup2(m->params, m->params_size);
    if (!strcmp(field, "cropmode")) *(int *)m->so->get_p(params, field) = (int)lroundf(value);
    else *(float *)m->so->get_p(params, field) = value;
    const int mode = *(int *)m->so->get_p(params, "cropmode");
    const int rc = fn(params, piece->buf_in.width, piece->buf_in.height, NULL, 0, 0, 0, CLAMP(mode, 0, 2), NULL, 0);
    if (!rc) {
        memcpy(m->params, params, m->params_size);
        oma_add_history_item(&g_dev, m, TRUE, TRUE);
        dt_dev_write_history(&g_dev);
    }
    g_free(params);
    return rc ? geometry_error(rc) : 0;
}

// ── colour zones over colorzones ────────────────────────────────────────
typedef struct {
    dt_iop_module_t *m;
    oma_curve_node *nodes;   // [3][20]
    int *num, *type, *channel, *mode, *splines;
    float *strength;
} oma_zones_fields;

static int zones_fields(int imgid, oma_zones_fields *zf) {
    memset(zf, 0, sizeof *zf);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("colorzones");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no colour zones module", NULL); return -1; }
    dt_introspection_field_t *fn = m->so->get_f("curve"), *fc = m->so->get_f("curve_num_nodes"), *ft = m->so->get_f("curve_type"),
                             *fch = m->so->get_f("channel"), *fm = m->so->get_f("mode"), *fs = m->so->get_f("splines_version"),
                             *fst = m->so->get_f("strength");
    if (!fn || !fc || !ft || !fch || !fm || !fs || !fst) { set_error("colour zones fields missing", NULL); return -1; }
    char *base = (char *)m->params;
    zf->m = m;
    zf->nodes = (oma_curve_node *)(base + fn->header.offset);
    zf->num = (int *)(base + fc->header.offset);
    zf->type = (int *)(base + ft->header.offset);
    zf->channel = (int *)(base + fch->header.offset);
    zf->mode = (int *)(base + fm->header.offset);
    zf->splines = (int *)(base + fs->header.offset);
    zf->strength = (float *)(base + fst->header.offset);
    return 0;
}

int oma_engine_zones_get(int imgid, oma_zones_info *info) {
    memset(info, 0, sizeof *info);
    oma_zones_fields zf;
    if (zones_fields(imgid, &zf)) return -1;
    info->found = 1;
    info->enabled = zf.m->enabled;
    for (int ch = 0; ch < 3; ++ch) {
        int n = zf.num[ch];
        if (n < 0) n = 0;
        if (n > OMA_CURVE_MAX) n = OMA_CURVE_MAX;
        info->count[ch] = n; info->type[ch] = zf.type[ch];
        for (int i = 0; i < n; ++i) { info->x[ch][i] = zf.nodes[ch * OMA_CURVE_MAX + i].x; info->y[ch][i] = zf.nodes[ch * OMA_CURVE_MAX + i].y; }
    }
    return 0;
}

int oma_engine_zones_set(int imgid, int channel, const float *x, const float *y, int count) {
    oma_zones_fields zf;
    if (zones_fields(imgid, &zf)) return -1;
    if (channel < 0 || channel >= 3) { set_error("no such zones channel", NULL); return -1; }
    if (count < 2 || count > OMA_CURVE_MAX) { set_error("a zones curve needs 2 to 20 nodes", NULL); return -1; }
    // The panel's contract: select by hue, smooth, v2 splines, Catmull-Rom, full mix.
    *zf.channel = 2; *zf.mode = 0; *zf.splines = 1; *zf.strength = 0.f;
    float xs[OMA_CURVE_MAX], ys[OMA_CURVE_MAX];
    for (int i = 0; i < count; ++i) { xs[i] = fminf(1.f, fmaxf(0.f, x[i])); ys[i] = fminf(1.f, fmaxf(0.f, y[i])); }
    for (int i = 1; i < count; ++i) {
        const float kx = xs[i], ky = ys[i]; int j = i - 1;
        while (j >= 0 && xs[j] > kx) { xs[j + 1] = xs[j]; ys[j + 1] = ys[j]; --j; }
        xs[j + 1] = kx; ys[j + 1] = ky;
    }
    for (int i = 0; i < count; ++i) { zf.nodes[channel * OMA_CURVE_MAX + i].x = xs[i]; zf.nodes[channel * OMA_CURVE_MAX + i].y = ys[i]; }
    zf.num[channel] = count;
    zf.type[channel] = CATMULL_ROM;
    oma_add_history_item(&g_dev, zf.m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

// ── white balance: temperature + tint over channelmixerrgb ─────────────
#include "common/illuminants.h"
#define OMA_TINT_SCALE 2000.f  // UI units per xy distance: ±100 ⇒ ±0.05

static void locus_xy(float t, float *x, float *y) {
    // What colour calibration itself does for illuminant D.
    if (t >= 4000.f) CCT_to_xy_daylight(t, x, y);
    else CCT_to_xy_blackbody(t, x, y);
}

// The native tint-direction helper excludes exactly 25000 K from its final
// branch and returns zero there. Use its limiting direction at our inclusive
// slider endpoint; keep the requested temperature and locus point unchanged.
static float tint_direction_temperature(float t) {
    return fminf(t, nextafterf(25000.f, 0.f));
}

static float *float_field(dt_iop_module_t *m, const char *field) {
    dt_introspection_field_t *f = m->so->get_f(field);
    return f ? (float *)m->so->get_p(m->params, field) : NULL;
}

static float cct_of(float x, float y) {
    float t = xy_to_CCT(x, y);
    if (t < 3000.f) t = CCT_reverse_lookup(x, y);
    return fmaxf(1667.f, fminf(25000.f, t));
}

// The illuminant the module is using right now, as xy and a temperature:
// custom carries its own xy; "as shot in camera" is derived from the raw
// coefficients the way the module's commit does; daylight and blackbody
// sit on the locus at their temperature.
static int illuminant_now(dt_iop_module_t *m, float *x, float *y, float *T, int *on_locus) {
    float *t = float_field(m, "temperature"), *px = float_field(m, "x"), *py = float_field(m, "y");
    dt_introspection_field_t *fi = m->so->get_f("illuminant");
    int *illum = fi ? (int *)m->so->get_p(m->params, "illuminant") : NULL;
    if (!t || !px || !py || !illum) { set_error("colour calibration fields missing", NULL); return -1; }
    *on_locus = 0;
    // Custom keeps the temperature the sliders (or the picker) wrote: the
    // CCT formula is not the exact inverse of the locus, and a tint must not
    // nudge the temperature.
    if (*illum == DT_ILLUMINANT_CUSTOM) { *x = *px; *y = *py; *T = *t; return 0; }
    if (*illum == DT_ILLUMINANT_CAMERA) {
        // As shot: the fields carry what the module derived when the image
        // loaded (copied back by as-shot); derive them only when unset.
        if (*t >= 1667.f && *px > 0.f && *py > 0.f) { *x = *px; *y = *py; *T = *t; return 0; }
        dt_aligned_pixel_t custom_wb = {1.f, 1.f, 1.f, 1.f};
        const dt_dev_chroma_t *chr = &g_dev.chroma;
        const int valid = chr->D65coeffs[0] > 0.0 && chr->D65coeffs[1] > 0.0 && chr->D65coeffs[2] > 0.0;
        const int changed = chr->wb_coeffs[0] > 1.f || chr->wb_coeffs[1] > 1.f || chr->wb_coeffs[2] > 1.f;
        if (valid && changed) for (int k = 0; k < 4; ++k) custom_wb[k] = (float)(chr->D65coeffs[k] / chr->wb_coeffs[k]);
        if (find_temperature_from_raw_coeffs(&g_dev.image_storage, custom_wb, x, y)) { *T = cct_of(*x, *y); return 0; }
    }
    *T = *t; locus_xy(*t, x, y); *on_locus = 1;
    return 0;
}

int oma_engine_wb_get(int imgid, float *temperature, float *tint) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("channelmixerrgb");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no colour calibration module", NULL); return -1; }
    float x = 0, y = 0, T = 5003.f; int on_locus = 1;
    if (illuminant_now(m, &x, &y, &T, &on_locus)) return -1;
    *temperature = T;
    if (on_locus) { *tint = 0.f; return 0; }
    // Signed distance from the locus point at this temperature, along the normal.
    float xl, yl;
    locus_xy(T, &xl, &yl);
    const float n = planckian_normal(xl, tint_direction_temperature(T));
    const float norm = sqrtf(1.f + n * n);
    // The xy describes the LIGHT; the module removes it. A magenta light
    // makes a greener picture, so the user's tint is the negative offset.
    *tint = (y - yl) * norm * OMA_TINT_SCALE;
    return 0;
}

int oma_engine_wb_set(int imgid, float temperature, float tint) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("channelmixerrgb");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no colour calibration module", NULL); return -1; }
    float *t = float_field(m, "temperature"), *x = float_field(m, "x"), *y = float_field(m, "y");
    dt_introspection_field_t *fi = m->so->get_f("illuminant");
    int *illum = fi ? (int *)m->so->get_p(m->params, "illuminant") : NULL;
    if (!t || !x || !y || !illum) { set_error("colour calibration fields missing", NULL); return -1; }
    temperature = fmaxf(1667.f, fminf(25000.f, temperature));
    *t = temperature;
    float xl, yl;
    locus_xy(temperature, &xl, &yl);
    blackbody_xy_to_tinted_xy(xl, yl, tint_direction_temperature(temperature), -tint / OMA_TINT_SCALE, x, y);
    // Tint 0 stays a plain daylight illuminant, which darktable handles
    // exactly; anything else is a custom xy.
    *illum = fabsf(tint) < 1e-4f ? DT_ILLUMINANT_D : DT_ILLUMINANT_CUSTOM;
    oma_add_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

// ── texture over diffuse ────────────────────────────────────────────────
#define OMA_TEXTURE_SPEED 0.15f
static int set_float_field(dt_iop_module_t *m, const char *field, float v) {
    float *p = float_field(m, field);
    if (!p) { set_error("diffuse field %s missing", field); return -1; }
    *p = v;
    return 0;
}
static int set_int_field(dt_iop_module_t *m, const char *field, int v) {
    dt_introspection_field_t *f = m->so->get_f(field);
    int *p = f ? (int *)m->so->get_p(m->params, field) : NULL;
    if (!p) { set_error("diffuse field %s missing", field); return -1; }
    *p = v;
    return 0;
}

int oma_engine_texture_get(int imgid, float *amount, int *enabled) {
    if (amount) *amount = 0;
    if (enabled) *enabled = 0;
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("diffuse");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no diffuse module", NULL); return -1; }
    const float *first = float_field(m, "first");
    if (!first) { set_error("diffuse field first missing", NULL); return -1; }
    if (amount) *amount = fmaxf(-100.f, fminf(100.f, -*first / OMA_TEXTURE_SPEED * 100.f));
    if (enabled) *enabled = m->enabled ? 1 : 0;
    return 0;
}

int oma_engine_texture_set(int imgid, float amount) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("diffuse");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no diffuse module", NULL); return -1; }
    amount = fmaxf(-100.f, fminf(100.f, amount));
    const float k = amount / 100.f;
    // The "local contrast | fine" preset, its four speeds scaled by the amount.
    const struct { const char *f; float v; } floats[] = {
        {"first", -OMA_TEXTURE_SPEED * k}, {"second", 0.05f * k}, {"third", 0.05f * k}, {"fourth", -OMA_TEXTURE_SPEED * k},
        {"anisotropy_first", 10.f}, {"anisotropy_second", 0.f}, {"anisotropy_third", 0.f}, {"anisotropy_fourth", 10.f},
        {"sharpness", 0.f}, {"regularization", 2.f}, {"variance_threshold", 0.f}, {"threshold", 0.f}};
    const struct { const char *f; int v; } ints[] = {{"iterations", 5}, {"radius", 170}, {"radius_center", 0}};
    for (size_t i = 0; i < sizeof floats / sizeof floats[0]; ++i) if (set_float_field(m, floats[i].f, floats[i].v)) return -1;
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; ++i) if (set_int_field(m, ints[i].f, ints[i].v)) return -1;
    const gboolean on = fabsf(amount) >= 0.5f;
    m->enabled = on;
    oma_add_history_item(&g_dev, m, on, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

int oma_engine_wb_as_shot(int imgid) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("channelmixerrgb");
    if (!m || !m->so->get_f || !m->default_params) { set_error("no colour calibration module", NULL); return -1; }
    dt_introspection_field_t *fi = m->so->get_f("illuminant"), *fa = m->so->get_f("adaptation");
    int *illum = fi ? (int *)m->so->get_p(m->params, "illuminant") : NULL;
    int *adapt = fa ? (int *)m->so->get_p(m->params, "adaptation") : NULL;
    if (!illum) { set_error("colour calibration fields missing", NULL); return -1; }
    if (dt_image_is_matrix_correction_supported(&g_dev.image_storage)) {
        // A raw: the module's own "as shot in camera" mode reads the
        // camera's white balance at commit time, as it did when the image loaded.
        *illum = DT_ILLUMINANT_CAMERA;
        if (adapt && *adapt == DT_ADAPTATION_RGB) *adapt = DT_ADAPTATION_CAT16;
        // The temperature and xy the module derived at load, so the sliders
        // read the camera's light rather than the last thing they wrote.
        static const char *fields[] = {"x", "y", "temperature"};
        for (size_t i = 0; i < sizeof fields / sizeof fields[0]; ++i) {
            dt_introspection_field_t *f = m->so->get_f(fields[i]);
            if (f) memcpy((char *)m->params + f->header.offset, (const char *)m->default_params + f->header.offset, f->header.size);
        }
        m->enabled = TRUE;
    } else {
        // Nothing was shot: back to the module's defaults for this image,
        // the illuminant half only, and its default switch.
        static const char *fields[] = {"illuminant", "illum_fluo", "illum_led", "x", "y", "temperature", "adaptation"};
        for (size_t i = 0; i < sizeof fields / sizeof fields[0]; ++i) {
            dt_introspection_field_t *f = m->so->get_f(fields[i]);
            if (!f) continue;
            memcpy((char *)m->params + f->header.offset, (const char *)m->default_params + f->header.offset, f->header.size);
        }
        m->enabled = m->default_enabled;
    }
    oma_add_history_item(&g_dev, m, m->enabled, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

int oma_engine_wb_set_xy(int imgid, float x, float y) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("channelmixerrgb");
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no colour calibration module", NULL); return -1; }
    float *t = float_field(m, "temperature"), *px = float_field(m, "x"), *py = float_field(m, "y");
    dt_introspection_field_t *fi = m->so->get_f("illuminant"), *fa = m->so->get_f("adaptation");
    int *illum = fi ? (int *)m->so->get_p(m->params, "illuminant") : NULL;
    int *adapt = fa ? (int *)m->so->get_p(m->params, "adaptation") : NULL;
    if (!t || !px || !py || !illum) { set_error("colour calibration fields missing", NULL); return -1; }
    if (!(x > 0.f && y > 0.f && x < 1.f && y < 1.f)) { set_error("no neutral there", NULL); return -1; }
    *px = x; *py = y;
    *t = cct_of(x, y);
    *illum = DT_ILLUMINANT_CUSTOM;
    if (adapt && *adapt == DT_ADAPTATION_RGB) *adapt = DT_ADAPTATION_CAT16;   // a JPEG's default is a plain mixer
    m->enabled = TRUE;
    oma_add_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

// A render with some modules switched off, for the curve histogram and
// the render without one module. The render reads history from the
// database, so the modules go off through history items; darktable may
// merge such an item into the user's last one, and every add drops the
// redo steps. So the history is kept by value, list and end, and put back
// exactly as it was: nothing the user did is lost or changed.
static GList *history_keep(int *end) {
    *end = g_dev.history_end;
    return dt_history_duplicate(g_dev.history);
}
static void history_put_back(GList *kept, int end) {
    g_list_free_full(g_dev.history, dt_dev_free_history_item);
    g_dev.history = kept;
    g_dev.history_end = end;
    dt_dev_write_history(&g_dev);
    reload();
}

int oma_engine_render_without(int imgid, const char *op, int max_w, int max_h, float **out, int *w, int *h) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module(op);
    if (!m || !m->enabled) return oma_engine_render(imgid, max_w, max_h, out, w, h);
    int end = 0;
    GList *kept = history_keep(&end);
    m->enabled = FALSE;
    oma_add_history_item(&g_dev, m, FALSE, TRUE);
    dt_dev_write_history(&g_dev);
    const int rc = oma_engine_render(imgid, max_w, max_h, out, w, h);
    m->enabled = TRUE;
    history_put_back(kept, end);
    return rc;
}

int oma_engine_render_exposure_meter(int imgid, float **out, int *w, int *h,
                                     double *black, double *bias, double *base) {
    if (load(imgid)) return -1;
    dt_iop_module_t *exposure = find_module("exposure"), *input = find_module("colorin");
    if (!exposure || !input || !exposure->so->get_p) return -1;
    float *ev = exposure->so->get_p(exposure->params, "exposure");
    float *bl = exposure->so->get_p(exposure->params, "black");
    float *def = exposure->so->get_p(exposure->default_params, "exposure");
    int *mode = exposure->so->get_p(exposure->params, "mode");
    gboolean *camera = exposure->so->get_p(exposure->params, "compensate_exposure_bias");
    gboolean *hilite = exposure->so->get_p(exposure->params, "compensate_hilite_pres");
    if (!ev || !bl || !def || !mode || !camera || !hilite) return -1;
    *black = *bl; *base = *def; *bias = 0.;
    // Match exposure.c's commit_params exactly, including Exif sentinel/ranges.
    const float eb = g_dev.image_storage.exif_exposure_bias;
    const float hb = g_dev.image_storage.exif_highlight_preservation;
    if (*camera && isfinite(eb) && eb != DT_EXIF_TAG_UNINITIALIZED) *bias -= CLAMP(eb, -5.f, 5.f);
    if (*hilite && isfinite(hb) && hb > 0.f && hb != DT_EXIF_TAG_UNINITIALIZED) *bias += CLAMP(hb, -1.f, 4.f);
    int end = 0;
    GList *kept = history_keep(&end);
    const gboolean new_step = g_new_step;
    *ev = 0.f; *mode = 0; *camera = FALSE; *hilite = FALSE;
    exposure->enabled = TRUE;
    oma_add_history_item(&g_dev, exposure, FALSE, TRUE);
    for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
        dt_iop_module_t *m = l->data;
        if (!m->enabled || m == exposure) continue;
        // Sensor processing and geometry stay; local exposure instances and
        // all creative/tone/output looks are excluded, wherever they are placed.
        const gboolean local_exposure = !strcmp(m->op, "exposure");
        const gboolean technical = m->iop_order <= input->iop_order
            || (m->operation_tags() & IOP_TAG_DISTORT)
            || !strcmp(m->op, "channelmixerrgb") || !strcmp(m->op, "omarawdnggain")
            || !strcmp(m->op, "colorout") || !strcmp(m->op, "gamma") || !strcmp(m->op, "finalscale");
        if (technical && !local_exposure) continue;
        m->enabled = FALSE;
        oma_add_history_item(&g_dev, m, FALSE, TRUE);
    }
    dt_dev_write_history(&g_dev);
    const int rc = oma_engine_render(imgid, 512, 512, out, w, h);
    history_put_back(kept, end);
    g_new_step = new_step;
    return rc;
}

int oma_engine_render_input(int imgid, const char *op, int max_w, int max_h, float **out, int *w, int *h) {
    if (load(imgid)) return -1;
    dt_iop_module_t *target = find_module(op);
    if (!target) return oma_engine_render(imgid, max_w, max_h, out, w, h);
    int end = 0;
    GList *kept = history_keep(&end);
    GList *off = NULL;
    for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
        dt_iop_module_t *m = (dt_iop_module_t *)l->data;
        if (!m->enabled || m->iop_order < target->iop_order) continue;
        if (!strcmp(m->op, "colorout") || !strcmp(m->op, "gamma") || !strcmp(m->op, "finalscale")) continue;
        m->enabled = FALSE;
        oma_add_history_item(&g_dev, m, FALSE, TRUE);
        off = g_list_prepend(off, m);
    }
    dt_dev_write_history(&g_dev);
    const int rc = oma_engine_render(imgid, max_w, max_h, out, w, h);
    for (GList *l = off; l; l = g_list_next(l)) ((dt_iop_module_t *)l->data)->enabled = TRUE;
    g_list_free(off);
    history_put_back(kept, end);
    return rc;
}

// Capture sharpening's automatic radius, measured once over the whole frame
// at full size and returned, so it can be stored: left automatic, every
// region (fitted preview, each 100% tile, the export) measured its own.
int oma_engine_measure_capture_radius(int imgid, float *radius) {
    if (!radius) return -1;
    *radius = 0.f;
    if (load(imgid)) return -1;
    dt_iop_module_t *demosaic = find_module("demosaic");
    if (!demosaic || !demosaic->enabled) { set_error("no active demosaic module", NULL); return -1; }
    // Lens correction, crop and other later effects can request only part
    // of the sensor even for a full-sized export. Measure before them so
    // capture sharpening has one whole-frame radius for preview and export.
    // Keep the exact history (including redo), as render_input does.
    int end = 0;
    GList *kept = history_keep(&end);
    for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
        dt_iop_module_t *m = l->data;
        if (!m->enabled || m->iop_order <= demosaic->iop_order) continue;
        if (!strcmp(m->op, "colorout") || !strcmp(m->op, "gamma") || !strcmp(m->op, "finalscale")) continue;
        m->enabled = FALSE;
        oma_add_history_item(&g_dev, m, FALSE, TRUE);
    }
    dt_dev_write_history(&g_dev);
    dt_omaraw_capture_whole_radius = 0.f;
    float *pixels = NULL; int w = 0, h = 0;
    const int rc = oma_engine_render(imgid, 1 << 16, 1 << 16, &pixels, &w, &h);
    free(pixels);
    const float measured = dt_omaraw_capture_whole_radius;
    history_put_back(kept, end);
    if (rc) return rc;
    if (!(measured > 0.f)) { set_error("no capture radius was measured", NULL); return -1; }
    *radius = measured;
    return 0;
}

static char *char_array_field(dt_iop_module_t *m, const char *field, int *len) {
    if (!m || !m->so->get_f || !m->so->get_p) return NULL;
    dt_introspection_field_t *f = m->so->get_f(field);
    if (!f || f->header.type != DT_INTROSPECTION_TYPE_ARRAY || f->Array.type != DT_INTROSPECTION_TYPE_CHAR) return NULL;
    if (len) *len = (int)f->Array.count;
    return (char *)m->so->get_p(m->params, field);
}

int oma_engine_param_get_string(int imgid, const char *op, const char *field, char *out, int len) {
    if (out && len > 0) out[0] = 0;
    if (load(imgid)) return -1;
    int n = 0;
    char *p = char_array_field(find_module(op), field, &n);
    if (!p) { set_error("no text field %s", field); return -1; }
    if (out && len > 0) snprintf(out, (size_t)len, "%.*s", n, p);
    return 0;
}

int oma_engine_param_set_string(int imgid, const char *op, const char *field, const char *value) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module(op);
    int n = 0;
    char *p = char_array_field(m, field, &n);
    if (!p) { set_error("no text field %s", field); return -1; }
    memset(p, 0, (size_t)n);
    if (value) snprintf(p, (size_t)n, "%s", value);
    oma_add_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

int oma_engine_module_set_enabled(int imgid, const char *op, int enabled) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module(op);
    if (!m) { set_error("no module %s", op); return -1; }
    // A module the pipeline always runs has no off: the engine keeps it
    // running whatever the step says, so an "off" step would only make the
    // controls disagree with the picture.
    if (!enabled && m->hide_enable_button && m->default_enabled) return 0;
    // darktable's `enable` argument only ever switches a module ON; the
    // darkroom flips the flag itself before recording the item. So do we.
    if (enabled) capture_print_baseline(m);
    m->enabled = enabled ? TRUE : FALSE;
    oma_add_history_item(&g_dev, m, enabled ? TRUE : FALSE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

const char *oma_engine_module_name(int imgid, const char *op) {
    if (load(imgid)) return op;
    dt_iop_module_t *m = find_module(op);
    return m && m->name ? m->name() : op;
}

// ── history ─────────────────────────────────────────────────────────────
int oma_engine_history_count(int imgid) {
    if (load(imgid)) return 0;
    return (int)g_list_length(g_dev.history);
}
int oma_engine_history_end(int imgid) {
    if (load(imgid)) return 0;
    return g_dev.history_end;
}
static dt_dev_history_item_t *hist(int imgid, int i) {
    if (load(imgid)) return NULL;
    return (dt_dev_history_item_t *)g_list_nth_data(g_dev.history, (guint)i);
}
const char *oma_engine_history_op(int imgid, int i) {
    dt_dev_history_item_t *h = hist(imgid, i);
    return h ? h->op_name : "";
}
int oma_engine_history_enabled(int imgid, int i) {
    dt_dev_history_item_t *h = hist(imgid, i);
    return h ? h->enabled : 0;
}
const char *oma_engine_history_label(int imgid, int i) {
    dt_dev_history_item_t *h = hist(imgid, i);
    if (!h) return "";
    static char label[192];
    const char *name = h->module && h->module->name ? h->module->name() : h->op_name;
    // Auto-applied presets carry "_builtin_…" instance names; users see the
    // module, and only a hand-given instance name is worth showing.
    if (h->multi_name[0] && strncmp(h->multi_name, "_builtin_", 9) != 0
        && !(h->multi_name[0] == '0' && !h->multi_name[1]))
        snprintf(label, sizeof label, "%s · %s", name, h->multi_name);
    else
        snprintf(label, sizeof label, "%s", name);
    return label;
}
// ── lens profiles (lensfun) ─────────────────────────────────────────────
// The module matches lenses itself at pipe time from `camera`/`lens` (or
// the EXIF when they are empty); this mirrors that lookup so the panel
// can say what it found, and lists the database for a manual pick.
#include <lensfun.h>
static lfDatabase *g_lf = NULL;
static char **g_lens_names = NULL;
static int g_lens_count = 0, g_lens_imgid = -1;

// The same char[] field in the module's DEFAULT params: darktable fills
// `camera`/`lens` from its own match at load, so "overridden" means
// "differs from the default", not "non-empty".
static const char *char_array_default(dt_iop_module_t *m, const char *field) {
    if (!m || !m->so->get_f || !m->so->get_p) return NULL;
    dt_introspection_field_t *f = m->so->get_f(field);
    if (!f || f->header.type != DT_INTROSPECTION_TYPE_ARRAY || f->Array.type != DT_INTROSPECTION_TYPE_CHAR) return NULL;
    return (const char *)m->so->get_p(m->default_params, field);
}

static lfDatabase *lens_db(void) {
    if (g_lf) return g_lf;
    lfDatabase *db = lf_db_new();
    if (!db) { set_error("lensfun: no database object", NULL); return NULL; }
    if (lf_db_load(db) != LF_NO_ERROR) { lf_db_destroy(db); set_error("lensfun: database not found", NULL); return NULL; }
    g_lf = db;
    return db;
}

static const lfCamera *lens_camera_for(const char *maker, const char *model) {
    if (!model || !model[0]) return NULL;
    const lfCamera **cams = lf_db_find_cameras_ext(g_lf, maker && maker[0] ? maker : NULL, model, 0);
    const lfCamera *cam = cams ? cams[0] : NULL;
    lf_free(cams);
    return cam;
}

static void lens_trim_copy(char *out, int len, const char *a, const char *b) {
    // "Maker Model" with the maker dropped when the model already starts with it.
    const char *ma = a ? a : "", *mo = b ? b : "";
    if (ma[0] && g_ascii_strncasecmp(mo, ma, strlen(ma)) != 0) snprintf(out, (size_t)len, "%s %s", ma, mo);
    else snprintf(out, (size_t)len, "%s", mo);
}

int oma_engine_camera(int imgid, char *normalised, int nlen, char *exif, int elen) {
    if (normalised && nlen > 0) normalised[0] = 0;
    if (exif && elen > 0) exif[0] = 0;
    if (load(imgid)) return -1;
    const dt_image_t *img = &g_dev.image_storage;
    if (normalised && nlen > 0) {
        if (img->camera_makermodel[0]) snprintf(normalised, (size_t)nlen, "%s", img->camera_makermodel);
        else lens_trim_copy(normalised, nlen, img->camera_maker, img->camera_model);
    }
    if (exif && elen > 0) lens_trim_copy(exif, elen, img->exif_maker, img->exif_model);
    return 0;
}

int oma_engine_lens_info(int imgid, oma_lens_info *out) {
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("lens");
    if (!m) return 0;
    out->found = 1;
    const dt_image_t *img = &g_dev.image_storage;
    lens_trim_copy(out->exif_camera, sizeof out->exif_camera, img->exif_maker, img->exif_model);
    snprintf(out->exif_lens, sizeof out->exif_lens, "%s", img->exif_lens);
    if (!lens_db()) return 0;
    int n = 0;
    const char *pcam = char_array_field(m, "camera", &n);
    const char *plens = char_array_field(m, "lens", &n);
    const char *pdef = char_array_default(m, "lens");
    const lfCamera *cam = NULL;
    if (pcam && pcam[0]) cam = lens_camera_for(NULL, pcam);
    if (!cam) cam = lens_camera_for(img->exif_maker, img->exif_model);
    if (cam) {
        out->camera_found = 1;
        lens_trim_copy(out->camera, sizeof out->camera, lf_mlstr_get(cam->Maker), lf_mlstr_get(cam->Model));
    }
    out->overridden = plens && strncmp(plens, pdef ? pdef : "", (size_t)n) != 0;
    const char *want = plens && plens[0] ? plens : img->exif_lens;
    if (want && want[0]) {
        const lfLens **ls = lf_db_find_lenses_hd(g_lf, cam, NULL, want, 0);
        if (ls && ls[0]) { out->lens_found = 1; snprintf(out->lens, sizeof out->lens, "%s", lf_mlstr_get(ls[0]->Model)); }
        lf_free(ls);
    }
    return 0;
}

static int lens_name_cmp(const void *a, const void *b) { return g_ascii_strcasecmp(*(char *const *)a, *(char *const *)b); }

static int lens_mount_ok(const lfLens *l, const lfCamera *cam, const lfMount *mount) {
    if (!cam || !cam->Mount || !l->Mounts) return 1;
    for (char **mt = l->Mounts; *mt; ++mt) {
        if (!strcmp(*mt, cam->Mount)) return 1;
        if (mount && mount->Compat) for (char **c = mount->Compat; *c; ++c) if (!strcmp(*mt, *c)) return 1;
    }
    return 0;
}

int oma_engine_lens_candidate_count(int imgid) {
    if (load(imgid)) return -1;
    if (!lens_db()) return -1;
    if (g_lens_imgid == imgid && g_lens_names) return g_lens_count;
    for (int i = 0; i < g_lens_count; ++i) free(g_lens_names[i]);
    free(g_lens_names); g_lens_names = NULL; g_lens_count = 0;
    const dt_image_t *img = &g_dev.image_storage;
    const lfCamera *cam = lens_camera_for(img->exif_maker, img->exif_model);
    const lfMount *mount = cam && cam->Mount ? lf_db_find_mount(g_lf, cam->Mount) : NULL;
    const lfLens *const *all = lf_db_get_lenses(g_lf);
    int n = 0;
    while (all && all[n]) ++n;
    g_lens_names = calloc((size_t)(n > 0 ? n : 1), sizeof(char *));
    for (int i = 0; i < n; ++i) {
        const lfLens *l = all[i];
        if (!lens_mount_ok(l, cam, mount)) continue;
        // A lens made for a smaller sensor vignettes on this body: lensfun skips it too.
        if (cam && cam->CropFactor > 0 && l->CropFactor > cam->CropFactor * 1.01f) continue;
        const char *model = lf_mlstr_get(l->Model);
        if (!model || !model[0]) continue;
        g_lens_names[g_lens_count++] = strdup(model);
    }
    qsort(g_lens_names, (size_t)g_lens_count, sizeof(char *), lens_name_cmp);
    // Drop repeats (variants of the same model share a name).
    int w = 0;
    for (int i = 0; i < g_lens_count; ++i) {
        if (w > 0 && !strcmp(g_lens_names[w - 1], g_lens_names[i])) { free(g_lens_names[i]); continue; }
        g_lens_names[w++] = g_lens_names[i];
    }
    g_lens_count = w;
    g_lens_imgid = imgid;
    return g_lens_count;
}

int oma_engine_lens_candidate(int imgid, int index, char *out, int len) {
    if (out && len > 0) out[0] = 0;
    if (g_lens_imgid != imgid && oma_engine_lens_candidate_count(imgid) < 0) return -1;
    if (index < 0 || index >= g_lens_count) { snprintf(g_error, sizeof g_error, "no lens %d", index); return -1; }
    if (out && len > 0) snprintf(out, (size_t)len, "%s", g_lens_names[index]);
    return 0;
}

int oma_engine_lens_override(int imgid, const char *lens) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_module("lens");
    if (!m) { set_error("no lens module", NULL); return -1; }
    int n = 0;
    char *p = char_array_field(m, "lens", &n);
    if (!p) { set_error("no text field %s", "lens"); return -1; }
    const char *pdef = char_array_default(m, "lens");
    memset(p, 0, (size_t)n);
    if (lens && lens[0]) snprintf(p, (size_t)n, "%s", lens);
    else if (pdef) snprintf(p, (size_t)n, "%.*s", n, pdef);   // back to darktable's own match
    oma_add_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    // A named lens only means something to the database method; clearing
    // goes back to the module's default method as well.
    if (lens && lens[0]) return oma_engine_param_set(imgid, "lens", "method", 1.0f);
    const int *dm = m->so->get_p ? (const int *)m->so->get_p(m->default_params, "method") : NULL;
    return dm ? oma_engine_param_set(imgid, "lens", "method", (float)*dm) : 0;
}

// Discards every step above the end, as a new edit would, without adding one.
int oma_engine_history_truncate(int imgid) {
    if (load(imgid)) return -1;
    drop_redo_tail(&g_dev);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_history_set_end(int imgid, int end) {
    if (load(imgid)) return -1;
    const int n = (int)g_list_length(g_dev.history);
    if (end < 0) end = 0;
    if (end > n) end = n;
    dt_dev_pop_history_items_ext(&g_dev, end);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

// The native bootstrap ends after its first run of built-in steps. Later
// edits may acquire built-in names too, so they are never part of the baseline.
static int bootstrap_history_end(void) {
    int start = 0, i = 0;
    gboolean in_run = FALSE;
    GList *l = g_dev.history;
    for (; l && i < g_dev.history_end; l = l->next, ++i) {
        const dt_dev_history_item_t *h = l->data;
        const gboolean builtin = g_str_has_prefix(h->multi_name, "_builtin_");
        if (!in_run && builtin && !strcmp(h->op_name, "rawprepare")) continue;
        if (builtin) { in_run = TRUE; start = i + 1; }
        else if (in_run) break;
    }
    // Keep the automatic first edit, just like Reset all: these steps are
    // written together immediately after the native bootstrap on first open.
    // Each module occurs once in that batch. Do not absorb a user's first
    // lens/noise/sharpening edit just because it immediately follows it.
    if (in_run && l && l->next && i + 1 < g_dev.history_end
        && !strcmp(((dt_dev_history_item_t *)l->data)->op_name, "demosaic")
        && !strcmp(((dt_dev_history_item_t *)l->next->data)->op_name, "denoiseprofile")) {
        start = i + 2; l = l->next->next;
        if (l && start < g_dev.history_end && !strcmp(((dt_dev_history_item_t *)l->data)->op_name, "lens")) { ++start; l = l->next; }
        // The batch finishes by storing the measured capture radius.
        if (l && start < g_dev.history_end) {
            const dt_dev_history_item_t *h = l->data;
            dt_iop_module_t *demosaic = find_module("demosaic");
            if (!strcmp(h->op_name, "demosaic") && demosaic && demosaic->so->get_p) {
                const gboolean *on = demosaic->so->get_p(h->params, "cs_enabled");
                const float *radius = demosaic->so->get_p(h->params, "cs_radius");
                if (h->enabled && on && *on && radius && *radius > 0) ++start;
            }
        }
    }
    return start;
}

int oma_engine_module_reset(int imgid, const char *op, const char *const *fields, int count) {
    if (count < 0 || (count && !fields) || load(imgid)) return -1;
    dt_iop_module_t *m = find_module(op);
    if (!m || !m->params) { set_error("no module %s", op); return -1; }
    const int end = g_dev.history_end;
    const gboolean was_on = m->enabled;
    dt_dev_pop_history_items_ext(&g_dev, bootstrap_history_end());
    void *baseline = g_memdup2(m->params, m->params_size);
    const gboolean baseline_on = m->enabled;
    const char *tones[] = {"sigmoid", "filmicrgb", "basecurve"};
    gboolean tone_on[3];
    for (int i = 0; i < 3; ++i) { dt_iop_module_t *tone = find_module(tones[i]); tone_on[i] = tone && tone->enabled; }
    dt_dev_pop_history_items_ext(&g_dev, end);
    void *next = g_memdup2(m->params, m->params_size);
    if (!count) memcpy(next, baseline, m->params_size);
    for (int i = 0; i < count; ++i) {
        dt_introspection_field_t *f = NULL;
        void *current = numeric_field(m, fields[i], &f);
        const ptrdiff_t offset = current ? (char *)current - (char *)m->params : -1;
        if (!f || offset < 0 || f->header.size <= 0 || offset + f->header.size > m->params_size) {
            g_free(next); g_free(baseline); set_error("cannot reset field %s", fields[i]); return -1;
        }
        memcpy((char *)next + offset, (char *)baseline + offset, f->header.size);
    }
    if (count && !strcmp(op, "ashift")) {
        // Perspective and rotation share the derived internal crop. Recompute
        // it after resetting either part, otherwise the old crop stays behind.
        oma_geometry_fn fn = geometry_function(m);
        oma_preview_cache *cache = prepare_preview(imgid, 0, 0, 0);
        dt_dev_pixelpipe_iop_t *piece = geometry_piece(cache);
        const int *mode = m->so->get_p(next, "cropmode");
        if (!fn || !piece || !mode || fn(next, piece->buf_in.width, piece->buf_in.height,
            NULL, 0, 0, 0, CLAMP(*mode, 0, 2), NULL, 0)) {
            g_free(next); g_free(baseline); return geometry_error(1);
        }
    }
    // A shared module remains on while its other tool still has an effect.
    gboolean on = !count || !memcmp(next, baseline, m->params_size) ? baseline_on : m->enabled;
    // Reset a parked mapper's settings without enabling a second mapper.
    if (!was_on && (!strcmp(op, "sigmoid") || !strcmp(op, "filmicrgb") || !strcmp(op, "basecurve"))) {
        for (int i = 0; i < 3; ++i) {
            dt_iop_module_t *tone = find_module(tones[i]);
            if (tone && tone != m && tone->enabled) on = FALSE;
        }
    }
    const gboolean changed = m->enabled != on || memcmp(m->params, next, m->params_size);
    if (changed) {
        memcpy(m->params, next, m->params_size); m->enabled = on;
        ++g_dev.focus_hash; g_new_step = TRUE;
        oma_add_history_item(&g_dev, m, on, TRUE);
    }
    g_free(next); g_free(baseline);
    // Resetting an alternative tone mapper restores the normal mapper it
    // parked, keeping that mapper's own settings. A print keeps owning tone.
    gboolean tones_active = FALSE;
    for (int i = 0; i < 3; ++i) { dt_iop_module_t *tone = find_module(tones[i]); tones_active |= tone && tone->enabled; }
    dt_iop_module_t *print = find_module("omarawprint");
    if (was_on && !on && !tones_active && !(print && print->enabled)
        && (!strcmp(op, "sigmoid") || !strcmp(op, "filmicrgb") || !strcmp(op, "basecurve"))) {
        for (int i = 0; i < 3; ++i) if (tone_on[i]) {
            dt_iop_module_t *tone = find_module(tones[i]);
            tone->enabled = TRUE; g_new_step = TRUE; ++g_dev.focus_hash;
            oma_add_history_item(&g_dev, tone, TRUE, TRUE);
        }
    }
    if (changed) dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

// Preset state belongs to a history prefix, never just a numeric cursor.
// Keeping this in the native catalogue gives backups and variants the same
// semantics as the edit itself, without path-keyed application caches.
static int preset_schema(void) {
    return sqlite3_exec(dt_database_get(darktable.db),
        "CREATE TABLE IF NOT EXISTS main.omaraw_preset_state"
        " (imgid INTEGER NOT NULL, history_end INTEGER NOT NULL, signature TEXT NOT NULL,"
        " state TEXT NOT NULL, PRIMARY KEY(imgid,history_end));"
        "CREATE TRIGGER IF NOT EXISTS main.omaraw_preset_state_remove AFTER DELETE ON main.images BEGIN"
        " DELETE FROM omaraw_preset_state WHERE imgid=OLD.id; END;",
        NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}
static char *preset_signature(int imgid, int end) {
    GChecksum *hash = g_checksum_new(G_CHECKSUM_SHA256);
    const char *queries[] = {
        "SELECT * FROM main.history WHERE imgid=?1 AND num<?2 ORDER BY num",
        "SELECT * FROM main.masks_history WHERE imgid=?1 AND num<?2 ORDER BY num,formid"
    };
    for (int i = 0; i < 2; ++i) {
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(dt_database_get(darktable.db), queries[i], -1, &stmt, NULL) != SQLITE_OK) {
            g_checksum_free(hash); return NULL;
        }
        sqlite3_bind_int(stmt, 1, imgid); sqlite3_bind_int(stmt, 2, end);
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            for (int c = 0; c < sqlite3_column_count(stmt); ++c) {
                const char *name = sqlite3_column_name(stmt, c);
                if (!strcmp(name, "imgid")) continue; // variants have the same history
                const int type = sqlite3_column_type(stmt, c);
                const void *bytes = sqlite3_column_blob(stmt, c);
                const int size = sqlite3_column_bytes(stmt, c);
                char frame[128];
                const int n = snprintf(frame, sizeof frame, "%d:%s:%d:%d:", i, name, type, size);
                g_checksum_update(hash, (const guchar *)frame, n);
                if (size) g_checksum_update(hash, bytes, size);
            }
        }
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { g_checksum_free(hash); return NULL; }
    }
    char *out = g_strdup(g_checksum_get_string(hash)); g_checksum_free(hash); return out;
}
int oma_engine_preset_state_get(int imgid, char **json) {
    if (!json) return -1;
    *json = NULL;
    if (load(imgid) || preset_schema()) return -1;
    dt_dev_write_history(&g_dev);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(dt_database_get(darktable.db),
        "SELECT history_end,signature,state FROM main.omaraw_preset_state"
        " WHERE imgid=?1 AND history_end<=?2 ORDER BY history_end DESC", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt,1,imgid); sqlite3_bind_int(stmt,2,g_dev.history_end);
    int rc;
    while ((rc=sqlite3_step(stmt)) == SQLITE_ROW) {
        char *signature=preset_signature(imgid,sqlite3_column_int(stmt,0));
        const gboolean match=signature && !g_strcmp0(signature,(const char *)sqlite3_column_text(stmt,1));
        g_free(signature);
        if (match) { *json=g_strdup((const char *)sqlite3_column_text(stmt,2)); break; }
    }
    sqlite3_finalize(stmt);
    return *json ? 1 : rc==SQLITE_DONE ? 0 : -1;
}
static int preset_state_insert(int imgid, int end, const char *json) {
    char *signature=preset_signature(imgid,end);
    if (!signature) return SQLITE_ERROR;
    sqlite3_stmt *stmt=NULL;
    int rc=sqlite3_prepare_v2(dt_database_get(darktable.db),
        "INSERT OR REPLACE INTO main.omaraw_preset_state VALUES(?1,?2,?3,?4)",-1,&stmt,NULL);
    if (rc==SQLITE_OK) {
        sqlite3_bind_int(stmt,1,imgid); sqlite3_bind_int(stmt,2,end);
        sqlite3_bind_text(stmt,3,signature,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,json,-1,SQLITE_TRANSIENT);
        rc=sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt); g_free(signature);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}
int oma_engine_preset_state_put(int imgid, int from_end, const char *json, const char *transition_json) {
    if (!json || !transition_json || strlen(json)>16*1024*1024 || strlen(transition_json)>16*1024*1024
        || load(imgid) || preset_schema()) return -1;
    dt_dev_write_history(&g_dev);
    if (from_end<0 || from_end>g_dev.history_end) return -1;
    sqlite3 *db=dt_database_get(darktable.db);
    if (sqlite3_exec(db,"SAVEPOINT omaraw_preset_save",NULL,NULL,NULL)!=SQLITE_OK) return -1;
    char *sql=sqlite3_mprintf("DELETE FROM main.omaraw_preset_state WHERE imgid=%d AND history_end>%d",imgid,from_end);
    int rc=sqlite3_exec(db,sql,NULL,NULL,NULL); sqlite3_free(sql);
    // A History click can land between the first write and the final preset.
    // Its baseline restores the union of the old and new preset's tools.
    if (rc==SQLITE_OK && from_end+1<g_dev.history_end)
        rc=preset_state_insert(imgid,from_end+1,transition_json);
    if (rc==SQLITE_OK) rc=preset_state_insert(imgid,g_dev.history_end,json);
    if (rc==SQLITE_OK && sqlite3_exec(db,"RELEASE omaraw_preset_save",NULL,NULL,NULL)==SQLITE_OK) return 0;
    set_error("could not save preset state: %s",sqlite3_errmsg(db));
    sqlite3_exec(db,"ROLLBACK TO omaraw_preset_save; RELEASE omaraw_preset_save",NULL,NULL,NULL);
    return -1;
}

// Keep the import baseline separately so Reset can restore it after Original
// has discarded the edit/redo history. Full resets clear preset ownership too.
int oma_engine_history_reset(int imgid, int to_start) {
    if (load(imgid) || preset_schema()) return -1;
    dt_dev_write_history(&g_dev);
    const int end = g_dev.history_end;
    // A reset from an earlier history position must still find the complete
    // import baseline, including an automatic first edit above that position.
    g_dev.history_end = g_list_length(g_dev.history);
    const int start = bootstrap_history_end();
    g_dev.history_end = end;
    sqlite3 *db = dt_database_get(darktable.db);
    if (sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS main.omaraw_import_baselines (imgid INTEGER PRIMARY KEY, history_end INTEGER);"
        "CREATE TABLE IF NOT EXISTS main.omaraw_import_history AS SELECT * FROM main.history WHERE 0;"
        "CREATE INDEX IF NOT EXISTS main.omaraw_import_history_imgid ON omaraw_import_history(imgid);"
        "CREATE TRIGGER IF NOT EXISTS main.omaraw_import_baseline_remove AFTER DELETE ON main.images BEGIN"
        " DELETE FROM omaraw_import_history WHERE imgid=OLD.id;"
        " DELETE FROM omaraw_import_baselines WHERE imgid=OLD.id; END;"
        "SAVEPOINT omaraw_history_reset", NULL, NULL, NULL) != SQLITE_OK) {
        set_error("could not prepare history reset: %s", sqlite3_errmsg(db)); return -1;
    }
    char *sql = sqlite3_mprintf(
        "INSERT INTO main.omaraw_import_history SELECT * FROM main.history WHERE imgid=%d AND num<%d"
        " AND NOT EXISTS(SELECT 1 FROM main.omaraw_import_baselines WHERE imgid=%d);"
        "INSERT OR IGNORE INTO main.omaraw_import_baselines VALUES(%d,%d);"
        "DELETE FROM main.history WHERE imgid=%d;"
        "DELETE FROM main.masks_history WHERE imgid=%d;"
        "DELETE FROM main.omaraw_preset_state WHERE imgid=%d;"
        "INSERT INTO main.history SELECT * FROM main.omaraw_import_history WHERE imgid=%d AND %d;"
        "UPDATE main.images SET history_end=CASE WHEN %d THEN"
        " (SELECT history_end FROM main.omaraw_import_baselines WHERE imgid=%d) ELSE 0 END WHERE id=%d;",
        imgid, start, imgid, imgid, start, imgid, imgid, imgid, imgid, !!to_start, !!to_start, imgid, imgid);
    int rc = sqlite3_exec(db, sql, NULL, NULL, NULL); sqlite3_free(sql);
    if (rc == SQLITE_OK) rc = sqlite3_exec(db, "RELEASE omaraw_history_reset", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        set_error("could not reset history: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK TO omaraw_history_reset; RELEASE omaraw_history_reset", NULL, NULL, NULL);
        return -1;
    }
    // Reload reinstates only the required native default steps for Original.
    // No discarded edit, mask or redo row remains. Publish the resulting hash
    // and history cursor for export, sidecars and subsequent application runs.
    reload();
    dt_dev_write_history(&g_dev);
    clear_overviews(imgid);
    g_new_step = TRUE;
    return 0;
}

// ── local adjustments ───────────────────────────────────────────────────
// A fresh instance made headlessly comes with ZEROED default params:
// nothing reloads them for the image the way the base module's were. For
// exposure that happens to be sane; for colour balance it means a grey
// fulcrum of 0 and a black render inside the mask. The base module's
// defaults are the ones darktable prepared, so the instance adopts them.
static void adopt_defaults(dt_iop_module_t *m, const dt_iop_module_t *base) {
    if (!m->default_params || !base->default_params || m->params_size != base->params_size) return;
    memcpy(m->default_params, base->default_params, base->params_size);
    memcpy(m->params, base->default_params, base->params_size);
}

static dt_iop_module_t *find_instance(const char *op, int priority) {
    for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
        dt_iop_module_t *m = (dt_iop_module_t *)l->data;
        if (!strcmp(m->op, op) && m->multi_priority == priority) return m;
    }
    return NULL;
}

// The drawn shape behind an instance's mask group, or NULL.
static dt_masks_form_t *instance_shape(dt_iop_module_t *m, dt_masks_form_t **group) {
    if (group) *group = NULL;
    if (!m->blend_params || !m->blend_params->mask_id) return NULL;
    dt_masks_form_t *grp = dt_masks_get_from_id(&g_dev, m->blend_params->mask_id);
    if (!grp || !(grp->type & DT_MASKS_GROUP) || !grp->points) return NULL;
    if (group) *group = grp;
    dt_masks_point_group_t *pt = (dt_masks_point_group_t *)grp->points->data;
    return dt_masks_get_from_id(&g_dev, pt->formid);
}

// A local exists in the picture only once a step at or below the history end
// made it and while it has its mask: Undo past its creation leaves the
// instance in the engine, off, and Reset (to a look it was not part of)
// returns its blending to the default, without a mask. Neither is listed;
// a full reset discards the old local history and masks.
static gboolean in_active_history(const dt_iop_module_t *m) {
    if (!m->blend_params || !m->blend_params->mask_id) return FALSE;
    int i = 0;
    for (GList *l = g_dev.history; l && i < g_dev.history_end; l = l->next, ++i)
        if (((dt_dev_history_item_t *)l->data)->module == m) return TRUE;
    return FALSE;
}

int oma_engine_local_count(int imgid, const char *op) {
    if (load(imgid)) return -1;
    int n = 0;
    for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
        dt_iop_module_t *m = (dt_iop_module_t *)l->data;
        if (!strcmp(m->op, op) && m->multi_priority > 0 && in_active_history(m)) ++n;
    }
    return n;
}

int oma_engine_local_get(int imgid, const char *op, int index, oma_local_info *info) {
    memset(info, 0, sizeof *info);
    if (load(imgid)) return -1;
    // Oldest first = ascending priority; the base (0) is not a local.
    int best = 0;
    dt_iop_module_t *pick = NULL;
    for (int want = 0; want <= index; ++want) {
        int lowest = 0x7fffffff; dt_iop_module_t *cand = NULL;
        for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
            dt_iop_module_t *m = (dt_iop_module_t *)l->data;
            if (strcmp(m->op, op) || m->multi_priority <= best || !in_active_history(m)) continue;
            if (m->multi_priority < lowest) { lowest = m->multi_priority; cand = m; }
        }
        if (!cand) return -1;
        best = lowest; pick = cand;
    }
    info->found = 1;
    info->priority = pick->multi_priority;
    info->enabled = pick->enabled;
    snprintf(info->name, sizeof info->name, "%s", pick->multi_name);
    dt_masks_form_t *grp = NULL;
    dt_masks_form_t *form = instance_shape(pick, &grp);
    if (getenv("OMARAW_LOCAL_DEBUG"))
        fprintf(stderr, "[local] %s/%d mask_mode=%u mask_id=%d forms=%u grp=%p form=%p type=%d hist=%u end=%d\n", pick->op, pick->multi_priority,
                pick->blend_params ? pick->blend_params->mask_mode : 0u, pick->blend_params ? (int)pick->blend_params->mask_id : -1,
                g_list_length(g_dev.forms), (void *)grp, (void *)form, form ? (int)form->type : -1, g_list_length(g_dev.history), g_dev.history_end);
    info->opacity = grp && grp->points ? entry_opacity((dt_masks_point_group_t *)grp->points->data) : 1.f;
    info->shapes = grp ? (int)g_list_length(grp->points) : 0;
    if (!form) return 0;
    if (form->type & DT_MASKS_CIRCLE && form->points) {
        dt_masks_point_circle_t *c = (dt_masks_point_circle_t *)form->points->data;
        info->shape = 1; info->cx = c->center[0]; info->cy = c->center[1]; info->radius = c->radius; info->border = c->border;
    } else if (form->type & DT_MASKS_GRADIENT && form->points) {
        dt_masks_point_gradient_t *g = (dt_masks_point_gradient_t *)form->points->data;
        info->shape = 2; info->cx = g->anchor[0]; info->cy = g->anchor[1]; info->rotation = g->rotation; info->compression = g->compression;
    } else if (form->type & DT_MASKS_PATH && form->points) {
        const dt_masks_point_path_t *p = form->points->data;
        info->shape = OMA_SHAPE_PATH; info->cx = p->corner[0]; info->cy = p->corner[1]; info->border = p->border[0];
    }
    return 0;
}

static void fill_shape(dt_masks_form_t *form, int shape, float cx, float cy, float a, float b, float rotation) {
    if (shape == 1) {
        dt_masks_point_circle_t *c = form->points ? (dt_masks_point_circle_t *)form->points->data : calloc(1, sizeof(dt_masks_point_circle_t));
        // A border of exactly 0 leaves darktable with no falloff to build and
        // the mask comes out empty — the module then looks switched off. The
        // Feather slider reaches 0, so hold it just above, as for paths.
        // The former .002 floor forced a 10px feather on a 5000px short side.
        c->center[0] = cx; c->center[1] = cy; c->radius = fmaxf(0.005f, a); c->border = fmaxf(0.0001f, b);
        if (!form->points) form->points = g_list_append(form->points, c);
    } else {
        dt_masks_point_gradient_t *g = form->points ? (dt_masks_point_gradient_t *)form->points->data : calloc(1, sizeof(dt_masks_point_gradient_t));
        g->anchor[0] = cx; g->anchor[1] = cy; g->rotation = rotation; g->compression = fminf(1.f, fmaxf(0.001f, a));
        g->steepness = 0.f; g->curvature = 0.f; g->state = DT_MASKS_GRADIENT_STATE_SIGMOIDAL;
        if (!form->points) form->points = g_list_append(form->points, g);
    }
}

static int local_add_form(int imgid, const char *op, int shape, float cx, float cy, float a, float b, float rotation, dt_masks_form_t *drawn) {
    if (load(imgid)) { if (drawn) dt_masks_free_form(drawn); return -1; }
    dt_iop_module_t *base = find_instance(op, 0);
    if (!base) base = find_module(op);
    if (!base) { if (drawn) dt_masks_free_form(drawn); set_error("no module %s", op); return -1; }
    dt_iop_module_t *m = dt_dev_module_duplicate_ext(&g_dev, base, TRUE);
    if (!m) { if (drawn) dt_masks_free_form(drawn); set_error("could not add an instance of %s", op); return -1; }
    adopt_defaults(m, base);
    // A new mask starts neutral. The main exposure module's defaults for a
    // RAW carry the scene-referred +0.7 EV and the camera's exposure-bias
    // compensation; copied into a mask they brightened its area the moment
    // it was added. Its defaults are neutral too, so a reset lands here.
    if(!strcmp(op, "exposure") && m->so->get_p)
    {
        void *const blocks[2] = { m->params, m->default_params };
        for(int k = 0; k < 2; k++)
        {
            if(!blocks[k]) continue;
            float *ev = m->so->get_p(blocks[k], "exposure"), *black = m->so->get_p(blocks[k], "black");
            gboolean *bias = m->so->get_p(blocks[k], "compensate_exposure_bias"), *hilite = m->so->get_p(blocks[k], "compensate_hilite_pres");
            if(ev) *ev = 0.f;
            if(black) *black = 0.f;
            if(bias) *bias = FALSE;
            if(hilite) *hilite = FALSE;
        }
    }
    const int n = oma_engine_local_count(imgid, op);
    snprintf(m->multi_name, sizeof m->multi_name, "%s %d", shape == 1 ? "Radial" : shape == OMA_SHAPE_PATH ? "Pen" : "Gradient", shape == OMA_SHAPE_PATH ? n + 1 : n);
    m->multi_name_hand_edited = TRUE;
    // The shape, then a group that carries it for this instance.
    dt_masks_form_t *form = drawn ? drawn : dt_masks_create(shape == 1 ? DT_MASKS_CIRCLE : DT_MASKS_GRADIENT);
    if (!drawn) fill_shape(form, shape, cx, cy, a, b, rotation);
    if (form->functions && form->functions->set_form_name) form->functions->set_form_name(form, (size_t)g_list_length(g_dev.forms) + 1);
    if (form->type & DT_MASKS_PATH) snprintf(form->name, sizeof form->name, "Pen shape");
    g_dev.forms = g_list_append(g_dev.forms, form);
    dt_masks_form_t *grp = dt_masks_create(DT_MASKS_GROUP);
    snprintf(grp->name, sizeof grp->name, "grp %s", m->multi_name);
    g_dev.forms = g_list_append(g_dev.forms, grp);
    dt_masks_point_group_t *pt = calloc(1, sizeof(dt_masks_point_group_t));
    pt->formid = form->formid; pt->parentid = grp->formid; pt->state = DT_MASKS_STATE_SHOW | DT_MASKS_STATE_USE; pt->opacity = 1.f;
    grp->points = g_list_append(grp->points, pt);
    m->blend_params->mask_id = grp->formid;
    m->blend_params->mask_mode = DEVELOP_MASK_ENABLED | DEVELOP_MASK_MASK;
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    const int priority = m->multi_priority;
    reload();
    return priority;
}

int oma_engine_local_add(int imgid, const char *op, int shape, float cx, float cy, float a, float b, float rotation) {
    return local_add_form(imgid, op, shape, cx, cy, a, b, rotation, NULL);
}

int oma_engine_local_set_mask(int imgid, const char *op, int priority, float cx, float cy, float a, float b, float rotation, float opacity) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *grp = NULL;
    dt_masks_form_t *form = instance_shape(m, &grp);
    if (!form || !grp) { set_error("instance has no drawn mask", NULL); return -1; }
    if (!(form->type & (DT_MASKS_CIRCLE | DT_MASKS_GRADIENT))) {
        set_error("use the shape editor for this mask", NULL); return -1;
    }
    fill_shape(form, form->type & DT_MASKS_CIRCLE ? 1 : 2, cx, cy, a, b, rotation);
    entry_set_opacity((dt_masks_point_group_t *)grp->points->data, opacity);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

// Drops a module instance: its history items go, the end pointer
// follows, then the module itself.
static void drop_instance(dt_iop_module_t *m) {
    GList *elem = g_dev.history;
    int pos = 0;
    while (elem) {
        GList *next = g_list_next(elem);
        dt_dev_history_item_t *h = (dt_dev_history_item_t *)elem->data;
        if (h->module == m) {
            dt_dev_free_history_item(h);
            g_dev.history = g_list_delete_link(g_dev.history, elem);
            if (pos < g_dev.history_end) g_dev.history_end--;
        } else ++pos;
        elem = next;
    }
    g_dev.iop = g_list_remove(g_dev.iop, m);
    dt_iop_cleanup_module(m);
    free(m);
}

// Removing a local is a History step like any other, so Undo brings it
// back: the local and the tools riding on its mask go off and let go of
// the mask (their blending returns to the default), which is what takes a
// local off the list (in_active_history). Nothing is deleted; the
// instances stay in the engine, off, and cost no rendering.
static void retire_instance(dt_iop_module_t *m) {
    m->enabled = FALSE;
    if (m->blend_params && m->default_blendop_params)
        memcpy(m->blend_params, m->default_blendop_params, sizeof(dt_develop_blend_params_t));
    ++g_dev.focus_hash;
    g_new_step = TRUE;
    oma_add_history_item(&g_dev, m, FALSE, TRUE);
}

int oma_engine_local_remove(int imgid, const char *op, int priority) {
    if (load(imgid) || priority <= 0) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    // The stack goes with the local; its members are found by the mask, so
    // they are listed before the anchor lets go of it.
    dt_iop_module_t *members[OMA_STACK_MAX];
    const int n = stack_members(m, members, OMA_STACK_MAX);
    for (int i = 0; i < n; ++i) retire_instance(members[i]);
    retire_instance(m);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

// ── shapes inside one local's mask group ────────────────────────────────
// The group that carries an instance's drawn shapes, or NULL.
// The group is found by id whatever the MASK bit says: a range-only local
// keeps an EMPTY group, so the id still ties its stack members to it.
static dt_masks_form_t *instance_group(dt_iop_module_t *m) {
    if (!m->blend_params || !m->blend_params->mask_id) return NULL;
    dt_masks_form_t *grp = dt_masks_get_from_id(&g_dev, m->blend_params->mask_id);
    if (!grp || !(grp->type & DT_MASKS_GROUP)) return NULL;
    return grp;
}

// Modules riding on `anchor`'s mask: any other instance (priority > 0)
// whose blend params point at the same group.
static int stack_members(dt_iop_module_t *anchor, dt_iop_module_t **out, int max) {
    int n = 0;
    if (!anchor->blend_params || !anchor->blend_params->mask_id) return 0;
    for (GList *l = g_dev.iop; l && n < max; l = g_list_next(l)) {
        dt_iop_module_t *o = (dt_iop_module_t *)l->data;
        if (o == anchor || o->multi_priority <= 0 || !o->blend_params) continue;
        if (o->blend_params->mask_id == anchor->blend_params->mask_id) out[n++] = o;
    }
    return n;
}

// The drawn half of the mask is on while the group holds a shape; the
// anchor and every member agree.
static void sync_mask_mode(dt_iop_module_t *anchor) {
    dt_masks_form_t *grp = instance_group(anchor);
    const int drawn = grp && grp->points;
    dt_iop_module_t *mods[OMA_STACK_MAX + 1]; mods[0] = anchor;
    const int n = 1 + stack_members(anchor, mods + 1, OMA_STACK_MAX);
    for (int i = 0; i < n; ++i) {
        if (drawn) mods[i]->blend_params->mask_mode |= DEVELOP_MASK_ENABLED | DEVELOP_MASK_MASK;
        else mods[i]->blend_params->mask_mode &= ~DEVELOP_MASK_MASK;
    }
}

// The group entry at `index` and, through `form`, the shape it points at.
static dt_masks_point_group_t *group_entry(dt_masks_form_t *grp, int index, dt_masks_form_t **form) {
    if (form) *form = NULL;
    if (!grp) return NULL;
    GList *l = g_list_nth(grp->points, index);
    if (!l) return NULL;
    dt_masks_point_group_t *pt = (dt_masks_point_group_t *)l->data;
    if (form) *form = dt_masks_get_from_id(&g_dev, pt->formid);
    return pt;
}

// ── bypass bookkeeping ──────────────────────────────────────────────────
// darktable has no switch on a group entry, and the pixelpipe combines
// every entry it finds. A bypassed shape therefore stays in the group but
// is made a no-op: its operator reads as a union and its opacity goes
// negative, since max(buffer, negative) leaves the buffer alone. The real
// operator lives in bits 8..12 of the state (darktable uses 0..7) for every
// entry, and the real opacity of a bypassed one is -(opacity + 1). The low
// operator bits are derived from that truth by normalize_group(): a
// bypassed entry, and the first live entry (nothing under it), are unions.
// The pinned engine patch initializes group destinations to an empty mask
// before any union, including groups whose shapes are all bypassed.
#define OMA_STASH_SHIFT 5
#define OMA_STASH_MASK  (DT_MASKS_STATE_OP << OMA_STASH_SHIFT)
#define OMA_STATE_BYPASS (1 << 13)

static int op_bits_to_combine(int bits) {
    if (bits & DT_MASKS_STATE_INTERSECTION) return OMA_MASK_INTERSECT;
    if (bits & DT_MASKS_STATE_DIFFERENCE) return OMA_MASK_DIFFERENCE;
    if (bits & DT_MASKS_STATE_EXCLUSION) return OMA_MASK_EXCLUSION;
    return OMA_MASK_UNION;
}
static int combine_to_op_bits(int combine) {
    switch (combine) {
    case OMA_MASK_INTERSECT: return DT_MASKS_STATE_INTERSECTION;
    case OMA_MASK_DIFFERENCE: return DT_MASKS_STATE_DIFFERENCE;
    case OMA_MASK_EXCLUSION: return DT_MASKS_STATE_EXCLUSION;
    default: return DT_MASKS_STATE_UNION;
    }
}
// The operator the user chose, whatever the pipe is currently shown.
static int combine_of(dt_masks_points_states_t state) {
    const int stash = (state & OMA_STASH_MASK) >> OMA_STASH_SHIFT;
    return op_bits_to_combine(stash ? stash : (state & DT_MASKS_STATE_OP));
}
static int entry_bypassed(const dt_masks_point_group_t *pt) { return (pt->state & OMA_STATE_BYPASS) ? 1 : 0; }
static float entry_opacity(const dt_masks_point_group_t *pt) { return pt->opacity < 0.f ? -pt->opacity - 1.f : pt->opacity; }
static void entry_set_opacity(dt_masks_point_group_t *pt, float v) {
    v = fminf(1.f, fmaxf(0.f, v));
    pt->opacity = entry_bypassed(pt) ? -(v + 1.f) : v;
}

// Truth in, pipe state out: the stash always holds the chosen operator,
// and the low bits are what darktable will actually run.
static void normalize_group(dt_masks_form_t *grp) {
    int live_seen = 0;
    for (GList *l = grp ? grp->points : NULL; l; l = g_list_next(l)) {
        dt_masks_point_group_t *pt = (dt_masks_point_group_t *)l->data;
        const int real = combine_to_op_bits(combine_of(pt->state));
        const int bypass = entry_bypassed(pt);
        const int inverse = pt->state & DT_MASKS_STATE_INVERSE;
        const float opacity = entry_opacity(pt);
        int eff = real;
        if (bypass || !live_seen) eff = DT_MASKS_STATE_UNION;
        pt->state = DT_MASKS_STATE_SHOW | (bypass ? 0 : DT_MASKS_STATE_USE) | inverse | eff
                    | (real << OMA_STASH_SHIFT) | (bypass ? OMA_STATE_BYPASS : 0);
        pt->opacity = bypass ? -(opacity + 1.f) : opacity;
        if (!bypass) live_seen = 1;
    }
}

static dt_masks_points_states_t state_for(int combine, int inverse, int first) {
    // Nothing sits under shape 0, so it can only be a union.
    const int real = combine_to_op_bits(first ? OMA_MASK_UNION : combine);
    dt_masks_points_states_t s = DT_MASKS_STATE_SHOW | DT_MASKS_STATE_USE | real | (real << OMA_STASH_SHIFT);
    if (inverse) s |= DT_MASKS_STATE_INVERSE;
    return s;
}

// Catmull-Rom tangents as bezier handles, the way brush.c does it for the
// GUI; forms we build in memory never pass through its event handlers.
static void catmull_to_bezier(float x1, float y1, float x2, float y2, float x3, float y3,
                              float x4, float y4, float *bx1, float *by1, float *bx2, float *by2) {
    *bx1 = (-x1 + 6 * x2 + x3) / 6; *by1 = (-y1 + 6 * y2 + y3) / 6;
    *bx2 = (x2 + 6 * x3 - x4) / 6;  *by2 = (y2 + 6 * y3 - y4) / 6;
}

static void brush_init_ctrl_points(dt_masks_form_t *form) {
    if (g_list_shorter_than(form->points, 2)) return;
    dt_masks_point_brush_t start[2], end[2];
    for (GList *l = form->points; l; l = g_list_next(l)) {
        dt_masks_point_brush_t *p3 = (dt_masks_point_brush_t *)l->data;
        GList *prev = g_list_previous(l), *pp = prev ? g_list_previous(prev) : NULL;
        GList *next = g_list_next(l), *nn = next ? g_list_next(next) : NULL;
        dt_masks_point_brush_t *p1 = pp ? pp->data : NULL, *p2 = prev ? prev->data : NULL;
        dt_masks_point_brush_t *p4 = next ? next->data : NULL, *p5 = nn ? nn->data : NULL;
        // The ends mirror their neighbourhood so the spline has tangents.
        if (!p1 && !p2) {
            start[0].corner[0] = start[1].corner[0] = 2 * p3->corner[0] - p4->corner[0];
            start[0].corner[1] = start[1].corner[1] = 2 * p3->corner[1] - p4->corner[1];
            p1 = &start[0]; p2 = &start[1];
        } else if (!p1) {
            start[0].corner[0] = 2 * p2->corner[0] - p3->corner[0];
            start[0].corner[1] = 2 * p2->corner[1] - p3->corner[1];
            p1 = &start[0];
        }
        if (!p4 && !p5) {
            end[0].corner[0] = end[1].corner[0] = 2 * p3->corner[0] - p2->corner[0];
            end[0].corner[1] = end[1].corner[1] = 2 * p3->corner[1] - p2->corner[1];
            p4 = &end[0]; p5 = &end[1];
        } else if (!p5) {
            end[0].corner[0] = 2 * p4->corner[0] - p3->corner[0];
            end[0].corner[1] = 2 * p4->corner[1] - p3->corner[1];
            p5 = &end[0];
        }
        float bx1 = 0.f, by1 = 0.f, bx2 = 0.f, by2 = 0.f;
        catmull_to_bezier(p1->corner[0], p1->corner[1], p2->corner[0], p2->corner[1],
                          p3->corner[0], p3->corner[1], p4->corner[0], p4->corner[1], &bx1, &by1, &bx2, &by2);
        if (p2->ctrl2[0] == -1.f) p2->ctrl2[0] = bx1;
        if (p2->ctrl2[1] == -1.f) p2->ctrl2[1] = by1;
        p3->ctrl1[0] = bx2; p3->ctrl1[1] = by2;
        catmull_to_bezier(p2->corner[0], p2->corner[1], p3->corner[0], p3->corner[1],
                          p4->corner[0], p4->corner[1], p5->corner[0], p5->corner[1], &bx1, &by1, &bx2, &by2);
        if (p4->ctrl1[0] == -1.f) p4->ctrl1[0] = bx2;
        if (p4->ctrl1[1] == -1.f) p4->ctrl1[1] = by2;
        p3->ctrl2[0] = bx1; p3->ctrl2[1] = by1;
    }
}

int oma_engine_shape_count(int imgid, const char *op, int priority) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *grp = instance_group(m);
    return grp ? (int)g_list_length(grp->points) : 0;
}

int oma_engine_shape_get(int imgid, const char *op, int priority, int index, oma_shape_info *info) {
    memset(info, 0, sizeof *info);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *form = NULL;
    dt_masks_point_group_t *pt = group_entry(instance_group(m), index, &form);
    if (!pt || !form) return 0;
    info->found = 1;
    info->opacity = entry_opacity(pt);
    info->combine = index == 0 ? OMA_MASK_UNION : combine_of(pt->state);
    info->inverse = (pt->state & DT_MASKS_STATE_INVERSE) ? 1 : 0;
    info->enabled = entry_bypassed(pt) ? 0 : 1;
    snprintf(info->name, sizeof info->name, "%s", form->name);
    if (form->type & DT_MASKS_CIRCLE && form->points) {
        dt_masks_point_circle_t *c = (dt_masks_point_circle_t *)form->points->data;
        info->shape = OMA_SHAPE_CIRCLE; info->cx = c->center[0]; info->cy = c->center[1];
        info->radius = c->radius; info->border = c->border;
    } else if (form->type & DT_MASKS_GRADIENT && form->points) {
        dt_masks_point_gradient_t *g = (dt_masks_point_gradient_t *)form->points->data;
        info->shape = OMA_SHAPE_GRADIENT; info->cx = g->anchor[0]; info->cy = g->anchor[1];
        info->rotation = g->rotation; info->compression = g->compression;
    } else if (form->type & DT_MASKS_BRUSH && form->points) {
        dt_masks_point_brush_t *b = (dt_masks_point_brush_t *)form->points->data;
        info->shape = OMA_SHAPE_BRUSH; info->cx = b->corner[0]; info->cy = b->corner[1];
        info->size = b->border[0]; info->hardness = b->hardness; info->density = b->density;
        info->points = (int)g_list_length(form->points);
    } else if (form->type & DT_MASKS_PATH && form->points) {
        const dt_masks_point_path_t *p = form->points->data;
        info->shape = OMA_SHAPE_PATH; info->cx = p->corner[0]; info->cy = p->corner[1];
        info->border = p->border[0]; info->points = (int)g_list_length(form->points);
    }
    return 0;
}

// Builds a shape, hangs it off the instance's group and records the history.
static int append_shape(dt_iop_module_t *m, dt_masks_form_t *form) {
    dt_masks_form_t *grp = instance_group(m);
    if (!grp) { set_error("instance has no mask group", NULL); dt_masks_free_form(form); return -1; }
    if (form->functions && form->functions->set_form_name)
        form->functions->set_form_name(form, (size_t)g_list_length(g_dev.forms) + 1);
    if (form->type & DT_MASKS_PATH) snprintf(form->name, sizeof form->name, "Pen shape %u", g_list_length(grp->points) + 1);
    g_dev.forms = g_list_append(g_dev.forms, form);
    dt_masks_point_group_t *pt = calloc(1, sizeof(dt_masks_point_group_t));
    pt->formid = form->formid; pt->parentid = grp->formid; pt->opacity = 1.f;
    pt->state = state_for(OMA_MASK_UNION, 0, g_list_length(grp->points) == 0);
    grp->points = g_list_append(grp->points, pt);
    normalize_group(grp);
    sync_mask_mode(m);
    const int index = (int)g_list_length(grp->points) - 1;
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return index;
}

int oma_engine_shape_add(int imgid, const char *op, int priority, int shape, float cx, float cy, float a, float b, float rotation) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *form = dt_masks_create(shape == OMA_SHAPE_CIRCLE ? DT_MASKS_CIRCLE : DT_MASKS_GRADIENT);
    fill_shape(form, shape == OMA_SHAPE_CIRCLE ? 1 : 2, cx, cy, a, b, rotation);
    return append_shape(m, form);
}

static int valid_path(const oma_path_node *nodes, int count, float feather) {
    if (!nodes || count < 3 || count > 512 || !isfinite(feather) || feather < 0.f || feather > 1.f) return 0;
    for (int i = 0; i < count; ++i) {
        const float values[] = {nodes[i].x, nodes[i].y, nodes[i].in_x, nodes[i].in_y, nodes[i].out_x, nodes[i].out_y};
        for (int k = 0; k < 6; ++k) if (!isfinite(values[k]) || fabsf(values[k]) > 8.f) return 0;
    }
    return 1;
}
static void fill_path(dt_masks_form_t *form, const oma_path_node *nodes, int count, float feather) {
    g_list_free_full(form->points, free); form->points = NULL;
    for (int i = 0; i < count; ++i) {
        dt_masks_point_path_t *p = calloc(1, sizeof *p);
        p->corner[0] = nodes[i].x; p->corner[1] = nodes[i].y;
        p->ctrl1[0] = nodes[i].in_x; p->ctrl1[1] = nodes[i].in_y;
        p->ctrl2[0] = nodes[i].out_x; p->ctrl2[1] = nodes[i].out_y;
        p->border[0] = p->border[1] = fmaxf(.0001f, feather);
        p->state = DT_MASKS_POINT_STATE_USER;
        form->points = g_list_append(form->points, p);
    }
}
int oma_engine_local_add_path(int imgid, const char *op, const oma_path_node *nodes, int count, float feather) {
    if (!valid_path(nodes, count, feather) || load(imgid)) return -1;
    dt_masks_form_t *form = dt_masks_create(DT_MASKS_PATH);
    fill_path(form, nodes, count, feather);
    return local_add_form(imgid, op, OMA_SHAPE_PATH, 0, 0, 0, 0, 0, form);
}

int oma_engine_local_add_selection(int imgid, const uint8_t *mask, int width, int height, int stride) {
    if (!mask || width < 1 || height < 1 || width > 4096 || height > 4096 || stride < width || load(imgid)) return -1;
    float *raster = malloc((size_t)width * height * sizeof(float));
    if (!raster) return -1;
    int selected = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int inside = mask[(size_t)y * stride + x] > 127;
        selected += inside;
        raster[(size_t)y * width + x] = inside ? 0.f : 1.f;
    }
    // Microscopic islands/pinholes become conspicuous independent path
    // handles even when they were barely visible in the selection overlay.
    // Scale the area cutoff with resolution (at most 16 pixels at 4096),
    // and cap it to 0.1% of selected coverage so small subjects survive.
    // Filter by contour area, not width: long thin details remain intact.
    const double edge = MAX(width, height);
    const int speck_area = MAX(1, MIN((int)ceil(edge * edge / (1024.0 * 1024.0)), selected / 1000));
    GList *signs = NULL;
    GList *forms = ras2forms(raster, width, height, NULL, .5f, speck_area, 1.0, &signs);
    // Detailed AI edges can exceed the editable path's node budget even
    // though their coverage is perfectly usable. Retry the same bitmap
    // with Potrace's smooth corner setting before refusing the selection.
    // The usual trace, islands and holes stay untouched when it already fits.
    const double smooth_retry[] = { 1.1, 1.2, 1.3 };
    for (int attempt = 0; forms && attempt < 3 && g_list_length(forms) <= 128; ++attempt) {
        int dense_outline = 0;
        for (GList *l = forms; l; l = l->next)
            if (g_list_length(((dt_masks_form_t *)l->data)->points) > 513) dense_outline = 1;
        if (!dense_outline) break;
        g_list_free_full(forms, (GDestroyNotify)dt_masks_free_form); g_list_free(signs); signs = NULL;
        forms = ras2forms(raster, width, height, NULL, .5f, speck_area, smooth_retry[attempt], &signs);
    }
    free(raster);
    int valid = forms && g_list_length(forms) <= 128;
    // Validate and inverse-map every node before recording any edit.
    for (GList *l = forms; valid && l; l = l->next) {
        dt_masks_form_t *form = l->data;
        GList *last = g_list_last(form->points);
        if (last && last != form->points) {
            dt_masks_point_path_t *a = form->points->data, *b = last->data;
            if (fabsf(a->corner[0] - b->corner[0]) < .0001f && fabsf(a->corner[1] - b->corner[1]) < .0001f) {
                memcpy(a->ctrl1, b->ctrl1, sizeof a->ctrl1);
                free(b); form->points = g_list_delete_link(form->points, last);
            }
        }
        const int n = g_list_length(form->points);
        if (n < 3 || n > 512) { valid = 0; break; }
        float xy[512 * 6]; int j = 0;
        for (GList *p = form->points; p; p = p->next) {
            const dt_masks_point_path_t *point = p->data;
            xy[j++] = point->corner[0] / width; xy[j++] = point->corner[1] / height;
            xy[j++] = point->ctrl1[0] / width; xy[j++] = point->ctrl1[1] / height;
            xy[j++] = point->ctrl2[0] / width; xy[j++] = point->ctrl2[1] / height;
        }
        if (oma_engine_mask_coordinates(imgid, xy, n * 3, 0)) { valid = 0; break; }
        j = 0;
        for (GList *p = form->points; p; p = p->next) {
            dt_masks_point_path_t *point = p->data;
            point->corner[0] = xy[j++]; point->corner[1] = xy[j++];
            point->ctrl1[0] = xy[j++]; point->ctrl1[1] = xy[j++];
            point->ctrl2[0] = xy[j++]; point->ctrl2[1] = xy[j++];
            point->border[0] = point->border[1] = .0001f;
        }
    }
    if (!valid) {
        g_list_free_full(forms, (GDestroyNotify)dt_masks_free_form); g_list_free(signs);
        set_error("Selection is empty, too detailed, or cannot follow this geometry. Refine the selection and try again.", NULL);
        return -1;
    }
    dt_masks_form_t *first = forms->data;
    forms = g_list_delete_link(forms, forms);
    signs = g_list_delete_link(signs, signs);
    const int priority = local_add_form(imgid, "exposure", OMA_SHAPE_PATH, 0, 0, 0, 0, 0, first);
    if (priority < 0 || load(imgid)) {
        g_list_free_full(forms, (GDestroyNotify)dt_masks_free_form); g_list_free(signs); return -1;
    }
    dt_iop_module_t *m = find_instance("exposure", priority);
    dt_masks_form_t *grp = instance_group(m);
    for (GList *l = forms, *s = signs; l; l = l->next, s = s->next) {
        dt_masks_form_t *form = l->data;
        snprintf(form->name, sizeof form->name, "AI selection %u", g_list_length(grp->points) + 1);
        g_dev.forms = g_list_append(g_dev.forms, form);
        dt_masks_point_group_t *pt = calloc(1, sizeof *pt);
        pt->formid = form->formid; pt->parentid = grp->formid; pt->opacity = 1.f;
        pt->state = state_for(GPOINTER_TO_INT(s->data) == '-' ? OMA_MASK_DIFFERENCE : OMA_MASK_UNION, 0, 0);
        grp->points = g_list_append(grp->points, pt);
    }
    g_list_free(forms); g_list_free(signs);
    snprintf(m->multi_name, sizeof m->multi_name, "AI mask %d", priority);
    normalize_group(grp); sync_mask_mode(m);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev); reload();
    return priority;
}
int oma_engine_shape_add_path(int imgid, const char *op, int priority, const oma_path_node *nodes, int count, float feather) {
    if (!valid_path(nodes, count, feather) || load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority); if (!m) return -1;
    dt_masks_form_t *form = dt_masks_create(DT_MASKS_PATH);
    fill_path(form, nodes, count, feather);
    return append_shape(m, form);
}
int oma_engine_shape_path_nodes(int imgid, const char *op, int priority, int index, oma_path_node *out, int capacity) {
    if (!out || capacity < 1 || load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority); dt_masks_form_t *form = NULL;
    if (!m || !group_entry(instance_group(m), index, &form) || !form || !(form->type & DT_MASKS_PATH)) return -1;
    int n = 0;
    for (GList *l = form->points; l && n < capacity; l = l->next, ++n) {
        const dt_masks_point_path_t *p = l->data;
        out[n] = (oma_path_node){p->corner[0], p->corner[1], p->ctrl1[0], p->ctrl1[1], p->ctrl2[0], p->ctrl2[1]};
    }
    return n;
}
int oma_engine_shape_set_path(int imgid, const char *op, int priority, int index, const oma_path_node *nodes, int count, float feather) {
    if (!valid_path(nodes, count, feather) || load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority); dt_masks_form_t *form = NULL;
    if (!m || !group_entry(instance_group(m), index, &form) || !form || !(form->type & DT_MASKS_PATH)) return -1;
    fill_path(form, nodes, count, feather);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev); reload(); return 0;
}

int oma_engine_shape_add_brush(int imgid, const char *op, int priority, const float *pts, int n, float size, float hardness, float density) {
    if (load(imgid)) return -1;
    if (!pts || n < 1) { set_error("a brush stroke needs at least one node", NULL); return -1; }
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *form = dt_masks_create(DT_MASKS_BRUSH);
    for (int i = 0; i < n; ++i) {
        dt_masks_point_brush_t *p = calloc(1, sizeof(dt_masks_point_brush_t));
        p->corner[0] = pts[i * 2]; p->corner[1] = pts[i * 2 + 1];
        p->ctrl1[0] = p->ctrl1[1] = p->ctrl2[0] = p->ctrl2[1] = -1.f;
        p->border[0] = p->border[1] = fmaxf(0.0005f, size);
        p->hardness = fminf(1.f, fmaxf(0.05f, hardness));
        p->density = fminf(2.f, fmaxf(0.f, density));
        p->state = DT_MASKS_POINT_STATE_NORMAL;
        form->points = g_list_append(form->points, p);
    }
    brush_init_ctrl_points(form);
    return append_shape(m, form);
}

int oma_engine_shape_brush_points(int imgid, const char *op, int priority, int index, float *out, int max) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *form = NULL;
    if (!group_entry(instance_group(m), index, &form) || !form || !(form->type & DT_MASKS_BRUSH)) return 0;
    int n = 0;
    for (GList *l = form->points; l && n < max; l = g_list_next(l), ++n) {
        dt_masks_point_brush_t *p = (dt_masks_point_brush_t *)l->data;
        out[n * 2] = p->corner[0]; out[n * 2 + 1] = p->corner[1];
    }
    return n;
}

int oma_engine_shape_set(int imgid, const char *op, int priority, int index, float cx, float cy, float a, float b, float rotation, float opacity) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *form = NULL;
    dt_masks_point_group_t *pt = group_entry(instance_group(m), index, &form);
    if (!pt || !form) { set_error("no such shape", NULL); return -1; }
    if (form->type & DT_MASKS_BRUSH) {
        // A stroke moves as a whole: shift every node by the drag, and take
        // `a` as the new node size when it is positive.
        dt_masks_point_brush_t *first = (dt_masks_point_brush_t *)form->points->data;
        const float dx = cx - first->corner[0], dy = cy - first->corner[1];
        for (GList *l = form->points; l; l = g_list_next(l)) {
            dt_masks_point_brush_t *p = (dt_masks_point_brush_t *)l->data;
            p->corner[0] += dx; p->corner[1] += dy;
            p->ctrl1[0] += dx; p->ctrl1[1] += dy;
            p->ctrl2[0] += dx; p->ctrl2[1] += dy;
            if (a > 0.f) p->border[0] = p->border[1] = a;
            if (b > 0.f) p->hardness = fminf(1.f, b);
        }
    } else if (form->type & DT_MASKS_PATH) {
        const dt_masks_point_path_t *first = form->points->data;
        const float dx = cx - first->corner[0], dy = cy - first->corner[1];
        for (GList *l = form->points; l; l = l->next) {
            dt_masks_point_path_t *p = l->data;
            p->corner[0] += dx; p->corner[1] += dy;
            p->ctrl1[0] += dx; p->ctrl1[1] += dy; p->ctrl2[0] += dx; p->ctrl2[1] += dy;
            if (a >= 0.f) p->border[0] = p->border[1] = fmaxf(.0001f, a);
        }
    } else {
        fill_shape(form, form->type & DT_MASKS_CIRCLE ? 1 : 2, cx, cy, a, b, rotation);
    }
    entry_set_opacity(pt, opacity);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_shape_set_combine(int imgid, const char *op, int priority, int index, int combine, int inverse) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *grp = instance_group(m);
    dt_masks_point_group_t *pt = group_entry(grp, index, NULL);
    if (!pt) { set_error("no such shape", NULL); return -1; }
    const int bypass = entry_bypassed(pt);
    const float opacity = entry_opacity(pt);
    pt->state = state_for(combine, inverse, index == 0) | (bypass ? OMA_STATE_BYPASS : 0);
    pt->opacity = opacity;
    normalize_group(grp);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_shape_remove(int imgid, const char *op, int priority, int index) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *grp = instance_group(m);
    dt_masks_form_t *form = NULL;
    dt_masks_point_group_t *pt = group_entry(grp, index, &form);
    if (!pt || !grp) { set_error("no such shape", NULL); return -1; }
    if (g_list_length(grp->points) < 2) {
        // Dropping the last drawn shape is only allowed when a range is
        // holding the mask together; otherwise the local would quietly
        // become a global adjustment.
        if (!(m->blend_params->blendif & 0x0000ffffu)) { set_error("a local keeps at least one shape unless a range narrows it", NULL); return -1; }
        grp->points = g_list_remove(grp->points, pt);
        free(pt);
        if (form) { g_dev.forms = g_list_remove(g_dev.forms, form); dt_masks_free_form(form); }
        // The group stays, empty: its id is what ties the stack to the local.
        sync_mask_mode(m);
        oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
        dt_dev_write_history(&g_dev);
        reload();
        return 0;
    }
    grp->points = g_list_remove(grp->points, pt);
    free(pt);
    // darktable's own unused-mask sweep reaches the GUI, so free it here.
    if (form) { g_dev.forms = g_list_remove(g_dev.forms, form); dt_masks_free_form(form); }
    // Whatever is first now can only be a union.
    normalize_group(grp);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

// ── shape housekeeping: duplicate, reorder, bypass, rename, brush ───────
static void nudge_form(dt_masks_form_t *form, float dx, float dy) {
    for (GList *l = form->points; l; l = g_list_next(l)) {
        if (form->type & DT_MASKS_CIRCLE) {
            dt_masks_point_circle_t *c = (dt_masks_point_circle_t *)l->data;
            c->center[0] += dx; c->center[1] += dy;
        } else if (form->type & DT_MASKS_GRADIENT) {
            dt_masks_point_gradient_t *g = (dt_masks_point_gradient_t *)l->data;
            g->anchor[0] += dx; g->anchor[1] += dy;
        } else if (form->type & DT_MASKS_BRUSH) {
            dt_masks_point_brush_t *p = (dt_masks_point_brush_t *)l->data;
            p->corner[0] += dx; p->corner[1] += dy;
            p->ctrl1[0] += dx; p->ctrl1[1] += dy;
            p->ctrl2[0] += dx; p->ctrl2[1] += dy;
        } else if (form->type & DT_MASKS_PATH) {
            dt_masks_point_path_t *p = l->data;
            p->corner[0] += dx; p->corner[1] += dy;
            p->ctrl1[0] += dx; p->ctrl1[1] += dy; p->ctrl2[0] += dx; p->ctrl2[1] += dy;
        }
    }
}

int oma_engine_shape_duplicate(int imgid, const char *op, int priority, int index) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *grp = instance_group(m);
    dt_masks_form_t *form = NULL;
    dt_masks_point_group_t *pt = group_entry(grp, index, &form);
    if (!pt || !form) { set_error("no such shape", NULL); return -1; }
    dt_masks_form_t *copy = dt_masks_dup_masks_form(form);
    if (!copy) { set_error("could not copy the shape", NULL); return -1; }
    // A fresh id the way darktable hands them out, then a fresh name.
    dt_masks_form_t *idsrc = dt_masks_create(form->type);
    copy->formid = idsrc->formid;
    dt_masks_free_form(idsrc);
    if (copy->functions && copy->functions->set_form_name) copy->functions->set_form_name(copy, (size_t)g_list_length(g_dev.forms) + 1);
    if (copy->type & DT_MASKS_PATH) snprintf(copy->name, sizeof copy->name, "Pen shape copy");
    // Nudged a little so the two are not on top of each other.
    nudge_form(copy, 0.03f, 0.03f);
    g_dev.forms = g_list_append(g_dev.forms, copy);
    dt_masks_point_group_t *npt = calloc(1, sizeof(dt_masks_point_group_t));
    *npt = *pt;
    npt->formid = copy->formid;
    grp->points = g_list_insert(grp->points, npt, index + 1);
    normalize_group(grp);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return index + 1;
}

int oma_engine_shape_move(int imgid, const char *op, int priority, int from, int to) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *grp = instance_group(m);
    if (!grp) { set_error("instance has no mask group", NULL); return -1; }
    const int n = (int)g_list_length(grp->points);
    if (from < 0 || from >= n || to < 0 || to >= n) { set_error("no such shape", NULL); return -1; }
    if (from == to) return 0;
    GList *elem = g_list_nth(grp->points, from);
    dt_masks_point_group_t *pt = (dt_masks_point_group_t *)elem->data;
    grp->points = g_list_delete_link(grp->points, elem);
    grp->points = g_list_insert(grp->points, pt, to);
    normalize_group(grp);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_shape_set_enabled(int imgid, const char *op, int priority, int index, int enabled) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *grp = instance_group(m);
    dt_masks_point_group_t *pt = group_entry(grp, index, NULL);
    if (!pt) { set_error("no such shape", NULL); return -1; }
    if (entry_bypassed(pt) == !enabled) return 0;
    const float opacity = entry_opacity(pt);
    if (enabled) pt->state &= ~OMA_STATE_BYPASS; else pt->state |= OMA_STATE_BYPASS;
    pt->opacity = opacity;
    normalize_group(grp);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_shape_rename(int imgid, const char *op, int priority, int index, const char *name) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *form = NULL;
    if (!group_entry(instance_group(m), index, &form) || !form) { set_error("no such shape", NULL); return -1; }
    snprintf(form->name, sizeof form->name, "%s", name ? name : "");
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_shape_set_brush(int imgid, const char *op, int priority, int index, float size, float hardness, float density) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_masks_form_t *form = NULL;
    if (!group_entry(instance_group(m), index, &form) || !form || !(form->type & DT_MASKS_BRUSH)) { set_error("no such brush stroke", NULL); return -1; }
    for (GList *l = form->points; l; l = g_list_next(l)) {
        dt_masks_point_brush_t *p = (dt_masks_point_brush_t *)l->data;
        if (size > 0.f) p->border[0] = p->border[1] = fmaxf(0.0005f, size);
        if (hardness > 0.f) p->hardness = fminf(1.f, fmaxf(0.05f, hardness));
        if (density >= 0.f) p->density = fminf(1.f, fmaxf(0.f, density));
    }
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_local_rename(int imgid, const char *op, int priority, const char *name) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m || !m->blend_params) { set_error("no such instance of %s", op); return -1; }
    const int mask_id = m->blend_params->mask_id;
    // Every module riding on the same mask carries the name, so History
    // reads as one local.
    for (GList *l = g_dev.iop; l; l = g_list_next(l)) {
        dt_iop_module_t *o = (dt_iop_module_t *)l->data;
        if (o != m && !(o->multi_priority > 0 && o->blend_params && mask_id && o->blend_params->mask_id == mask_id)) continue;
        snprintf(o->multi_name, sizeof o->multi_name, "%s", name ? name : "");
        o->multi_name_hand_edited = TRUE;
        oma_add_history_item(&g_dev, o, o->enabled, TRUE);
    }
    dt_masks_form_t *grp = instance_group(m);
    if (grp) {
        snprintf(grp->name, sizeof grp->name, "grp %s", name ? name : "");
        // A rename must not switch a bypassed local back on.
        oma_add_masks_history_item(&g_dev, m, m->enabled, TRUE);
    }
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

// ── retouch spots ───────────────────────────────────────────────────────
// The retouch module keeps its shapes in the group its blend params point
// at (mask_id) - the blend itself stays off, the module reads the group -
// and a parallel rt_forms[] array with the per-shape algorithm. Both are
// kept in step here the way rt_resynch_params does it in the GUI.
#define RT_NO_FORMS 300
typedef struct rt_form_data {
    int formid;
    int scale;
    int algorithm;
    int blur_type;
    float blur_radius;
    int fill_mode;
    float fill_color[3];
    float fill_brightness;
    int distort_mode;
} rt_form_data;

static dt_iop_module_t *retouch_module(void) {
    dt_iop_module_t *m = find_instance("retouch", 0);
    return m ? m : find_module("retouch");
}
// The rt_forms array lives at the head of the params struct.
static rt_form_data *rt_forms_of(dt_iop_module_t *m) {
    if (!m || !m->params || m->params_size < (int)(sizeof(rt_form_data) * RT_NO_FORMS)) return NULL;
    return (rt_form_data *)m->params;
}
static dt_masks_form_t *retouch_group(dt_iop_module_t *m, int create) {
    if (!m->blend_params) return NULL;
    dt_masks_form_t *grp = m->blend_params->mask_id ? dt_masks_get_from_id(&g_dev, m->blend_params->mask_id) : NULL;
    if (grp && !(grp->type & DT_MASKS_GROUP)) grp = NULL;
    if (!grp && create) {
        grp = dt_masks_create(DT_MASKS_GROUP);
        snprintf(grp->name, sizeof grp->name, "grp retouch");
        g_dev.forms = g_list_append(g_dev.forms, grp);
        m->blend_params->mask_id = grp->formid;
    }
    return grp;
}
static int rt_index_of(dt_iop_module_t *m, int formid) {
    rt_form_data *f = rt_forms_of(m);
    if (!f) return -1;
    for (int i = 0; i < RT_NO_FORMS; ++i) if (f[i].formid == formid) return i;
    return -1;
}
static dt_masks_point_group_t *spot_entry(dt_iop_module_t *m, int index, dt_masks_form_t **form) {
    return group_entry(retouch_group(m, 0), index, form);
}

int oma_engine_spot_count(int imgid) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = retouch_module();
    if (!m) return 0;
    dt_masks_form_t *grp = retouch_group(m, 0);
    return grp ? (int)g_list_length(grp->points) : 0;
}

int oma_engine_spot_get(int imgid, int index, oma_spot_info *info) {
    memset(info, 0, sizeof *info);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = retouch_module();
    if (!m) { set_error("no retouch module", NULL); return -1; }
    dt_masks_form_t *form = NULL;
    dt_masks_point_group_t *pt = spot_entry(m, index, &form);
    if (!pt || !form || !form->points) return 0;
    info->found = 1;
    if(form->type & DT_MASKS_CIRCLE) {
        const dt_masks_point_circle_t *c=form->points->data;
        info->shape=1;info->cx=c->center[0];info->cy=c->center[1];info->radius=c->radius;info->border=c->border;
    } else if(form->type & DT_MASKS_BRUSH) {
        const dt_masks_point_brush_t *b=form->points->data;
        info->shape=3;info->cx=b->corner[0];info->cy=b->corner[1];info->radius=b->border[0];info->border=b->border[0]*(1-b->hardness);
    } else if(form->type & DT_MASKS_PATH) {
        const dt_masks_point_path_t *p=form->points->data;
        info->shape=4;info->cx=p->corner[0];info->cy=p->corner[1];info->radius=.02f;info->border=p->border[0];
    } else {info->found=0;return 0;}
    info->points=g_list_length(form->points);
    info->sx = form->source[0]; info->sy = form->source[1];
    info->opacity = pt->opacity;
    const int i = rt_index_of(m, form->formid);
    rt_form_data *f = rt_forms_of(m);
    if (i >= 0 && f) { info->algorithm = f[i].algorithm; info->blur_radius = f[i].blur_radius; info->fill_brightness = f[i].fill_brightness; info->scale=f[i].scale; }
    return 0;
}

static void rt_fill(rt_form_data *d, int formid, int algorithm, float blur_radius, float fill_brightness) {
    memset(d, 0, sizeof *d);
    d->formid = formid;
    d->scale = 0;
    d->algorithm = algorithm;
    d->distort_mode = 2;
    d->blur_type = 0;                                   // gaussian
    d->blur_radius = fminf(200.f, fmaxf(0.1f, blur_radius > 0.f ? blur_radius : 10.f));
    d->fill_mode = 0;                                   // erase
    d->fill_brightness = fminf(1.f, fmaxf(-1.f, fill_brightness));
}

int oma_engine_spot_add(int imgid, int algorithm, float cx, float cy, float radius, float border, float sx, float sy) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = retouch_module();
    if (!m || !m->blend_params) { set_error("no retouch module", NULL); return -1; }
    rt_form_data *forms = rt_forms_of(m);
    if (!forms) { set_error("retouch params are not the shape expected", NULL); return -1; }
    int slot = -1;
    for (int i = 0; i < RT_NO_FORMS; ++i) if (forms[i].formid == 0) { slot = i; break; }
    if (slot < 0) { set_error("no room for another spot", NULL); return -1; }
    dt_masks_form_t *grp = retouch_group(m, 1);
    if (algorithm < OMA_SPOT_CLONE || algorithm > OMA_SPOT_FILL) algorithm = OMA_SPOT_HEAL;
    const int clone = algorithm == OMA_SPOT_CLONE || algorithm == OMA_SPOT_HEAL;
    dt_masks_form_t *form = dt_masks_create(DT_MASKS_CIRCLE | (clone ? DT_MASKS_CLONE : DT_MASKS_NON_CLONE));
    fill_shape(form, 1, cx, cy, radius, border, 0.f);
    form->source[0] = sx; form->source[1] = sy; form->source[2] = 0.f;
    if (form->functions && form->functions->set_form_name) form->functions->set_form_name(form, (size_t)g_list_length(g_dev.forms) + 1);
    g_dev.forms = g_list_append(g_dev.forms, form);
    dt_masks_point_group_t *pt = calloc(1, sizeof(dt_masks_point_group_t));
    pt->formid = form->formid; pt->parentid = grp->formid; pt->state = DT_MASKS_STATE_USE; pt->opacity = 1.f;
    grp->points = g_list_append(grp->points, pt);
    // Writing history and reloading the develop context destroys this group.
    const int index = (int)g_list_length(grp->points) - 1;
    rt_fill(&forms[slot], form->formid, algorithm, 10.f, 0.f);
    m->enabled = TRUE;
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return index;
}

int oma_engine_spot_set(int imgid, int index, float cx, float cy, float radius, float border, float sx, float sy, float opacity) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = retouch_module();
    if (!m) { set_error("no retouch module", NULL); return -1; }
    dt_masks_form_t *form = NULL;
    dt_masks_point_group_t *pt = spot_entry(m, index, &form);
    if (!pt || !form) { set_error("no such spot", NULL); return -1; }
    if(form->type & DT_MASKS_CIRCLE) fill_shape(form, 1, cx, cy, radius, border, 0.f);
    else if(form->type & (DT_MASKS_BRUSH|DT_MASKS_PATH)) {
        float *first=form->points->data;const float dx=cx-first[0],dy=cy-first[1];
        for(GList *l=form->points;l;l=l->next) {
            // Both mask structures start with corner, ctrl1, ctrl2.
            float *p=l->data;for(int k=0;k<6;k+=2){p[k]+=dx;p[k+1]+=dy;}
            if(form->type & DT_MASKS_BRUSH) {dt_masks_point_brush_t *b=l->data;b->border[0]=b->border[1]=fmaxf(.0005f,radius);b->hardness=fmaxf(.05f,1-border/fmaxf(radius,.0005f));}
            else {dt_masks_point_path_t *p=l->data;p->border[0]=p->border[1]=fmaxf(0,border);}
        }
    }
    form->source[0] = sx; form->source[1] = sy;
    pt->opacity = fminf(1.f, fmaxf(0.f, opacity));
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_spot_set_algorithm(int imgid, int index, int algorithm, float blur_radius, float fill_brightness) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = retouch_module();
    if (!m) { set_error("no retouch module", NULL); return -1; }
    dt_masks_form_t *form = NULL;
    if (!spot_entry(m, index, &form) || !form) { set_error("no such spot", NULL); return -1; }
    rt_form_data *forms = rt_forms_of(m);
    const int i = rt_index_of(m, form->formid);
    if (!forms || i < 0) { set_error("spot has no retouch entry", NULL); return -1; }
    if (algorithm < OMA_SPOT_CLONE || algorithm > OMA_SPOT_FILL) algorithm = OMA_SPOT_HEAL;
    const int scale=forms[i].scale;
    rt_fill(&forms[i], form->formid, algorithm, blur_radius, fill_brightness);
    forms[i].scale=scale;
    // The form's type says whether it has a source.
    const int clone = algorithm == OMA_SPOT_CLONE || algorithm == OMA_SPOT_HEAL;
    form->type = (form->type & ~(DT_MASKS_CLONE|DT_MASKS_NON_CLONE)) | (clone ? DT_MASKS_CLONE : DT_MASKS_NON_CLONE);
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_spot_remove(int imgid, int index) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = retouch_module();
    if (!m) { set_error("no retouch module", NULL); return -1; }
    dt_masks_form_t *grp = retouch_group(m, 0);
    dt_masks_form_t *form = NULL;
    dt_masks_point_group_t *pt = group_entry(grp, index, &form);
    if (!pt || !grp) { set_error("no such spot", NULL); return -1; }
    rt_form_data *forms = rt_forms_of(m);
    const int i = form ? rt_index_of(m, form->formid) : -1;
    if (forms && i >= 0) {
        // Compact the array the way the GUI does, so no hole is left.
        memmove(&forms[i], &forms[i + 1], sizeof(rt_form_data) * (RT_NO_FORMS - 1 - i));
        memset(&forms[RT_NO_FORMS - 1], 0, sizeof(rt_form_data));
    }
    grp->points = g_list_remove(grp->points, pt);
    free(pt);
    if (form) { g_dev.forms = g_list_remove(g_dev.forms, form); dt_masks_free_form(form); }
    if (!grp->points) m->enabled = FALSE;          // nothing left to do
    oma_add_masks_history_item(&g_dev, m, m->enabled, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

// ── parametric range masks ──────────────────────────────────────────────
// blendif packs one channel per bit, the same bit + 16 meaning "inverted",
// and four trapezoid nodes per channel in blendif_parameters. Which index
// holds luminance, hue and chroma depends on the module's blend
// colourspace, so ask the module rather than assuming.
static int range_channel(dt_iop_module_t *m, int channel) {
    const int cst = m->blend_params ? m->blend_params->blend_cst : DEVELOP_BLEND_CS_NONE;
    switch (channel) {
    case OMA_RANGE_LUMA:
        // L in Lab, gray in both RGB pipelines: index 0 either way.
        return cst == DEVELOP_BLEND_CS_NONE ? -1 : 0;
    case OMA_RANGE_HUE:
        if (cst == DEVELOP_BLEND_CS_LAB) return DEVELOP_BLENDIF_h_in;
        if (cst == DEVELOP_BLEND_CS_RGB_DISPLAY) return DEVELOP_BLENDIF_H_in;
        if (cst == DEVELOP_BLEND_CS_RGB_SCENE) return DEVELOP_BLENDIF_hz_in;
        return -1;
    case OMA_RANGE_CHROMA:
        if (cst == DEVELOP_BLEND_CS_LAB) return DEVELOP_BLENDIF_C_in;
        if (cst == DEVELOP_BLEND_CS_RGB_DISPLAY) return DEVELOP_BLENDIF_S_in;
        if (cst == DEVELOP_BLEND_CS_RGB_SCENE) return DEVELOP_BLENDIF_Cz_in;
        return -1;
    default: return -1;
    }
}

// The scene-referred luminance channel is LINEAR, so a slider straight
// onto it spends its first half on the deepest shadows: 0.22 linear is
// already mid-grey on screen. The UI works in display terms and the
// bridge applies the gamma, so "half way" means half way to a reader.
static int range_is_linear(dt_iop_module_t *m, int channel) {
    return channel == OMA_RANGE_LUMA && m->blend_params
           && m->blend_params->blend_cst == DEVELOP_BLEND_CS_RGB_SCENE;
}
static float range_to_engine(float v) { return powf(fmaxf(0.f, fminf(1.f, v)), 2.2f); }
static float range_to_ui(float v) { return powf(fmaxf(0.f, fminf(1.f, v)), 1.f / 2.2f); }

// One channel of one module's blendif, in UI terms, no history.
static int range_read(dt_iop_module_t *m, int channel, oma_range_info *info) {
    memset(info, 0, sizeof *info);
    const int ch = range_channel(m, channel);
    if (ch < 0) return -1;
    const dt_develop_blend_params_t *bp = m->blend_params;
    info->active = (bp->mask_mode & DEVELOP_MASK_CONDITIONAL) && (bp->blendif & (1u << ch)) ? 1 : 0;
    info->inverse = (bp->blendif & (1u << (ch + 16))) ? 1 : 0;
    const int lin = range_is_linear(m, channel);
    info->p0 = lin ? range_to_ui(bp->blendif_parameters[ch * 4 + 0]) : bp->blendif_parameters[ch * 4 + 0];
    info->p1 = lin ? range_to_ui(bp->blendif_parameters[ch * 4 + 1]) : bp->blendif_parameters[ch * 4 + 1];
    info->p2 = lin ? range_to_ui(bp->blendif_parameters[ch * 4 + 2]) : bp->blendif_parameters[ch * 4 + 2];
    info->p3 = lin ? range_to_ui(bp->blendif_parameters[ch * 4 + 3]) : bp->blendif_parameters[ch * 4 + 3];
    return 0;
}
static int range_write(dt_iop_module_t *m, int channel, int active, int inverse, float p0, float p1, float p2, float p3) {
    const int ch = range_channel(m, channel);
    if (ch < 0) return -1;
    dt_develop_blend_params_t *bp = m->blend_params;
    // Nodes must climb, or the trapezoid folds in on itself.
    if (p1 < p0) p1 = p0;
    if (p2 < p1) p2 = p1;
    if (p3 < p2) p3 = p2;
    const int lin = range_is_linear(m, channel);
    bp->blendif_parameters[ch * 4 + 0] = lin ? range_to_engine(p0) : p0;
    bp->blendif_parameters[ch * 4 + 1] = lin ? range_to_engine(p1) : p1;
    bp->blendif_parameters[ch * 4 + 2] = lin ? range_to_engine(p2) : p2;
    bp->blendif_parameters[ch * 4 + 3] = lin ? range_to_engine(p3) : p3;
    if (active) bp->blendif |= (1u << ch); else bp->blendif &= ~(1u << ch);
    if (inverse) bp->blendif |= (1u << (ch + 16)); else bp->blendif &= ~(1u << (ch + 16));
    // Any active channel switches the parametric half of the mask on; none
    // switches it off again, so a cleared range does not dim the module.
    const uint32_t any = bp->blendif & 0x0000ffffu;
    if (any) bp->mask_mode |= DEVELOP_MASK_ENABLED | DEVELOP_MASK_CONDITIONAL;
    else bp->mask_mode &= ~DEVELOP_MASK_CONDITIONAL;
    return 0;
}
// A member's blend follows the anchor: same group, same ranges (mapped
// into the member's own colourspace), same polarity.
static void stack_follow(dt_iop_module_t *anchor, dt_iop_module_t *member) {
    member->blend_params->mask_id = anchor->blend_params->mask_id;
    member->blend_params->mask_mode = DEVELOP_MASK_ENABLED
        | (anchor->blend_params->mask_mode & (DEVELOP_MASK_MASK | DEVELOP_MASK_CONDITIONAL));
    member->blend_params->mask_combine = anchor->blend_params->mask_combine;
    member->blend_params->blur_radius = anchor->blend_params->blur_radius;
    member->blend_params->feathering_radius = anchor->blend_params->feathering_radius;
    member->blend_params->feathering_guide = anchor->blend_params->feathering_guide;
    member->blend_params->feather_version = anchor->blend_params->feather_version;
    member->blend_params->contrast = anchor->blend_params->contrast;
    member->blend_params->brightness = anchor->blend_params->brightness;
    for (int ch = 0; ch < 3; ++ch) {
        oma_range_info ri;
        if (range_read(anchor, ch, &ri) == 0) range_write(member, ch, ri.active, ri.inverse, ri.p0, ri.p1, ri.p2, ri.p3);
    }
}
static void stack_follow_all(dt_iop_module_t *anchor) {
    dt_iop_module_t *members[OMA_STACK_MAX];
    const int n = stack_members(anchor, members, OMA_STACK_MAX);
    for (int i = 0; i < n; ++i) {
        stack_follow(anchor, members[i]);
        oma_add_masks_history_item(&g_dev, members[i], members[i]->enabled, TRUE);
    }
}

int oma_engine_range_get(int imgid, const char *op, int priority, int channel, oma_range_info *info) {
    memset(info, 0, sizeof *info);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m || !m->blend_params) { set_error("no such instance of %s", op); return -1; }
    range_read(m, channel, info);
    return 0;
}

int oma_engine_range_set(int imgid, const char *op, int priority, int channel, int active, int inverse,
                         float p0, float p1, float p2, float p3) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m || !m->blend_params) { set_error("no such instance of %s", op); return -1; }
    if (range_write(m, channel, active, inverse, p0, p1, p2, p3) != 0) { set_error("%s cannot mask on that channel", op); return -1; }
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    stack_follow_all(m);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_mask_get_invert(int imgid, const char *op, int priority) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m || !m->blend_params) { set_error("no such instance of %s", op); return -1; }
    return (m->blend_params->mask_combine & DEVELOP_COMBINE_INV) ? 1 : 0;
}

int oma_engine_mask_set_invert(int imgid, const char *op, int priority, int invert) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m || !m->blend_params) { set_error("no such instance of %s", op); return -1; }
    dt_develop_blend_params_t *bp = m->blend_params;
    // NORM_EXCL and INV_INCL are darktable's own inclusive/exclusive pair:
    // flipping the mask flips how the drawn and parametric halves meet too.
    bp->mask_combine = invert ? DEVELOP_COMBINE_INV_INCL : DEVELOP_COMBINE_NORM_EXCL;
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    stack_follow_all(m);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_refine_get(int imgid, const char *op, int priority, oma_refine_info *info) {
    memset(info, 0, sizeof *info);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m || !m->blend_params) { set_error("no such instance of %s", op); return -1; }
    const dt_develop_blend_params_t *bp = m->blend_params;
    info->blur = bp->blur_radius;
    info->feather = bp->feathering_radius;
    info->guide = (bp->feathering_guide == DEVELOP_MASK_GUIDE_OUT_BEFORE_BLUR || bp->feathering_guide == DEVELOP_MASK_GUIDE_OUT_AFTER_BLUR)
                  ? OMA_GUIDE_OUTPUT : OMA_GUIDE_INPUT;
    info->contrast = bp->contrast;
    info->brightness = bp->brightness;
    return 0;
}

int oma_engine_refine_set(int imgid, const char *op, int priority, const oma_refine_info *info) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m || !m->blend_params) { set_error("no such instance of %s", op); return -1; }
    dt_develop_blend_params_t *bp = m->blend_params;
    bp->blur_radius = fminf(100.f, fmaxf(0.f, info->blur));
    bp->feathering_radius = fminf(250.f, fmaxf(0.f, info->feather));
    // Feathering before the blur, the way darktable's GUI defaults it.
    bp->feathering_guide = info->guide == OMA_GUIDE_OUTPUT ? DEVELOP_MASK_GUIDE_OUT_BEFORE_BLUR : DEVELOP_MASK_GUIDE_IN_BEFORE_BLUR;
    bp->feather_version = 1;
    bp->contrast = fminf(1.f, fmaxf(-1.f, info->contrast));
    bp->brightness = fminf(1.f, fmaxf(-1.f, info->brightness));
    bp->mask_mode |= DEVELOP_MASK_ENABLED;
    oma_add_masks_history_item(&g_dev, m, TRUE, TRUE);
    stack_follow_all(m);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_image_size(int imgid, int *width, int *height) {
    if (width) *width = 0;
    if (height) *height = 0;
    if (load(imgid)) return -1;
    const dt_image_t *img = dt_image_cache_get(imgid, 'r');
    if (!img) { set_error("no such image", NULL); return -1; }
    const int w = img->final_width > 0 ? img->final_width : img->width;
    const int h = img->final_height > 0 ? img->final_height : img->height;
    dt_image_cache_read_release(img);
    if (width) *width = w;
    if (height) *height = h;
    return 0;
}

int oma_engine_is_scene_referred(int imgid) {
    if (load(imgid)) return -1;
    // Match the engine's automatic scene-referred preset selection. Linear
    // camera files (e.g. demosaiced DxO DNGs) carry S_RAW rather than RAW and
    // need the same exposure/tone treatment even though demosaic is off.
    return dt_image_is_rawprepare_supported(&g_dev.image_storage) ? 1 : 0;
}

int oma_engine_raw_calibration(int imgid, float matrix[9], float *white, float *highlight_bias, int geometry[6]) {
    if (load(imgid)) return -1;
    dt_iop_module_t *raw = find_module("rawprepare");
    const uint16_t *level = raw && raw->so->get_p ? raw->so->get_p(raw->params, "raw_white_point") : NULL;
    if (!level || *level <= 1) return -1;
    *white = *level;
    const dt_image_t *img = dt_image_cache_get(imgid, 'r');
    if (!img) return -1;
    geometry[0] = img->width; geometry[1] = img->height;
    const char *crop_fields[] = {"left", "top", "right", "bottom"};
    for(int i = 0; i < 4; ++i) {
        const int32_t *value = raw->so->get_p(raw->params, crop_fields[i]);
        geometry[i + 2] = value ? *value : 0;
    }
    *highlight_bias = isfinite(img->exif_highlight_preservation) ? CLAMP(img->exif_highlight_preservation, 0.f, 4.f) : 0.f;
    float energy = 0.f;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) {
        const float value = img->adobe_XYZ_to_CAM[r][c];
        matrix[r * 3 + c] = value; energy += fabsf(value);
    }
    dt_image_cache_read_release(img);
    return isfinite(energy) && energy > 0.f ? 0 : -1;
}

// ── the local's adjustment stack ────────────────────────────────────────
static dt_iop_module_t *stack_member(dt_iop_module_t *anchor, const char *op) {
    dt_iop_module_t *members[OMA_STACK_MAX];
    const int n = stack_members(anchor, members, OMA_STACK_MAX);
    for (int i = 0; i < n; ++i) if (!strcmp(members[i]->op, op)) return members[i];
    return NULL;
}
// Writes one introspected field; no history.
static int set_field(dt_iop_module_t *m, const char *field, float value) {
    if (!m->so->get_f || !m->so->get_p) { set_error("%s has no introspection", m->op); return -1; }
    dt_introspection_field_t *f = m->so->get_f(field);
    void *p = f ? m->so->get_p(m->params, field) : NULL;
    if (!p) { set_error("no field %s", field); return -1; }
    return store_field(f, p, field, value);
}

int oma_engine_stack_param_get(int imgid, const char *anchor_op, int priority, const char *op, const char *field, oma_param_info *info) {
    memset(info, 0, sizeof *info);
    if (load(imgid)) return -1;
    dt_iop_module_t *anchor = find_instance(anchor_op, priority);
    if (!anchor) { set_error("no such instance of %s", anchor_op); return -1; }
    dt_iop_module_t *member = stack_member(anchor, op);
    if (!member) return 0;
    // Two independent, zero-centred colour-cast controls represented by
    // the native midtone wheel. Keep the native parameters as the sole
    // saved state, so history, copies and exports all agree.
    if (!strcmp(op, "colorbalancergb") && (!strcmp(field, "omaraw_warmth") || !strcmp(field, "omaraw_tint"))) {
        const int rc = oma_engine_param_get_instance(imgid, op, member->multi_priority, "midtones_C", info);
        float *h = float_field(member, "midtones_H"), *c = float_field(member, "midtones_C");
        if (rc || !h || !c) return -1;
        const double angle = (*h - 50.f) * M_PI / 180.0;
        info->value = *c * 1000.f * (!strcmp(field, "omaraw_warmth") ? cos(angle) : -sin(angle));
        info->min = -100; info->max = 100; info->def = 0;
        return 0;
    }
    const int result = oma_engine_param_get_instance(imgid, op, member->multi_priority, field, info);
    if (!strcmp(op, "lowpass") && !strcmp(field, "radius") && member->blend_params) {
        info->min = 0; info->max = 100; info->def = 0;
        if (member->blend_params->opacity == 0.f) info->value = 0;
    }
    return result;
}

int oma_engine_stack_param_set(int imgid, const char *anchor_op, int priority, const char *op, const char *field, float value) {
    if (!isfinite(value)) { set_error("invalid local adjustment", NULL); return -1; }
    if (load(imgid)) return -1;
    dt_iop_module_t *anchor = find_instance(anchor_op, priority);
    if (!anchor || !anchor->blend_params || !anchor->blend_params->mask_id) { set_error("no such local of %s", anchor_op); return -1; }
    dt_iop_module_t *member = stack_member(anchor, op);
    const gboolean created = !member;
    if (!member) {
        dt_iop_module_t *base = find_instance(op, 0);
        if (!base) base = find_module(op);
        if (!base) { set_error("no module %s", op); return -1; }
        if (stack_members(anchor, (dt_iop_module_t *[OMA_STACK_MAX]){0}, OMA_STACK_MAX) >= OMA_STACK_MAX) { set_error("the stack is full", NULL); return -1; }
        member = dt_dev_module_duplicate_ext(&g_dev, base, TRUE);
        if (!member) { set_error("could not add an instance of %s", op); return -1; }
        adopt_defaults(member, base);
        snprintf(member->multi_name, sizeof member->multi_name, "%s", anchor->multi_name);
        member->multi_name_hand_edited = TRUE;
        member->enabled = anchor->enabled;
        stack_follow(anchor, member);
    }
    // A local lowpass is the colour moiré tool. Blur only Lab chroma;
    // the blend preserves luminance detail. Zero uses opacity so toggling
    // the whole local back on cannot reintroduce a hidden blur.
    if (!strcmp(op, "lowpass") && !strcmp(field, "radius")) {
        value = fminf(100.f, fmaxf(0.f, value));
        member->blend_params->blend_cst = DEVELOP_BLEND_CS_LAB;
        member->blend_params->blend_mode = DEVELOP_BLEND_LAB_COLOR;
        member->blend_params->opacity = value > 0.f ? 100.f : 0.f;
        value = fmaxf(0.1f, value);
        set_field(member, "lowpass_algo", 0);
        set_field(member, "order", 0);
        set_field(member, "contrast", 1);
        set_field(member, "brightness", 0);
        set_field(member, "saturation", 1);
        set_field(member, "unbound", 1);
    }
    if (!strcmp(op, "colorbalancergb") && (!strcmp(field, "omaraw_warmth") || !strcmp(field, "omaraw_tint"))) {
        float *h = float_field(member, "midtones_H"), *c = float_field(member, "midtones_C");
        if (!h || !c) { set_error("local colour controls unavailable", NULL); return -1; }
        const double angle = (*h - 50.f) * M_PI / 180.0;
        double warmth = *c * cos(angle), tint = -*c * sin(angle);
        if (!strcmp(field, "omaraw_warmth")) warmth = CLAMP(value, -100.f, 100.f) / 1000.;
        else tint = CLAMP(value, -100.f, 100.f) / 1000.;
        *c = hypot(warmth, tint);
        *h = *c < 1e-8f ? 0.f : fmod(atan2(-tint, warmth) * 180. / M_PI + 410., 360.);
    } else if (set_field(member, field, value) != 0) return -1;
    if (created) oma_add_masks_history_item(&g_dev, member, member->enabled, TRUE);
    else oma_add_history_item(&g_dev, member, member->enabled, TRUE);
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_local_set_enabled(int imgid, const char *op, int priority, int enabled) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = find_instance(op, priority);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    dt_iop_module_t *mods[OMA_STACK_MAX + 1]; mods[0] = m;
    const int n = 1 + stack_members(m, mods + 1, OMA_STACK_MAX);
    for (int i = 0; i < n; ++i) {
        mods[i]->enabled = enabled ? TRUE : FALSE;
        oma_add_history_item(&g_dev, mods[i], enabled ? TRUE : FALSE, TRUE);
    }
    dt_dev_write_history(&g_dev);
    reload();
    return 0;
}

int oma_engine_param_get_instance(int imgid, const char *op, int priority, const char *field, oma_param_info *info) {
    memset(info, 0, sizeof *info);
    if (load(imgid)) return -1;
    dt_iop_module_t *m = priority ? find_instance(op, priority) : find_module(op);
    if (!m || !m->so->get_f || !m->so->get_p) return 0;
    dt_introspection_field_t *f = m->so->get_f(field);
    void *p = f ? m->so->get_p(m->params, field) : NULL;
    if (!p) return 0;
    info->found = 1; info->enabled = m->enabled; info->type = field_type(f);
    switch (info->type) {
    case 0: info->value = *(float *)p; info->min = f->Float.Min; info->max = f->Float.Max; info->def = f->Float.Default; break;
    case 1: info->value = (float)*(int *)p; info->min = (float)f->Int.Min; info->max = (float)f->Int.Max; info->def = (float)f->Int.Default; break;
    case 2: info->value = *(gboolean *)p ? 1.f : 0.f; info->max = 1; break;
    case 3: info->value = (float)*(int *)p; break;
    default: break;
    }
    return 0;
}

int oma_engine_param_set_instance(int imgid, const char *op, int priority, const char *field, float value) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = priority ? find_instance(op, priority) : find_module(op);
    if (!m || !m->so->get_f || !m->so->get_p) { set_error("no such instance of %s", op); return -1; }
    dt_introspection_field_t *f = m->so->get_f(field);
    void *p = f ? m->so->get_p(m->params, field) : NULL;
    if (!p) { set_error("no field %s", field); return -1; }
    if (store_field(f, p, field, value)) return -1;
    oma_add_history_item(&g_dev, m, TRUE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

int oma_engine_module_set_enabled_instance(int imgid, const char *op, int priority, int enabled) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m = priority ? find_instance(op, priority) : find_module(op);
    if (!m) { set_error("no such instance of %s", op); return -1; }
    m->enabled = enabled ? TRUE : FALSE;
    oma_add_history_item(&g_dev, m, enabled ? TRUE : FALSE, TRUE);
    dt_dev_write_history(&g_dev);
    return 0;
}

#include "developtools.inc"

#include "repair.inc"
