#include "gphoto.h"
#include "download.h"

#include <gphoto2/gphoto2.h>
#include <gphoto2/gphoto2-version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static GPContext *g_ctx = NULL;
static CameraAbilitiesList *g_abilities = NULL;
static GPPortInfoList *g_ports = NULL;
static Camera *g_cam = NULL;
static CameraWidget *g_config = NULL;
static CameraAbilities g_cap;
static char g_model[128], g_summary[4096], g_error[512];
static int g_open = 0;

static void set_error(const char *what, int rc) {
    if (rc) snprintf(g_error, sizeof g_error, "%s: %s", what, gp_result_as_string(rc));
    else snprintf(g_error, sizeof g_error, "%s", what);
}
const char *oma_gp_error(void) { return g_error; }
const char *oma_gp_version(void) { const char **v = gp_library_version(GP_VERSION_SHORT); return v && v[0] ? v[0] : "?"; }
void oma_gp_free(void *p) { free(p); }

static void ctx_error(GPContext *c, const char *text, void *data) { (void)c; (void)data; snprintf(g_error, sizeof g_error, "%s", text ? text : "camera error"); }
static void ctx_status(GPContext *c, const char *text, void *data) { (void)c; (void)text; (void)data; }

int oma_gp_init(void) {
    if (g_ctx) return 0;
    g_error[0] = 0;
    g_ctx = gp_context_new();
    if (!g_ctx) { set_error("gp_context_new failed", 0); return -1; }
    gp_context_set_error_func(g_ctx, ctx_error, NULL);
    gp_context_set_status_func(g_ctx, ctx_status, NULL);
    int rc = gp_abilities_list_new(&g_abilities);
    if (rc >= GP_OK) rc = gp_abilities_list_load(g_abilities, g_ctx);
    if (rc >= GP_OK) rc = gp_port_info_list_new(&g_ports);
    if (rc >= GP_OK) rc = gp_port_info_list_load(g_ports);
    if (rc < GP_OK) { set_error("libgphoto2 init", rc); return -1; }
    return 0;
}

void oma_gp_cleanup(void) {
    oma_gp_close();
    if (g_ports) { gp_port_info_list_free(g_ports); g_ports = NULL; }
    if (g_abilities) { gp_abilities_list_free(g_abilities); g_abilities = NULL; }
    if (g_ctx) { gp_context_unref(g_ctx); g_ctx = NULL; }
}

int oma_gp_detect(char (*models)[128], char (*ports)[64], int max) {
    if (!g_ctx && oma_gp_init()) return -1;
    // Ports change when a camera is plugged in after we started.
    gp_port_info_list_free(g_ports);
    gp_port_info_list_new(&g_ports);
    gp_port_info_list_load(g_ports);
    CameraList *list = NULL;
    gp_list_new(&list);
    const int rc = gp_abilities_list_detect(g_abilities, g_ports, list, g_ctx);
    if (rc < GP_OK) { gp_list_free(list); set_error("detect", rc); return -1; }
    int n = 0;
    const int count = gp_list_count(list);
    for (int i = 0; i < count && n < max; ++i) {
        const char *name = NULL, *port = NULL;
        gp_list_get_name(list, i, &name);
        gp_list_get_value(list, i, &port);
        // "usb:" alone is the bus, not a camera; skip disks and such
        if (!name || !port || !strncmp(port, "disk:", 5)) continue;
        if (!strcmp(port, "usb:")) continue;
        snprintf(models[n], 128, "%s", name);
        snprintf(ports[n], 64, "%s", port);
        ++n;
    }
    gp_list_free(list);
    return n;
}

