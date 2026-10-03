// Soft proofing: the engine's float linear render pushed through a printer
// ICC profile and back to linear RGB with Little CMS before the OCIO view, so
// the screen shows what the print will keep. Optional gamut warning
// paints the pixels the profile cannot hold. Pure functions.
#pragma once

#include <QImage>
#include <QString>

namespace Proof {

// intent: 0 perceptual, 1 relative colorimetric, 2 saturation, 3 absolute.
// Float linear inputs stay float; encoded inputs return encoded previews.
// Returns the proofed image (same size); on failure the input and
// a reason in *error.
QImage apply(const QImage &srgb, const QString &iccPath, int intent, bool gamutWarning, QString *error = nullptr);

// Application-managed output: the sRGB image converted INTO an RGB output
// profile (a paper/printer profile), with the intent and black point
// compensation, as device values in an RGB32 image. Only RGB profiles;
// a CMYK or Lab profile is refused with a reason.
// Into an RGB paper/printer profile, from the picture's own embedded profile
// (sRGB when it has none); 16-bit pictures are read at full depth.
QImage convert(const QImage &in, const QString &iccPath, int intent, bool blackPointCompensation, QString *error = nullptr);

// The profile's description text, or the file name when it has none.
QString profileName(const QString &iccPath);
// Validated RGB profile bytes, kept with a queued job so a profile file
// changed or removed later cannot change that job's output.
QByteArray readRgbProfile(const QString &iccPath, QString *error);

// For tests and demos: writes an sRGB profile, or a deliberately narrow
// RGB profile, to `path`.
bool writeTestProfile(const QString &path, bool narrow);

} // namespace Proof
