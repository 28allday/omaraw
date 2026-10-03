#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

// Lossless scene-linear snapshot overviews. Legacy JPEG previews remain
// readable. All decoding is bounded before allocating image/payload storage.
namespace SnapshotPreview {
constexpr int MaxEdge = 1024;
QByteArray encode(const QImage &linear);
QImage decode(const QByteArray &stored);
// Independent read-only connection: safe for asynchronous image providers.
QByteArray read(const QString &catalogPath, int id);
}
