#include "metadata.h"
#include "offlinestore.h"
#include "metadataexport.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <stdexcept>

#include <QDateTime>
#include <QCryptographicHash>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QSemaphore>
#include <QtEndian>
#include <QTransform>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <memory>
#include <omp.h>

#include <exiv2/exiv2.hpp>
#include <libheif/heif.h>
#include <libraw/libraw.h>

namespace Metadata {

QStringList rawExtensions() {
    static const QStringList e = {
        QStringLiteral("raf"), QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("dng"),
        QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("crw"), QStringLiteral("arw"),
        QStringLiteral("srf"), QStringLiteral("sr2"), QStringLiteral("orf"), QStringLiteral("ori"), QStringLiteral("rw2"),
        QStringLiteral("pef"), QStringLiteral("raw"), QStringLiteral("rwl"), QStringLiteral("iiq"),
        QStringLiteral("3fr"), QStringLiteral("fff"), QStringLiteral("erf"), QStringLiteral("mef"),
        QStringLiteral("mos"), QStringLiteral("srw"), QStringLiteral("x3f"), QStringLiteral("kdc"),
        QStringLiteral("dcr"), QStringLiteral("mrw"), QStringLiteral("gpr")
    };
    return e;
}

QStringList imageExtensions() {
    static const QStringList e = rawExtensions() + QStringList{
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("tif"), QStringLiteral("tiff"),
        QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("heic"), QStringLiteral("heif"),
        QStringLiteral("avif"), QStringLiteral("jxl")
    };
    return e;
}

bool isRawExtension(const QString &ext) { return rawExtensions().contains(ext.toLower()); }

QString formatFor(const QString &path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    // Some phone transfers leave a .DNG name on a rendered JPEG. Inspect
    // the signature before treating that file as RAW; never rewrite it.
    if (isRawExtension(ext)) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && file.read(3) == QByteArray::fromHex("ffd8ff")) {
            // Raspberry Pi and some other cameras append a real sensor frame
            // to a JPEG. Keep that RAW when the decoder recognizes it.
            auto *raw = libraw_init(0);
            const bool sensor = raw && libraw_open_file(raw, QFile::encodeName(path).constData()) == LIBRAW_SUCCESS
                                && raw->idata.raw_count > 0;
            if (raw) libraw_close(raw);
            if (!sensor) return QStringLiteral("JPEG");
        }
    }
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) return QStringLiteral("JPEG");
    if (ext == QLatin1String("tif") || ext == QLatin1String("tiff")) return QStringLiteral("TIFF");
    if (ext == QLatin1String("heic") || ext == QLatin1String("heif")) return QStringLiteral("HEIF");
    return ext.toUpper();
}

static QString exifString(const Exiv2::ExifData &d, const char *key) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end()) return QString();
    return QString::fromStdString(it->toString()).trimmed();
}

static double exifFloat(const Exiv2::ExifData &d, const char *key, bool *ok = nullptr) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end() || it->count() == 0) { if (ok) *ok = false; return 0; }
    if (ok) *ok = true;
    return it->toFloat(0);
}

static qint64 exifInt(const Exiv2::ExifData &d, const char *key) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end() || it->count() == 0) return 0;
    return it->toInt64(0);
}

static QString exifInterpreted(const Exiv2::ExifData &d, const char *key) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end()) return QString();
    std::ostringstream os;
    it->write(os, &d);
    return QString::fromStdString(os.str()).trimmed();
}

static double gpsCoord(const Exiv2::ExifData &d, const char *key, const char *refKey, bool *ok) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end() || it->count() < 3) { *ok = false; return 0; }
    const double deg = it->toFloat(0), min = it->toFloat(1), sec = it->toFloat(2);
    double v = deg + min / 60.0 + sec / 3600.0;
    const QString ref = exifString(d, refKey);
    if (ref.startsWith(QLatin1Char('S')) || ref.startsWith(QLatin1Char('W'))) v = -v;
    *ok = true;
    return v;
}

static QString isoFromExifDate(const QString &s) {
    // "2025:05:20 10:42:13" → "2025-05-20T10:42:13"
    QDateTime dt = QDateTime::fromString(s, QStringLiteral("yyyy:MM:dd HH:mm:ss"));
    if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODate);
    return dt.isValid() ? dt.toString(Qt::ISODate) : QString();
}

static void readLibRaw(const QString &path, AssetRecord &rec) {
    libraw_data_t *lr = libraw_init(0);
    if (!lr) return;
    if (libraw_open_file(lr, path.toLocal8Bit().constData()) == LIBRAW_SUCCESS) {
        int w = lr->sizes.width, h = lr->sizes.height;
        if (lr->sizes.flip == 5 || lr->sizes.flip == 6) std::swap(w, h);
        // LibRaw knows the visible frame and the sensor's real maximum;
        // exiv2's numbers for a RAW describe the container or a thumbnail.
        if (w > 0 && h > 0) { rec.width = w; rec.height = h; }
        if (lr->color.maximum > 0)
            rec.bitDepth = int(std::ceil(std::log2(double(lr->color.maximum) + 1.0)));
        if (rec.make.isEmpty()) rec.make = QString::fromLatin1(lr->idata.make).trimmed();
        if (rec.model.isEmpty()) rec.model = QString::fromLatin1(lr->idata.model).trimmed();
        if (rec.lens.isEmpty()) rec.lens = QString::fromLatin1(lr->lens.Lens).trimmed();
        if (rec.iso == 0) rec.iso = int(lr->other.iso_speed + 0.5f);
        if (rec.shutter == 0) rec.shutter = lr->other.shutter;
        if (rec.aperture == 0) rec.aperture = lr->other.aperture;
        if (rec.focalLength == 0) rec.focalLength = lr->other.focal_len;
        if (rec.capturedAt.isEmpty() && lr->other.timestamp > 0)
            rec.capturedAt = QDateTime::fromSecsSinceEpoch(lr->other.timestamp).toString(Qt::ISODate);
    }
    libraw_close(lr);
}

static bool isHeifExtension(const QString &ext);
static QImage decodeHeif(const QString &path, int maxEdge, int *fullW = nullptr, int *fullH = nullptr);

// exiv2 renders a LangAlt as 'lang="x-default" text'; keep the text.
static QString langAlt(const std::string &raw) {
    QString v = QString::fromStdString(raw).trimmed();
    if (v.startsWith(QLatin1String("lang=\""))) { const int q = v.indexOf(QLatin1Char('"'), 6); if (q > 0) v = v.mid(q + 1).trimmed(); }
    return v;
}

