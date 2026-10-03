#pragma once
// Internal C boundary for the engine's format adapter. Converts straight
// linear Rec.709 floats in place before the real encoder quantizes/writes.
// ICC bytes describe the canonical interchange and the embedded output.
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*oma_colour_stage_fn)(const float *input, float *output, int width, int height,
                                    const float *matrix, const float *const lut[3],
                                    const float *coefficients, int lutsize, int curves_before);
void oma_colour_stage(const float *input, float *output, int width, int height,
                      const float *matrix, const float *const lut[3],
                      const float *coefficients, int lutsize, int curves_before);
// Reset at the start of a serialized engine job; fail the job after any
// processor error instead of accepting a render with substituted colours.
int oma_colour_engine_check(int reset, char *error, int error_size);
unsigned long long oma_colour_stage_calls(void);
// Canonical linear Rec.709 ICC used by Qt surfaces. Borrowed immutable bytes.
const void *oma_colour_interchange_profile(int *size);
// Reconcile native RGB floats with that ICC using the native profile's actual
// runtime RGB-to-XYZ D50 matrix (row-major 3x3, before ICC fixed-point rounding).
// Does not clamp RGB or alter alpha. Call once when pixels leave the engine.
int oma_colour_interchange(float *rgba, int width, int height, const double matrix[9],
                           char *error, int error_size);
int oma_colour_output(float *rgba, int width, int height, int profile,
                      const void *source_icc, int source_size,
                      const void *target_icc, int target_size, int intent,
                      char *error, int error_size);
#ifdef __cplusplus
}
#endif
