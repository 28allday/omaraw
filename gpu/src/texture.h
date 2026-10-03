// SPDX-License-Identifier: GPL-3.0-or-later
// Five-pass "local contrast | fine" diffusion, adapted from darktable's
// src/iop/diffuse.c and src/common/{bspline,math}.h at
// 281f60c9957b05b9379084b272c81a5e62fa46e3.
// Copyright (C) 2009-2026 darktable developers; 2026 Oma GPU contributors.
// Keep the CPU exponent approximation, clipping and summation order: using
// a different exponential or Gaussian changes the developed photograph.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace oma::gpu::detail {
inline std::string decimal(float n) {
    std::ostringstream s; s.imbue(std::locale::classic());
    s << std::scientific << std::setprecision(9) << n; return s.str();
}
inline std::vector<float> textureRadii(float zoom) {
    constexpr float sigma = 1.0553651328015339f;
    std::vector<float> radii{sigma};
    while (radii.back() < 340.f / zoom && radii.size() < 10) {
        const float next = float(1 << radii.size()) * sigma;
        radii.push_back(std::sqrt(radii.back() * radii.back() + next * next));
    }
    return radii;
}
inline float textureExp(float x) {
    // Clamping below -100 avoids integer overflow; that range is already
    // zero in the engine's approximation.
    const int bits = std::max(0, 0x3f800000 + int(std::max(x, -100.f) * float(0x402df854 - 0x3f800000)));
    float out; std::memcpy(&out, &bits, sizeof out); return out;
}
inline std::array<float, 9> textureKernel(const float *p) {
    float dx = (p[7] - p[1]) / 2.f, dy = (p[5] - p[3]) / 2.f;
    const float magnitude = std::sqrt(dx * dx + dy * dy);
    const float c2 = textureExp(-magnitude * 100.f);
    dx = magnitude != 0 ? dx / magnitude : 1.f;
    dy = magnitude != 0 ? dy / magnitude : 0.f;
    const float a00 = dx * dx + c2 * dy * dy;
    const float a11 = c2 * dx * dx + dy * dy;
    const float cross = (c2 - 1.f) * (dx * dy) / 2.f;
    return {cross, a11, -cross, a00, -2.f * (a00 + a11), a00, -cross, a11, cross};
}
inline void cpuTexture(Image &image, float amount, float zoom, const std::atomic_bool *cancel) {
    const auto radii = textureRadii(zoom);
    const int w = image.width, h = image.height;
    const size_t count = image.rgba.size();
    std::vector<float> originalAlpha(size_t(w) * h);
    for (size_t i = 0; i < originalAlpha.size(); ++i) originalAlpha[i] = image.rgba[4*i+3];
    std::vector<std::vector<float>> hf(radii.size(), std::vector<float>(count));
    std::vector<float> vertical(count), low(count), next(count), working = image.rgba;
    constexpr float weights[] = {1.f/16, 4.f/16, 6.f/16, 4.f/16, 1.f/16};
    constexpr float isotropic[] = {.25f,.5f,.25f,.5f,-3.f,.5f,.25f,.5f,.25f};
    const auto stop = [&] { return cancel && cancel->load(); };
    for (int iteration = 0; iteration < 5; ++iteration) {
        for (size_t s = 0; s < radii.size(); ++s) {
            if (stop()) return;
            const int step = 1 << s;
            const auto &input = s ? low : working;
            for (int y = 0; y < h; ++y) {
                if (stop()) return;
                for (int x = 0; x < w; ++x) for (int c = 0; c < 3; ++c) {
                    float value = 0;
                    for (int k = -2; k <= 2; ++k)
                        value += weights[k+2] * input[(size_t(std::clamp(y+k*step,0,h-1))*w+x)*4+c];
                    vertical[(size_t(y)*w+x)*4+c] = std::max(0.f, value);
                }
            }
            for (int y = 0; y < h; ++y) {
                if (stop()) return;
                for (int x = 0; x < w; ++x) for (int c = 0; c < 3; ++c) {
                    const size_t i = (size_t(y)*w+x)*4+c;
                    float value = 0;
                    for (int k = -2; k <= 2; ++k)
                        value += weights[k+2] * vertical[(size_t(y)*w+std::clamp(x+k*step,0,w-1))*4+c];
                    next[i] = std::max(0.f, value); hf[s][i] = input[i] - next[i];
                }
            }
            low.swap(next);
        }
        for (int s = int(radii.size())-1; s >= 0; --s) {
            const int step = 1 << s;
            const float r = radii[s], real = r * zoom;
            const float norm = std::exp(-(real*real)/(170.f*170.f));
            const float k = amount / 100.f;
            const float speeds[] = {(-.15f*k)*.25f*norm, (.05f*k)*.25f*norm,
                                    (.05f*k)*.25f*norm, (-.15f*k)*.25f*norm};
            const float regularization = 99.f * (r*r) / 9.f;
            for (int y = 0; y < h; ++y) {
                if (stop()) return;
                for (int x = 0; x < w; ++x) for (int c = 0; c < 3; ++c) {
                    float lf[9], high[9];
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                        const size_t i = (size_t(std::clamp(y+dy*step,0,h-1))*w+std::clamp(x+dx*step,0,w-1))*4+c;
                        const int n = (dy+1)*3+dx+1; lf[n] = low[i]; high[n] = hf[s][i];
                    }
                    const auto first = textureKernel(lf), fourth = textureKernel(high);
                    float derivatives[4] = {}, variance = 0;
                    for (int n = 0; n < 9; ++n) {
                        derivatives[0] += first[n] * lf[n]; derivatives[1] += isotropic[n] * lf[n];
                        derivatives[2] += isotropic[n] * high[n]; derivatives[3] += fourth[n] * high[n];
                        variance += high[n] * high[n];
                    }
                    float acc = 0;
                    for (int n = 0; n < 4; ++n) acc += derivatives[n] * speeds[n];
                    next[(size_t(y)*w+x)*4+c] = std::max(0.f, (high[4] + acc/(1.f+variance*regularization)) + lf[4]);
                }
            }
            low.swap(next);
        }
        working.swap(low);
    }
    image.rgba.swap(working);
    for (size_t i = 0; i < originalAlpha.size(); ++i) image.rgba[4*i+3] = originalAlpha[i];
}
inline std::string textureBlur(int step, bool horizontal) {
    std::string body = "ivec2 pos=ivec2(gl_GlobalInvocationID.xy); ivec2 sz=textureSize(oma_input,0); precise vec3 sum=vec3(0);\n";
    constexpr float weights[] = {1.f/16,4.f/16,6.f/16,4.f/16,1.f/16};
    for (int k = -2; k <= 2; ++k)
        body += "sum += " + decimal(weights[k+2]) + "*texelFetch(oma_input,clamp(pos+ivec2(" + std::to_string(horizontal ? k*step : 0)
            + "," + std::to_string(horizontal ? 0 : k*step) + "),ivec2(0),sz-1),0).rgb;\n";
    return body + "color=vec4(max(sum,vec3(0)),1);";
}
inline std::string textureSolve(float amount, float zoom, float radius, int step) {
    const float real = radius * zoom, norm = std::exp(-(real*real)/(170.f*170.f)), k = amount/100.f;
    // Two isotropic and two isophote kernels. The exponent uses the same
    // float-to-integer approximation as the CPU engine, not GLSL exp().
    std::string body = R"(
ivec2 pos=ivec2(gl_GlobalInvocationID.xy); ivec2 sz=textureSize(oma_input,0);
precise vec3 lf[9], hf[9];
for(int y=0;y<3;++y) for(int x=0;x<3;++x) {
    ivec2 p=clamp(pos+(ivec2(x,y)-1)*STEP,ivec2(0),sz-1);
    lf[y*3+x]=texelFetch(oma_input,p,0).rgb; hf[y*3+x]=texelFetch(oma_foreground,p,0).rgb;
}
precise vec3 kernels[18];
for(int t=0;t<2;++t) {
    precise vec3 dx=((t==0?lf[7]:hf[7])-(t==0?lf[1]:hf[1]))/2.0;
    precise vec3 dy=((t==0?lf[5]:hf[5])-(t==0?lf[3]:hf[3]))/2.0;
    precise vec3 magnitude=sqrt(dx*dx+dy*dy);
    precise vec3 exponent=max(-magnitude*100.0,vec3(-100.0));
    ivec3 bits=max(ivec3(0),ivec3(1065353216)+ivec3(exponent*float(0x402df854-0x3f800000)));
    vec3 c2=intBitsToFloat(bits);
    for(int c=0;c<3;++c) { dx[c]=magnitude[c]!=0.0?dx[c]/magnitude[c]:1.0; dy[c]=magnitude[c]!=0.0?dy[c]/magnitude[c]:0.0; }
    precise vec3 a00=dx*dx+c2*dy*dy, a11=c2*dx*dx+dy*dy;
    precise vec3 cross=(c2-1.0)*(dx*dy)/2.0;
    int n=t*9;
    kernels[n]=cross; kernels[n+1]=a11; kernels[n+2]=-cross;
    kernels[n+3]=a00; kernels[n+4]=-2.0*(a00+a11); kernels[n+5]=a00;
    kernels[n+6]=-cross; kernels[n+7]=a11; kernels[n+8]=cross;
}
const float iso[9]=float[](.25,.5,.25,.5,-3.0,.5,.25,.5,.25);
precise vec3 d0=vec3(0),d1=vec3(0),d2=vec3(0),d3=vec3(0),variance=vec3(0);
for(int n=0;n<9;++n) {
    d0+=kernels[n]*lf[n]; d1+=iso[n]*lf[n]; d2+=iso[n]*hf[n]; d3+=kernels[9+n]*hf[n]; variance+=hf[n]*hf[n];
}
)";
    const auto marker = body.find("STEP"); body.replace(marker, 4, std::to_string(step));
    body += "precise vec3 acc=vec3(0); acc+=d0*"+decimal((-.15f*k)*.25f*norm)+"; acc+=d1*"+decimal((.05f*k)*.25f*norm)
        +"; acc+=d2*"+decimal((.05f*k)*.25f*norm)+"; acc+=d3*"+decimal((-.15f*k)*.25f*norm)+";\n";
    return body + "precise vec3 result=(hf[4]+acc/(vec3(1)+variance*"+decimal(99.f*(radius*radius)/9.f)+"))+lf[4]; color=vec4(max(result,vec3(0)),1);";
}
}
