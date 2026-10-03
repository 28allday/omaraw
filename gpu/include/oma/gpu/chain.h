#pragma once
#include <array>
#include <vector>

namespace oma::gpu {
// One stage of the engine's colour chain: the per-pixel modules between
// demosaic and colorout, described argument for argument as darktable's own
// OpenCL kernels take them. Every matrix is row-major 3x4 with the fourth
// column unused: out.r = m[0]*r + m[1]*g + m[2]*b.
struct ChainStage {
    enum Kind { Exposure = 1, ColorIn = 2, LabToRgb = 3, ChannelMixer = 4, Sigmoid = 5, PrimaryGrade = 6 };
    int kind = Exposure;
    // Signed gamma-2.2 LGG/Offset. Linear input and output; alpha unchanged.
    std::array<float, 3> lift{0,0,0}, gamma{1,1,1}, gain{1,1,1}, offset{0,0,0};
    // Lum Mix (1 = colour pushes move brightness too): below 1 the linear
    // luminance, by the working space's Y row, is held towards the grade of
    // the master levels alone (Lift, Gamma, Gain, Offset).
    std::array<float, 4> master{0,1,1,0};
    std::array<float, 3> luma{.2627f,.6780f,.0593f};
    float lumMix = 1;
    // exposure: out = (in - black) * scale
    float black = 0, scale = 1;
    // colorin: multiply by coeffs unless blue mapping, optional blue mapping,
    // matrix to XYZ (or to a clipping RGB, clamp to [0,1], lmatrix to XYZ),
    // then XYZ to Lab
    std::array<float, 4> coeffs{1, 1, 1, 1};
    bool blueMapping = false, clipping = false;
    std::array<float, 12> matrix{}, lmatrix{};
    // channelmixer
    int adaptation = 1, version = 2;
    bool clip = false, applyGrey = false;
    std::array<float, 12> rgbToXyz{}, xyzToRgb{}, mix{};
    std::array<float, 4> illuminant{}, saturation{}, lightness{}, grey{};
    float p = 1, gamut = 0;
    // sigmoid: method 0 per channel, 1 RGB ratio
    int method = 0;
    float whiteTarget = 1, blackTarget = 0, paperExposure = 0, filmFog = 0, filmPower = 1, paperPower = 1, huePreservation = 0;
    std::array<float, 12> pipeToBase{}, baseToRendering{}, renderingToPipe{};
};
}