bool read(const QString &path, AssetRecord &rec) {
    QFileInfo fi(path);
    if (!fi.exists()) return false;
    rec.path = fi.absoluteFilePath();
    rec.filename = fi.fileName();
    rec.size = fi.size();
    rec.mtime = fi.lastModified().toSecsSinceEpoch();
    rec.format = formatFor(path);
    rec.isRaw = isRawExtension(rec.format);
    int floatingBits = 0;

    try {
        auto image = Exiv2::ImageFactory::open(path.toStdString());
        if (image.get()) {
            image->readMetadata();
            const Exiv2::ExifData &d = image->exifData();
            rec.width = image->pixelWidth();
            rec.height = image->pixelHeight();
            rec.orientation = int(exifInt(d, "Exif.Image.Orientation"));
            if (rec.orientation < 1 || rec.orientation > 8) rec.orientation = 1;
            if (rec.orientation >= 5 && rec.width > 0 && rec.height > 0 && !rec.isRaw)
                std::swap(rec.width, rec.height);
            rec.make = exifString(d, "Exif.Image.Make");
            rec.model = exifString(d, "Exif.Image.Model");
            if (rec.model.startsWith(rec.make, Qt::CaseInsensitive) && !rec.make.isEmpty())
                rec.model = rec.model.mid(rec.make.size()).trimmed();
            rec.lens = exifInterpreted(d, "Exif.Photo.LensModel");
            if (rec.lens.isEmpty()) rec.lens = exifInterpreted(d, "Exif.Photo.LensSpecification");
            if (rec.lens.isEmpty()) {
                const auto lens = Exiv2::lensName(d);
                if (lens != d.end()) rec.lens = QString::fromStdString(lens->print(&d)).trimmed();
            }
            rec.focalLength = exifFloat(d, "Exif.Photo.FocalLength");
            rec.aperture = exifFloat(d, "Exif.Photo.FNumber");
            rec.shutter = exifFloat(d, "Exif.Photo.ExposureTime");
            rec.iso = int(exifInt(d, "Exif.Photo.ISOSpeedRatings"));
            if (rec.iso == 0) rec.iso = int(exifInt(d, "Exif.Photo.RecommendedExposureIndex"));
            rec.capturedAt = isoFromExifDate(exifString(d, "Exif.Photo.DateTimeOriginal"));
            if (rec.capturedAt.isEmpty()) rec.capturedAt = isoFromExifDate(exifString(d, "Exif.Image.DateTime"));
            rec.bitDepth = int(exifInt(d, "Exif.Image.BitsPerSample"));
            if (rec.bitDepth > 16 || rec.bitDepth < 0) rec.bitDepth = 0;
            // Float DNG WhiteLevel is a numeric scale (often 1), not a count
            // of integer codes. Read the storage depth from its actual IFD.
            for (const auto &entry : d) {
                const QString key = QString::fromStdString(entry.key());
                if (key.endsWith(".SampleFormat") && entry.toInt64() == 3) {
                    const auto bits = exifInt(d, (key.left(key.size() - 12) + "BitsPerSample").toLatin1().constData());
                    if (bits == 16 || bits == 24 || bits == 32 || bits == 64) floatingBits = qMax(floatingBits, int(bits));
                }
            }
            rec.copyright = exifString(d, "Exif.Image.Copyright");
            rec.creator = exifString(d, "Exif.Image.Artist");
            bool okLat = false, okLon = false;
            const double lat = gpsCoord(d, "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef", &okLat);
            const double lon = gpsCoord(d, "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef", &okLon);
            if (okLat && okLon) { rec.hasGps = true; rec.gpsLat = lat; rec.gpsLon = lon; }
            const Exiv2::XmpData &x = image->xmpData();
            if (rec.creator.isEmpty()) {
                auto it = x.findKey(Exiv2::XmpKey("Xmp.dc.creator"));
                if (it != x.end()) rec.creator = QString::fromStdString(it->toString()).trimmed();
            }
            if (rec.copyright.isEmpty()) {
                auto it = x.findKey(Exiv2::XmpKey("Xmp.dc.rights"));
                if (it != x.end()) rec.copyright = QString::fromStdString(it->toString()).trimmed();
            }
            // Title and caption: XMP first, the EXIF description as the caption's fallback.
            if (auto it = x.findKey(Exiv2::XmpKey("Xmp.dc.title")); it != x.end()) rec.title = langAlt(it->toString());
            if (auto it = x.findKey(Exiv2::XmpKey("Xmp.dc.description")); it != x.end()) rec.caption = langAlt(it->toString());
            if (rec.caption.isEmpty()) rec.caption = exifString(d, "Exif.Image.ImageDescription");
        }
    } catch (const Exiv2::Error &e) {
        qWarning() << "exiv2:" << path << e.what();
    } catch (...) {
        qWarning() << "exiv2: unknown error on" << path;
    }

    if (rec.isRaw) {
        readLibRaw(path, rec);
    } else if (rec.width == 0) {
        QImageReader r(path);
        const QSize s = r.size();
        if (s.isValid()) { rec.width = s.width(); rec.height = s.height(); }
        if (rec.bitDepth == 0) rec.bitDepth = 8;
    }
    if (floatingBits) rec.bitDepth = floatingBits;
    if ((rec.width == 0 || rec.height == 0) && isHeifExtension(fi.suffix())) {
        int w = 0, h = 0;
        decodeHeif(path, 0, &w, &h);
        rec.width = w; rec.height = h;
    }
    return true;
}

static QImage applyOrientation(QImage img, int orientation) {
    QTransform t;
    switch (orientation) {
    case 2: return img.mirrored(true, false);
    case 3: t.rotate(180); break;
    case 4: return img.mirrored(false, true);
    case 5: return img.mirrored(true, false).transformed(QTransform().rotate(90));
    case 6: t.rotate(90); break;
    case 7: return img.mirrored(true, false).transformed(QTransform().rotate(-90));
    case 8: t.rotate(-90); break;
    default: return img;
    }
    return img.transformed(t);
}

static int flipToOrientation(int flip) {
    switch (flip) {
    case 3: return 3;
    case 5: return 8;
    case 6: return 6;
    default: return 1;
    }
}

// HEIF/HEIC and AVIF through libheif (Qt ships no plugin for either);
// libheif applies the file's rotation and crop itself.
static bool isHeifExtension(const QString &ext) {
    const QString e = ext.toLower();
    return e == QLatin1String("heic") || e == QLatin1String("heif") || e == QLatin1String("avif");
}

static QImage decodeHeif(const QString &path, int maxEdge, int *fullW, int *fullH) {
    QImage out;
    heif_context *ctx = heif_context_alloc();
    if (!ctx) return out;
    if (heif_context_read_from_file(ctx, path.toLocal8Bit().constData(), nullptr).code != heif_error_Ok) { heif_context_free(ctx); return out; }
    heif_image_handle *handle = nullptr;
    if (heif_context_get_primary_image_handle(ctx, &handle).code != heif_error_Ok || !handle) { heif_context_free(ctx); return out; }
    if (fullW) *fullW = heif_image_handle_get_width(handle);
    if (fullH) *fullH = heif_image_handle_get_height(handle);
    if (maxEdge > 0) {
        heif_image *img = nullptr;
        if (heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr).code == heif_error_Ok && img) {
            int stride = 0;
            const uint8_t *data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
            const int w = heif_image_get_width(img, heif_channel_interleaved), h = heif_image_get_height(img, heif_channel_interleaved);
            if (data && w > 0 && h > 0) out = QImage(data, w, h, stride, QImage::Format_RGB888).copy();
            heif_image_release(img);
        }
    }
    heif_image_handle_release(handle);
    heif_context_free(ctx);
    return out;
}

static int longEdge(const QImage &image) { return std::max(image.width(), image.height()); }
static bool cancelled(const std::atomic_bool *cancel) { return cancel && cancel->load(); }

// Decode JPEGs at the requested size rather than allocating a full-resolution
// camera preview for every grid cell. A RAW orientation takes precedence over
// any orientation inside its JPEG, so the two are never applied twice.
static QImage previewBytes(const unsigned char *data, int size, int edge, int orientation) {
    QByteArray bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(data), size);
    QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(orientation == 1);
    const QSize dimensions = reader.size();
    if (dimensions.isValid() && std::max(dimensions.width(), dimensions.height()) > edge)
        reader.setScaledSize(dimensions.scaled(edge, edge, Qt::KeepAspectRatio));
    return applyOrientation(reader.read(), orientation);
}

