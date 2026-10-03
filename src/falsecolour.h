#pragma once
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QVariantList>
#include <QVariantMap>
#include <array>

// False colour: the picture repainted by how bright each part of it is, so
// exposure can be read off the photograph itself. Brightness is the luma of
// the sRGB render (what the waveform plots), in per cent. The bands that
// matter are given a colour; the ranges between them stay a black-and-white
// copy of the picture, so it remains readable. Lost highlights and shadows
// use the clipping indicators' own tests: any channel at the top, every
// channel at the bottom.
namespace FalseColour {
struct Band {
    const char *name;     // what a photographer calls it
    int from, to;         // luma, per cent; `to` exclusive (the last band runs to 100)
    QRgb colour;          // 0 = leave the picture's own grey there
};
// Middle grey (18 % reflectance) sits at 46 % of the sRGB signal; light skin
// about two thirds of a stop to a stop above it.
inline constexpr std::array<Band, 11> kBands{{
    {QT_TRANSLATE_NOOP("FalseColour", "Lost shadows"), 0, 0, 0xff8a3ffc},
    {QT_TRANSLATE_NOOP("FalseColour", "Deep shadows"), 0, 8, 0xff2f5fd0},
    {QT_TRANSLATE_NOOP("FalseColour", "Shadows"), 8, 20, 0xff2ba3b8},
    {QT_TRANSLATE_NOOP("FalseColour", "Dark tones"), 20, 42, 0},
    {QT_TRANSLATE_NOOP("FalseColour", "Middle grey"), 42, 51, 0xff3fb950},
    {QT_TRANSLATE_NOOP("FalseColour", "Midtones"), 51, 58, 0},
    {QT_TRANSLATE_NOOP("FalseColour", "Light skin"), 58, 66, 0xfff08fb0},
    {QT_TRANSLATE_NOOP("FalseColour", "Light tones"), 66, 80, 0},
    {QT_TRANSLATE_NOOP("FalseColour", "Highlights"), 80, 92, 0xfff2d33c},
    {QT_TRANSLATE_NOOP("FalseColour", "Near white"), 92, 101, 0xfff08a24},
    {QT_TRANSLATE_NOOP("FalseColour", "Lost highlights"), 100, 100, 0xffe5322d},
}};
constexpr int kLostShadows = 0, kLostHighlights = 10;

// 8-bit luma, as the waveform computes it.
inline int luma(QRgb c) { return (qRed(c) * 2126 + qGreen(c) * 7152 + qBlue(c) * 722 + 5000) / 10000; }

inline int bandOf(QRgb c) {
    const int mx = qMax(qRed(c), qMax(qGreen(c), qBlue(c)));
    if (mx >= 254) return kLostHighlights;
    if (mx <= 2) return kLostShadows;
    const int percent = luma(c) * 100 / 255;
    for (int i = 1; i < kLostHighlights; ++i) if (percent < kBands[size_t(i)].to) return i;
    return kLostHighlights - 1;
}

// The band of every 8-bit luma, for pixels that are not lost either way.
inline const std::array<quint8, 256> &lumaBands() {
    static const std::array<quint8, 256> table = [] {
        std::array<quint8, 256> t{};
        for (int v = 0; v < 256; ++v) { const int percent = v * 100 / 255; int band = kLostHighlights - 1; for (int i = 1; i < kLostHighlights; ++i) if (percent < kBands[size_t(i)].to) { band = i; break; } t[size_t(v)] = quint8(band); }
        return t; }();
    return table;
}

// The picture in false colour. Takes any 8-bit sRGB image; alpha is kept.
inline QImage map(const QImage &source) {
    if (source.isNull()) return {};
    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    const auto &bands = lumaBands();
    for (int y = 0; y < out.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb c = line[x];
            const int mx = qMax(qRed(c), qMax(qGreen(c), qBlue(c)));
            const int l = luma(c);
            const int band = mx >= 254 ? kLostHighlights : mx <= 2 ? kLostShadows : bands[size_t(l)];
            const QRgb colour = kBands[size_t(band)].colour;
            line[x] = colour ? ((c & 0xff000000) | (colour & 0x00ffffff)) : qRgba(l, l, l, qAlpha(c));
        }
    }
    return out;
}

// How much of the picture falls in each band, for the legend: name, range,
// colour and share (per cent), in order from black to white.
inline QVariantList shares(const QImage &source) {
    std::array<qint64, kBands.size()> counts{};
    qint64 total = 0;
    if (!source.isNull()) {
        const QImage small = source.convertToFormat(QImage::Format_ARGB32).scaled(384, 384, Qt::KeepAspectRatio, Qt::FastTransformation);
        for (int y = 0; y < small.height(); ++y) { const QRgb *line = reinterpret_cast<const QRgb *>(small.constScanLine(y)); for (int x = 0; x < small.width(); ++x) { ++counts[size_t(bandOf(line[x]))]; ++total; } }
    }
    QVariantList out;
    for (size_t i = 0; i < kBands.size(); ++i) {
        const Band &b = kBands[i];
        const bool lost = int(i) == kLostShadows || int(i) == kLostHighlights;
        out << QVariantMap{{"name", QCoreApplication::translate("FalseColour", b.name)}, {"from", b.from}, {"to", qMin(100, b.to)}, {"lost", lost},
                           {"colour", b.colour ? QColor::fromRgb(b.colour).name() : QString()}, {"share", total ? 100.0 * counts[i] / total : 0.0}};
    }
    return out;
}
}
