// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ExposureMeter {
struct Result { bool valid = false; double ev = 0, mean = 0, highlight = 0; };

// Input is unbounded, linear Rec.709 at exposure parameter zero, with camera
// exposure compensation disabled. Never pass display pixels or a tone map.
inline Result measure(const QImage &image, double black, double bias, double base, double headroom = .98) {
    if (image.isNull() || image.format() != QImage::Format_RGBA32FPx4
        || !std::isfinite(black) || !std::isfinite(bias) || !std::isfinite(base)
        || !std::isfinite(headroom) || headroom <= 0 || black >= 1) return {};
    std::vector<double> luminance, peaks;
    luminance.reserve(image.width() * image.height()); peaks.reserve(luminance.capacity());
    for (int y = 0; y < image.height(); ++y) {
        const float *p = reinterpret_cast<const float *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x, p += 4) {
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || !(p[3] > 0)) continue;
            luminance.push_back(std::max(0., .2126*p[0] + .7152*p[1] + .0722*p[2]));
            peaks.push_back(std::max({0., double(p[0]), double(p[1]), double(p[2])}));
        }
    }
    if (luminance.empty()) return {};
    std::sort(luminance.begin(), luminance.end()); std::sort(peaks.begin(), peaks.end());
    // Trim 1% at each end for the grey-world estimate. Limit gain by the
    // 99.5th channel percentile: small lights may clip, broad highlights must
    // retain headroom. This is a conservative starting point, not a subject meter.
    const size_t trim = luminance.size() / 100;
    double sum = 0;
    for (size_t i = trim; i < luminance.size() - trim; ++i) sum += luminance[i];
    const double mean = sum / (luminance.size() - 2*trim);
    const double highlight = peaks[size_t(std::ceil(.995 * peaks.size())) - 1];
    if (!(mean > 1e-6) || !(highlight > 1e-6)) return {};
    const double gain = std::min(.18 / mean, headroom / highlight);
    // Native exposure is (input-black)/(2^(-EV-bias)-black), not simply
    // input*2^EV when black is nonzero. Solve that equation, then remove the
    // camera compensation already applied by the native exposure module.
    const double white = black + (1-black) / gain;
    if (!(white > 0) || !std::isfinite(white)) return {};
    return {true, std::clamp(-std::log2(white) - bias, base-3., base+3.), mean, highlight};
}
}
