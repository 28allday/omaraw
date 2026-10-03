#pragma once
#include <QVariantList>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

// Tone regions: the parametric curve's four regions (highlights, lights,
// darks, shadows), shared by the Develop sliders and by presets brought in
// from the source editor's parametric curve.
namespace ToneRegions {
// The regions' centres, and how far ±100 moves them: ±0.10 keeps a monotone
// spline monotone.
inline constexpr double kX[4] = {0.875, 0.625, 0.375, 0.125};
inline constexpr double kScale = 0.10;

// Straight, plus a smooth bump per region: each is zero at the other
// regions' centres (so a slider reads back alone) and tapers to nothing
// at black and white. A curve through six offset nodes bent past
// straight beside each moved one, so raising Darks darkened the deepest
// shadows and the lights a little; the sum of bumps never does.
// Sampled every 1/16, the regions' centres among the samples.
inline void curve(const double sliders[4], QVariantList &xs, QVariantList &ys) {
    double offset[4];
    for (int r = 0; r < 4; ++r) offset[r] = sliders[r] / 100.0 * kScale;
    xs.clear(); ys.clear();
    for (int i = 0; i <= 16; ++i) {
        const double x = i / 16.0;
        double lift = 0;
        for (int r = 0; r < 4; ++r) {
            const double d = std::abs(x - kX[r]);
            // Squared, so the bump starts flat: the engine's monotone curve
            // then has no slope change to overshoot beside it.
            if (d < 0.25) { const double b = 0.5 * (1.0 + std::cos(M_PI * d / 0.25)); lift += offset[r] * b * b; }
        }
        const double taper = std::min({1.0, x / 0.125, (1.0 - x) / 0.125});
        xs << x; ys << qBound(0.0, x + taper * lift, 1.0);
    }
}
}
