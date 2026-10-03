#pragma once
#include <QVariant>
#include <QStringList>
#include <cmath>

// Curated controls for the additional native modules. Values remain in engine
// units in history/presets; factor/offset only change their presentation.
namespace DevelopTools {
inline QVariantList parameters() {
    QVariantList out;
    auto add = [&](const char *group, const char *op, const QString &field, const QString &label,
                   double lo, double hi, int decimals = 2, const QString &suffix = QString(),
                   double factor = 1, double offset = 0, int channel = -1) {
        out << QVariantMap{{"group", group}, {"op", op}, {"field", field}, {"label", label},
            {"uiMin", lo}, {"uiMax", hi}, {"decimals", decimals}, {"suffix", suffix},
            {"factor", factor}, {"offset", offset}, {"channel", channel}, {"advancedTool", true}};
    };
    const QStringList tones = {"noise", "ultra_deep_blacks", "deep_blacks", "blacks", "shadows", "midtones", "highlights", "whites", "speculars"};
    for (int i = 0; i < tones.size(); ++i)
        add("Basic", "toneequal", tones[i], QString("Zone %1 EV").arg(i - 8), -2, 2, 2, " EV");
    add("Basic", "toneequal", "exposure_boost", "Mask exposure", -16, 16, 2, " EV");
    add("Basic", "toneequal", "contrast_boost", "Mask contrast", -16, 16);
    add("Basic", "toneequal", "details", "Preserve details", 0, 6);
    add("Basic", "toneequal", "blending", "Smoothing diameter", 0.01, 100, 2, " %");
    add("Basic", "toneequal", "feathering", "Edge refinement", 0.01, 100, 2);
    add("Basic", "toneequal", "iterations", "Filter passes", 1, 10, 0);
    add("Detail", "contrastntexture", "gain_local_contrast", "Amount", 0, 5, 0, " %", 100, -100);
    add("Detail", "contrastntexture", "detail_level", "Detail level", 0, 15);
    add("Detail", "contrastntexture", "edge_protection", "Edge protection", -10, 10);
    add("Detail", "contrastntexture", "noise_bias", "Noise protection", 0, 1, 3);
    add("Detail", "contrastntexture", "filter_iterations", "Filter passes", 1, 10, 0);
    add("Detail", "denoiseprofile", "wavelet_color_mode", "Colour mode", 0, 1);
    add("Detail", "denoiseprofile", "shadows", "Preserve shadows", 0, 1.8);
    add("Detail", "denoiseprofile", "central_pixel_weight", "Preserve detail", 0, 10);
    add("Detail", "denoiseprofile", "radius", "Patch size", 0, 12, 1);
    add("Detail", "denoiseprofile", "nbhood", "Search radius", 1, 30, 0);
    add("Detail", "denoiseprofile", "scattering", "Scattering", 0, 20);
    add("Detail", "denoiseprofile", "overshooting", "Auto adjustment", 0.01, 5);
    for (int ch = 0; ch < 6; ++ch) for (int b = 0; b < 7; ++b)
        add("Detail", "denoiseprofile", QString("y[%1][%2]").arg(ch).arg(b),
            b == 0 ? "Coarsest" : b == 6 ? "Finest" : QString("Detail %1").arg(b + 1), 0, 1, 2, "", 1, 0, ch);
    add("Detail", "atrous", "mix", "Mix", -2, 2, 0, " %", 100);
    for (int ch = 0; ch < 5; ++ch) for (int b = 0; b < 6; ++b)
        add("Detail", "atrous", QString("y[%1][%2]").arg(ch).arg(b),
            b == 0 ? "Coarsest" : b == 5 ? "Finest" : QString("Detail %1").arg(b + 1), 0, 1, 2, "", 1, 0, ch);
    for (const auto &colour : {QString("red"), QString("green"), QString("blue")}) {
        // The native editor's practical hue range. Full half-turns can put
        // all three primaries on one line, which has no valid RGB matrix.
        add("Color", "primaries", colour + "_hue", colour + " hue", -M_PI / 9, M_PI / 9, 1, "°", 180 / M_PI);
        add("Color", "primaries", colour + "_purity", colour + " purity", 0.01, 5, 0, " %", 100);
    }
    add("Color", "primaries", "achromatic_tint_hue", "Tint hue", -M_PI, M_PI, 1, "°", 180 / M_PI);
    add("Color", "primaries", "achromatic_tint_purity", "Tint amount", 0, 0.99, 0, " %", 100);
    add("Curve", "rgblevels", "autoscale", "Channels", 0, 1);
    add("Curve", "rgblevels", "preserve_colors", "Preserve colours", 0, 8);
    for (int ch = 0; ch < 3; ++ch) for (int b = 0; b < 3; ++b)
        add("Curve", "rgblevels", QString("levels[%1][%2]").arg(ch).arg(b),
            QStringList{"Black point", "Midpoint", "White point"}[b], 0, 1, 4, "", 1, 0, ch);
    add("Color", "negadoctor", "film_stock", "Film stock", 0, 1);
    for (int ch = 0; ch < 3; ++ch) {
        const QString colour = QStringList{"Red", "Green", "Blue"}[ch];
        add("Color", "negadoctor", QString("Dmin[%1]").arg(ch), colour + " film base", 0.00001, 1.5, 4);
        add("Color", "negadoctor", QString("wb_low[%1]").arg(ch), colour + " shadows", 0.25, 2, 3);
        add("Color", "negadoctor", QString("wb_high[%1]").arg(ch), colour + " highlights", 0.25, 2, 3);
    }
    add("Color", "negadoctor", "D_max", "Film density range", 0.1, 6, 3);
    add("Color", "negadoctor", "offset", "Scan exposure bias", -1, 1, 3);
    add("Color", "negadoctor", "black", "Paper black", -0.5, 0.5, 3);
    add("Color", "negadoctor", "gamma", "Paper grade", 1, 8);
    add("Color", "negadoctor", "soft_clip", "Paper gloss", 0.0001, 1, 3);
    add("Color", "negadoctor", "exposure", "Print exposure", 0.5, 2, 3);
    add("Retouch", "retouch", "num_scales", "Frequency layers", 0, 15, 0);
    add("Retouch", "retouch", "curr_scale", "Selected layer", 0, 16, 0);
    return out;
}
}
