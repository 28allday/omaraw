#pragma once
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

// Lab colour as sliders. Each of Lab's two colour axes (a: green to magenta,
// b: blue to yellow) is a curve whose centre is neutral grey. A slider never
// asks anyone to draw one: it places seven points, the ends pinned, the five
// in the middle carrying the whole adjustment:
//
//   (0, 0)  (0.35, m - 0.15 gN)  (0.46, m - 0.04 gN)  (0.5, m)  (0.54, m + 0.04 gP)  (0.65, m + 0.15 gP)  (1, 1)      m = 0.5 + c
//
// gN and gP are how steeply the curve leaves the centre towards the axis's
// negative and positive colours (1 = untouched), so one side can be boosted
// and the other left alone; c moves the centre, shifting a cast. The two
// points hugging the centre keep each side's slope on its own side: without
// them the spline shares it, and boosting greens leaked into magentas. The inner
// points sit 38 Lab units out, where real pictures keep most of their colour,
// and the pinned ends mean strong colours are pushed less than muted ones and
// nothing is ever clipped. All zeros is the straight line.
namespace LabColour {
constexpr double kReach = 0.15;   // inner points, as a fraction of the axis
constexpr double kNear = 0.04;    // the pair beside the centre: 10 Lab units
constexpr double kTint = 0.08;    // centre shift at ±100: 20 Lab units
constexpr double kBoost = 1.5;    // +100 leaves the centre 2.5 times as steep
constexpr double kMute = 0.75;    // −100 leaves it a quarter as steep

inline double gain(double value) { value = std::clamp(value, -100.0, 100.0); return 1.0 + (value >= 0 ? kBoost : kMute) * value / 100.0; }
inline double valueOf(double gain) { return std::clamp((gain - 1.0) / (gain >= 1.0 ? kBoost : kMute) * 100.0, -100.0, 100.0); }

struct Axis { double negative = 0, positive = 0, shift = 0; bool custom = false; };

inline void curve(const Axis &axis, QVariantList &xs, QVariantList &ys) {
    const double c = std::clamp(axis.shift, -100.0, 100.0) / 100.0 * kTint;
    const double m = 0.5 + c, gN = gain(axis.negative), gP = gain(axis.positive);
    xs = {0.0, 0.5 - kReach, 0.5 - kNear, 0.5, 0.5 + kNear, 0.5 + kReach, 1.0};
    ys = {0.0, m - kReach * gN, m - kNear * gN, m, m + kNear * gP, m + kReach * gP, 1.0};
}

// What the sliders read for a curve: exact for one they placed (and for the
// untouched straight line); `custom` for a curve shaped by hand.
inline Axis read(const QVariantList &xs, const QVariantList &ys) {
    Axis axis;
    const int n = int(std::min(xs.size(), ys.size()));
    bool straight = n >= 2;
    for (int i = 0; i < n; ++i) if (std::fabs(xs[i].toDouble() - ys[i].toDouble()) > 1e-5) straight = false;
    if (straight) return axis;
    const double layout[7] = {0.0, 0.5 - kReach, 0.5 - kNear, 0.5, 0.5 + kNear, 0.5 + kReach, 1.0};
    axis.custom = n != 7;
    for (int i = 0; i < 7 && !axis.custom; ++i) if (std::fabs(xs[i].toDouble() - layout[i]) > 1e-4) axis.custom = true;
    if (!axis.custom && (std::fabs(ys[0].toDouble()) > 1e-4 || std::fabs(ys[6].toDouble() - 1.0) > 1e-4)) axis.custom = true;
    // The pair beside the centre must lie on the lines the outer pair define,
    // or somebody has moved a point by hand.
    if (!axis.custom) {
        const double m = ys[3].toDouble();
        if (std::fabs((m - ys[2].toDouble()) / kNear - (m - ys[1].toDouble()) / kReach) > 1e-3
            || std::fabs((ys[4].toDouble() - m) / kNear - (ys[5].toDouble() - m) / kReach) > 1e-3) axis.custom = true;
    }
    if (axis.custom) return axis;
    const double round = 100.0; // two decimals: the sliders show whole numbers
    axis.shift = std::round((ys[3].toDouble() - 0.5) / kTint * 100.0 * round) / round;
    axis.negative = std::round(valueOf((ys[3].toDouble() - ys[1].toDouble()) / kReach) * round) / round;
    axis.positive = std::round(valueOf((ys[5].toDouble() - ys[3].toDouble()) / kReach) * round) / round;
    return axis;
}

// The four family totals split into one shared "separation" and what each
// family adds to it. While a session's own separation still explains the
// totals it is kept, so a slider never jumps under the pointer; a photo just
// opened takes the value most families share (the smallest when none do).
inline QVariantMap sliders(const Axis &a, const Axis &b, double *master, bool *masterKnown) {
    const double totals[4] = {a.negative, a.positive, b.negative, b.positive};
    bool keep = *masterKnown;
    for (double t : totals) if (std::fabs(t - *master) > 100.0 + 1e-6) keep = false;
    if (!keep) {
        double best = totals[0]; int bestCount = 0;
        for (double candidate : totals) {
            int count = 0;
            for (double t : totals) if (std::fabs(t - candidate) < 0.005) ++count;
            if (count > bestCount || (count == bestCount && candidate < best)) { best = candidate; bestCount = count; }
        }
        if (bestCount < 2) best = *std::min_element(totals, totals + 4);
        *master = best; *masterKnown = true;
    }
    return {{QStringLiteral("separation"), *master},
            {QStringLiteral("greens"), totals[0] - *master}, {QStringLiteral("magentas"), totals[1] - *master},
            {QStringLiteral("blues"), totals[2] - *master}, {QStringLiteral("yellows"), totals[3] - *master},
            {QStringLiteral("tintA"), a.shift}, {QStringLiteral("tintB"), b.shift},
            {QStringLiteral("custom"), a.custom || b.custom}};
}
}
