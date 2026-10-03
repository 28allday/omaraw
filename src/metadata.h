// Reads what the catalog needs from a photo: EXIF/IPTC/XMP through exiv2,
// dimensions and bit depth through LibRaw for RAW files, and the embedded
// preview LibRaw finds inside the RAW. Pure functions; safe on any thread.
#pragma once

#include "catalog.h"
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>
#include <QVariantMap>
#include <atomic>

namespace Metadata {

// File extensions the importer accepts, lower-case, no dot.
QStringList rawExtensions();
QStringList imageExtensions(); // everything importable (raw + standard)
bool isRawExtension(const QString &ext);
// Uses the extension, except JPEG contents with a RAW filename (phone transfers).
QString formatFor(const QString &path); // "RAF", "NEF", "DNG", "JPEG", "TIFF", "PNG", ...

// Fills every field of `rec` that can be read from the file; path, size and
// mtime are read too. Returns false only when the file cannot be opened.
bool read(const QString &path, AssetRecord &rec);

// An upright preview at roughly maxEdge: use an adequate embedded image,
// otherwise render the RAW. Cancellation also interrupts expensive RAW work.
QImage preview(const QString &path, int maxEdge, const std::atomic_bool *cancel = nullptr);

// XMP sidecars: a proprietary
// RAW's is <name>.xmp beside it (DSC_0833.NEF → DSC_0833.xmp). Other files
// (DNG, JPEG, TIFF…) support embedded XMP. Without embedding,
// theirs is <file>.xmp (photo.jpg.xmp) and never meets a RAW twin's. The
// older <file>.xmp of a RAW (OmaRAW before, darktable) is still read.
// Reading never fails loudly; writing keeps every namespace it does not own.
struct Sidecar {
    bool present = false;
    QString file;             // the sidecar that was read
    int rating = -1;          // -1 = not set
    QString label;            // "" = not set
    QStringList keywords;     // catalog form: a hierarchy is "Places/UK/London"
    QString title, caption;   // dc:title, dc:description ("" = not set)
    QString creator, copyright; // dc:creator, dc:rights ("" = not set)
    // The develop sections: OmaRAW's exact edit (the engine's own history,
    // darktable's namespace) and CRS settings (the source editor's crs:).
    bool hasEdit = false, hasCameraRaw = false;
    bool cameraRawOurs = false; // the crs: settings are the ones OmaRAW last wrote
    QByteArray packet;          // the whole XMP, for reading the edits
};
// What goes into the develop sections on a write.
struct SidecarEdit {
    QByteArray engineXmp;     // an XMP packet; only its darktable: part is taken
    QVariantMap cameraRaw;    // crs: name → value (a QStringList is written as a sequence)
};
QString sidecarPath(const QString &path);            // where OmaRAW writes it
QStringList sidecarNames(const QString &path);       // every name one may have, the preferred first
QString existingSidecar(const QString &path);        // the one read, "" when none
// Each sidecar of `from` that exists, with where it goes when the photo becomes `to`.
QVector<QPair<QString, QString>> sidecarsFollowing(const QString &from, const QString &to);
// `embedded`: a DNG, JPEG or TIFF with no sidecar is read from inside itself.
Sidecar readSidecar(const QString &path, bool embedded = false);
bool embeddable(const QString &path);               // DNG, JPEG, TIFF: support embedded XMP
bool writeEmbedded(const QString &path, const Sidecar &data, const SidecarEdit *edit, QString *error = nullptr);
// `edit` null: the develop sections already there stay as they are. A
// CRS section OmaRAW did not write (the source editor's own edit) is never
// replaced by OmaRAW's approximation of its edit.
bool writeSidecar(const QString &path, const Sidecar &data, const SidecarEdit *edit, QString *error = nullptr);
bool writeSidecar(const QString &path, int rating, const QString &label, const QStringList &keywords,
                  const QString &title, const QString &caption, QString *error = nullptr);
inline bool writeSidecar(const QString &path, int rating, const QString &label, const QStringList &keywords, QString *error = nullptr) {
    return writeSidecar(path, rating, label, keywords, QString(), QString(), error);
}

// What the catalog says about a photo, written into an exported file:
// title, caption, copyright and creator (EXIF and XMP where each lives).
struct Describe {
    QString title, caption, copyright, creator;
    QStringList keywords;
    int ppi = 0;
    bool catalogValues = false, keywordsSpecified = false;
    bool any() const { return catalogValues || keywordsSpecified || !title.isEmpty() || !caption.isEmpty() || !copyright.isEmpty() || !creator.isEmpty() || ppi > 0; }
};
// The pixel density a file declares (EXIF X/YResolution in inches), 0 when none.
int resolutionPpi(const QString &path);
Describe describeFromMap(const QVariantMap &m);

// After an export: set the Software tag to ours, write the description
// when there is one, and, when asked, drop darktable's own XMP namespace
// (its develop history). Silent no-op on containers exiv2 cannot write.
bool stampExport(const QString &path, const QString &software, bool stripHistory, const Describe &describe = Describe(), int flags = -1);
bool exportPackets(const QByteArray &exif, const QByteArray &xmp, int flags, const Describe &describe,
                   QByteArray *outExif, QByteArray *outXmp, QString *error);
QString softwareTag(const QString &path);
// Copy descriptive/camera metadata from a rendered source without copying
// Source storage tags, orientation or a development recipe into generated pixels.
bool copyProcessedMetadata(const QString &source, const QString &destination, QString *error = nullptr);
// Linear DNG pixels retain sensor orientation/calibration. Only capture and
// descriptive metadata are copied, never CFA storage or develop instructions.
bool copyDenoiseMetadata(const QString &source, const QString &destination, QString *error = nullptr);
int exifCount(const QString &path); // number of EXIF tags, 0 when none/unreadable
bool hasDarktableXmp(const QString &path);

// Formatting shared by the inspector, the grid badges and the tests.
QString formatShutter(double seconds);      // "1/250 s", "2 s", "0.5 s"
QString formatAperture(double f);           // "f/4.0"
QString formatFocal(double mm);             // "32 mm"
QString formatIso(int iso);                 // "ISO 800"
QString formatSize(qint64 bytes);           // "82.6 MB"
QString formatExposure(const AssetRecord &r); // "1/250 sec at f/4.0"
QString formatCaptured(const QString &iso8601, bool withTime); // "2025-05-20 10:42"

} // namespace Metadata
