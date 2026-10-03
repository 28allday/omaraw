// OmaRAW ↔ darktable engine bridge. A small C API over libdarktable, driven
// headlessly the way darktable-cli drives it: no GTK, one develop context,
// history in darktable's own library database, renders through the same
// pixelpipe as export. Every call must come from ONE thread (the engine
// worker); the engine keeps its own worker pool underneath. Only
// oma_engine_cancel_preview may be called from another thread.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// prefix: darktable install prefix (…/_install or /usr). configdir/cachedir:
// OmaRAW-owned directories. library: path of the engine's library.db.
// Returns 0 on success.
int oma_engine_init(const char *prefix, const char *configdir, const char *cachedir, const char *library);
void oma_engine_cleanup(void);
const char *oma_engine_version(void);

// Import (or look up) a file; returns the engine image id of the master
// (version 0), or -1.
int oma_engine_open(const char *path);
// Unedited, reduced source payload for the managed Smart Preview store.
int oma_engine_smart_data(int imgid, void **out, size_t *size, int *width, int *height);
int oma_engine_using_smart_preview(int imgid);
int oma_engine_repair_source(int imgid, const char *path, const char *mask_path, const uint8_t *mask, int width, int height, int stride);
int oma_engine_repair_check(int imgid);
// Bounded sensor-clipping mask in developed coordinates. 2 means the source
// is not a supported 16-bit Bayer/X-Trans RAW; never modifies editing history.
int oma_engine_raw_clipping(int imgid, int uncropped, int width, int height, uint8_t **out);
// Liquify warps: eight numbers per point (type, centre xy, strength xy,
// radius xy, feather), normalised to the original sensor dimensions.
int oma_engine_liquify_get(int imgid, float *warps, int capacity, int *enabled);
int oma_engine_liquify_set(int imgid, const float *warps, int count, int enabled);
int oma_engine_liquify_add(int imgid, int type, float x, float y, float dx, float dy, float radius, float strength);
// Connect an imported raster mask to one base adjustment; an empty path
// disconnects that adjustment. Existing values and other users are preserved.
int oma_engine_raster_set(int imgid, const char *path, const char *target, int inverse, float opacity);
int oma_engine_raster_get(int imgid, const char *target, int *inverse, float *opacity);
int oma_engine_levels_set(int imgid, int channel, float black, float middle, float white);
// Start a separate preset/snapshot action so it cannot replace the previous
// action's last history row when both begin/end on the same module.
int oma_engine_begin_edit(int imgid);
// Image Match owns one native history item. The signature includes source
// identity and history, so an old preview cannot overwrite a later edit.
char *oma_engine_match_signature(int imgid);
int oma_engine_match_set(int imgid, const float *values, int count, int enabled);
// The engine image of `path` at `version` (0 = master). Versions are made
// by oma_engine_duplicate; an unknown version returns -1.
int oma_engine_open_version(const char *path, int version);
// The id of a version the engine already knows, or -1 (nothing imported).
int oma_engine_known_version(const char *path, int version);
// The edit in darktable's sidecar form (an XMP packet; free with oma_engine_free),
// and applying one from an XMP file (its darktable section replaces the history).
char *oma_engine_edit_xmp(int imgid);
int oma_engine_apply_edit_xmp(int imgid, const char *xmp_path);
// A new version of `imgid` with a copy of its full history (a variant /
// virtual copy). Returns the new engine image id; its version number
// lands in *version. -1 on failure.
int oma_engine_duplicate(int imgid, int *version);
// Disposable saved-settings comparison. Copies history into a private,
// journalled row without changing source history or variant numbering.
// Remove when comparison closes; abandoned rows are reclaimed at startup.
int oma_engine_comparison_create(int imgid);
void oma_engine_comparison_remove(int imgid);
// The version number of an engine image (0 = master), or -1.
int oma_engine_image_version(int imgid);
// Exchange the version numbers of two images of the same file (promote a
// variant to master). 0 on ok.
int oma_engine_swap_versions(int a, int b);
// Remove an image and its history from the engine (a deleted variant).
int oma_engine_remove(int imgid);
// A folder moved: point its film roll (and those beneath) at the new
// path so every image keeps its history. 0 on ok.
int oma_engine_relink_folder(const char *old_dir, const char *new_dir);
// One file moved or renamed: every version of it follows to the new film
// roll and name, histories intact. A file the engine never saw is no error.
int oma_engine_move_image(const char *old_path, const char *new_path);

