#include "curatedpresets.h"
#include <QCoreApplication>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("CuratedPresets", text); }
QVariantMap p(const char *op, const char *field, double value) {
    return {{"op", op}, {"field", field}, {"value", value}};
}
QVariantMap enabled(const char *op, bool on) { return {{"op", op}, {"enabled", on}}; }

// Every recipe owns this same small set of creative controls. Replacing their
// values, including neutral ones, makes browsing recipes independent of the
// previous recipe. Exposure, WB, curves, detail, geometry and masks stay intact.
// Values are engine units, not the percentages displayed by the UI.
QVariantList recipe(int stock, const QVariantList &changes) {
    QVariantList values{
        p("omarawprint", "stock", stock < 0 ? 23 : stock), p("omarawprint", "strength", 100),
        p("omarawprint", "white", 0), p("omarawprint", "paper", 2),
        p("omarawprint", "trim_r", 0), p("omarawprint", "trim_g", 0), p("omarawprint", "trim_b", 0),
        p("omarawprint", "gain_r", 1), p("omarawprint", "gain_g", 1), p("omarawprint", "gain_b", 1),
        // A partial-strength print blends with the same sigmoid baseline,
        // even when the previous recipe used a different tone mapper.
        p("omarawprint", "base_tone", 1),
        QVariantMap{{"op", "omarawprint"}, {"field", "profile"}, {"text", ""}},
        p("sigmoid", "middle_grey_contrast", 1.5), p("sigmoid", "contrast_skewness", 0),
        p("colorbalancergb", "saturation_global", 0), p("colorbalancergb", "chroma_global", 0),
        p("colorbalancergb", "vibrance", 0), p("colorbalancergb", "contrast", 0),
        p("omarawgrade", "lift_H", 0), p("omarawgrade", "lift_C", 0), p("omarawgrade", "lift_Y", 0),
        p("omarawgrade", "gamma_H", 0), p("omarawgrade", "gamma_C", 0), p("omarawgrade", "gamma_Y", 1),
        p("omarawgrade", "gain_H", 0), p("omarawgrade", "gain_C", 0), p("omarawgrade", "gain_Y", 1),
        p("omarawgrade", "offset_H", 0), p("omarawgrade", "offset_C", 0), p("omarawgrade", "offset_Y", 0),
        p("omarawgrade", "lum_mix", 100),
        p("grain", "strength", 0), p("grain", "scale", 3), p("grain", "midtones_bias", 80),
        p("omarawhalation", "strength", 0), p("omarawhalation", "scatter", 25),
        p("omarawhalation", "dye", 40), p("omarawhalation", "boost", 60), p("omarawhalation", "threshold", 65),
        p("bloom", "strength", 0), p("bloom", "size", 15), p("bloom", "threshold", 90),
        p("vignette", "brightness", 0), p("vignette", "saturation", 0),
        p("vignette", "scale", 75), p("vignette", "falloff_scale", 60), p("vignette", "shape", 1)
    };
    values << enabled("monochrome", false) << p("monochrome", "size", 2) << p("monochrome", "highlights", 0);
    for (const char *band : {"mix_red", "mix_orange", "mix_yellow", "mix_green", "mix_aqua", "mix_blue", "mix_purple", "mix_magenta"})
        values << p("monochrome", band, 0);
    for (const auto &change : changes) {
        const auto row = change.toMap();
        for (auto &value : values) {
            const auto current = value.toMap();
            if (current.value("op") == row.value("op") && current.value("field") == row.value("field")) {
                value = change;
                break;
            }
        }
    }
    // Keep effects with no contribution off; do not revive a hidden default.
    const auto nonzero = [&](const char *op, const char *field) {
        for (const auto &value : values) {
            const auto row = value.toMap();
            if (row.value("op") == op && row.value("field") == field) return row.value("value").toDouble() != 0;
        }
        return false;
    };
    values << enabled("omarawprint", stock >= 0) << enabled("sigmoid", stock < 0)
           << enabled("filmicrgb", false) << enabled("basecurve", false) << enabled("colorbalancergb", true)
           << enabled("omarawgrade", nonzero("omarawgrade", "lift_C") || nonzero("omarawgrade", "gain_C"))
           << enabled("grain", nonzero("grain", "strength"))
           << enabled("omarawhalation", nonzero("omarawhalation", "strength"))
           << enabled("bloom", nonzero("bloom", "strength"))
           << enabled("vignette", nonzero("vignette", "brightness"));
    return values;
}
}

