#pragma once
#include <algorithm>
#include <cmath>

// A circular hue selection stored in the engine's existing ascending
// trapezoid. A band crossing zero is the inverse of the gap opposite it.
namespace ColourRange {
struct Band { bool inverse; double p0, p1, p2, p3; };
inline Band hue(double centre, double width, double softness) {
    centre -= std::floor(centre);
    const double half = std::clamp(width / 2, 0.001, 0.5);
    const double fade = std::clamp(softness, 0.0, 0.5 - half);
    if (half >= 0.5) return {false, 0, 0, 1, 1};
    const auto turn = [](double x) { return x - std::floor(x); };
    if (centre-half < 0 || centre+half > 1) {
        const double a = turn(centre+half), b = turn(centre-half);
        const double middle = (a+b)/2;
        return {true, a, std::min(a+fade, middle), std::max(middle, b-fade), b};
    }
    return {false, std::max(0.0, centre-half-fade), centre-half, centre+half, std::min(1.0, centre+half+fade)};
}
// A brightness band for a luminosity mask: fully selected from `from` to
// `to`, fading over `softness` either side. An end at 0 or 1 stays hard, so
// the deepest shadows and the brightest highlights are always included.
inline Band luminance(double from, double to, double softness) {
    from = std::clamp(from, 0.0, 1.0); to = std::clamp(to, from, 1.0);
    const double fade = std::clamp(softness, 0.0, 1.0);
    return {false, from <= 0 ? 0.0 : std::max(0.0, from - fade), from, to, to >= 1 ? 1.0 : std::min(1.0, to + fade)};
}
// Luminosity presets in the range's own units: scene light under a 2.2
// curve, so mid grey sits near 0.46.
struct LuminosityPreset { const char *key; double from, to, softness; };
inline constexpr LuminosityPreset luminosityPresets[] = {
    {"highlights", 0.65, 1.0, 0.15}, {"midtones", 0.35, 0.60, 0.15}, {"shadows", 0.0, 0.30, 0.15},
};
}