static QImage processedPreview(const libraw_processed_image_t *image, int edge, int orientation) {
    if (!image) return {};
    if (image->type == LIBRAW_IMAGE_JPEG)
        return previewBytes(image->data, int(image->data_size), edge, orientation);
    if (image->type != LIBRAW_IMAGE_BITMAP || !image->width || !image->height
        || (image->colors != 1 && image->colors != 3) || (image->bits != 8 && image->bits != 16)) return {};
    const int stride = int(image->width) * image->colors * (image->bits / 8);
    if (quint64(stride) * image->height > image->data_size) return {};
    QImage result;
    if (image->bits == 8) {
        result = QImage(image->data, image->width, image->height, stride,
                        image->colors == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888).copy();
    } else {
        result = QImage(image->width, image->height,
                        image->colors == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
        if (result.isNull()) return {};
        for (int y = 0; y < image->height; ++y) {
            auto *row = result.scanLine(y);
            for (int x = 0; x < image->width * image->colors; ++x) {
                quint16 value; std::memcpy(&value, image->data + y * stride + x * 2, 2);
                row[x] = value >> 8;
            }
        }
    }
    return applyOrientation(result, orientation);
}

// Prefer the smallest adequate preview; when all are too small, try the
// largest first. An oversized preview is still preferable to a postage stamp.
static bool previewOrder(int a, int b, int edge) {
    const bool aFits = a >= edge, bFits = b >= edge;
    if (aFits != bFits) return aFits;
    return aFits ? a < b : a > b;
}

// Some LibRaw builds omit the Sigma decoder. X3F still carries ordinary JPEG
// previews in its indexed SECi sections; read those without decoding sensors.
static QImage x3fPreview(const QString &path, int edge, const std::atomic_bool *cancel) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray header = file.read(52);
    if (header.size() != 52 || !header.startsWith("FOVb")) return {};
    const auto word = [](const QByteArray &bytes, int offset) {
        return qFromLittleEndian<quint32>(bytes.constData() + offset);
    };
    const quint32 version = word(header, 4);
    if (version < 0x20000 || version >= 0x50000 || !file.seek(file.size() - 4)) return {};
    const QByteArray tail = file.read(4);
    if (tail.size() != 4) return {};
    const quint32 directoryOffset = word(tail, 0);
    if (quint64(directoryOffset) + 12 > quint64(file.size() - 4) || !file.seek(directoryOffset)) return {};
    const QByteArray directory = file.read(12);
    if (directory.size() != 12 || !directory.startsWith("SECd")) return {};
    const quint32 count = word(directory, 8);
    if (count > 50 || quint64(directoryOffset) + 12 + count * 12 > quint64(file.size() - 4)) return {};
    const QByteArray entries = file.read(count * 12);
    if (entries.size() != count * 12) return {};
    const int rotation = int(word(header, version < 0x40000 ? 36 : 48));
    const int orientation = rotation == 90 ? 6 : rotation == 180 ? 3 : rotation == 270 ? 8 : 1;
    QImage best;
    for (quint32 i = 0; i < count; ++i) {
        if (cancelled(cancel)) return {};
        const quint32 offset = word(entries, i * 12), size = word(entries, i * 12 + 4);
        if (size <= 28 || size > 128 * 1024 * 1024 || quint64(offset) + size > directoryOffset || !file.seek(offset)) continue;
        const QByteArray section = file.read(28);
        if (section.size() != 28 || !section.startsWith("SECi") || word(section, 8) != 2 || word(section, 12) != 18) continue;
        const QByteArray jpeg = file.read(size - 28);
        if (jpeg.size() != size - 28) continue;
        const auto decoded = previewBytes(reinterpret_cast<const unsigned char *>(jpeg.constData()), jpeg.size(), edge, orientation);
        if (longEdge(decoded) > longEdge(best)) best = decoded;
    }
    return best;
}

static QImage exifPreview(const QString &path, int edge, int orientation, const std::atomic_bool *cancel) {
    QImage best;
    try {
        auto image = Exiv2::ImageFactory::open(path.toStdString());
        if (!image) return {};
        image->readMetadata();
        if (orientation == 1) {
            const int exifOrientation = int(exifInt(image->exifData(), "Exif.Image.Orientation"));
            if (exifOrientation >= 1 && exifOrientation <= 8) orientation = exifOrientation;
        }
        Exiv2::PreviewManager manager(*image);
        auto previews = manager.getPreviewProperties();
        std::stable_sort(previews.begin(), previews.end(), [edge](const auto &a, const auto &b) {
            return previewOrder(int(std::max(a.width_, a.height_)), int(std::max(b.width_, b.height_)), edge);
        });
        for (const auto &properties : previews) {
            if (cancelled(cancel)) return {};
            // Only rendered previews here; a TIFF RAW's sensor IFD must never
            // be mistaken for a usable RGB image by a generic TIFF decoder.
            if (properties.mimeType_ != "image/jpeg") continue;
            try {
                const auto preview = manager.getPreviewImage(properties);
                auto decoded = previewBytes(preview.pData(), int(preview.size()), edge, orientation);
                if (longEdge(decoded) > longEdge(best)) best = decoded;
                if (longEdge(best) >= edge) break;
            } catch (const Exiv2::Error &) { /* Try the next embedded preview. */ }
        }
    } catch (const Exiv2::Error &) { /* No readable embedded preview. */ }
    return best;
}

static QImage renderRawPreview(libraw_data_t *raw, int edge, int orientation, const std::atomic_bool *cancel) {
    // RAWs without usable previews must not start one demosaic per CPU core.
    // Bound both concurrent sensor decodes and each decode's OpenMP team.
    static QSemaphore decodePermits(2);
    while (!decodePermits.tryAcquire(1, 50)) if (cancelled(cancel)) return {};
    QSemaphoreReleaser release(&decodePermits);
    if (cancelled(cancel)) return {};
    struct ThreadLimit {
        int previous = omp_get_max_threads();
        ThreadLimit() { omp_set_num_threads(std::min(previous, 2)); }
        ~ThreadLimit() { omp_set_num_threads(previous); }
    } threads;
    raw->params.half_size = std::max(raw->sizes.width, raw->sizes.height) >= edge * 2;
    raw->params.user_qual = 0;
    raw->params.use_camera_wb = 1;
    raw->params.output_color = 1; // sRGB primaries and transfer curve.
    raw->params.gamm[0] = 1.0 / 2.4; raw->params.gamm[1] = 12.92;
    raw->params.output_bps = 8;
    raw->params.user_flip = 0; // Apply the saved orientation once, below.
    raw->rawparams.max_raw_memory_mb = 1024;
    if (libraw_unpack(raw) != LIBRAW_SUCCESS || cancelled(cancel)
        || libraw_dcraw_process(raw) != LIBRAW_SUCCESS || cancelled(cancel)) return {};
    int error = 0;
    auto *image = libraw_dcraw_make_mem_image(raw, &error);
    const QImage result = error == LIBRAW_SUCCESS ? processedPreview(image, edge, orientation) : QImage();
    libraw_dcraw_clear_mem(image);
    return result;
}