QVariantList CuratedPresets::presets() {
    QVariantList out;
    // Negative IDs distinguish built-ins from catalogue presets. Names also
    // identify favourites and import rules.
    const auto add = [&](int id, const char *name, const char *category, const char *description,
                         const QStringList &tags, int stock, const QVariantList &changes) {
        out << QVariantMap{{"id", id}, {"name", tr(name)}, {"category", tr(category)},
                          {"description", tr(description)}, {"tags", tags}, {"builtin", true},
                          {"values", recipe(stock, changes)}};
    };
    add(-101, "Portrait 160 · Gentle", "Film", "Soft colour and fine grain for daylight portraits.", {"portrait", "subtle", "daylight"}, 8,
        {p("grain", "strength", 8), p("grain", "scale", 2)});
    add(-102, "Portrait 400 · Everyday", "Film", "A balanced colour-negative look with restrained grain.", {"travel", "portrait", "natural"}, 9,
        {p("grain", "strength", 14), p("grain", "scale", 3)});
    add(-103, "Portrait 800 · Evening", "Film", "Soft colour, more visible grain and a little highlight halation.", {"night", "grain", "low light"}, 10,
        {p("grain", "strength", 23), p("grain", "scale", 4.5), p("omarawhalation", "strength", 8)});
    add(-104, "Fine Grain 100 · Colour", "Film", "Rich colour and fine grain for bright outdoor scenes.", {"landscape", "travel", "vivid"}, 11,
        {p("grain", "strength", 6), p("grain", "scale", 1.8)});
    add(-105, "Warm 200 · Sunshine", "Film", "Warm print colour with modest grain for everyday photographs.", {"warm", "travel", "summer"}, 12,
        {p("grain", "strength", 15), p("grain", "scale", 3.5), p("omarawprint", "white", 1)});
    add(-106, "Vivid 400 · Weekend", "Film", "Lively colour with visible grain and gently shaded corners.", {"street", "travel", "colour negative"}, 13,
        {p("grain", "strength", 20), p("grain", "scale", 4), p("vignette", "brightness", -0.07)});
    add(-107, "Portrait 400 · Soft Fade", "Film", "A gentler Portrait print with reduced density contrast and quiet colour.", {"portrait", "soft", "muted"}, 9,
        {p("omarawprint", "strength", 85), p("omarawprint", "gain_r", 0.9), p("omarawprint", "gain_g", 0.9), p("omarawprint", "gain_b", 0.9),
         p("colorbalancergb", "chroma_global", -0.08), p("grain", "strength", 12)});
    add(-108, "Warm 200 · Nostalgia", "Film", "Warm, softened colour with coarser grain and a trace of glow.", {"vintage", "warm", "grain"}, 12,
        {p("omarawprint", "white", 1), p("omarawprint", "strength", 85), p("colorbalancergb", "chroma_global", -0.12),
         p("grain", "strength", 25), p("grain", "scale", 5), p("bloom", "strength", 5), p("vignette", "brightness", -0.1)});

    add(-201, "Portrait · Clean", "Portrait", "Gentle contrast and restrained colour without grain or glow.", {"people", "skin", "natural", "studio"}, -1,
        {p("sigmoid", "middle_grey_contrast", 1.35), p("colorbalancergb", "saturation_global", -0.03), p("colorbalancergb", "vibrance", 0.04)});
    add(-202, "Portrait · Soft Light", "Portrait", "A light touch of Portrait with softened contrast and subtle highlight glow.", {"people", "skin", "soft", "wedding"}, 8,
        {p("omarawprint", "strength", 55), p("sigmoid", "middle_grey_contrast", 1.3), p("bloom", "strength", 4)});
    add(-203, "Portrait · Warm Editorial", "Portrait", "A warm Portrait print with fine grain and softly darkened corners.", {"people", "warm", "fashion", "editorial"}, 9,
        {p("omarawprint", "strength", 75), p("omarawprint", "white", 1), p("grain", "strength", 7), p("vignette", "brightness", -0.08)});
    add(-204, "Portrait · Quiet Contrast", "Portrait", "Deeper tonal separation and slightly muted colour for directional light.", {"people", "moody", "studio", "low key"}, -1,
        {p("sigmoid", "middle_grey_contrast", 1.75), p("sigmoid", "contrast_skewness", 0.08),
         p("colorbalancergb", "chroma_global", -0.08), p("vignette", "brightness", -0.12)});

    add(-301, "Landscape · Natural", "Landscape", "Crisp midtones and a modest colour lift for outdoor scenes.", {"outdoors", "travel", "natural", "daylight"}, -1,
        {p("sigmoid", "middle_grey_contrast", 1.65), p("colorbalancergb", "vibrance", 0.1)});
    add(-302, "Landscape · Vivid", "Landscape", "Stronger contrast and richer colour for bold scenery.", {"outdoors", "vivid", "mountains", "colour"}, -1,
        {p("sigmoid", "middle_grey_contrast", 1.85), p("colorbalancergb", "saturation_global", 0.08), p("colorbalancergb", "vibrance", 0.16)});
    add(-303, "Landscape · Muted Earth", "Landscape", "Quiet colour and gently warm shadows for woodland and overcast light.", {"outdoors", "woodland", "earthy", "muted"}, -1,
        {p("sigmoid", "middle_grey_contrast", 1.4), p("colorbalancergb", "chroma_global", -0.18),
         p("omarawgrade", "lift_H", 45), p("omarawgrade", "lift_C", 0.012)});
    add(-304, "Landscape · Coastal", "Landscape", "Cool shadows and lightly warm highlights for sea and sky.", {"outdoors", "sea", "blue", "split tone"}, -1,
        {p("sigmoid", "middle_grey_contrast", 1.55), p("colorbalancergb", "vibrance", 0.06),
         p("omarawgrade", "lift_H", 220), p("omarawgrade", "lift_C", 0.017),
         p("omarawgrade", "gain_H", 55), p("omarawgrade", "gain_C", 0.025)});

    add(-401, "Cinema · Classic 2383", "Cinematic", "A warm release print with fine grain and restrained halation.", {"cinema", "warm", "film"}, 23,
        {p("grain", "strength", 12), p("omarawhalation", "strength", 7), p("vignette", "brightness", -0.06)});
    add(-402, "Cinema · Deep 2393", "Cinematic", "A punchier release print with deep shadows and visible grain.", {"cinema", "dramatic", "contrast"}, 15,
        {p("grain", "strength", 18), p("grain", "scale", 3.5), p("omarawhalation", "strength", 6), p("vignette", "brightness", -0.1)});
    add(-403, "Cinema · Cool Print", "Cinematic", "A cooler cinema print with subtle grain and cool shadow colour.", {"cinema", "cool", "print"}, 17,
        {p("grain", "strength", 10), p("omarawgrade", "lift_H", 220), p("omarawgrade", "lift_C", 0.012)});
    add(-404, "Cinema · Soft Evening", "Cinematic", "A gentle Fuji print with soft highlight glow and warm highlights.", {"fuji", "cinema", "soft", "glow"}, 16,
        {p("omarawprint", "strength", 85), p("grain", "strength", 12), p("omarawhalation", "strength", 10),
         p("bloom", "strength", 5), p("omarawgrade", "gain_H", 50), p("omarawgrade", "gain_C", 0.025)});

    add(-501, "Mono · Fine Art", "Black & white", "Neutral black and white with clear midtones and fine grain.", {"monochrome", "bw", "architecture", "fine art"}, -1,
        {enabled("monochrome", true),
         p("sigmoid", "middle_grey_contrast", 1.65), p("grain", "strength", 7), p("grain", "scale", 2)});
    add(-502, "Mono · Soft Silver", "Black & white", "Gentle black and white with softer contrast for portraits and mist.", {"monochrome", "bw", "portrait", "soft"}, -1,
        {enabled("monochrome", true),
         p("sigmoid", "middle_grey_contrast", 1.25), p("sigmoid", "contrast_skewness", -0.1), p("grain", "strength", 10)});
    add(-503, "Mono · Street", "Black & white", "Strong tonal separation, coarse grain and shaded corners.", {"monochrome", "bw", "street", "grain", "documentary"}, -1,
        {enabled("monochrome", true),
         p("sigmoid", "middle_grey_contrast", 1.95), p("grain", "strength", 28), p("grain", "scale", 5), p("vignette", "brightness", -0.15)});
    add(-504, "Mono · Silver Print", "Black & white", "The Cinema Mono 2302 monochrome print with film grain and light halation.", {"monochrome", "bw", "cinema", "2302"}, 21,
        {p("grain", "strength", 20), p("grain", "scale", 3.5), p("omarawhalation", "strength", 5)});
    return out;
}
