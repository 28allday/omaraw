#include "psdwriter.h"

#include <QColorSpace>
#include <QDataStream>
#include <QtEndian>
#include <QFile>
#include <QImageReader>
#include <QSaveFile>
#include <QDebug>

#include <exiv2/exiv2.hpp>

#include <algorithm>
#include <vector>

namespace Psd {

namespace {
// One "8BIM" image resource block: id, an empty name, even-padded data.
void resource(QDataStream &s, quint16 id, const QByteArray &data) {
    s.writeRawData("8BIM", 4);
    s << id;
    s << quint16(0);                       // pascal string "" padded to two bytes
    s << quint32(data.size());
    s.writeRawData(data.constData(), data.size());
    if (data.size() & 1) s << quint8(0);
}
} // namespace

QString write(const QString &path, const QImage &src, int bpp, const QByteArray &icc, int ppi) {
    if (src.isNull()) return QStringLiteral("nothing to write");
    if (src.width() > 30000 || src.height() > 30000) return QStringLiteral("PSD holds at most 30000 px a side");
    bpp = bpp == 16 ? 16 : 8;
    const QImage img = src.convertToFormat(bpp == 16 ? QImage::Format_RGBX64 : QImage::Format_RGB32);
    const int w = img.width(), h = img.height();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QStringLiteral("cannot write %1").arg(path);
    QDataStream s(&file);
    s.setByteOrder(QDataStream::BigEndian);

    // header
    s.writeRawData("8BPS", 4);
    s << quint16(1);                       // version 1: PSD
    s.writeRawData("\0\0\0\0\0\0", 6);
    s << quint16(3);                       // channels
    s << quint32(h) << quint32(w);
    s << quint16(bpp);
    s << quint16(3);                       // colour mode RGB
    s << quint32(0);                       // colour mode data: none

    // image resources: resolution, colour profile
    QByteArray res;
    {
        QDataStream r(&res, QIODevice::WriteOnly);
        r.setByteOrder(QDataStream::BigEndian);
        if (ppi > 0) {
            QByteArray info;
            QDataStream i(&info, QIODevice::WriteOnly);
            i.setByteOrder(QDataStream::BigEndian);
            // fixed 16.16 pixels per inch, unit 1 = per inch, size unit 1 = inches; twice.
            // The whole part has 16 bits, so a larger number would wrap and
            // write a resolution nobody asked for. The dialog offers at most
            // 600, but a saved output template carries this value too.
            const quint32 fixed = quint32(std::min(ppi, 0xffff)) << 16;
            i << fixed << quint16(1) << quint16(1) << fixed << quint16(1) << quint16(1);
            resource(r, 1005, info);
        }
        if (!icc.isEmpty()) resource(r, 1039, icc);
    }
    s << quint32(res.size());
    s.writeRawData(res.constData(), res.size());

    s << quint32(0);                       // layer and mask information: none, flat
    s << quint16(0);                       // image data: raw, planar, big-endian

    // planes R, G, B; rows top to bottom
    if (bpp == 16) {
        std::vector<quint16> line(static_cast<size_t>(w));
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < h; ++y) {
                const quint16 *p = reinterpret_cast<const quint16 *>(img.constScanLine(y));
                for (int x = 0; x < w; ++x) line[size_t(x)] = qToBigEndian<quint16>(p[x * 4 + c]);
                s.writeRawData(reinterpret_cast<const char *>(line.data()), w * 2);
            }
    } else {
        std::vector<quint8> line(static_cast<size_t>(w));
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < h; ++y) {
                const QRgb *p = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = 0; x < w; ++x) line[size_t(x)] = quint8(c == 0 ? qRed(p[x]) : c == 1 ? qGreen(p[x]) : qBlue(p[x]));
                s.writeRawData(reinterpret_cast<const char *>(line.data()), w);
            }
    }
    if (s.status() != QDataStream::Ok || !file.commit()) return QStringLiteral("write failed: %1").arg(file.errorString());
    return QString();
}

QString fromTiff(const QString &tiff, const QString &psd, int bpp, int ppi) {
    QImageReader reader(tiff);
    reader.setAutoTransform(false);
    const QImage img = reader.read();
    if (img.isNull()) return QStringLiteral("cannot read the engine's TIFF: %1").arg(reader.errorString());
    // The profile the engine embedded, byte for byte, and the metadata it
    // wrote (EXIF, IPTC, XMP), which the PSD carries in its resources; Qt's
    // parse of the profile as a fallback (a standard profile it knows).
    QByteArray icc;
    Exiv2::ExifData exif; Exiv2::IptcData iptc; Exiv2::XmpData xmp;
    try {
        auto ex = Exiv2::ImageFactory::open(tiff.toStdString());
        if (ex.get()) {
            ex->readMetadata();
            const Exiv2::DataBuf &buf = ex->iccProfile();
            if (!buf.empty()) icc = QByteArray(reinterpret_cast<const char *>(buf.c_data()), int(buf.size()));
            exif = ex->exifData(); iptc = ex->iptcData(); xmp = ex->xmpData();
        }
    } catch (const Exiv2::Error &e) { qWarning() << "psd: metadata from" << tiff << e.what(); }
    if (icc.isEmpty() && img.colorSpace().isValid()) icc = img.colorSpace().iccProfile();
    const QString err = write(psd, img, bpp, icc, ppi);
    if (!err.isEmpty()) return err;
    if (!exif.empty() || !iptc.empty() || !xmp.empty()) {
        try {
            auto out = Exiv2::ImageFactory::open(psd.toStdString());
            if (out.get()) {
                out->readMetadata();
                // TIFF structure tags describe the container, not the picture
                for (auto it = exif.begin(); it != exif.end();) {
                    const std::string k = it->key();
                    const bool structural = k == "Exif.Image.ImageWidth" || k == "Exif.Image.ImageLength" || k == "Exif.Image.BitsPerSample"
                        || k == "Exif.Image.Compression" || k == "Exif.Image.PhotometricInterpretation" || k == "Exif.Image.StripOffsets"
                        || k == "Exif.Image.SamplesPerPixel" || k == "Exif.Image.RowsPerStrip" || k == "Exif.Image.StripByteCounts"
                        || k == "Exif.Image.PlanarConfiguration" || k == "Exif.Image.ExtraSamples" || k == "Exif.Image.SampleFormat"
                        || k == "Exif.Image.Predictor" || k == "Exif.Image.InterColorProfile";
                    it = structural ? exif.erase(it) : std::next(it);
                }
                out->setExifData(exif); out->setIptcData(iptc); out->setXmpData(xmp);
                out->writeMetadata();
            }
        } catch (const Exiv2::Error &e) {
            qWarning() << "psd: metadata into" << psd << e.what();
            return QStringLiteral("PSD written without its metadata: %1").arg(QString::fromUtf8(e.what()));
        }
    }
    QFile::remove(tiff);
    return QString();
}

} // namespace Psd
