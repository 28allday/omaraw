#pragma once
#include <QImage>
#include <QVariantList>
#include <QVector>

// A picture of what a local adjustment's mask actually covers.
//
// darktable computes the real mask inside a running pixelpipe, and the
// only switch that would hand it back reaches for the GUI (it wants
// module focus and the full pipe). So the preview is built here from the
// same numbers the mask is built from, using darktable's own falloff
// curves — quadratic for a circle, an error function across a gradient —
// so what you see lines up with what renders. The range half is measured
// off the render on screen rather than the module's input, which is the
// one place the preview approximates rather than reproduces.
namespace MaskPreview {

// Edge refinement, darktable's blend post-processing on the finished
// mask: a gaussian blur (radius in full-image pixels, so `scale` is
// preview pixels per image pixel) and a tone curve. The guided-filter
// feather cannot be reproduced without the pipe and is not attempted.
struct Refine {
    double blur = 0;          // 0..100, full-image pixels
    double contrast = 0;      // -1..1
    double brightness = 0;    // -1..1
    double scale = 1;         // preview px per image px
    bool any() const { return blur > 0.1 || contrast != 0 || brightness != 0; }
};

// Coverage of one local, 0..1 per pixel, at `w` × `h`.
// `shapes` and `ranges` are the rows EngineService publishes.
QVector<float> coverage(const QVariantList &shapes, const QVariantList &ranges, bool inverted,
                        const QImage &render, int w, int h, const Refine &refine = Refine());

// The same, tinted for the viewer: `colour` where the mask bites, clear
// where it does not, alpha scaled by `strength`.
QImage overlay(const QVariantList &shapes, const QVariantList &ranges, bool inverted,
               const QImage &render, int w, int h, const QColor &colour, double strength,
               const Refine &refine = Refine());

}