QImage preview(const QString &path, int maxEdge, const std::atomic_bool *cancel) {
    if (cancelled(cancel)) return {};
    maxEdge = qBound(1, maxEdge, 8192);
    if (!QFileInfo(path).isFile()) {
        const QString workingCopy = OfflineStore::instance().resolve(path);
        return workingCopy.isEmpty() ? QImage() : preview(workingCopy, maxEdge, cancel);
    }
    QImage out;
    const QString format = formatFor(path);
    if (isHeifExtension(format)) {
        out = decodeHeif(path, maxEdge);
    } else if (isRawExtension(format)) {
        if (format == QLatin1String("X3F")) {
            out = x3fPreview(path, maxEdge, cancel);
            if (longEdge(out) >= maxEdge || cancelled(cancel)) return out;
        }
        const auto closeRaw = [](libraw_data_t *raw) { if (raw) libraw_close(raw); };
        std::unique_ptr<libraw_data_t, decltype(closeRaw)> owner(libraw_init(0), closeRaw);
        auto *lr = owner.get();
        if (!lr) return {};
        libraw_set_progress_handler(lr, [](void *data, LibRaw_progress, int, int) {
            return cancelled(static_cast<const std::atomic_bool *>(data)) ? 1 : 0;
        }, const_cast<std::atomic_bool *>(cancel));
        const bool opened = libraw_open_file(lr, QFile::encodeName(path).constData()) == LIBRAW_SUCCESS;
        const int orientation = opened ? flipToOrientation(lr->sizes.flip) : 1;
        const int sensorEdge = opened ? std::max(int(lr->sizes.width), int(lr->sizes.height)) : 0;
        const int target = sensorEdge > 0 ? std::min(maxEdge, sensorEdge) : maxEdge;
        if (opened) {
            QVector<int> previews;
            for (int i = 0; i < lr->thumbs_list.thumbcount && i < LIBRAW_THUMBNAIL_MAXCOUNT; ++i) previews << i;
            std::stable_sort(previews.begin(), previews.end(), [&](int a, int b) {
                const auto &x = lr->thumbs_list.thumblist[a], &y = lr->thumbs_list.thumblist[b];
                return previewOrder(std::max(x.twidth, x.theight), std::max(y.twidth, y.theight), maxEdge);
            });
            if (previews.isEmpty()) previews << -1;
            for (int index : previews) {
                if (cancelled(cancel)) return {};
                int error = index < 0 ? libraw_unpack_thumb(lr) : libraw_unpack_thumb_ex(lr, index);
                if (error != LIBRAW_SUCCESS) continue;
                auto *image = libraw_dcraw_make_mem_thumb(lr, &error);
                const QImage decoded = error == LIBRAW_SUCCESS ? processedPreview(image, maxEdge, orientation) : QImage();
                libraw_dcraw_clear_mem(image);
                if (longEdge(decoded) > longEdge(out)) out = decoded;
                if (longEdge(out) >= target) break;
            }
        }
        if (longEdge(out) < target * .9 && !cancelled(cancel)) {
            const QImage embedded = exifPreview(path, maxEdge, orientation, cancel);
            if (longEdge(embedded) > longEdge(out)) out = embedded;
        }
        if (opened && longEdge(out) < target * .9 && !cancelled(cancel)) {
            const QImage rendered = renderRawPreview(lr, maxEdge, orientation, cancel);
            if (longEdge(rendered) > longEdge(out)) out = rendered;
        }
    } else {
        QImageReader r(path);
        r.setDecideFormatFromContent(true);
        r.setAutoTransform(true);
        const QSize s = r.size();
        if (s.isValid() && std::max(s.width(), s.height()) > maxEdge)
            r.setScaledSize(s.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio));
        out = r.read();
    }
    if (cancelled(cancel)) return {};
    if (!out.isNull() && std::max(out.width(), out.height()) > maxEdge)
        out = out.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return out;
}

static bool proprietaryRaw(const QString &path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return isRawExtension(ext) && ext != QLatin1String("dng");
}

QString sidecarPath(const QString &path) {
    const QFileInfo fi(path);
    return proprietaryRaw(path) ? fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".xmp")
                                : path + QStringLiteral(".xmp");
}

QStringList sidecarNames(const QString &path) {
    QStringList names{sidecarPath(path)};
    if (proprietaryRaw(path)) names << path + QStringLiteral(".xmp");   // OmaRAW before, darktable
    return names;
}

QString existingSidecar(const QString &path) {
    for (const QString &name : sidecarNames(path)) if (QFileInfo(name).isFile()) return name;
    return QString();
}

QVector<QPair<QString, QString>> sidecarsFollowing(const QString &from, const QString &to) {
    QVector<QPair<QString, QString>> out;
    const QStringList a = sidecarNames(from), b = sidecarNames(to);
    for (int i = 0; i < a.size(); ++i) {
        if (!QFileInfo(a[i]).isFile()) continue;
        // Same position in the list = same convention, whatever the formats.
        out.append({a[i], i < b.size() ? b[i] : to + QStringLiteral(".xmp")});
    }
    return out;
}

static QString labelFromXmp(const QString &v) {
    // The source uses "Red"/"Yellow"…; keep our lower-case names.
    const QString l = v.trimmed().toLower();
    static const QStringList known = {QStringLiteral("red"), QStringLiteral("orange"), QStringLiteral("yellow"),
                                      QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("purple")};
    return known.contains(l) ? l : QString();
}

// Namespaces exiv2 does not know by itself. darktable's is registered by
// the engine too; registering the same prefix and URI again is harmless.
static void registerNamespaces() {
    static const bool once = [] {
        try {
            Exiv2::XmpProperties::registerNs("http://darktable.sf.net/", "darktable");
            Exiv2::XmpProperties::registerNs("http://ns.omaraw.org/sidecar/1.0/", "omaraw");
        } catch (const Exiv2::Error &e) { qWarning() << "xmp namespaces:" << e.what(); }
        return true;
    }();
    Q_UNUSED(once)
}