// Render current history to straight float linear Rec.709 D65, including
// the edit's tone mapper but before display encoding/quantisation. No larger than
// max_w × max_h. The outward colourants match Qt's SRgbLinear ICC exactly;
// native pixelpipe colourants are reconciled once at the buffer boundary.
// On success *out (free with oma_engine_free) holds
// (*w) × (*h) × 4 floats with opaque alpha. Returns 0 on success.
int oma_engine_render(int imgid, int max_w, int max_h, float **out, int *w, int *h);
// Current overview with reusable module nodes and unchanged CPU stages.
// Falls back to the ordinary renderer if a cached render fails.
int oma_engine_render_cached(int imgid, int max_w, int max_h, float **out, int *w, int *h);
// Separate pixelpipe and completed-overview accounting; excludes source
// decode and module storage. Overview storage follows its budget/16 photos.
typedef struct oma_preview_stats {
    uint64_t rebuilds, resyncs, fallbacks, retained_bytes, peak_bytes;
    uint64_t overview_hits, overview_misses, overview_bytes, overview_entries;
    // Fitted overviews served from the engine's reduced mosaic.
    uint64_t reduced_renders;
    // Pixelpipe region calls (including overviews) and upstream cache hits.
    uint64_t region_renders, region_cache_hits, largest_context_bytes;
    // Tiles cut from an area already rendered with a feathering margin.
    uint64_t region_area_hits;
    uint64_t region_area_bytes;
} oma_preview_stats;
void oma_engine_preview_stats(oma_preview_stats *stats);
// History step 0 in a private preview context; no history writes.
int oma_engine_render_original(int imgid, int max_w, int max_h, float **out, int *w, int *h);
// A rectangle in the developed image, at scale (0 < scale <= 1).
// Coordinates are in scaled output pixels. Same float colour contract as overview.
int oma_engine_render_region(int imgid, int x, int y, int width, int height, double scale,
                             float **out, int *w, int *h);
// The block of tiles the caller is about to ask for one by one, in the next
// region call's scaled pixels; that call consumes it. While a mask is
// feathered or blurred the block is rendered once, with the margin those
// need around it, and the tiles are cut from it so they meet without an edge.
void oma_engine_region_hint(int x, int y, int width, int height);
int oma_engine_render_region_mode(int imgid, int original, int uncropped,
                                  int x, int y, int width, int height, double scale,
                                  float **out, int *w, int *h);
// `original` mode: 0 current, 1 history zero, 2 saved comparison history.
// Modes 1 and 2 share the second preview context and are mutually exclusive.
int oma_engine_preview_dimensions(int imgid, int original, int uncropped, int *width, int *height);
// Scope one worker preview request. The predicate must be safe to call from
// any thread and stay valid until end; once cancelled it must remain so.
// Requests outside this scope (including exports) cannot be interrupted.
// Scoped preview renders return OMA_ENGINE_CANCELLED without reporting an
// error or falling back to another renderer. Always pair begin/end.
#define OMA_ENGINE_CANCELLED 1
void oma_engine_preview_begin(int (*cancelled)(void *), void *request);
void oma_engine_preview_end(void);
// Wake the active pixelpipe only if its own request has become obsolete.
// A detail generation change therefore cannot stop the current overview.
void oma_engine_cancel_preview(void);
void oma_engine_free(void *p);

// Export through a real format module (JPEG) to `path`. quality 1–100.
int oma_engine_export_jpeg(int imgid, const char *path, int max_w, int max_h, int quality);
// Export through any format module: "jpeg", "tiff", "png", "webp", "avif",
// "jpegxl". `path` is the stem; the storage appends the format's extension.
// quality 1–100 where the format has one; bpp 8 or 16 where it has one.
// Returns 0 on success; the written path lands in out_path (PATH_MAX).
int oma_engine_export(int imgid, const char *path, const char *format, int max_w, int max_h, int quality, int bpp,
                      char *out_path, int out_len);
// Same, with darktable's export-metadata flags (OMA_META_*); -1 = defaults.
#define OMA_META_EXIF     (1 << 0)
#define OMA_META_METADATA (1 << 1)
#define OMA_META_GEOTAG   (1 << 2)
#define OMA_META_TAG      (1 << 3)
#define OMA_META_HISTORY  (1 << 5)
int oma_engine_export_meta(int imgid, const char *path, const char *format, int max_w, int max_h, int quality, int bpp,
                           int meta_flags, char *out_path, int out_len);
// Output profile: 0 sRGB, 1 RGB (1998), 2 linear ProPhoto RGB, 3 supplied RGB ICC.
// OCIO converts float pixels before the format writer, which embeds the ICC.
// intent: perceptual, relative, saturation, absolute (0..3).
int oma_engine_export_profile(int imgid, const char *path, const char *format, int max_w, int max_h, int quality, int bpp,
                              int meta_flags, int profile, const void *icc, int icc_size, int intent, char *out_path, int out_len);
