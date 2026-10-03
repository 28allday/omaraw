// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's colour chain as one fused per-pixel pass, adapted from
// darktable's OpenCL kernels (data/kernels/basic.cl, colorspaces.cl,
// channelmixer.cl, sigmoid.cl, colorspace.h) and their CPU twins at
// 281f60c9957b05b9379084b272c81a5e62fa46e3, and from OmaRAW's own colorin
// path. Copyright (C) 2009-2026 darktable developers; 2026 OmaRAW.
// Keep the operation order and the summation order of every matrix product:
// the CPU twin below and the GLSL are written to match the engine.
#pragma once
#include <oma/gpu/chain.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdio>
#include <string>
#include <vector>

namespace oma::gpu::detail {

// ── shared arithmetic, written once for C++ and mirrored in GLSL ──────────
struct V4 { float x = 0, y = 0, z = 0, w = 0; };
inline V4 v4(float x, float y, float z, float w = 0) { return {x, y, z, w}; }
inline V4 operator+(V4 a, V4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline V4 operator-(V4 a, V4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
inline V4 operator*(V4 a, V4 b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
inline V4 operator/(V4 a, V4 b) { return {a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w}; }
inline V4 operator*(V4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline V4 operator/(V4 a, float s) { return {a.x / s, a.y / s, a.z / s, a.w / s}; }
inline V4 operator+(V4 a, float s) { return {a.x + s, a.y + s, a.z + s, a.w + s}; }
inline V4 operator-(float s, V4 a) { return {s - a.x, s - a.y, s - a.z, s - a.w}; }
inline V4 operator-(V4 a, float s) { return {a.x - s, a.y - s, a.z - s, a.w - s}; }
inline V4 vmax(V4 a, float s) { return {std::fmax(a.x, s), std::fmax(a.y, s), std::fmax(a.z, s), std::fmax(a.w, s)}; }
inline float dot4(V4 a, V4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr float kNormMin = 1.52587890625e-05f;
constexpr float kInvSqrt3 = 0.5773502691896258f;

// out.r = m[0]*x + m[1]*y + m[2]*z, rows four apart; w carried.
inline V4 matrixProduct(V4 v, const float *m) {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z,
            m[4] * v.x + m[5] * v.y + m[6] * v.z,
            m[8] * v.x + m[9] * v.y + m[10] * v.z, v.w};
}
inline V4 matrixProduct3(V4 v, const float m[3][3]) {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z, v.w};
}
inline float labF(float x) {
    const float epsilon = 216.0f / 24389.0f, kappa = 24389.0f / 27.0f;
    return x > epsilon ? std::cbrt(x) : (kappa * x + 16.0f) / 116.0f;
}
inline V4 xyzToLab(V4 xyz) {
    const float fx = labF(xyz.x * (1.0f / 0.9642f)), fy = labF(xyz.y), fz = labF(xyz.z * (1.0f / 0.8249f));
    return {116.0f * fy - 16.0f, 500.0f * (fx - fy), -200.0f * (fz - fy), xyz.w};
}
inline float labFInv(float x) {
    const float epsilon = 0.20689655172413796f, kappa = 24389.0f / 27.0f;
    return x > epsilon ? x * x * x : (116.0f * x - 16.0f) / kappa;
}
inline V4 labToXyz(V4 lab) {
    const float fy = (lab.x + 16.0f) / 116.0f, fx = lab.y / 500.0f + fy, fz = fy - lab.z / 200.0f;
    return {0.9642f * labFInv(fx), 1.0f * labFInv(fy), 0.8249f * labFInv(fz), lab.w};
}
inline V4 xyYToUvY(V4 xyY) {
    const float d = -2.f * xyY.x + 12.f * xyY.y + 3.f;
    return {4.f * xyY.x / d, 9.f * xyY.y / d, xyY.z, xyY.w};
}
inline V4 uvYToXyY(V4 uvY) {
    const float d = 6.0f * uvY.x - 16.f * uvY.y + 12.0f;
    return {9.f * uvY.x / d, 4.f * uvY.y / d, uvY.z, uvY.w};
}
inline V4 xyYToXyz(V4 xyY) {
    V4 out{0, 0, 0, xyY.w};
    if (xyY.y != 0.0f) { out.x = xyY.z * xyY.x / xyY.y; out.y = xyY.z; out.z = xyY.z * (1.f - xyY.x - xyY.y) / xyY.y; }
    return out;
}
constexpr float kXyzToBradford[3][3] = {{0.8951f, 0.2664f, -0.1614f}, {-0.7502f, 1.7135f, 0.0367f}, {0.0389f, -0.0685f, 1.0296f}};
constexpr float kBradfordToXyz[3][3] = {{0.9870f, -0.1471f, 0.1600f}, {0.4323f, 0.5184f, 0.0493f}, {-0.0085f, 0.0400f, 0.9685f}};
constexpr float kXyzToCat16[3][3] = {{0.401288f, 0.650173f, -0.051461f}, {-0.250268f, 1.204414f, 0.045854f}, {-0.002079f, 0.048952f, 0.953127f}};
constexpr float kCat16ToXyz[3][3] = {{1.862068f, -1.011255f, 0.149187f}, {0.38752f, 0.621447f, -0.008974f}, {-0.015841f, -0.034123f, 1.049964f}};

inline V4 gamutMapping(V4 input, float compression, bool clip) {
    const float sum = input.x + input.y + input.z, Y = input.y;
    V4 xyY{sum > 0.0f ? input.x / sum : 0.34567f, sum > 0.0f ? input.y / sum : 0.35850f, Y, 0.0f};
    V4 uvY = xyYToUvY(xyY);
    const float D50u = 0.20915914598542354f, D50v = 0.488075320769787f;
    const float dx = D50u - uvY.x, dy = D50v - uvY.y;
    const float Delta = Y * (dx * dx + dy * dy);
    const float correction = compression == 0.0f ? 0.f : std::pow(Delta, compression);
    const float tx = correction * dx + uvY.x, ty = correction * dy + uvY.y;
    uvY.x = uvY.x > D50u ? std::fmax(tx, D50u) : std::fmin(tx, D50u);
    uvY.y = uvY.y > D50v ? std::fmax(ty, D50v) : std::fmin(ty, D50v);
    xyY = uvYToXyY(uvY);
    if (clip) { xyY.x = std::fmax(xyY.x, 0.0f); xyY.y = std::fmax(xyY.y, 0.0f); }
    xyY.y = std::fmax(xyY.y, kNormMin);
    const float scale = xyY.x + xyY.y;
    if (scale >= 1.f) { xyY.x /= scale; xyY.y /= scale; }
    return xyYToXyz(xyY);
}
inline float euclideanNorm(V4 v) { return std::fmax(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), kNormMin); }
inline V4 lumaChroma(V4 input, V4 saturation, V4 lightness, int version) {
    float norm = euclideanNorm(input);
    const float avg = std::fmax((input.x + input.y + input.z) / 3.0f, kNormMin);
    if (norm > 0.f && avg > 0.f) {
        const float mix = dot4(input, lightness);
        if (version == 2) norm *= kInvSqrt3;
        V4 output = input / norm;
        const float coeffRatio = version == 0 ? dot4(1.f - output, saturation) : dot4(output, saturation) / 3.f;
        const V4 minRatio{output.x < 0 ? output.x : 0, output.y < 0 ? output.y : 0, output.z < 0 ? output.z : 0, output.w < 0 ? output.w : 0};
        const V4 inverse = 1.0f - output;
        V4 o = inverse * coeffRatio + output;
        output = {std::fmax(o.x, minRatio.x), std::fmax(o.y, minRatio.y), std::fmax(o.z, minRatio.z), std::fmax(o.w, minRatio.w)};
        if (version == 2) norm /= euclideanNorm(output) * kInvSqrt3;
        norm *= std::fmax(1.f + mix / avg, 0.f);
        return output * norm;
    }
    return input;
}
inline V4 xyzToLms(V4 xyz, int kind, const ChainStage &s) {
    switch (kind) {
    case 0: case 2: return matrixProduct3(xyz, kXyzToBradford);
    case 1: return matrixProduct3(xyz, kXyzToCat16);
    case 3: return xyz;
    default: return matrixProduct(xyz, s.xyzToRgb.data());
    }
}
inline V4 lmsToXyz(V4 lms, int kind, const ChainStage &s) {
    switch (kind) {
    case 0: case 2: return matrixProduct3(lms, kBradfordToXyz);
    case 1: return matrixProduct3(lms, kCat16ToXyz);
    case 3: return lms;
    default: return matrixProduct(lms, s.rgbToXyz.data());
    }
}
inline void downscale(V4 &v, float scaling) { const bool valid = scaling > kNormMin && !std::isnan(scaling); v = v / (valid ? scaling + kNormMin : kNormMin); }
inline void upscale(V4 &v, float scaling) { const bool valid = scaling > kNormMin && !std::isnan(scaling); v = v * (valid ? scaling + kNormMin : kNormMin); }
inline V4 chromaAdapt(V4 rgb, const ChainStage &s) {
    const int kind = s.adaptation;
    const V4 ill{s.illuminant[0], s.illuminant[1], s.illuminant[2], s.illuminant[3]};
    if (kind == 4) return matrixProduct(matrixProduct(rgb, s.mix.data()), s.rgbToXyz.data());
    V4 xyz = matrixProduct(rgb, s.rgbToXyz.data());
    const float Y = xyz.y;
    if (kind == 3) {
        downscale(xyz, Y);
        const V4 D50{0.9642119944211994f, 1.0f, 0.8251882845188288f, 0.f};
        xyz = xyz * D50 / ill;
        upscale(xyz, Y);
        return matrixProduct(xyz, s.mix.data());
    }
    V4 lms = xyzToLms(xyz, kind, s);
    downscale(lms, Y);
    if (kind == 1) {
        const V4 D50{0.994535f, 1.000997f, 0.833036f, 0.f};
        lms = lms * D50 / ill; // full adaptation: D = 1
    } else {
        const V4 D50{0.996078f, 1.020646f, 0.818155f, 0.f};
        if (kind == 2) {
            V4 t = lms / ill;
            t.z = t.z > 0.f ? std::pow(t.z, s.p) : t.z;
            lms = D50 * t;
        } else lms = lms * D50 / ill;
    }
    upscale(lms, Y);
    return lmsToXyz(matrixProduct(lms, s.mix.data()), kind, s);
}
inline V4 channelMixer(V4 pixIn, const ChainStage &s) {
    V4 rgb = pixIn; rgb.w = 0.f;
    if (s.clip) rgb = vmax(rgb, 0.f);
    V4 xyz = chromaAdapt(rgb, s);
    if (s.clip) xyz = vmax(xyz, 0.f);
    xyz = gamutMapping(xyz, s.gamut, s.clip);
    V4 lms = xyzToLms(xyz, s.adaptation, s);
    if (s.clip) lms = vmax(lms, 0.f);
    lms = lumaChroma(lms, {s.saturation[0], s.saturation[1], s.saturation[2], s.saturation[3]},
                     {s.lightness[0], s.lightness[1], s.lightness[2], s.lightness[3]}, s.version);
    if (s.clip) lms = vmax(lms, 0.f);
    if (s.applyGrey) {
        const float greyMix = std::fmax(dot4(lms, {s.grey[0], s.grey[1], s.grey[2], s.grey[3]}), 0.0f);
        return {greyMix, greyMix, greyMix, pixIn.w};
    }
    xyz = lmsToXyz(lms, s.adaptation, s);
    if (s.clip) xyz = vmax(xyz, 0.f);
    rgb = matrixProduct(xyz, s.xyzToRgb.data());
    if (s.clip) rgb = vmax(rgb, 0.f);
    rgb.w = pixIn.w;
    return rgb;
}
inline float loglogistic(float value, float magnitude, float paperExp, float filmFog, float filmPower, float paperPower) {
    const float clamped = std::fmax(value, 0.0f);
    const float film = std::pow(filmFog + clamped, filmPower);
    const float paper = magnitude * std::pow(film / (paperExp + film), paperPower);
    return std::isnan(paper) ? magnitude : paper;
}
inline V4 desaturateNegative(V4 i) {
    const float avg = std::fmax((i.x + i.y + i.z) / 3.0f, 0.0f);
    const float minValue = std::fmin(std::fmin(i.x, i.y), i.z);
    const float factor = minValue < 0.0f ? -avg / (minValue - avg) : 1.0f;
    return (i - avg) * factor + avg;
}
inline void channelOrder(const float *p, int &mn, int &mid, int &mx) {
    if (p[0] >= p[1]) {
        if (p[1] > p[2]) { mx = 0; mid = 1; mn = 2; }
        else if (p[2] > p[0]) { mx = 2; mid = 0; mn = 1; }
        else if (p[2] > p[1]) { mx = 0; mid = 2; mn = 1; }
        else { mx = 0; mid = 1; mn = 2; }
    } else {
        if (p[0] >= p[2]) { mx = 1; mid = 0; mn = 2; }
        else if (p[2] > p[1]) { mx = 2; mid = 1; mn = 0; }
        else { mx = 1; mid = 2; mn = 0; }
    }
}
inline void preserveHue(float *pix, const float *per, int mn, int mid, int mx, float hue) {
    const float chroma = pix[mx] - pix[mn];
    const float midscale = chroma != 0.f ? (pix[mid] - pix[mn]) / chroma : 0.f;
    const float full = per[mn] + (per[mx] - per[mn]) * midscale;
    const float naiveMid = (1.0f - hue) * per[mid] + hue * full;
    const float energy = per[0] + per[1] + per[2];
    const float naiveEnergy = per[mn] + naiveMid + per[mx];
    const float minPlusMid = pix[mn] + pix[mid];
    const float blend = minPlusMid != 0.f ? 2.0f * pix[mn] / minPlusMid : 0.f;
    const float target = blend * energy + (1.0f - blend) * naiveEnergy;
    if (naiveMid <= per[mid]) {
        const float corrected = ((1.0f - hue) * per[mid] + hue * (midscale * per[mx] + (1.0f - midscale) * (target - per[mx]))) / (1.0f + hue * (1.0f - midscale));
        pix[mn] = target - per[mx] - corrected; pix[mid] = corrected; pix[mx] = per[mx];
    } else {
        const float corrected = ((1.0f - hue) * per[mid] + hue * (per[mn] * (1.0f - midscale) + midscale * (target - per[mn]))) / (1.0f + hue * midscale);
        pix[mn] = per[mn]; pix[mid] = corrected; pix[mx] = target - per[mn] - corrected;
    }
}
inline V4 sigmoid(V4 in, const ChainStage &s) {
    const float alpha = in.w;
    if (s.method == 0) {
        V4 i = matrixProduct(in, s.pipeToBase.data());
        i = desaturateNegative(i);
        i = matrixProduct(i, s.baseToRendering.data());
        float pix[3] = {i.x, i.y, i.z};
        const float per[3] = {loglogistic(i.x, s.whiteTarget, s.paperExposure, s.filmFog, s.filmPower, s.paperPower),
                              loglogistic(i.y, s.whiteTarget, s.paperExposure, s.filmFog, s.filmPower, s.paperPower),
                              loglogistic(i.z, s.whiteTarget, s.paperExposure, s.filmFog, s.filmPower, s.paperPower)};
        int mn, mid, mx; channelOrder(pix, mn, mid, mx);
        preserveHue(pix, per, mn, mid, mx, s.huePreservation);
        V4 o = matrixProduct({pix[0], pix[1], pix[2], 0.f}, s.renderingToPipe.data());
        o.w = alpha; return o;
    }
    V4 i = desaturateNegative(in);
    const float luma = (i.x + i.y + i.z) / 3.0f;
    const float mapped = loglogistic(luma, s.whiteTarget, s.paperExposure, s.filmFog, s.filmPower, s.paperPower);
    if (luma > 1e-9f) i = i * (mapped / luma); else i = {mapped, mapped, mapped, mapped};
    const float pmin = std::fmin(std::fmin(i.x, i.y), i.z), pmax = std::fmax(std::fmax(i.x, i.y), i.z);
    const float epsilon = 1e-6f;
    const float borderWhite = (s.whiteTarget - mapped) / (pmax - mapped + epsilon);
    const float borderBlack = (s.blackTarget - mapped) / (pmin - mapped - epsilon);
    const float border = std::fmin(borderWhite, borderBlack);
    const float chromaVsBorder = (mapped - pmin) / (mapped + epsilon);
    const float adjustment = 1.0f / (chromaVsBorder * border + epsilon);
    const float hyperbolic = 2.0f * chromaVsBorder / (1.0f - chromaVsBorder * chromaVsBorder + epsilon) * adjustment;
    const float z = std::sqrt(hyperbolic * hyperbolic + 1.0f);
    const float factor = hyperbolic / (1.0f + z) * border;
    i = (i - mapped) * factor + mapped;
    i.w = alpha; return i;
}
inline V4 colorIn(V4 in, const ChainStage &s) {
    V4 cam = in;
    if (!s.blueMapping) { cam.x *= s.coeffs[0]; cam.y *= s.coeffs[1]; cam.z *= s.coeffs[2]; }
    if (s.blueMapping) {
        const float YY = cam.x + cam.y + cam.z;
        if (YY > 0.0f) {
            const float zz = cam.z / YY, boundZ = 0.5f, boundY = 0.5f, amount = 0.11f;
            if (zz > boundZ) { const float t = (zz - boundZ) / (1.0f - boundZ) * std::fmin(1.0f, YY / boundY); cam.y += t * amount; cam.z -= t * amount; }
        }
    }
    V4 xyz = matrixProduct(cam, s.matrix.data());
    if (s.clipping) {
        xyz.x = std::fmin(std::fmax(xyz.x, 0.f), 1.f); xyz.y = std::fmin(std::fmax(xyz.y, 0.f), 1.f); xyz.z = std::fmin(std::fmax(xyz.z, 0.f), 1.f);
        xyz = matrixProduct(xyz, s.lmatrix.data());
    }
    V4 lab = xyzToLab(xyz); lab.w = in.w; return lab;
}
inline V4 primaryGrade(V4 p, const ChainStage &s) {
    if (s.lift == std::array<float,3>{0,0,0} && s.gamma == std::array<float,3>{1,1,1}
        && s.gain == std::array<float,3>{1,1,1} && s.offset == std::array<float,3>{0,0,0}) return p;
    const auto power = [](float x, float e) { return std::copysign(std::fmin(std::pow(std::abs(x), e), std::numeric_limits<float>::max()/64.f), x); };
    const auto channel = [&](float x, float lift, float gamma, float gain, float offset) {
        const float encoded = power(x,1.f/2.2f);
        // No lift above white: under a print stock the wheels see scene light,
        // and (1 - encoded) going negative turned a lift into a darkening.
        const float lifted = encoded + lift*std::fmax(0.f,1.f-encoded);
        return power(power(lifted*gain,1.f/gamma) + offset,2.2f);
    };
    const float in[] = {p.x,p.y,p.z};
    float values[3];
    for (int c=0;c<3;++c) values[c] = channel(in[c],s.lift[c],s.gamma[c],s.gain[c],s.offset[c]);
    float scale = 1.f;
    if (s.lumMix < 1.f) {
        // Lum Mix: hold the luminance towards the master levels' alone.
        // Faded out where opposite-signed channels all but cancel, as natively.
        float base = 0.f, full = 0.f, peak = 0.f;
        for (int c=0;c<3;++c) {
            base += s.luma[c]*channel(in[c],s.master[0],s.master[1],s.master[2],s.master[3]);
            full += s.luma[c]*values[c];
            peak = std::fmax(peak, std::fabs(values[c]));
        }
        if (full > 1e-6f && base >= 0.f) {
            const float weight = std::fmin(1.f, std::fmax(0.f, (full/peak - .01f)/.02f));
            scale = 1.f + weight*((base + s.lumMix*(full - base))/full - 1.f);
        }
    }
    return {values[0]*scale,values[1]*scale,values[2]*scale,p.w};
}
inline V4 applyStage(V4 p, const ChainStage &s) {
    switch (s.kind) {
    case ChainStage::Exposure: { V4 o = (p - s.black) * s.scale; o.w = p.w; return o; }
    case ChainStage::ColorIn: return colorIn(p, s);
    case ChainStage::LabToRgb: { V4 o = matrixProduct(labToXyz(p), s.matrix.data()); o.w = p.w; return o; }
    case ChainStage::ChannelMixer: return channelMixer(p, s);
    case ChainStage::Sigmoid: return sigmoid(p, s);
    case ChainStage::PrimaryGrade: return primaryGrade(p, s);
    }
    return p;
}
inline void cpuChainRows(const float *in, float *out, int width, int rows, const std::vector<ChainStage> &stages, const std::atomic_bool *cancel) {
    for (int y = 0; y < rows; ++y) {
        if (cancel && cancel->load()) return;
        const float *src = in + size_t(y) * width * 4;
        float *dst = out + size_t(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            V4 p{src[4*x], src[4*x+1], src[4*x+2], src[4*x+3]};
            for (const auto &s : stages) p = applyStage(p, s);
            dst[4*x] = p.x; dst[4*x+1] = p.y; dst[4*x+2] = p.z; dst[4*x+3] = p.w;
        }
    }
}
inline void cpuChain(Image &image, const std::vector<ChainStage> &stages, const std::atomic_bool *cancel) {
    cpuChainRows(image.rgba.data(), image.rgba.data(), image.width, image.height, stages, cancel);
}
// ── GLSL: the same arithmetic, constants baked per stage ──────────────────
inline std::string g(float v) {
    if (std::isnan(v)) return "(0.0/0.0)";
    if (std::isinf(v)) return v > 0 ? "(1.0/0.0)" : "(-1.0/0.0)";
    char b[40]; std::snprintf(b, sizeof b, "%.9e", v); return b;
}
inline std::string mat(const float *m) {
    // out = vec4(m0*x + m1*y + m2*z, ..., v.w) in that summation order
    return "vec4(" + g(m[0]) + "*v.x+" + g(m[1]) + "*v.y+" + g(m[2]) + "*v.z," +
           g(m[4]) + "*v.x+" + g(m[5]) + "*v.y+" + g(m[6]) + "*v.z," +
           g(m[8]) + "*v.x+" + g(m[9]) + "*v.y+" + g(m[10]) + "*v.z,v.w)";
}
inline std::string chainHeader() {
    return R"(
float oma_grade_power(float x, float exponent) {
    if (x == 0.0) return x;
    float y = min(pow(abs(x), exponent), 5.3169116662270134e36);
    return x < 0.0 ? -y : y;
}
vec3 oma_grade_power3(vec3 x, vec3 exponent) {
    return vec3(oma_grade_power(x.x,exponent.x),oma_grade_power(x.y,exponent.y),oma_grade_power(x.z,exponent.z));
}
vec3 oma_grade_channels(vec3 x, vec3 lift, vec3 gamma, vec3 gain, vec3 offset) {
    vec3 encoded = oma_grade_power3(x,vec3(1.0/2.2));
    vec3 lifted = encoded + lift*max(vec3(0),vec3(1)-encoded);
    vec3 graded = oma_grade_power3(lifted*gain,vec3(1)/gamma) + offset;
    return oma_grade_power3(graded,vec3(2.2));
}
// master: Lift, Gamma, Gain, Offset levels alone; mix: Y row, then Lum Mix.
vec4 oma_primary_grade(vec4 p, vec3 lift, vec3 gamma, vec3 gain, vec3 offset, vec4 master, vec4 mix) {
    if (all(equal(lift,vec3(0))) && all(equal(gamma,vec3(1))) && all(equal(gain,vec3(1))) && all(equal(offset,vec3(0)))) return p;
    vec3 full = oma_grade_channels(p.rgb,lift,gamma,gain,offset);
    float scale = 1.0;
    if (mix.w < 1.0) {
        float base = dot(mix.xyz,oma_grade_channels(p.rgb,vec3(master.x),vec3(master.y),vec3(master.z),vec3(master.w)));
        float y = dot(mix.xyz,full);
        float peak = max(abs(full.x),max(abs(full.y),abs(full.z)));
        if (y > 1e-6 && base >= 0.0) scale = 1.0 + clamp((y/peak - 0.01)/0.02,0.0,1.0)*((base + mix.w*(y - base))/y - 1.0);
    }
    return vec4(full*scale,p.a);
}
const float NORM_MIN = 1.52587890625e-05;
const float INV_SQRT3 = 0.5773502691896258;
float lab_f(float x) { const float e = 216.0/24389.0, k = 24389.0/27.0; return x > e ? pow(x, 1.0/3.0) : (k*x + 16.0)/116.0; }
vec4 xyz_to_lab(vec4 xyz) { float fx = lab_f(xyz.x*(1.0/0.9642)), fy = lab_f(xyz.y), fz = lab_f(xyz.z*(1.0/0.8249)); return vec4(116.0*fy - 16.0, 500.0*(fx - fy), -200.0*(fz - fy), xyz.w); }
float lab_f_inv(float x) { const float e = 0.20689655172413796, k = 24389.0/27.0; return x > e ? x*x*x : (116.0*x - 16.0)/k; }
vec4 lab_to_xyz(vec4 lab) { float fy = (lab.x + 16.0)/116.0, fx = lab.y/500.0 + fy, fz = fy - lab.z/200.0; return vec4(0.9642*lab_f_inv(fx), lab_f_inv(fy), 0.8249*lab_f_inv(fz), lab.w); }
vec4 mat3x3v(vec4 v, mat3 m) { return vec4(m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z, m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z, m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z, v.w); }
const mat3 XYZ_TO_BRADFORD = mat3(0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296);
const mat3 BRADFORD_TO_XYZ = mat3(0.9870, -0.1471, 0.1600, 0.4323, 0.5184, 0.0493, -0.0085, 0.0400, 0.9685);
const mat3 XYZ_TO_CAT16 = mat3(0.401288, 0.650173, -0.051461, -0.250268, 1.204414, 0.045854, -0.002079, 0.048952, 0.953127);
const mat3 CAT16_TO_XYZ = mat3(1.862068, -1.011255, 0.149187, 0.38752, 0.621447, -0.008974, -0.015841, -0.034123, 1.049964);
vec4 xyY_to_uvY(vec4 a) { float d = -2.0*a.x + 12.0*a.y + 3.0; return vec4(4.0*a.x/d, 9.0*a.y/d, a.z, a.w); }
vec4 uvY_to_xyY(vec4 a) { float d = 6.0*a.x - 16.0*a.y + 12.0; return vec4(9.0*a.x/d, 4.0*a.y/d, a.z, a.w); }
vec4 xyY_to_XYZ(vec4 a) { vec4 o = vec4(0.0, 0.0, 0.0, a.w); if (a.y != 0.0) { o.x = a.z*a.x/a.y; o.y = a.z; o.z = a.z*(1.0 - a.x - a.y)/a.y; } return o; }
vec4 gamut_mapping(vec4 input_, float compression, bool clip_) {
    float sum = input_.x + input_.y + input_.z, Y = input_.y;
    vec4 xyY = vec4(sum > 0.0 ? input_.x/sum : 0.34567, sum > 0.0 ? input_.y/sum : 0.35850, Y, 0.0);
    vec4 uvY = xyY_to_uvY(xyY);
    const float D50u = 0.20915914598542354, D50v = 0.488075320769787;
    float dx = D50u - uvY.x, dy = D50v - uvY.y;
    float Delta = Y*(dx*dx + dy*dy);
    // A negative Delta (negative luminance) makes the CPU's powf NaN for a
    // fractional exponent, and its fmaxf/fminf then return the white point.
    // For a whole exponent (the default gamut setting gives 1.0) powf is
    // finite, with the sign of an odd power. GLSL leaves pow of a negative
    // (and max/min of NaN) undefined, so both outcomes are taken explicitly.
    if (compression != 0.0 && Delta < 0.0 && fract(compression) != 0.0) { uvY.x = D50u; uvY.y = D50v; }
    else {
        float correction = compression == 0.0 ? 0.0
                         : Delta >= 0.0 ? pow(Delta, compression)
                         : (mod(compression, 2.0) == 1.0 ? -pow(-Delta, compression) : pow(-Delta, compression));
        float tx = correction*dx + uvY.x, ty = correction*dy + uvY.y;
        uvY.x = uvY.x > D50u ? max(tx, D50u) : min(tx, D50u);
        uvY.y = uvY.y > D50v ? max(ty, D50v) : min(ty, D50v);
    }
    xyY = uvY_to_xyY(uvY);
    if (clip_) { xyY.x = max(xyY.x, 0.0); xyY.y = max(xyY.y, 0.0); }
    xyY.y = max(xyY.y, NORM_MIN);
    float scale = xyY.x + xyY.y;
    if (scale >= 1.0) { xyY.x /= scale; xyY.y /= scale; }
    return xyY_to_XYZ(xyY);
}
float euclidean_norm(vec4 v) { return max(sqrt(v.x*v.x + v.y*v.y + v.z*v.z), NORM_MIN); }
vec4 luma_chroma(vec4 input_, vec4 saturation, vec4 lightness, int version) {
    float norm_ = euclidean_norm(input_);
    float avg = max((input_.x + input_.y + input_.z)/3.0, NORM_MIN);
    if (norm_ > 0.0 && avg > 0.0) {
        float mix_ = dot(input_, lightness);
        if (version == 2) norm_ *= INV_SQRT3;
        vec4 output_ = input_/norm_;
        float coeff_ratio = version == 0 ? dot(1.0 - output_, saturation) : dot(output_, saturation)/3.0;
        vec4 min_ratio = min(output_, vec4(0.0));
        vec4 output_inverse = 1.0 - output_;
        output_ = max(output_inverse*coeff_ratio + output_, min_ratio);
        if (version == 2) norm_ /= euclidean_norm(output_)*INV_SQRT3;
        norm_ *= max(1.0 + mix_/avg, 0.0);
        return output_*norm_;
    }
    return input_;
}
void downscale(inout vec4 v, float s) { bool valid = s > NORM_MIN && !isnan(s); v /= valid ? (s + NORM_MIN) : NORM_MIN; }
void upscale(inout vec4 v, float s) { bool valid = s > NORM_MIN && !isnan(s); v *= valid ? (s + NORM_MIN) : NORM_MIN; }
float loglogistic(float value, float magnitude, float paper_exp, float film_fog, float film_power, float paper_power) {
    float clamped = max(value, 0.0);
    float film = pow(film_fog + clamped, film_power);
    float paper = magnitude*pow(film/(paper_exp + film), paper_power);
    return isnan(paper) ? magnitude : paper;
}
vec4 desaturate_negative(vec4 i) {
    float avg = max((i.x + i.y + i.z)/3.0, 0.0);
    float mn = min(min(i.x, i.y), i.z);
    float factor = mn < 0.0 ? -avg/(mn - avg) : 1.0;
    return (i - avg)*factor + avg;
}
void channel_order(vec3 p, out int mn, out int mid, out int mx) {
    if (p.x >= p.y) {
        if (p.y > p.z) { mx = 0; mid = 1; mn = 2; }
        else if (p.z > p.x) { mx = 2; mid = 0; mn = 1; }
        else if (p.z > p.y) { mx = 0; mid = 2; mn = 1; }
        else { mx = 0; mid = 1; mn = 2; }
    } else {
        if (p.x >= p.z) { mx = 1; mid = 0; mn = 2; }
        else if (p.z > p.y) { mx = 2; mid = 1; mn = 0; }
        else { mx = 1; mid = 2; mn = 0; }
    }
}
vec3 preserve_hue(vec3 pix, vec3 per, int mn, int mid, int mx, float hue) {
    float chroma = pix[mx] - pix[mn];
    float midscale = chroma != 0.0 ? (pix[mid] - pix[mn])/chroma : 0.0;
    float full = per[mn] + (per[mx] - per[mn])*midscale;
    float naive_mid = (1.0 - hue)*per[mid] + hue*full;
    float energy = per.x + per.y + per.z;
    float naive_energy = per[mn] + naive_mid + per[mx];
    float min_plus_mid = pix[mn] + pix[mid];
    float blend = min_plus_mid != 0.0 ? 2.0*pix[mn]/min_plus_mid : 0.0;
    float target = blend*energy + (1.0 - blend)*naive_energy;
    vec3 o = pix;
    if (naive_mid <= per[mid]) {
        float corrected = ((1.0 - hue)*per[mid] + hue*(midscale*per[mx] + (1.0 - midscale)*(target - per[mx])))/(1.0 + hue*(1.0 - midscale));
        o[mn] = target - per[mx] - corrected; o[mid] = corrected; o[mx] = per[mx];
    } else {
        float corrected = ((1.0 - hue)*per[mid] + hue*(per[mn]*(1.0 - midscale) + midscale*(target - per[mn])))/(1.0 + hue*midscale);
        o[mn] = per[mn]; o[mid] = corrected; o[mx] = target - per[mn] - corrected;
    }
    return o;
}
)";
}
inline std::string chainStage(const ChainStage &s, int index) {
    std::string b;
    const std::string sx = std::to_string(index);
    switch (s.kind) {
    case ChainStage::PrimaryGrade:
        b += "p = oma_primary_grade(p, oma_grade_lift_" + sx + ", oma_grade_gamma_" + sx
            + ", oma_grade_gain_" + sx + ", oma_grade_offset_" + sx + ", oma_grade_master_" + sx + ", oma_grade_mix_" + sx + ");\n";
        break;
    case ChainStage::Exposure:
        b += "p = vec4(((p - " + g(s.black) + ") * " + g(s.scale) + ").xyz, p.w);\n";
        break;
    case ChainStage::ColorIn:
        b += "{ vec4 v = p;\n";
        if (!s.blueMapping) b += "v.x *= " + g(s.coeffs[0]) + "; v.y *= " + g(s.coeffs[1]) + "; v.z *= " + g(s.coeffs[2]) + ";\n";
        else b += "{ float YY = v.x + v.y + v.z; if (YY > 0.0) { float zz = v.z / YY; if (zz > 0.5) { float t = (zz - 0.5) / (1.0 - 0.5) * min(1.0, YY / 0.5); v.y += t * 0.11; v.z -= t * 0.11; } } }\n";
        b += "v = " + mat(s.matrix.data()) + ";\n";
        if (s.clipping) b += "v = vec4(clamp(v.xyz, 0.0, 1.0), v.w); v = " + mat(s.lmatrix.data()) + ";\n";
        b += "p = vec4(xyz_to_lab(v).xyz, p.w); }\n";
        break;
    case ChainStage::LabToRgb:
        b += "{ vec4 v = lab_to_xyz(p); p = vec4((" + mat(s.matrix.data()) + ").xyz, p.w); }\n";
        break;
    case ChainStage::ChannelMixer: {
        const int kind = s.adaptation;
        const std::string clip = s.clip ? "true" : "false";
        b += "{ vec4 pix_in = p; vec4 RGB = vec4(p.xyz, 0.0); vec4 XYZ; vec4 LMS; vec4 v;\n";
        if (s.clip) b += "RGB = max(RGB, 0.0);\n";
        const std::string ill = "vec4(" + g(s.illuminant[0]) + "," + g(s.illuminant[1]) + "," + g(s.illuminant[2]) + "," + g(s.illuminant[3]) + ")";
        if (kind == 4) {
            b += "v = RGB; v = " + mat(s.mix.data()) + "; v = " + mat(s.rgbToXyz.data()) + "; XYZ = v;\n";
        } else {
            b += "v = RGB; XYZ = " + mat(s.rgbToXyz.data()) + "; float Y = XYZ.y;\n";
            if (kind == 3) {
                b += "downscale(XYZ, Y); XYZ = XYZ * vec4(0.9642119944211994, 1.0, 0.8251882845188288, 0.0) / " + ill + "; upscale(XYZ, Y); v = XYZ; XYZ = " + mat(s.mix.data()) + ";\n";
            } else {
                b += std::string("LMS = mat3x3v(XYZ, ") + (kind == 1 ? "XYZ_TO_CAT16" : "XYZ_TO_BRADFORD") + "); downscale(LMS, Y);\n";
                if (kind == 1) b += "LMS = LMS * vec4(0.994535, 1.000997, 0.833036, 0.0) / " + ill + ";\n";
                else if (kind == 2) b += "{ vec4 t = LMS / " + ill + "; t.z = t.z > 0.0 ? pow(t.z, " + g(s.p) + ") : t.z; LMS = vec4(0.996078, 1.020646, 0.818155, 0.0) * t; }\n";
                else b += "LMS = LMS * vec4(0.996078, 1.020646, 0.818155, 0.0) / " + ill + ";\n";
                b += "upscale(LMS, Y); v = LMS; v = " + mat(s.mix.data()) + "; XYZ = mat3x3v(v, " + (kind == 1 ? "CAT16_TO_XYZ" : "BRADFORD_TO_XYZ") + ");\n";
            }
        }
        if (s.clip) b += "XYZ = max(XYZ, 0.0);\n";
        b += "XYZ = gamut_mapping(XYZ, " + g(s.gamut) + ", " + clip + ");\n";
        if (kind == 0 || kind == 2) b += "LMS = mat3x3v(XYZ, XYZ_TO_BRADFORD);\n";
        else if (kind == 1) b += "LMS = mat3x3v(XYZ, XYZ_TO_CAT16);\n";
        else if (kind == 3) b += "LMS = XYZ;\n";
        else b += "v = XYZ; LMS = " + mat(s.xyzToRgb.data()) + ";\n";
        if (s.clip) b += "LMS = max(LMS, 0.0);\n";
        b += "LMS = luma_chroma(LMS, vec4(" + g(s.saturation[0]) + "," + g(s.saturation[1]) + "," + g(s.saturation[2]) + "," + g(s.saturation[3]) + "), vec4(" +
             g(s.lightness[0]) + "," + g(s.lightness[1]) + "," + g(s.lightness[2]) + "," + g(s.lightness[3]) + "), " + std::to_string(s.version) + ");\n";
        if (s.clip) b += "LMS = max(LMS, 0.0);\n";
        if (s.applyGrey) {
            b += "{ float grey_mix = max(dot(LMS, vec4(" + g(s.grey[0]) + "," + g(s.grey[1]) + "," + g(s.grey[2]) + "," + g(s.grey[3]) + ")), 0.0); p = vec4(grey_mix, grey_mix, grey_mix, pix_in.w); }\n";
        } else {
            if (kind == 0 || kind == 2) b += "XYZ = mat3x3v(LMS, BRADFORD_TO_XYZ);\n";
            else if (kind == 1) b += "XYZ = mat3x3v(LMS, CAT16_TO_XYZ);\n";
            else if (kind == 3) b += "XYZ = LMS;\n";
            else b += "v = LMS; XYZ = " + mat(s.rgbToXyz.data()) + ";\n";
            if (s.clip) b += "XYZ = max(XYZ, 0.0);\n";
            b += "v = XYZ; RGB = " + mat(s.xyzToRgb.data()) + ";\n";
            if (s.clip) b += "RGB = max(RGB, 0.0);\n";
            b += "p = vec4(RGB.xyz, pix_in.w); }\n";
        }
        if (s.applyGrey) b += "}\n";
        break;
    }
    case ChainStage::Sigmoid: {
        const std::string args = g(s.whiteTarget) + ", " + g(s.paperExposure) + ", " + g(s.filmFog) + ", " + g(s.filmPower) + ", " + g(s.paperPower);
        if (s.method == 0) {
            b += "{ float alpha = p.w; vec4 v = p; v = " + mat(s.pipeToBase.data()) + "; v = desaturate_negative(v); v = " + mat(s.baseToRendering.data()) + ";\n";
            b += "vec3 pix = v.xyz; vec3 per = vec3(loglogistic(v.x, " + args + "), loglogistic(v.y, " + args + "), loglogistic(v.z, " + args + "));\n";
            b += "int mn, mid, mx; channel_order(pix, mn, mid, mx); pix = preserve_hue(pix, per, mn, mid, mx, " + g(s.huePreservation) + ");\n";
            b += "v = vec4(pix, 0.0); v = " + mat(s.renderingToPipe.data()) + "; p = vec4(v.xyz, alpha); }\n";
        } else {
            b += "{ float alpha = p.w; vec4 i = desaturate_negative(p); float luma = (i.x + i.y + i.z)/3.0; float mapped = loglogistic(luma, " + args + ");\n";
            b += "if (luma > 1e-9) i = i * (mapped/luma); else i = vec4(mapped);\n";
            b += "float pmin = min(min(i.x, i.y), i.z), pmax = max(max(i.x, i.y), i.z); const float eps = 1e-6;\n";
            b += "float bw = (" + g(s.whiteTarget) + " - mapped)/(pmax - mapped + eps); float bb = (" + g(s.blackTarget) + " - mapped)/(pmin - mapped - eps); float border = min(bw, bb);\n";
            b += "float cvb = (mapped - pmin)/(mapped + eps); float adj = 1.0/(cvb*border + eps); float hyp = 2.0*cvb/(1.0 - cvb*cvb + eps)*adj; float z = sqrt(hyp*hyp + 1.0); float factor = hyp/(1.0 + z)*border;\n";
            b += "i = (i - mapped)*factor + mapped; p = vec4(i.xyz, alpha); }\n";
        }
        break;
    }
    }
    return b;
}
inline std::string chainBody(const std::vector<ChainStage> &stages) {
    std::string body = "ivec2 pos = ivec2(gl_GlobalInvocationID.xy); vec4 p = texelFetch(oma_input, pos, 0);\n";
    for (size_t i = 0; i < stages.size(); ++i) body += chainStage(stages[i], int(i));
    body += "color = p;";
    return body;
}
}
