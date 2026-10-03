// A flat Photoshop document: one RGB image at 8 or 16 bits with the colour
// profile embedded, the hand-off from a RAW developer to a layered editor.
// The pixels come from the engine's own export (a 16-bit TIFF), so the
// picture, its finishing and its profile are exactly what every other
// format gets; the metadata is stamped afterwards through Exiv2, which
// reads and writes PSD resources.
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace Psd {

// Writes `img` to `path` as a single-layer RGB PSD at `bpp` (8 or 16).
// `icc` is embedded as the document's colour profile when not empty; `ppi`
// sets the print resolution when positive. Returns "" on success, else a
// short reason. Sides above 30000 px are not representable (that is PSB).
QString write(const QString &path, const QImage &img, int bpp, const QByteArray &icc, int ppi);

// Converts an engine export (a TIFF, read through Qt for its pixels and
// through Exiv2 for its embedded profile) to a PSD beside it. The TIFF is
// removed when the PSD was written. Returns "" on success.
QString fromTiff(const QString &tiff, const QString &psd, int bpp, int ppi);

} // namespace Psd