int oma_gp_open(const char *model, const char *port) {
    if (!g_ctx && oma_gp_init()) return -1;
    oma_gp_close();
    int rc = gp_camera_new(&g_cam);
    if (rc < GP_OK) { set_error("gp_camera_new", rc); return -1; }
    const int m = gp_abilities_list_lookup_model(g_abilities, model);
    if (m < GP_OK) { set_error("unknown camera model", m); oma_gp_close(); return -1; }
    gp_abilities_list_get_abilities(g_abilities, m, &g_cap);
    gp_camera_set_abilities(g_cam, g_cap);
    const int p = gp_port_info_list_lookup_path(g_ports, port);
    if (p < GP_OK) { set_error("port not found", p); oma_gp_close(); return -1; }
    GPPortInfo info;
    gp_port_info_list_get_info(g_ports, p, &info);
    gp_camera_set_port_info(g_cam, info);
    rc = gp_camera_init(g_cam, g_ctx);
    if (rc < GP_OK) {
        // The usual culprit is a desktop automounter holding the device.
        set_error(rc == GP_ERROR_IO_USB_CLAIM ? "camera is held by another program (a file manager mounted it?)" : "connect", rc);
        oma_gp_close();
        return -1;
    }
    snprintf(g_model, sizeof g_model, "%s", model);
    CameraText text;
    if (gp_camera_get_summary(g_cam, &text, g_ctx) >= GP_OK) snprintf(g_summary, sizeof g_summary, "%s", text.text);
    else g_summary[0] = 0;
    g_open = 1;
    return 0;
}

void oma_gp_close(void) {
    if (g_config) { gp_widget_free(g_config); g_config = NULL; }
    if (g_cam) { if (g_open) gp_camera_exit(g_cam, g_ctx); gp_camera_unref(g_cam); g_cam = NULL; }
    g_open = 0;
    g_model[0] = 0; g_summary[0] = 0;
}

int oma_gp_is_open(void) { return g_open; }
const char *oma_gp_model(void) { return g_model; }
const char *oma_gp_summary(void) { return g_summary; }
int oma_gp_can_capture(void) { return g_open && (g_cap.operations & GP_OPERATION_CAPTURE_IMAGE) ? 1 : 0; }
int oma_gp_can_preview(void) { return g_open && (g_cap.operations & GP_OPERATION_CAPTURE_PREVIEW) ? 1 : 0; }
int oma_gp_can_config(void) { return g_open && (g_cap.operations & GP_OPERATION_CONFIG) ? 1 : 0; }

// ── configuration ───────────────────────────────────────────────────────
static int reload_config(void) {
    if (!g_open) { set_error("no camera open", 0); return -1; }
    if (g_config) { gp_widget_free(g_config); g_config = NULL; }
    const int rc = gp_camera_get_config(g_cam, &g_config, g_ctx);
    if (rc < GP_OK) { g_config = NULL; set_error("read configuration", rc); return -1; }
    return 0;
}

typedef struct { CameraWidget *w; const char *section; } leaf_t;
static leaf_t g_leaves[512];
static int g_leaf_count = 0;

static void collect(CameraWidget *w, const char *section) {
    CameraWidgetType t;
    gp_widget_get_type(w, &t);
    if (t == GP_WIDGET_WINDOW || t == GP_WIDGET_SECTION) {
        const char *label = NULL;
        gp_widget_get_label(w, &label);
        const int n = gp_widget_count_children(w);
        for (int i = 0; i < n; ++i) {
            CameraWidget *c = NULL;
            if (gp_widget_get_child(w, i, &c) >= GP_OK) collect(c, t == GP_WIDGET_SECTION ? label : section);
        }
        return;
    }
    if (g_leaf_count < (int)(sizeof g_leaves / sizeof g_leaves[0])) { g_leaves[g_leaf_count].w = w; g_leaves[g_leaf_count].section = section; ++g_leaf_count; }
}

int oma_gp_property_count(void) {
    g_leaf_count = 0;
    if (reload_config()) return -1;
    collect(g_config, "");
    return g_leaf_count;
}

static int widget_value(CameraWidget *w, char *value, int len) {
    CameraWidgetType t;
    gp_widget_get_type(w, &t);
    value[0] = 0;
    switch (t) {
    case GP_WIDGET_RADIO: case GP_WIDGET_MENU: case GP_WIDGET_TEXT: {
        const char *s = NULL;
        if (gp_widget_get_value(w, &s) >= GP_OK && s) snprintf(value, (size_t)len, "%s", s);
        return t == GP_WIDGET_TEXT ? 2 : 0;
    }
    case GP_WIDGET_TOGGLE: { int v = 0; gp_widget_get_value(w, &v); snprintf(value, (size_t)len, "%d", v ? 1 : 0); return 1; }
    case GP_WIDGET_RANGE: { float v = 0; gp_widget_get_value(w, &v); snprintf(value, (size_t)len, "%g", (double)v); return 3; }
    case GP_WIDGET_DATE: { int v = 0; gp_widget_get_value(w, &v); snprintf(value, (size_t)len, "%d", v); return 4; }
    default: return 4;
    }
}