/* Output finishing on the float pixels of an export, once, before the
   engine quantises them for the writer. The hook receives straight RGBA
   float32 in the canonical linear Rec.709 interchange (width*height*4), the writer's
   bit depth, and `data`. NULL clears it. Set around one export at a time. */
typedef void (*oma_export_hook_fn)(float *rgba, int width, int height, int bpp, void *data);
void oma_engine_set_export_hook(oma_export_hook_fn hook, void *data);
int oma_engine_export_described(int imgid, const char *path, const char *format, int max_w, int max_h, int quality, int bpp,
                               int meta_flags, int profile, const void *icc, int icc_size, int intent,
                               const char *description_json, char *out_path, int out_len);
// Whether the next exports replace an existing file of the same name (1) or
// write beside it as "name_01.ext" (0, the default). out_path says which.
void oma_engine_set_export_overwrite(int overwrite);
int oma_engine_format_available(const char *format);
const char *oma_engine_format_extension(const char *format);

// Parameters, addressed by module op name and introspected field name.
typedef struct oma_param_info {
    int found;        // module + field exist
    int type;         // 0 float, 1 int, 2 bool, 3 enum, 4 other
    int enabled;      // module is in the pipe
    float value, min, max, def;
} oma_param_info;
int oma_engine_param_get(int imgid, const char *op, const char *field, oma_param_info *info);
// Sets the field and records a history item (enabling the module). 0 on ok.
int oma_engine_param_set(int imgid, const char *op, const char *field, float value);
// Enum fields (type 3): the choices, in declaration order. `option` returns
// the human description from the module's comments (its identifier when
// there is none) and fills `value` with the enum's integer.
// Newline-separated numeric fields of sigmoid, filmicrgb or basecurve.
int oma_engine_tone_fields(int imgid, const char *op, char *out, int length);
int oma_engine_param_enum_count(int imgid, const char *op, const char *field);
const char *oma_engine_param_enum_option(int imgid, const char *op, const char *field, int index, int *value);
int oma_engine_module_set_enabled(int imgid, const char *op, int enabled);
int oma_engine_module_enabled(int imgid, const char *op);
// Text fields (char arrays), such as lut3d's filepath. Absolute paths are
// fine: the engine's LUT folder is "/". get returns 0 on ok and fills
// `out`; set records a history item (enabling the module).
int oma_engine_param_get_string(int imgid, const char *op, const char *field, char *out, int len);
int oma_engine_param_set_string(int imgid, const char *op, const char *field, const char *value);
// Decode a bundled camera style into typed numeric rows without changing the
// image. Only the tone mappers and colorbalancergb are allowed. Caller frees.
char *oma_engine_camera_style_fields(int imgid, const char *op, int version, const char *encoded);
// Current fields for those same modules, including hidden tone parameters.
int oma_engine_camera_look_fields(int imgid, const char *op, char *out, int length);
// Tone curves over the rgbcurve module. Up to 20 nodes per channel, x and
// y in 0..1 (mid grey sits at 0.5: the module's middle-grey compensation is
// switched on the first time a curve is written). Channel 0 is the RGB
// curve while the channels are linked, else red; 1 green; 2 blue. Types:
// 0 cubic spline, 1 Catmull-Rom, 2 monotone Hermite (the default).
#define OMA_CURVE_MAX 20
typedef struct oma_curve_info {
    int found, enabled, linked, count, type;
    float x[OMA_CURVE_MAX], y[OMA_CURVE_MAX];
} oma_curve_info;
int oma_engine_curve_get(int imgid, int channel, oma_curve_info *info);
// Replaces the channel's nodes (sorted by x, 2..20 of them) and records a
// history item, enabling the module. 0 on ok.
int oma_engine_curve_set(int imgid, int channel, const float *x, const float *y, int count, int type);
// Linked = one curve for all three channels. Going independent copies the
// shared curve into all three so the picture does not change.
int oma_engine_curve_set_linked(int imgid, int linked);
// Samples the channel's curve at `res` evenly spaced x in [0,1] with the
// module's own spline, y clamped to 0..1. 0 on ok.
int oma_engine_curve_sample(int imgid, int channel, float *y, int res);
// Colour zones (colorzones, selecting by Lab hue): three curves over hue,
// channel 0 lightness (y 0.5 neutral, ±0.25 = ±1 EV), 1 chroma (y 0.5
// neutral, 0 grey, 1 double), 2 hue (y − 0.5 turns of shift). Up to 20
// nodes each, x = hue in turns. The panel writes eight named bands.
typedef struct oma_zones_info {
    int found, enabled;
    int count[3], type[3];
    float x[3][OMA_CURVE_MAX], y[3][OMA_CURVE_MAX];
} oma_zones_info;
int oma_engine_zones_get(int imgid, oma_zones_info *info);
// Replaces one channel's nodes (sorted by x, 2..20), pins the module to
// select-by-hue / smooth / v2 splines / Catmull-Rom, records history. 0 on ok.
int oma_engine_zones_set(int imgid, int channel, const float *x, const float *y, int count);
// The parametric tone curve rides on the tonecurve module's L curve
// (display-referred, after the tone mapper): six nodes, the four inner
// ones at 0.125 / 0.375 / 0.625 / 0.875 lifted or lowered by the region
// sliders. get/set share oma_curve_info; set records one history item.
int oma_engine_lcurve_get(int imgid, oma_curve_info *info);
int oma_engine_lcurve_set(int imgid, const float *x, const float *y, int count);
// The same module's two colour curves, Lab's a (channel 1: green at x = 0,
// magenta at 1) and b (channel 2: blue to yellow). x and y are (a + 128) / 256,
// so 0.5 is neutral and a straight diagonal changes nothing. The engine only
// applies them in its "Lab, independent channels" mode, which also makes the L
// curve act on lightness alone; set() selects that mode while either curve
// departs from the diagonal and the module's default (RGB, linked) once both
// are straight again. info->linked reports the latter. One history item.
int oma_engine_labcurve_get(int imgid, int channel, oma_curve_info *info);
int oma_engine_labcurve_set(int imgid, int channel, const float *x, const float *y, int count, int type);
int oma_engine_labcurve_sample(int imgid, int channel, float *y, int res);
// Both colour curves in one history item (the Lab colour sliders move both).
int oma_engine_labcurves_set(int imgid, const float *xa, const float *ya, int na, const float *xb, const float *yb, int nb, int type);
// Crop, over the crop module: left/top/right/bottom as fractions of the
// module's input (after straightening), ratio_n:ratio_d the locked aspect
// (0:0 free, 1:0 original, d < 0 portrait), as darktable keeps it.
// orig_w/orig_h: the image's own pixel size, orientation applied — what
// the "original" ratio (1:0) means, whatever the straightening did.
typedef struct oma_crop_info { int found, enabled; float cx, cy, cw, ch; int ratio_n, ratio_d; int orig_w, orig_h; int orientation; } oma_crop_info;
int oma_engine_crop_get(int imgid, oma_crop_info *info);
// One history item. The full frame switches the module off.
int oma_engine_crop_set(int imgid, float cx, float cy, float cw, float ch, int ratio_n, int ratio_d);
// The picture with the crop lifted (for the crop tool): every crop item in
// history is switched off for the render and restored after.
int oma_engine_render_uncropped(int imgid, int max_w, int max_h, float **out, int *w, int *h);
// Geometry actions: 0 constrain crop, 1 level, 2 vertical, 3 horizontal,
// 4 both, 5 guides, 6 reset geometry. Guides are x0,y0,x1,y1 in fractions
// of the displayed uncropped frame. At most four, with two per fitted axis.
// crop_mode: 0 off, 1 largest area, 2 original aspect. One history item;
// failed/obsolete estimates leave parameters and history untouched.
int oma_engine_geometry_available(void);
int oma_engine_geometry_crop_mode(int imgid);
int oma_engine_geometry(int imgid, int operation, int crop_mode, const float *guides, int count);
// White balance as the photographer sees it, over colour calibration:
// temperature (K) on the daylight locus and tint as a signed offset along
// the locus normal (+ magenta, − green; ±100 covers the useful range).
// The module's illuminant becomes CUSTOM with the computed CIE xy.
int oma_engine_wb_get(int imgid, float *temperature, float *tint);
// Texture: the diffuse module driven as one slider, -100..100. Positive
// sharpens fine detail (its "local contrast | fine" preset, speeds scaled
// by amount/100), negative smooths it; 0 switches the module off. `get`
// reads the amount back from the first-order speed, and whether it is on.
int oma_engine_texture_get(int imgid, float *amount, int *enabled);
int oma_engine_texture_set(int imgid, float amount);
int oma_engine_wb_set(int imgid, float temperature, float tint);
// As shot: the illuminant the module derived from the camera's own white
// balance when the image loaded (its defaults), the module back to its
// default switch. 0 on ok.
int oma_engine_wb_as_shot(int imgid);
// A custom illuminant at CIE xy (a picked neutral's chromaticity in D50
// XYZ); the temperature field takes its correlated colour temperature so
// the sliders read sensibly. 0 on ok.
int oma_engine_wb_set_xy(int imgid, float x, float y);
// The picture as module `op` sees it: rendered with that module switched
// off through a history item that is added for the render and removed
// after, so the History panel never sees it.
int oma_engine_render_without(int imgid, const char *op, int max_w, int max_h, float **out, int *w, int *h);
// The picture as `op` receives it: rendered with `op` and every module after
// it switched off for the moment, except the output conversion (linear
// Rec. 709) and the final scaling. For a module's input histogram.
int oma_engine_render_input(int imgid, const char *op, int max_w, int max_h, float **out, int *w, int *h);
// Technical, ungraded linear Rec.709 at exposure parameter zero. Retains
// crop, camera profile, WB and DNG gain; restores history including redo.
// bias is the effective native camera compensation, base its default EV.
int oma_engine_render_exposure_meter(int imgid, float **out, int *w, int *h,
                                     double *black, double *bias, double *base);
