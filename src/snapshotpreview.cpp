#include "snapshotpreview.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QImageReader>
#include <QtEndian>
#include <sqlite3.h>
#include <zlib.h>
#include <cmath>
#include <cstring>

namespace SnapshotPreview {
namespace {
constexpr char Magic[] = "OMASNP1";
constexpr qsizetype HeaderSize = 8 + 4 + 4 + 32;
constexpr qsizetype MaxBytes = qsizetype(MaxEdge) * MaxEdge * 16;
constexpr qsizetype MaxStored = MaxBytes + 65536;
bool finite(const QImage &image) {
    for (int y = 0; y < image.height(); ++y) {
        const float *row = reinterpret_cast<const float *>(image.constScanLine(y));
        for (int x = 0; x < image.width() * 4; ++x) if (!std::isfinite(row[x])) return false;
    }
    return true;
}
}

QByteArray encode(const QImage &linear) {
    if (linear.isNull() || linear.format() != QImage::Format_RGBA32FPx4
        || linear.colorSpace() != QColorSpace(QColorSpace::SRgbLinear)) return {};
    const QImage image = qMax(linear.width(), linear.height()) > MaxEdge
        ? linear.scaled(MaxEdge, MaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation) : linear;
    if (image.isNull() || !finite(image)) return {};
    const qsizetype rowBytes = qsizetype(image.width()) * 16;
    QByteArray pixels(rowBytes * image.height(), Qt::Uninitialized);
    for (int y = 0; y < image.height(); ++y) {
        char *to = pixels.data() + y * rowBytes;
        std::memcpy(to, image.constScanLine(y), size_t(rowBytes));
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
        for (qsizetype x = 0; x < rowBytes; x += 4) {
            quint32 bits; std::memcpy(&bits, to + x, 4); qToLittleEndian(bits, to + x);
        }
#endif
    }
    QByteArray result(HeaderSize, '\0');
    std::memcpy(result.data(), Magic, 8);
    qToLittleEndian(quint32(image.width()), result.data() + 8);
    qToLittleEndian(quint32(image.height()), result.data() + 12);
    const QByteArray digest = QCryptographicHash::hash(pixels, QCryptographicHash::Sha256);
    std::memcpy(result.data() + 16, digest.constData(), 32);
    result += qCompress(pixels, 6);
    return result;
}

QImage decode(const QByteArray &stored) {
    if (stored.isEmpty() || stored.size() > MaxStored) return {};
    if (stored.size() < 8 || std::memcmp(stored.constData(), Magic, 8)) {
        QBuffer input; input.setData(stored); input.open(QIODevice::ReadOnly);
        QImageReader reader(&input, "JPEG");
        const QSize size = reader.size();
        if (size.width() <= 0 || size.height() <= 0 || size.width() > MaxEdge || size.height() > MaxEdge) return {};
        QImage image = reader.read();
        if (!image.colorSpace().isValid()) image.setColorSpace(QColorSpace::SRgb);
        return image;
    }
    if (stored.size() < HeaderSize + 4) return {};
    const quint32 width = qFromLittleEndian<quint32>(stored.constData() + 8);
    const quint32 height = qFromLittleEndian<quint32>(stored.constData() + 12);
    if (!width || !height || width > MaxEdge || height > MaxEdge) return {};
    const qsizetype expected = qsizetype(width) * height * 16;
    const QByteArray compressed = stored.mid(HeaderSize);
    // Validate both the advertised size and the actual zlib stream. A forged
    // qCompress size must not cause a decoder to grow past our fixed buffer.
    if (qFromBigEndian<quint32>(compressed.constData()) != expected) return {};
    QByteArray pixels(expected, Qt::Uninitialized);
    uLongf actual = uLongf(expected); uLong inputBytes = uLong(compressed.size() - 4);
    const int result = uncompress2(reinterpret_cast<Bytef *>(pixels.data()), &actual,
        reinterpret_cast<const Bytef *>(compressed.constData() + 4), &inputBytes);
    if (result != Z_OK || actual != uLongf(expected) || inputBytes != uLong(compressed.size() - 4)
        || QCryptographicHash::hash(pixels, QCryptographicHash::Sha256) != stored.mid(16, 32)) return {};
    QImage image(int(width), int(height), QImage::Format_RGBA32FPx4);
    if (image.isNull()) return {};
    const qsizetype rowBytes = qsizetype(width) * 16;
    for (quint32 y = 0; y < height; ++y) {
        uchar *to = image.scanLine(int(y));
        const char *from = pixels.constData() + y * rowBytes;
        std::memcpy(to, from, size_t(rowBytes));
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
        for (qsizetype x = 0; x < rowBytes; x += 4) {
            const quint32 bits = qFromLittleEndian<quint32>(from + x); std::memcpy(to + x, &bits, 4);
        }
#endif
    }
    if (!finite(image)) return {};
    image.setColorSpace(QColorSpace::SRgbLinear);
    return image;
}

QByteArray read(const QString &catalogPath, int id) {
    if (catalogPath.isEmpty() || id <= 0) return {};
    sqlite3 *db = nullptr;
    const int opened = sqlite3_open_v2(QFile::encodeName(catalogPath).constData(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    QByteArray result;
    if (opened == SQLITE_OK) {
        sqlite3_busy_timeout(db, 1000);
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT CASE WHEN length(preview)<=?2 THEN preview END FROM snapshots WHERE id=?1", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id); sqlite3_bind_int64(stmt, 2, MaxStored);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const int bytes = sqlite3_column_bytes(stmt, 0);
                if (bytes > 0 && bytes <= MaxStored) result = QByteArray(static_cast<const char *>(sqlite3_column_blob(stmt, 0)), bytes);
            }
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return result;
}
}