int oma_gp_property(int i, char *name, int name_len, char *label, int label_len, char *value, int value_len,
                    int *type, int *read_only, const char **section) {
    if (i < 0 || i >= g_leaf_count) return -1;
    CameraWidget *w = g_leaves[i].w;
    const char *n = NULL, *l = NULL;
    gp_widget_get_name(w, &n);
    gp_widget_get_label(w, &l);
    snprintf(name, (size_t)name_len, "%s", n ? n : "");
    snprintf(label, (size_t)label_len, "%s", l ? l : (n ? n : ""));
    *type = widget_value(w, value, value_len);
    int ro = 0;
    gp_widget_get_readonly(w, &ro);
    *read_only = ro;
    if (section) *section = g_leaves[i].section ? g_leaves[i].section : "";
    return 0;
}

static CameraWidget *find_widget(const char *name) {
    if (!g_config && reload_config()) return NULL;
    CameraWidget *w = NULL;
    if (gp_widget_get_child_by_name(g_config, name, &w) >= GP_OK) return w;
    if (gp_widget_get_child_by_label(g_config, name, &w) >= GP_OK) return w;
    return NULL;
}

int oma_gp_property_choices(const char *name, char *buf, int len) {
    CameraWidget *w = find_widget(name);
    if (!w) { set_error("no such property", 0); return -1; }
    const int n = gp_widget_count_choices(w);
    int used = 0;
    buf[0] = 0;
    for (int i = 0; i < n; ++i) {
        const char *c = NULL;
        if (gp_widget_get_choice(w, i, &c) < GP_OK || !c) continue;
        const int wrote = snprintf(buf + used, (size_t)(len - used), "%s%s", i ? "\n" : "", c);
        if (wrote < 0 || used + wrote >= len) break;
        used += wrote;
    }
    return n;
}

int oma_gp_property_range(const char *name, float *lo, float *hi, float *step) {
    CameraWidget *w = find_widget(name);
    if (!w) return -1;
    return gp_widget_get_range(w, lo, hi, step) >= GP_OK ? 0 : -1;
}

int oma_gp_set_property(const char *name, const char *value) {
    CameraWidget *w = find_widget(name);
    if (!w) { set_error("no such property", 0); return -1; }
    CameraWidgetType t;
    gp_widget_get_type(w, &t);
    int rc;
    switch (t) {
    case GP_WIDGET_RADIO: case GP_WIDGET_MENU: case GP_WIDGET_TEXT: rc = gp_widget_set_value(w, value); break;
    case GP_WIDGET_TOGGLE: { const int v = atoi(value) ? 1 : 0; rc = gp_widget_set_value(w, &v); break; }
    case GP_WIDGET_RANGE: { const float v = (float)atof(value); rc = gp_widget_set_value(w, &v); break; }
    default: set_error("property cannot be set", 0); return -1;
    }
    if (rc < GP_OK) { set_error("set value", rc); return -1; }
    // Only the changed widget goes back to the camera.
    rc = gp_camera_set_single_config(g_cam, name, w, g_ctx);
    if (rc < GP_OK) rc = gp_camera_set_config(g_cam, g_config, g_ctx);
    if (rc < GP_OK) { set_error("apply to camera", rc); return -1; }
    return 0;
}

// ── capture ─────────────────────────────────────────────────────────────
static int download(const CameraFilePath *cp, const char *dest_dir, const char *stem, int delete_on_camera, char *paths, int paths_len, int index) {
    CameraFile *file = NULL;
    const int created = gp_file_new(&file);
    if (created < GP_OK || !file) { set_error("allocate camera file", created); return -1; }
    int rc = gp_camera_file_get(g_cam, cp->folder, cp->name, GP_FILE_TYPE_NORMAL, file, g_ctx);
    if (rc < GP_OK) { gp_file_unref(file); set_error("download", rc); return -1; }
    const char *data = NULL;
    unsigned long size = 0;
    rc = gp_file_get_data_and_size(file, &data, &size);
    if (rc < GP_OK) { gp_file_unref(file); set_error("read camera file", rc); return -1; }
    const char *dot = strrchr(cp->name, '.');
    char out[4096] = {0};
    const size_t used = strlen(paths);
    // Refuse before saving/deleting if this batch cannot report the new path.
    if (used + strlen(dest_dir) + strlen(stem) + (dot ? strlen(dot) : 0) + 32 >= (size_t)paths_len) {
        gp_file_unref(file); set_error("too many capture paths; camera copy retained", 0); return -1;
    }
    char problem[512] = {0};
    rc = oma_capture_save(data, size, dest_dir, stem, dot, out, sizeof out, problem, sizeof problem);
    if (*problem) set_error(problem, 0);
    gp_file_unref(file);
    if (rc < 0) return -1;
    snprintf(paths + used, (size_t)paths_len - used, "%s%s", index ? "\n" : "", out);
    if (delete_on_camera && rc == 0) {
        const int deleted = gp_camera_file_delete(g_cam, cp->folder, cp->name, g_ctx);
        if (deleted < GP_OK) set_error("saved and verified; cannot delete camera copy", deleted);
    }
    return 0;
}

