// Output finishing: output sharpening for screen or print, and a text or
// image watermark, applied to the export's float pixels once, inside the
// engine's export, before they are quantised for the writer. The file is
// encoded exactly once and carries the engine's metadata as written.
// Pure functions; safe on the export worker.
#pragma once

#include <QColorSpace>
#include <QImage>
#include <QString>
#include <QVariantMap>

namespace Finishing {

struct Options {
    // Sharpening: 0 off, 1 screen (fine radius), 2 print (wider radius).
    int sharpen = 0;
    // 0 low, 1 standard, 2 high.
    int amount = 1;
    // Watermark: text and/or an image file (PNG with alpha works best).
    QString text;
    QString imagePath;
    // 0..8: top-left, top, top-right, left, centre, right, bottom-left, bottom, bottom-right.
    int anchor = 8;
    // Text height / image width as a percentage of the long edge.
    double size = 3.0;
    double opacity = 0.6;
    // Distance from the edge as a percentage of the long edge.
    double margin = 2.0;
    // Re-encode quality for lossy files (the export's own setting).
    int jpegQuality = 90;

    bool active() const { return sharpen > 0 || !text.trimmed().isEmpty() || !imagePath.isEmpty(); }
    static Options fromMap(const QVariantMap &m);
    QVariantMap toMap() const;
};

// Applies the options to straight RGBA float32 pixels in the output
// profile's encoding; `output` names that profile so an image mark can be
// converted into it. The buffer is edited in place.
void applyFloat(float *rgba, int width, int height, const Options &o, const QColorSpace &output);

// The building blocks, exposed for the tests. sharpen() works on any
// QImage by way of float; drawWatermark() draws on 8-bit, 16-bit or float.
QImage sharpen(const QImage &src, double radius, double amount);
void sharpenFloat(float *rgba, int width, int height, double radius, double amount);
void drawWatermark(QImage &img, const Options &o);
// Sharpening radius/amount the presets map to (long edge in px matters for print).
void sharpenParams(const Options &o, int longEdge, double *radius, double *amount);

} // namespace Finishing
