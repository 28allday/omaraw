#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Internal optional preview adapter. Negative cpu_ms attempts admitted GPU
// work; positive cpu_ms offers a completed native CPU reference for warmup.
typedef int (*oma_texture_stage_fn)(const float *, float *, int, int, float, float, double);
int oma_texture_stage(const float *input, float *output, int width, int height,
                      float amount, float source_scale, double cpu_ms,
                      int (*cancelled)(void *), void *request);
void oma_texture_cancel(void);
#ifdef __cplusplus
}
#endif