// ── local adjustments: a second instance of a module under a drawn mask ─
typedef struct oma_local_info {
    int found;
    int priority;                 // the instance's multi_priority (> 0)
    int enabled;
    int shape;                    // first shape: radial, gradient, brush or path
    float cx, cy;                 // centre / anchor, 0..1 of the image
    float radius, border;         // radial: fractions of min(width, height)
    float rotation, compression;  // gradient: degrees, 0..1
    float opacity;                // 0..1 of the first shape
    int shapes;                   // how many shapes are in its mask group
    char name[64];
} oma_local_info;
// Instances of `op` beyond the base one (priority > 0), oldest first.
int oma_engine_local_count(int imgid, const char *op);
int oma_engine_local_get(int imgid, const char *op, int index, oma_local_info *info);
// Adds an instance of `op` with a fresh mask; returns its priority or -1.
// shape 1: (cx, cy, a = radius, b = border). shape 2: (cx, cy, a = compression, rotation).
int oma_engine_local_add(int imgid, const char *op, int shape, float cx, float cy, float a, float b, float rotation);
// Moves/reshapes the instance's FIRST shape (same argument meaning) and sets its opacity.
int oma_engine_local_set_mask(int imgid, const char *op, int priority, float cx, float cy, float a, float b, float rotation, float opacity);
// Drops the instance and its history items.
int oma_engine_local_remove(int imgid, const char *op, int priority);

