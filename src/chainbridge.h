#pragma once
// OmaRAW colour chain ABI 1, mirrored from the engine's common/omaraw_chain.h:
// a run of consecutive per-pixel modules (exposure, colorin, the implicit
// Lab-to-RGB step, channelmixerrgb, sigmoid) described so the application
// can run it as one resident GPU pass. Keep the two definitions identical.
#ifdef __cplusplus
extern "C" {
#endif
enum {
    OMA_CHAIN_EXPOSURE = 1,
    OMA_CHAIN_COLORIN = 2,
    OMA_CHAIN_LAB_TO_RGB = 3,
    OMA_CHAIN_CHANNELMIXER = 4,
    OMA_CHAIN_SIGMOID = 5,
    OMA_CHAIN_PRIMARY_GRADE = 6
};
// Every matrix is row-major 3x4 with the fourth column unused:
// out.r = m[0]*r + m[1]*g + m[2]*b.
typedef struct oma_chain_stage_t {
    int kind;
    float black, scale;
    float coeffs[4];
    int blue_mapping, clipping, corrected;
    float d65[4];
    float matrix[12];
    float lmatrix[12];
    int adaptation, version, clip, apply_grey;
    float rgb_to_xyz[12], xyz_to_rgb[12], mix[12];
    // PRIMARY_GRADE uses these four RGB vectors as Lift, Gamma, Gain, Offset,
    // the fourth of each as that wheel's master level alone, and mix as the
    // working space's Y row then the Lum Mix (0..1).
    float illuminant[4], saturation[4], lightness[4], grey[4];
    float p, gamut;
    int method;
    float white_target, black_target, paper_exposure, film_fog, film_power, paper_power, hue_preservation;
    float pipe_to_base[12], base_to_rendering[12], rendering_to_pipe[12];
} oma_chain_stage_t;
// Negative cpu_ms attempts the GPU; positive offers a completed native
// reference and its time. Returns 1 only when output was supplied or the
// request was cancelled.
typedef int (*oma_chain_stage_fn)(const oma_chain_stage_t *stages, int count,
                                  const float *input, float *output, int width, int height,
                                  double cpu_ms, int (*cancelled)(void *), void *request);
int oma_chain_stage(const oma_chain_stage_t *stages, int count,
                    const float *input, float *output, int width, int height,
                    double cpu_ms, int (*cancelled)(void *), void *request);
// Attempts (GPU asked), offers (native reference received), served (GPU output supplied).
void oma_chain_stats(unsigned long long *attempts, unsigned long long *offers, unsigned long long *served);
#ifdef __cplusplus
}
#endif