// A fingerprint of a sidecar's CRS settings: when it matches the one
// OmaRAW stored with them, nobody has changed them since OmaRAW wrote them.
static QString cameraRawDigest(const Exiv2::XmpData &x) {
    QStringList parts;
    for (const auto &d : x)
        if (d.key().rfind("Xmp.crs.", 0) == 0) parts << QString::fromStdString(d.key() + "=" + d.toString());
    parts.sort();
    return QString::fromLatin1(QCryptographicHash::hash(parts.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

// Keywords: The source stores the hierarchy to lr:hierarchicalSubject
// ("Places|UK|London") and each level, flat, to dc:subject.
static QStringList keywordsFrom(const Exiv2::XmpData &x) {
    QStringList paths, flat;
    auto list = [&](const char *key, QStringList &into) {
        auto k = x.findKey(Exiv2::XmpKey(key));
        if (k == x.end()) return;
        for (size_t i = 0; i < k->count(); ++i) {
            const QString kw = QString::fromStdString(k->toString(long(i))).trimmed();
            if (!kw.isEmpty() && !into.contains(kw, Qt::CaseInsensitive)) into << kw;
        }
    };
    list("Xmp.lr.hierarchicalSubject", paths);
    list("Xmp.dc.subject", flat);
    QStringList out;
    QSet<QString> levels;
    for (const QString &p : std::as_const(paths)) {
        const QStringList parts = p.split(QLatin1Char('|'), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        const QString path = parts.join(QLatin1Char('/'));
        if (!out.contains(path, Qt::CaseInsensitive)) out << path;
        for (const QString &part : parts) levels.insert(part.trimmed().toLower());
    }
    for (const QString &f : std::as_const(flat))
        if (!levels.contains(f.toLower()) && !out.contains(f, Qt::CaseInsensitive)) out << f;
    return out;
}

static void readInto(const Exiv2::XmpData &x, Sidecar &sc) {
    auto r = x.findKey(Exiv2::XmpKey("Xmp.xmp.Rating"));
    if (r != x.end()) sc.rating = qBound(0, int(r->toInt64()), 5);
    auto l = x.findKey(Exiv2::XmpKey("Xmp.xmp.Label"));
    if (l != x.end()) sc.label = labelFromXmp(QString::fromStdString(l->toString()));
    sc.keywords = keywordsFrom(x);
    if (auto t = x.findKey(Exiv2::XmpKey("Xmp.dc.title")); t != x.end()) sc.title = langAlt(t->toString());
    if (auto c = x.findKey(Exiv2::XmpKey("Xmp.dc.description")); c != x.end()) sc.caption = langAlt(c->toString());
    if (auto c = x.findKey(Exiv2::XmpKey("Xmp.dc.creator")); c != x.end() && c->count()) sc.creator = QString::fromStdString(c->toString(0)).trimmed();
    if (auto c = x.findKey(Exiv2::XmpKey("Xmp.dc.rights")); c != x.end()) sc.copyright = langAlt(c->toString());
    bool settings = false;
    for (const auto &d : x) {
        const std::string k = d.key();
        if (k == "Xmp.darktable.history" || k.rfind("Xmp.darktable.history[", 0) == 0) sc.hasEdit = true;
        if (k.rfind("Xmp.crs.", 0) == 0 && k != "Xmp.crs.Version" && k != "Xmp.crs.ProcessVersion" && k != "Xmp.crs.RawFileName"
            && k != "Xmp.crs.HasSettings" && k != "Xmp.crs.AlreadyApplied") settings = true;
    }
    auto has = x.findKey(Exiv2::XmpKey("Xmp.crs.HasSettings"));
    sc.hasCameraRaw = has != x.end() ? QString::fromStdString(has->toString()).compare(QStringLiteral("True"), Qt::CaseInsensitive) == 0 && settings : settings;
    // An export: the settings are already in its pixels.
    auto applied = x.findKey(Exiv2::XmpKey("Xmp.crs.AlreadyApplied"));
    if (applied != x.end() && QString::fromStdString(applied->toString()).compare(QStringLiteral("True"), Qt::CaseInsensitive) == 0) sc.hasCameraRaw = false;
    auto mark = x.findKey(Exiv2::XmpKey("Xmp.omaraw.CameraRawDigest"));
    sc.cameraRawOurs = mark != x.end() && QString::fromStdString(mark->toString()) == cameraRawDigest(x);
}

bool embeddable(const QString &path) {
    static const QStringList e{QStringLiteral("dng"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("tif"), QStringLiteral("tiff")};
    return e.contains(QFileInfo(path).suffix().toLower());
}

Sidecar readSidecar(const QString &path, bool embedded) {
    Sidecar sc;
    QString sp = existingSidecar(path);
    // With in-file metadata on, a DNG, JPEG or TIFF with no sidecar carries
    // its own, in the source format.
    const bool inFile = sp.isEmpty() && embedded && embeddable(path) && QFileInfo(path).isFile();
    if (inFile) sp = path;
    if (sp.isEmpty()) return sc;
    registerNamespaces();
    try {
        auto img = Exiv2::ImageFactory::open(sp.toStdString());
        if (!img.get()) return sc;
        img->readMetadata();
        if (inFile && img->xmpData().empty()) return sc;
        sc.present = true;
        sc.file = sp;
        sc.packet = QByteArray::fromStdString(img->xmpPacket());
        readInto(img->xmpData(), sc);
        // Inside a file, an engine history is only an edit to apply when
        // OmaRAW put it there as that file's edit: an exported JPEG carries
        // the history already baked into its pixels.
        if (inFile && sc.hasEdit) {
            auto mark = img->xmpData().findKey(Exiv2::XmpKey("Xmp.omaraw.InFileEdit"));
            sc.hasEdit = mark != img->xmpData().end() && mark->toString() == "True";
        }
    } catch (const Exiv2::Error &e) {
        qWarning() << "sidecar read:" << sp << e.what();
    }
    return sc;
}

static bool ownedMetadata(const std::string &k) {
    return k == "Xmp.xmp.Rating" || k == "Xmp.xmp.Label" || k == "Xmp.dc.subject" || k == "Xmp.lr.hierarchicalSubject"
        || k == "Xmp.dc.title" || k == "Xmp.dc.description" || k == "Xmp.dc.creator" || k == "Xmp.dc.rights"
        || k == "Xmp.xmp.MetadataDate";
}
static bool startsWith(const std::string &k, const char *prefix) { return k.rfind(prefix, 0) == 0; }

// The XMP a photo's metadata and edit make, over what `old` held: every
// datum OmaRAW does not own is carried over verbatim, its own are replaced.
// A CRS section OmaRAW did not write is the source editor's edit, and stays.
// `darktable` tells whether `old` held darktable's own edit.
static bool compose(const Exiv2::XmpData &old, const Sidecar &data, const SidecarEdit *edit, Exiv2::XmpData &x, bool *darktable, QString *error, bool inFile = false) {
    bool anyCameraRaw = false;
    for (const auto &d : old) {
        if (startsWith(d.key(), "Xmp.crs.")) anyCameraRaw = true;
        if (darktable && startsWith(d.key(), "Xmp.darktable.")) *darktable = true;
    }
    auto mark = old.findKey(Exiv2::XmpKey("Xmp.omaraw.CameraRawDigest"));
    const bool foreignCameraRaw = anyCameraRaw && (mark == old.end() || QString::fromStdString(mark->toString()) != cameraRawDigest(old));
    for (const auto &d : old) {
        const std::string k = d.key();
        if (ownedMetadata(k)) continue;
        if (edit && (startsWith(k, "Xmp.darktable.") || k == "Xmp.omaraw.InFileEdit")) continue;
        if (edit && !foreignCameraRaw && (startsWith(k, "Xmp.crs.") || k == "Xmp.omaraw.CameraRawDigest")) continue;
        x.add(d);
    }
    x["Xmp.xmp.Rating"] = int32_t(qBound(0, data.rating, 5));
    if (!data.label.isEmpty()) x["Xmp.xmp.Label"] = (data.label.left(1).toUpper() + data.label.mid(1)).toStdString();
    if (!data.keywords.isEmpty()) {
        // Flat, every level once, for everything that reads dc:subject;
        // the hierarchy beside it, in the source format.
        QStringList flat, paths;
        for (const QString &k : data.keywords) {
            const QStringList parts = k.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            for (const QString &part : parts) if (!flat.contains(part.trimmed(), Qt::CaseInsensitive)) flat << part.trimmed();
            if (parts.size() > 1) paths << parts.join(QLatin1Char('|'));
        }
        Exiv2::Value::UniquePtr v = Exiv2::Value::create(Exiv2::xmpBag);
        for (const QString &k : std::as_const(flat)) v->read(k.toStdString());
        x.add(Exiv2::XmpKey("Xmp.dc.subject"), v.get());
        if (!paths.isEmpty()) {
            Exiv2::Value::UniquePtr h = Exiv2::Value::create(Exiv2::xmpBag);
            for (const QString &k : std::as_const(paths)) h->read(k.toStdString());
            x.add(Exiv2::XmpKey("Xmp.lr.hierarchicalSubject"), h.get());
        }
    }
    const auto alt = [&](const char *key, const QString &text) {
        if (text.isEmpty()) return;
        Exiv2::Value::UniquePtr v = Exiv2::Value::create(Exiv2::langAlt); v->read(text.toStdString()); x.add(Exiv2::XmpKey(key), v.get());
    };
    alt("Xmp.dc.title", data.title);
    alt("Xmp.dc.description", data.caption);
    alt("Xmp.dc.rights", data.copyright);
    if (!data.creator.isEmpty()) { Exiv2::Value::UniquePtr v = Exiv2::Value::create(Exiv2::xmpSeq); v->read(data.creator.toStdString()); x.add(Exiv2::XmpKey("Xmp.dc.creator"), v.get()); }
    x["Xmp.xmp.MetadataDate"] = QDateTime::currentDateTime().toOffsetFromUtc(QDateTime::currentDateTime().offsetFromUtc()).toString(Qt::ISODate).toStdString();
    if (edit) {
        if (!edit->engineXmp.isEmpty()) {
            Exiv2::XmpData engine;
            if (Exiv2::XmpParser::decode(engine, edit->engineXmp.toStdString()) != 0) { if (error) *error = QStringLiteral("the edit could not be read"); return false; }
            for (const auto &d : engine) if (startsWith(d.key(), "Xmp.darktable.")) x.add(d);
            if (inFile) x["Xmp.omaraw.InFileEdit"] = "True";
        }
        if (!foreignCameraRaw && !edit->cameraRaw.isEmpty()) {
            for (auto it = edit->cameraRaw.cbegin(); it != edit->cameraRaw.cend(); ++it) {
                const Exiv2::XmpKey key("Xmp.crs." + it.key().toStdString());
                if (it.value().metaType().id() == QMetaType::QStringList) {
                    Exiv2::Value::UniquePtr v = Exiv2::Value::create(Exiv2::xmpSeq);
                    for (const QString &item : it.value().toStringList()) v->read(item.toStdString());
                    x.add(key, v.get());
                } else x[key.key()] = it.value().toString().toStdString();
            }
            x["Xmp.omaraw.CameraRawDigest"] = cameraRawDigest(x).toStdString();
        }
    }
    return true;
}

// rename(2): the new file takes the old one's place in one step, or the old
// one stays. (QFile::rename refuses an existing target, which would mean
// deleting first and hoping the rename followed.)
static bool replaceWith(const QString &tmp, const QString &target, QString *error) {
    if (::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(target).constData())) {
        if (error) *error = QStringLiteral("cannot rename into %1: %2").arg(target, QString::fromLocal8Bit(std::strerror(errno)));
        QFile::remove(tmp);
        return false;
    }
    return true;
}

bool writeSidecar(const QString &path, const Sidecar &data, const SidecarEdit *edit, QString *error) {
    registerNamespaces();
    const QString sp = sidecarPath(path);
    const QString tmp = sp + QStringLiteral(".part");
    QFile::remove(tmp);
    // Whatever name it was found under, it is written under this one.
    const QString was = existingSidecar(path);
    bool darktableThere = false;
    try {
        // exiv2 merges a sidecar's existing packet on write, so a removed key
        // would come back. Build a fresh file instead.
        Exiv2::XmpData old, x;
        if (!was.isEmpty()) {
            auto o = Exiv2::ImageFactory::open(was.toStdString());
            if (o.get()) { o->readMetadata(); old = o->xmpData(); }
        }
        if (!compose(old, data, edit, x, &darktableThere, error)) return false;
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::create(Exiv2::ImageType::xmp, tmp.toStdString());
        if (!img.get()) { if (error) *error = QStringLiteral("cannot create %1").arg(tmp); return false; }
        img->setXmpData(x);
        img->writeMetadata();
    } catch (const Exiv2::Error &e) {
        if (error) *error = QString::fromUtf8(e.what());
        QFile::remove(tmp);
        return false;
    }
    if (!replaceWith(tmp, sp, error)) return false;
    // The former name (OmaRAW's before, photo.NEF.xmp) has been carried into
    // the new one and goes, unless darktable wrote its own edit there: that
    // file is darktable's, and darktable reads it under that name.
    if (!was.isEmpty() && was != sp && !darktableThere) QFile::remove(was);
    return true;
}

// In the file itself (a DNG, JPEG or TIFF), using embedded XMP: on a
// copy beside it, put in its place in one step, so the photograph is never
// left half-written.
bool writeEmbedded(const QString &path, const Sidecar &data, const SidecarEdit *edit, QString *error) {
    registerNamespaces();
    if (!embeddable(path)) { if (error) *error = QStringLiteral("%1 cannot carry its metadata inside").arg(QFileInfo(path).fileName()); return false; }
    const QString tmp = path + QStringLiteral(".omaraw-part");
    QFile::remove(tmp);
    if (!QFile::copy(path, tmp)) { if (error) *error = QStringLiteral("cannot copy %1").arg(QFileInfo(path).fileName()); return false; }
    try {
        auto img = Exiv2::ImageFactory::open(tmp.toStdString());
        if (!img.get()) { if (error) *error = QStringLiteral("cannot open %1").arg(QFileInfo(path).fileName()); QFile::remove(tmp); return false; }
        img->readMetadata();
        Exiv2::XmpData x;
        if (!compose(img->xmpData(), data, edit, x, nullptr, error, true)) { QFile::remove(tmp); return false; }
        img->setXmpData(x);
        img->writeMetadata();
    } catch (const Exiv2::Error &e) {
        if (error) *error = QString::fromUtf8(e.what());
        QFile::remove(tmp);
        return false;
    }
    return replaceWith(tmp, path, error);
}

bool writeSidecar(const QString &path, int rating, const QString &label, const QStringList &keywords,
                  const QString &title, const QString &caption, QString *error) {
    Sidecar data;
    data.rating = rating; data.label = label; data.keywords = keywords; data.title = title; data.caption = caption;
    return writeSidecar(path, data, nullptr, error);
}

Describe describeFromMap(const QVariantMap &m) {
    Describe d;
    d.title = m.value(QStringLiteral("title")).toString().trimmed();
    d.caption = m.value(QStringLiteral("caption")).toString().trimmed();
    d.copyright = m.value(QStringLiteral("copyright")).toString().trimmed();
    d.creator = m.value(QStringLiteral("creator")).toString().trimmed();
    d.ppi = m.value(QStringLiteral("ppi")).toInt();
    d.keywords = m.value(QStringLiteral("keywords")).toStringList();
    d.keywordsSpecified = m.contains(QStringLiteral("keywords"));
    d.catalogValues = m.contains(QStringLiteral("title")) || m.contains(QStringLiteral("caption"))
        || m.contains(QStringLiteral("creator")) || m.contains(QStringLiteral("copyright"));
    return d;
}

int resolutionPpi(const QString &path) {
    try {
        auto img = Exiv2::ImageFactory::open(path.toStdString());
        if (!img.get()) return 0;
        img->readMetadata();
        const Exiv2::ExifData &ex = img->exifData();
        auto x = ex.findKey(Exiv2::ExifKey("Exif.Image.XResolution"));
        if (x == ex.end()) return 0;
        const Exiv2::Rational r = x->toRational();
        if (r.second == 0) return 0;
        double v = double(r.first) / r.second;
        auto u = ex.findKey(Exiv2::ExifKey("Exif.Image.ResolutionUnit"));
        if (u != ex.end() && u->toInt64() == 3) v *= 2.54;   // centimetres → inches
        return int(std::lround(v));
    } catch (...) { return 0; }
}

static void setLangAlt(Exiv2::XmpData &x, const char *key, const QString &text) {
    auto it = x.findKey(Exiv2::XmpKey(key));
    if (it != x.end()) x.erase(it);
    if (text.isEmpty()) return;
    Exiv2::Value::UniquePtr v = Exiv2::Value::create(Exiv2::langAlt);
    v->read(text.toStdString());
    x.add(Exiv2::XmpKey(key), v.get());
}

static void prepareExportMetadata(Exiv2::ExifData &ex, Exiv2::XmpData &x, const QString &software,
                                  bool stripHistory, const Describe &describe, int flags) {
    const bool exif = flags < 0 || (flags & 1);
    const bool location = flags < 0 || (flags & 4);
    const bool keywords = flags < 0 || (flags & 8);
    if (!exif) ex.clear();
    if (!location) for (auto it = ex.begin(); it != ex.end();) {
        if (it->key().rfind("Exif.GPSInfo.", 0) == 0) it = ex.erase(it); else ++it;
    }
    for (auto it = x.begin(); it != x.end();) {
        const QString key = QString::fromStdString(it->key());
        const bool geo = key.contains("GPS", Qt::CaseInsensitive) || key.contains("LocationCreated") || key.contains("LocationShown")
            || key == "Xmp.Iptc4xmpCore.Location" || key == "Xmp.photoshop.City" || key == "Xmp.photoshop.State" || key == "Xmp.photoshop.Country";
        const bool tag = key == "Xmp.dc.subject" || key == "Xmp.lr.hierarchicalSubject" || key == "Xmp.digiKam.TagsList";
        if ((!location && geo) || ((!keywords || describe.keywordsSpecified) && tag)
            || (stripHistory && key.startsWith("Xmp.darktable."))
            || (!exif && (key.startsWith("Xmp.exif.") || key.startsWith("Xmp.exifEX.") || key.startsWith("Xmp.tiff.")))) it = x.erase(it);
        else ++it;
    }
    ex["Exif.Image.Software"] = software.toStdString();
    x["Xmp.xmp.CreatorTool"] = software.toStdString();
    if (describe.ppi > 0) {
        ex["Exif.Image.XResolution"] = Exiv2::Rational(describe.ppi, 1);
        ex["Exif.Image.YResolution"] = Exiv2::Rational(describe.ppi, 1);
        ex["Exif.Image.ResolutionUnit"] = uint16_t(2);
    }
    if (describe.catalogValues) {
        for (const char *key : {"Exif.Image.ImageDescription", "Exif.Image.Copyright", "Exif.Image.Artist"}) {
            const auto it = ex.findKey(Exiv2::ExifKey(key)); if (it != ex.end()) ex.erase(it);
        }
        for (const char *key : {"Xmp.dc.title", "Xmp.dc.description", "Xmp.dc.rights", "Xmp.dc.creator"}) {
            const auto it = x.findKey(Exiv2::XmpKey(key)); if (it != x.end()) x.erase(it);
        }
    }
    if (!describe.caption.isEmpty()) ex["Exif.Image.ImageDescription"] = describe.caption.toStdString();
    if (!describe.copyright.isEmpty()) ex["Exif.Image.Copyright"] = describe.copyright.toStdString();
    if (!describe.creator.isEmpty()) ex["Exif.Image.Artist"] = describe.creator.toStdString();
    if (describe.catalogValues || !describe.title.isEmpty()) setLangAlt(x, "Xmp.dc.title", describe.title);
    if (describe.catalogValues || !describe.caption.isEmpty()) setLangAlt(x, "Xmp.dc.description", describe.caption);
    if (describe.catalogValues || !describe.copyright.isEmpty()) setLangAlt(x, "Xmp.dc.rights", describe.copyright);
    if (!describe.creator.isEmpty()) {
        auto it = x.findKey(Exiv2::XmpKey("Xmp.dc.creator")); if (it != x.end()) x.erase(it);
        auto value = Exiv2::Value::create(Exiv2::xmpSeq); value->read(describe.creator.toStdString());
        x.add(Exiv2::XmpKey("Xmp.dc.creator"), value.get());
    }
    if (keywords && describe.keywordsSpecified && !describe.keywords.isEmpty()) {
        auto value = Exiv2::Value::create(Exiv2::xmpBag);
        for (const auto &keyword : describe.keywords) value->read(keyword.toStdString());
        x.add(Exiv2::XmpKey("Xmp.dc.subject"), value.get());
        x.add(Exiv2::XmpKey("Xmp.lr.hierarchicalSubject"), value.get());
    }
}

bool stampExport(const QString &path, const QString &software, bool stripHistory, const Describe &describe, int flags) {
    try {
        auto img = Exiv2::ImageFactory::open(path.toStdString());
        if (!img.get()) return false;
        img->readMetadata();
        prepareExportMetadata(img->exifData(), img->xmpData(), software, stripHistory, describe, flags);
        auto &iptc = img->iptcData();
        for (auto it = iptc.begin(); it != iptc.end();) {
            const QString key = QString::fromStdString(it->key());
            const bool geo = key == "Iptc.Application2.City" || key == "Iptc.Application2.SubLocation"
                || key == "Iptc.Application2.ProvinceState" || key == "Iptc.Application2.CountryName" || key == "Iptc.Application2.CountryCode";
            const bool word = key == "Iptc.Application2.ObjectName" || key == "Iptc.Application2.Caption"
                || key == "Iptc.Application2.Copyright" || key == "Iptc.Application2.Byline";
            if ((geo && flags >= 0 && !(flags & 4)) || (word && describe.catalogValues)
                || (key == "Iptc.Application2.Keywords" && (describe.keywordsSpecified || (flags >= 0 && !(flags & 8))))) it = iptc.erase(it);
            else ++it;
        }
        img->writeMetadata();
        return true;
    } catch (const Exiv2::Error &e) {
        qWarning() << "stampExport:" << path << e.what();
        return false;
    }
}

bool copyProcessedMetadata(const QString &source, const QString &destination, QString *error) {
    try {
        auto from = Exiv2::ImageFactory::open(source.toStdString());
        auto to = Exiv2::ImageFactory::open(destination.toStdString());
        from->readMetadata(); to->readMetadata();
        auto &exif = to->exifData();
        for (const auto &entry : from->exifData()) {
            const QString key = QString::fromStdString(entry.key());
            if ((key.startsWith("Exif.Photo.") && key != "Exif.Photo.MakerNote"
                 && key != "Exif.Photo.ColorSpace" && key != "Exif.Photo.PixelXDimension"
                 && key != "Exif.Photo.PixelYDimension") || key.startsWith("Exif.GPSInfo.")
                || key == "Exif.Image.Make" || key == "Exif.Image.Model" || key == "Exif.Image.DateTime"
                || key == "Exif.Image.Artist" || key == "Exif.Image.Copyright" || key == "Exif.Image.ImageDescription")
                exif[entry.key()] = entry.value();
        }
        exif["Exif.Image.Orientation"] = uint16_t(1);
        exif["Exif.Photo.ColorSpace"] = uint16_t(65535); // linear DNG profile, not encoded sRGB
        exif["Exif.Image.Software"] = "OmaRAW " OMARAW_VERSION " (AI removal)";
        auto xmp = from->xmpData();
        for (auto it = xmp.begin(); it != xmp.end();) {
            const QString key = QString::fromStdString(it->key());
            if (key.startsWith("Xmp.darktable.") || key.startsWith("Xmp.crs.") || key == "Xmp.tiff.Orientation") it = xmp.erase(it);
            else ++it;
        }
        to->setXmpData(xmp); to->setIptcData(from->iptcData()); to->writeMetadata(); return true;
    } catch (const std::exception &e) {
        if (error) *error = QString::fromUtf8(e.what()); return false;
    }
}

bool copyDenoiseMetadata(const QString &source, const QString &destination, QString *error) {
    try {
        auto from = Exiv2::ImageFactory::open(source.toStdString());
        auto to = Exiv2::ImageFactory::open(destination.toStdString());
        from->readMetadata(); to->readMetadata();
        auto &exif = to->exifData();
        for (const auto &entry : from->exifData()) {
            const QString key = QString::fromStdString(entry.key());
            // Maker notes can contain raw offsets, dimensions and CFA calibration.
            if ((key.startsWith("Exif.Photo.") && key != "Exif.Photo.MakerNote"
                 && key != "Exif.Photo.PixelXDimension" && key != "Exif.Photo.PixelYDimension"
                 && key != "Exif.Photo.ColorSpace") || key.startsWith("Exif.GPSInfo.")
                || key == "Exif.Image.DateTime" || key == "Exif.Image.Artist"
                || key == "Exif.Image.Copyright" || key == "Exif.Image.ImageDescription")
                exif[entry.key()] = entry.value();
        }
        exif["Exif.Image.Software"] = "OmaRAW " OMARAW_VERSION " (AI denoise / RawForge float32)";
        AssetRecord capture;
        if (read(source, capture)) {
            // Some cameras store ISO only in maker notes, which are unsafe to
            // transplant into a newly constructed DNG.
            if (capture.iso > 0) {
                exif["Exif.Photo.ISOSpeedRatings"] = uint16_t(qMin(capture.iso, 65535));
                exif["Exif.Photo.SensitivityType"] = uint16_t(3);
                exif["Exif.Photo.ISOSpeed"] = uint32_t(capture.iso);
            }
            if (!capture.lens.isEmpty()) exif["Exif.Photo.LensModel"] = capture.lens.toStdString();
        }
        // Only descriptive namespaces: do not carry DNG gain maps, profiles,
        // orientation/dimensions or either application's edit history across.
        Exiv2::XmpData xmp;
        for (const auto &entry : from->xmpData()) {
            const QString key = QString::fromStdString(entry.key());
            if (key.startsWith("Xmp.dc.") || key.startsWith("Xmp.photoshop.") || key.startsWith("Xmp.iptc.")
                || key.startsWith("Xmp.Iptc4xmpCore.") || key.startsWith("Xmp.Iptc4xmpExt.") || key == "Xmp.xmp.Rating" || key == "Xmp.xmp.Label")
                xmp[entry.key()] = entry.value();
        }
        to->setXmpData(xmp); to->setIptcData(from->iptcData()); to->writeMetadata(); return true;
    } catch (const std::exception &e) { if (error) *error = QString::fromUtf8(e.what()); return false; }
}

bool exportPackets(const QByteArray &exif, const QByteArray &xmp, int flags, const Describe &describe,
                   QByteArray *outExif, QByteArray *outXmp, QString *error) {
    try {
        Exiv2::ExifData ex;
        Exiv2::XmpData x;
        if (!exif.isEmpty()) Exiv2::ExifParser::decode(ex, reinterpret_cast<const uint8_t *>(exif.constData()), exif.size());
        if (!xmp.isEmpty() && Exiv2::XmpParser::decode(x, xmp.toStdString())) throw std::runtime_error("Cannot read source XMP metadata");
        prepareExportMetadata(ex, x, QStringLiteral("OmaRAW ") + QStringLiteral(OMARAW_VERSION), flags >= 0 && !(flags & 32), describe, flags);
        Exiv2::Blob blob; Exiv2::ExifParser::encode(blob, Exiv2::bigEndian, ex);
        std::string packet;
        if (Exiv2::XmpParser::encode(packet, x, Exiv2::XmpParser::useCompactFormat | Exiv2::XmpParser::omitPacketWrapper))
            throw std::runtime_error("Cannot write export XMP metadata");
        *outExif = QByteArray(reinterpret_cast<const char *>(blob.data()), blob.size());
        *outXmp = QByteArray::fromStdString(packet);
        error->clear(); return true;
    } catch (const std::exception &e) { *error = QString::fromUtf8(e.what()); return false; }
}

QString softwareTag(const QString &path) {
    try {
        auto img = Exiv2::ImageFactory::open(path.toStdString());
        if (!img.get()) return QString();
        img->readMetadata();
        const Exiv2::ExifData &ex = img->exifData();
        auto it = ex.findKey(Exiv2::ExifKey("Exif.Image.Software"));
        return it != ex.end() ? QString::fromStdString(it->toString()) : QString();
    } catch (...) { return QString(); }
}

int exifCount(const QString &path) {
    try {
        auto img = Exiv2::ImageFactory::open(path.toStdString());
        if (!img.get()) return 0;
        img->readMetadata();
        return int(img->exifData().count());
    } catch (...) { return 0; }
}

bool hasDarktableXmp(const QString &path) {
    try {
        auto img = Exiv2::ImageFactory::open(path.toStdString());
        if (!img.get()) return false;
        img->readMetadata();
        for (const auto &d : img->xmpData()) if (d.key().rfind("Xmp.darktable.", 0) == 0) return true;
        return false;
    } catch (...) { return false; }
}

QString formatShutter(double s) {
    if (s <= 0) return QString();
    if (s >= 1.0) {
        if (std::fabs(s - std::round(s)) < 0.01) return QStringLiteral("%1 s").arg(int(std::round(s)));
        return QStringLiteral("%1 s").arg(s, 0, 'f', 1);
    }
    const double inv = 1.0 / s;
    if (std::fabs(inv - std::round(inv)) < 0.05 || inv > 10)
        return QStringLiteral("1/%1 s").arg(int(std::round(inv)));
    return QStringLiteral("%1 s").arg(s, 0, 'f', 2);
}

QString formatAperture(double f) {
    if (f <= 0) return QString();
    return QStringLiteral("f/%1").arg(f, 0, 'f', 1);
}

QString formatFocal(double mm) {
    if (mm <= 0) return QString();
    if (std::fabs(mm - std::round(mm)) < 0.05) return QStringLiteral("%1 mm").arg(int(std::round(mm)));
    return QStringLiteral("%1 mm").arg(mm, 0, 'f', 1);
}

QString formatIso(int iso) {
    return iso > 0 ? QStringLiteral("ISO %1").arg(iso) : QString();
}

QString formatSize(qint64 b) {
    if (b < 0) return QString();
    if (b < 1024) return QStringLiteral("%1 B").arg(b);
    if (b < 1024 * 1024) return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 0);
    if (b < 1024LL * 1024 * 1024) return QStringLiteral("%1 MB").arg(b / (1024.0 * 1024), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(b / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

QString formatExposure(const AssetRecord &r) {
    QStringList parts;
    const QString sh = formatShutter(r.shutter);
    const QString ap = formatAperture(r.aperture);
    if (!sh.isEmpty() && !ap.isEmpty()) return QStringLiteral("%1 at %2").arg(sh.chopped(2) + QStringLiteral(" sec"), ap);
    if (!sh.isEmpty()) return sh;
    return ap;
}

QString formatCaptured(const QString &iso, bool withTime) {
    const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid()) return QString();
    return dt.toString(withTime ? QStringLiteral("yyyy-MM-dd HH:mm") : QStringLiteral("yyyy-MM-dd"));
}

} // namespace Metadata

extern "C" int oma_export_metadata(const void *exif, int exif_size, const char *xmp, int flags,
                                    const char *description_json, void **out_exif, int *out_size,
                                    char **out_xmp, char *error, int error_size) {
    *out_exif = nullptr; *out_xmp = nullptr; *out_size = 0;
    try {
    QByteArray encoded, packet;
    QString problem;
    const auto describe = Metadata::describeFromMap(QJsonDocument::fromJson(QByteArray(description_json ? description_json : "{}")).object().toVariantMap());
    if (!Metadata::exportPackets(QByteArray(static_cast<const char *>(exif), exif_size), QByteArray(xmp ? xmp : ""), flags, describe, &encoded, &packet, &problem)) {
        if (error && error_size > 0) std::snprintf(error, size_t(error_size), "%s", problem.toUtf8().constData());
        return -1;
    }
    *out_exif = std::malloc(size_t(encoded.size()));
    *out_xmp = static_cast<char *>(std::malloc(size_t(packet.size()) + 1));
    if (!*out_exif || !*out_xmp) {
        std::free(*out_exif); std::free(*out_xmp); *out_exif = nullptr; *out_xmp = nullptr;
        if (error && error_size > 0) std::snprintf(error, size_t(error_size), "Cannot allocate export metadata");
        return -1;
    }
    std::memcpy(*out_exif, encoded.constData(), size_t(encoded.size())); *out_size = int(encoded.size());
    std::memcpy(*out_xmp, packet.constData(), size_t(packet.size())); (*out_xmp)[packet.size()] = 0;
    return 0;
    } catch (const std::exception &e) {
        std::free(*out_exif); std::free(*out_xmp); *out_exif = nullptr; *out_xmp = nullptr; *out_size = 0;
        if (error && error_size > 0) std::snprintf(error, size_t(error_size), "%s", e.what());
        return -1;
    }
}