// After a shot, more files may follow (RAW+JPEG): drain events until the
// camera goes quiet for a moment.
static int drain(const char *dest_dir, const char *stem, int delete_on_camera, char *paths, int paths_len, int count, int quiet_ms) {
    int idle = 0;
    while (idle < quiet_ms) {
        CameraEventType ev;
        void *data = NULL;
        const int rc = gp_camera_wait_for_event(g_cam, 200, &ev, &data, g_ctx);
        if (rc < GP_OK) { free(data); set_error("camera event", rc); return -1; }
        if (ev == GP_EVENT_FILE_ADDED && data) {
            const CameraFilePath *cp = (const CameraFilePath *)data;
            if (download(cp, dest_dir, stem, delete_on_camera, paths, paths_len, count) == 0) ++count;
            else { free(data); return -1; }
            idle = 0;
        } else if (ev == GP_EVENT_TIMEOUT) idle += 200;
        free(data);
    }
    return count;
}

int oma_gp_capture(const char *dest_dir, const char *stem, int delete_on_camera, char *paths, int paths_len) {
    if (!g_open) { set_error("no camera open", 0); return -1; }
    paths[0] = 0;
    g_error[0] = 0;
    CameraFilePath cp;
    const int rc = gp_camera_capture(g_cam, GP_CAPTURE_IMAGE, &cp, g_ctx);
    if (rc < GP_OK) { set_error("capture", rc); return -1; }
    int count = 0;
    if (download(&cp, dest_dir, stem, delete_on_camera, paths, paths_len, 0) == 0) count = 1;
    else return -1;
    return drain(dest_dir, stem, delete_on_camera, paths, paths_len, count, 1200);
}

int oma_gp_wait_for_files(int ms, const char *dest_dir, const char *stem, int delete_on_camera, char *paths, int paths_len) {
    if (!g_open) { set_error("no camera open", 0); return -1; }
    paths[0] = 0;
    g_error[0] = 0;
    int waited = 0, count = 0;
    while (waited < ms) {
        CameraEventType ev;
        void *data = NULL;
        const int rc = gp_camera_wait_for_event(g_cam, 200, &ev, &data, g_ctx);
        if (rc < GP_OK) { free(data); set_error("wait", rc); return -1; }
        if (ev == GP_EVENT_FILE_ADDED && data) {
            if (download((const CameraFilePath *)data, dest_dir, stem, delete_on_camera, paths, paths_len, count) == 0) ++count;
            else { free(data); return -1; }
            free(data);
            return drain(dest_dir, stem, delete_on_camera, paths, paths_len, count, 1200);
        }
        free(data);
        waited += 200;
    }
    return count;
}

int oma_gp_preview(uint8_t **jpeg, size_t *len) {
    if (!g_open) { set_error("no camera open", 0); return -1; }
    CameraFile *file = NULL;
    const int created = gp_file_new(&file);
    if (created < GP_OK || !file) { set_error("allocate camera file", created); return -1; }
    const int rc = gp_camera_capture_preview(g_cam, file, g_ctx);
    if (rc < GP_OK) { gp_file_unref(file); set_error("live view", rc); return -1; }
    const char *data = NULL;
    unsigned long size = 0;
    gp_file_get_data_and_size(file, &data, &size);
    *jpeg = malloc(size);
    if (!*jpeg) { gp_file_unref(file); set_error("out of memory", 0); return -1; }
    memcpy(*jpeg, data, size);
    *len = size;
    gp_file_unref(file);
    return 0;
}