// ── shapes inside one local's mask group ────────────────────────────────
// A local's mask is a group of shapes, each combined into the running
// mask with its own operator and optional inversion. Shape 0 is always a
// union (there is nothing under it to combine with).
enum { OMA_SHAPE_CIRCLE = 1, OMA_SHAPE_GRADIENT = 2, OMA_SHAPE_BRUSH = 3, OMA_SHAPE_PATH = 4 };
// Closed cubic Bezier path. Coordinates and controls use the original frame.
typedef struct oma_path_node { float x, y, in_x, in_y, out_x, out_y; } oma_path_node;
int oma_engine_local_add_path(int imgid, const char *op, const oma_path_node *nodes, int count, float feather);
// Binary selection in developed (cropped/rotated) coordinates -> independent
// editable native paths, with holes. Returns the new local priority.
int oma_engine_local_add_selection(int imgid, const uint8_t *mask, int width, int height, int stride);
int oma_engine_shape_add_path(int imgid, const char *op, int priority, const oma_path_node *nodes, int count, float feather);
int oma_engine_shape_path_nodes(int imgid, const char *op, int priority, int index, oma_path_node *out, int capacity);
int oma_engine_shape_set_path(int imgid, const char *op, int priority, int index, const oma_path_node *nodes, int count, float feather);
enum { OMA_MASK_UNION = 0, OMA_MASK_INTERSECT = 1, OMA_MASK_DIFFERENCE = 2, OMA_MASK_EXCLUSION = 3 };
typedef struct oma_shape_info {
    int found;
    int shape;                    // OMA_SHAPE_*
    int combine;                  // OMA_MASK_*
    int inverse;                  // this shape's own area is flipped first
    float cx, cy;                 // centre / anchor / first brush node, 0..1
    float radius, border;         // radial: fractions of min(width, height)
    float rotation, compression;  // gradient: degrees, 0..1
    float opacity;                // 0..1
    int points;                   // brush/path node count, else 0
    float size, hardness;         // brush: border and hardness of node 0
    float density;                // brush: flow of node 0 (0..1)
    int enabled;                  // 0 = bypassed: kept in the group, skipped by the mask
    char name[128];
} oma_shape_info;
int oma_engine_shape_count(int imgid, const char *op, int priority);
int oma_engine_shape_get(int imgid, const char *op, int priority, int index, oma_shape_info *info);
// Appends a shape to the instance's group; returns its index or -1.
int oma_engine_shape_add(int imgid, const char *op, int priority, int shape, float cx, float cy, float a, float b, float rotation);
// Appends a brush stroke: `pts` is x,y pairs in 0..1, `n` nodes.
int oma_engine_shape_add_brush(int imgid, const char *op, int priority, const float *pts, int n, float size, float hardness, float density);
// Reads a brush stroke's nodes into `out` (x,y pairs); returns the node count written.
int oma_engine_shape_brush_points(int imgid, const char *op, int priority, int index, float *out, int max);
int oma_engine_shape_set(int imgid, const char *op, int priority, int index, float cx, float cy, float a, float b, float rotation, float opacity);
int oma_engine_shape_set_combine(int imgid, const char *op, int priority, int index, int combine, int inverse);
int oma_engine_shape_remove(int imgid, const char *op, int priority, int index);
// A copy of the shape right after it in the group, nudged a little so the
// two can be told apart; same operator, inversion and opacity. Returns the
// new index or -1.
int oma_engine_shape_duplicate(int imgid, const char *op, int priority, int index);
// Moves the shape at `from` to position `to`; the running mask is rebuilt
// in the new order and whatever comes first is a union.
int oma_engine_shape_move(int imgid, const char *op, int priority, int from, int to);
// Bypass: a switched-off shape keeps its place, operator and opacity but
// contributes nothing until it is switched back on.
int oma_engine_shape_set_enabled(int imgid, const char *op, int priority, int index, int enabled);
int oma_engine_shape_rename(int imgid, const char *op, int priority, int index, const char *name);
// Width, hardness and flow of every node of a brush stroke.
int oma_engine_shape_set_brush(int imgid, const char *op, int priority, int index, float size, float hardness, float density);
// Renames the local: its instance (and any module sharing its mask), and
// the mask group, so History and the overlay follow.
int oma_engine_local_rename(int imgid, const char *op, int priority, const char *name);

