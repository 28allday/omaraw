// Tethered capture over libgphoto2, straight: detect cameras on USB/PTP,
// open one, read and set its configuration, shoot and download, and pull
// live-view frames. One camera at a time. Every call must come from the
// same thread (the capture worker); none of them touch the engine.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int oma_gp_init(void);          // 0 on ok
void oma_gp_cleanup(void);
const char *oma_gp_error(void); // last error text ("" when none)
const char *oma_gp_version(void);

// Cameras currently detected: model and port strings, up to `max` each.
// Returns the count (0 when none, -1 on failure).
int oma_gp_detect(char (*models)[128], char (*ports)[64], int max);

// Opens the camera at model/port. Returns 0 on ok.
int oma_gp_open(const char *model, const char *port);
void oma_gp_close(void);
int oma_gp_is_open(void);
const char *oma_gp_model(void);
// gphoto's summary text (battery, storage… as the driver reports it).
const char *oma_gp_summary(void);
// Capability flags read from the driver's abilities.
int oma_gp_can_capture(void);
int oma_gp_can_preview(void);
int oma_gp_can_config(void);

// Configuration: every leaf widget the camera exposes, as name/label/
// value/type. type: 0 choice (radio/menu), 1 toggle, 2 text, 3 range,
// 4 other (read-only). Reloads the config tree first.
int oma_gp_property_count(void);
int oma_gp_property(int i, char *name, int name_len, char *label, int label_len, char *value, int value_len,
                    int *type, int *read_only, const char **section);
// Choices of a choice widget, newline-separated into buf. Returns count.
int oma_gp_property_choices(const char *name, char *buf, int len);
// Range widget bounds.
int oma_gp_property_range(const char *name, float *lo, float *hi, float *step);
// Sets a property from its text form (choice text, "1"/"0", text, number).
int oma_gp_set_property(const char *name, const char *value);

// Shoots once and downloads every file the camera produces (RAW+JPEG
// gives two) into dest_dir, named <stem>.<original extension>; `paths`
// receives the written paths newline-separated. delete_on_camera removes
// the files from the card afterwards. Returns the file count or -1.
int oma_gp_capture(const char *dest_dir, const char *stem, int delete_on_camera, char *paths, int paths_len);

// One live-view frame as JPEG bytes (free with oma_gp_free). 0 on ok.
int oma_gp_preview(uint8_t **jpeg, size_t *len);
void oma_gp_free(void *p);

// Blocks up to `ms` waiting for a file the camera produced itself (the
// shutter button on the body); downloads it like oma_gp_capture. Returns
// the file count (0 when nothing arrived) or -1.
int oma_gp_wait_for_files(int ms, const char *dest_dir, const char *stem, int delete_on_camera, char *paths, int paths_len);

#ifdef __cplusplus
}
#endif
