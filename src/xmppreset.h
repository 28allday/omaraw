#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace XmpPreset {
// Namespace-aware, bounded RDF properties, shared with creative-profile import.
QVariantMap readXmpProperties(const QByteArray &bytes, QString &error);
// A Develop preset converted to explicit OmaRAW settings. Every unhandled
// setting is reported; parsing never evaluates Lua or resolves XML resources.
struct Result {
    QString name, category, error;
    QVariantList values;
    QStringList converted, warnings;
};
Result read(const QByteArray &bytes, const QString &fileName);
// A photo's own XMP edit, from its XMP sidecar (the crs: settings).
// `raw`: the photo is a RAW, whose white balance is absolute and whose
// exposure starts where OmaRAW's rendering of a RAW does.
Result readSidecar(const QByteArray &bytes, bool raw);
// The other way: OmaRAW's settings (curated rows as the engine reports them,
// and its point curve) as CRS settings, for compatible editors
// to open roughly the same look. Always approximate.
QVariantMap cameraRawFrom(const QVariantList &values, const QVariantMap &curve, bool raw);
}