// ── parametric range masks on the same local ────────────────────────────
// A local can also narrow its mask by pixel value: luminance, hue or
// chroma. Each range is a trapezoid over 0..1 - nothing below p0, full
// between p1 and p2, nothing above p3 - and can be flipped. The channel
// indices differ per blend colourspace; the bridge maps them.
enum { OMA_RANGE_LUMA = 0, OMA_RANGE_HUE = 1, OMA_RANGE_CHROMA = 2 };
typedef struct oma_range_info {
    int active;                   // this channel narrows the mask
    int inverse;                  // the band is what it keeps out, not in
    float p0, p1, p2, p3;         // 0..1, ascending
} oma_range_info;
int oma_engine_range_get(int imgid, const char *op, int priority, int channel, oma_range_info *info);
int oma_engine_range_set(int imgid, const char *op, int priority, int channel, int active, int inverse,
                         float p0, float p1, float p2, float p3);
// Edge refinement of the finished mask, darktable's blend post-processing:
// a gaussian blur (radius in full-image pixels), a guided-filter feather
// steered by the module's input or output (radius likewise), and a tone
// curve over the mask (contrast -1..1, brightness -1..1). Members of the
// local's stack follow.
enum { OMA_GUIDE_INPUT = 0, OMA_GUIDE_OUTPUT = 1 };
typedef struct oma_refine_info {
    float blur;                   // 0..100
    float feather;                // 0..250
    int guide;                    // OMA_GUIDE_*
    float contrast, brightness;   // -1..1
} oma_refine_info;
int oma_engine_refine_get(int imgid, const char *op, int priority, oma_refine_info *info);
int oma_engine_refine_set(int imgid, const char *op, int priority, const oma_refine_info *info);
// The full pixel size of the developed image (after crop), for scaling
// pixel radii onto a preview.
int oma_engine_image_size(int imgid, int *width, int *height);
// Whether the source is camera-linear (mosaic RAW or already-demosaiced
// linear RAW), using the same classification as automatic exposure/tone
// presets. This is independent of the current edit's enabled modules.
// Returns 1/0, or -1 if the image could not be loaded.
int oma_engine_is_scene_referred(int imgid);
// Camera calibration used by the active RAW pipeline. The denoise decoder may
// have a different camera table or ADC maximum; carry the editor's calibration
// into the linear copy rather than silently changing its brightness/colour.
// Returns 0 on success, -1 when unavailable (e.g. an ordinary RGB image).
int oma_engine_raw_calibration(int imgid, float matrix[9], float *white, float *highlight_bias, int geometry[6]);
// Whole-mask polarity: `invert` flips the finished mask (drawn and
// parametric together).
int oma_engine_mask_get_invert(int imgid, const char *op, int priority);
int oma_engine_mask_set_invert(int imgid, const char *op, int priority, int invert);
// Exact native mask in output geometry: float RGBA, R/G/B = coverage,
// alpha = 1. Caller frees with oma_engine_free. Does not alter history.
int oma_engine_render_mask(int imgid, const char *op, int priority, int max_w, int max_h,
                           float **out, int *w, int *h);
// Range eyedropper in native input-channel units, at normalised output coordinates.
int oma_engine_pick_range(int imgid, int priority, int channel, float x, float y, float *value);
// Same native channel, averaged over an output-space rectangle (a click
// uses a small neighbourhood). Hue uses a circular mean; spread is the
// 90th percentile distance from the mean, in the same 0..1 units.
int oma_engine_pick_range_area(int imgid, int priority, int channel, float x, float y, float x2, float y2,
                               float *value, float *spread);
// ── the local's adjustment stack: more modules on the same mask ─────────
// A local is anchored by one instance (exposure), but any module that
// blends can ride on its mask: a member is an instance of `op` whose
// blend params point at the anchor's mask group and carry the same
// ranges and polarity. Members are made on first set, carry the local's
// name in History, and go with the local. `found` is 0 until then.
int oma_engine_stack_param_get(int imgid, const char *anchor_op, int priority, const char *op, const char *field, oma_param_info *info);
int oma_engine_stack_param_set(int imgid, const char *anchor_op, int priority, const char *op, const char *field, float value);
// Switches the anchor and every member together.
int oma_engine_local_set_enabled(int imgid, const char *op, int priority, int enabled);
// Parameters and switch of one instance (priority 0 = the base module).
int oma_engine_param_get_instance(int imgid, const char *op, int priority, const char *field, oma_param_info *info);
int oma_engine_param_set_instance(int imgid, const char *op, int priority, const char *field, float value);
int oma_engine_module_set_enabled_instance(int imgid, const char *op, int priority, int enabled);

// ── retouch: heal, clone, blur and fill spots ───────────────────────────
// Spots are circle forms in the retouch module's own group, each with an
// algorithm and, for heal and clone, a source position. All coordinates
// are fractions of the image; radius and feather are fractions of the
// short side, like drawn masks.
enum { OMA_SPOT_CLONE = 1, OMA_SPOT_HEAL = 2, OMA_SPOT_BLUR = 3, OMA_SPOT_FILL = 4 };
typedef struct oma_spot_info {
    int found;
    int algorithm;                // OMA_SPOT_*
    float cx, cy, radius, border;
    float sx, sy;                 // source centre (heal / clone)
    float opacity;                // 0..1
    float blur_radius;            // blur: 0.1..200
    float fill_brightness;        // fill: -1..1 over the erased tone
    int shape, points, scale;     // 1 circle, 3 brush, 4 polygon; frequency layer
} oma_spot_info;
int oma_engine_spot_add_drawn(int imgid, int algorithm, int shape, const float *xy, int count, float radius, float border, float sx, float sy, int scale);
int oma_engine_spot_points(int imgid, int index, float *xy, int capacity);
int oma_engine_spot_set_scale(int imgid, int index, int scale);
int oma_engine_mask_coordinates(int imgid, float *xy, int count, int forward);
int oma_engine_retouch_preview(int imgid, int scale, int width, int height, float **out, int *w, int *h);
int oma_engine_spot_count(int imgid);
int oma_engine_spot_get(int imgid, int index, oma_spot_info *info);
// Adds a spot; returns its index or -1. The module is switched on.
int oma_engine_spot_add(int imgid, int algorithm, float cx, float cy, float radius, float border, float sx, float sy);
int oma_engine_spot_set(int imgid, int index, float cx, float cy, float radius, float border, float sx, float sy, float opacity);
int oma_engine_spot_set_algorithm(int imgid, int index, int algorithm, float blur_radius, float fill_brightness);
int oma_engine_spot_remove(int imgid, int index);

// Translated display name of a module ("color calibration"), or op when unknown.
const char *oma_engine_module_name(int imgid, const char *op);

// History
int oma_engine_history_count(int imgid);
int oma_engine_history_end(int imgid);
// Measures capture sharpening's automatic radius over the whole frame at
// full size (needs capture sharpening on); 0 on success.
int oma_engine_measure_capture_radius(int imgid, float *radius);
// op name / enabled / user label of item i (0 = oldest)
const char *oma_engine_history_op(int imgid, int i);
int oma_engine_history_enabled(int imgid, int i);
const char *oma_engine_history_label(int imgid, int i);
// Move the history end (0 = original); a later param_set truncates.
int oma_engine_history_set_end(int imgid, int end);
// Discards every step above the end (what Redo would bring back).
int oma_engine_history_truncate(int imgid);
// Discard the edit/redo history and masks. to_start restores the separately
// retained import baseline; otherwise restore the native camera defaults.
// Returns 0 on success, or -1. Full resets cannot be undone.
int oma_engine_history_reset(int imgid, int to_start);
// Preset replacement state is bound to a verified history prefix. Undo/Redo,
// reopening and variants select the corresponding state; a replaced branch
// cannot reuse a stale numeric history position. Caller frees *json.
int oma_engine_preset_state_get(int imgid, char **json);
int oma_engine_preset_state_put(int imgid, int from_end, const char *json, const char *transition_json);
// Restore one base adjustment to its image-specific starting state. An optional
// field list resets only that tool's fields in a shared native module.
int oma_engine_module_reset(int imgid, const char *op, const char *const *fields, int count);

// ── lens profiles (lensfun) ─────────────────────────────────────────────
// What the lens module will correct with for imgid: the camera and lens
// lensfun matched - from the file's EXIF, or from the module's own
// camera/lens override - and whether each was found in the database.
typedef struct {
    int found;              // the lens module exists
    int camera_found, lens_found;
    int overridden;         // the module carries a lens name of its own
    char camera[160], lens[160];        // database names ("" when not found)
    char exif_camera[160], exif_lens[160];
} oma_lens_info;
int oma_engine_lens_info(int imgid, oma_lens_info *out);
// The photo's camera, as the engine names it ("Canon EOS 5D Mark III":
// maker and model normalised) and as its EXIF gives it, for matching a
// camera profile to the photo. Either may be empty.
int oma_engine_camera(int imgid, char *normalised, int nlen, char *exif, int elen);
// The lenses the database lists for the matched camera's mount (and its
// compatible mounts; the whole database when the body is unknown),
// sorted by name. Count, then names by index.
int oma_engine_lens_candidate_count(int imgid);
int oma_engine_lens_candidate(int imgid, int index, char *out, int len);
// Set (non-empty) or clear ("" / NULL) the module's lens override; a set
// also switches the correction method to the Lensfun database.
int oma_engine_lens_override(int imgid, const char *lens);

// Engine cache occupancy, for memory budgeting and its tests. The engine
// bounds decoded full images and reduced float buffers by ENTRY COUNT, not
// by bytes, so a set of large RAWs can retain far more memory than the
// thumbnail cache's byte quota suggests. Any pointer may be NULL.
//   thumb_bytes/thumb_quota  thumbnail cache, in bytes
//   full_entries/full_quota  decoded full images, as a count
//   f_entries/f_quota        reduced float buffers, as a count
void oma_engine_cache_stats(size_t *thumb_bytes, size_t *thumb_quota,
                            int *full_entries, int *full_quota,
                            int *f_entries, int *f_quota);

// Reduced-quality fitted previews. The engine's "fast" pipe skips dual
// demosaicing, capture sharpening, green equilibration and colour smoothing
// while a photo is shown fitted to the viewport. Native detail tiles,
// original comparison at native scale and every export keep full quality,
// so what is inspected at 100% and what is written to disk never change.
// Off by default: it changes what the fitted view shows. Rebuilds the
// preview contexts, so call it between jobs.
void oma_engine_set_reduced_previews(int reduced);
int oma_engine_reduced_previews(void);
// Exports process at full resolution and downsample last (slow, darktable's
// "high quality resampling") instead of downsampling right after demosaic.
void oma_engine_set_export_full_resolution(int full);
int oma_engine_export_full_resolution(void);
// The engine's colour chain (exposure to sigmoid as one job): present in the
// patched engine, on unless OMARAW_CHAIN=0, switchable at runtime for tests.
void oma_engine_set_chain_enabled(int enabled);
int oma_engine_chain_available(void);
int oma_engine_chain_enabled(void);

// The ceiling on decoded source held behind the current photo, in bytes;
// zero restores the engine's own entry-count behaviour. A single word, so
// it may be set from any thread, but it only takes effect on the engine
// worker's next photo load. OMARAW_SOURCE_CACHE sets the initial value.
void oma_engine_set_source_ceiling(size_t bytes);
size_t oma_engine_source_ceiling(void);

// Bytes of decoded source the engine is holding for images this process has
// loaded, measured from the engine's own cache entries.
size_t oma_engine_source_bytes(void);

// Worker-thread only, between jobs. Context bytes cover all reusable pipes
// and area caches; the active decode/render may require additional memory.
void oma_engine_set_memory_budget(size_t working, size_t sources, size_t contexts, size_t overviews);
void oma_engine_memory_budget(size_t *working, size_t *sources, size_t *contexts, size_t *overviews);

// Last error text ("" when none).
const char *oma_engine_error(void);

#ifdef __cplusplus
}
#endif
