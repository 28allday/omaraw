#include "exportstate.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace ExportState {
bool save(const QString &path, const QVariantList &rows, QString *error) {
    QJsonArray jobs;
    QJsonObject profiles;
    for (const auto &value : rows) {
        auto row = value.toMap();
        auto colour = row.value("colour").toMap();
        const QByteArray icc = colour.take("icc").toByteArray();
        if (!icc.isEmpty()) {
            const QString key = QString::fromLatin1(QCryptographicHash::hash(icc, QCryptographicHash::Sha256).toHex());
            profiles.insert(key, QString::fromLatin1(icc.toBase64()));
            colour.insert("iccKey", key);
        }
        row.insert("colour", colour);
        jobs.append(QJsonObject::fromVariantMap(row));
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"jobs", jobs}, {"profiles", profiles}}).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    if (bytes.size() > 64 * 1024 * 1024) { *error = QStringLiteral("The export queue exceeds 64 MB; clear completed jobs."); return false; }
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QStringLiteral("Cannot save the export queue: %1").arg(file.errorString()); return false;
    }
    error->clear();
    return true;
}

bool load(const QString &path, QVariantList *rows, QString *error) {
    rows->clear(); error->clear();
    QFile file(path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
        *error = QStringLiteral("Cannot read the saved export queue."); return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = document.object();
    auto invalid = [&] { *error = QStringLiteral("The saved export queue is damaged or from an unsupported version. Its file has been kept."); return false; };
    if (parse.error != QJsonParseError::NoError || !document.isObject() || object.value("version").toInt() != 1
        || !object.value("jobs").isArray() || !object.value("profiles").isObject()) return invalid();
    const auto jobs = object.value("jobs").toArray();
    if (jobs.size() > 100000) return invalid();
    const auto profiles = object.value("profiles").toObject();
    QVariantList restored;
    for (const auto &job : jobs) {
        if (!job.isObject()) return invalid();
        auto row = job.toObject().toVariantMap();
        const QString status = row.value("status").toString();
        if (!QStringList{"queued", "rendering", "done", "failed", "cancelled"}.contains(status)
            || !QFileInfo(row.value("file").toString()).isAbsolute()
            || !QFileInfo(row.value("folder").toString()).isAbsolute()
            || !QStringList{"jpeg", "tiff", "png", "webp", "avif", "jpegxl", "psd"}.contains(row.value("format").toString())
            || row.value("stem").toString().isEmpty() || row.value("stem").toString().contains('/')
            || row.value("stem").toString() == "." || row.value("stem").toString() == "..") return invalid();
        auto colour = row.value("colour").toMap();
        const QString key = colour.take("iccKey").toString();
        if (!key.isEmpty()) {
            const QByteArray icc = QByteArray::fromBase64(profiles.value(key).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (icc.isEmpty() || icc.size() > 16 * 1024 * 1024
                || QString::fromLatin1(QCryptographicHash::hash(icc, QCryptographicHash::Sha256).toHex()) != key) return invalid();
            colour.insert("icc", icc);
        }
        if (colour.value("profile").toInt() == 3 && colour.value("icc").toByteArray().isEmpty()) return invalid();
        row.insert("colour", colour);
        if (status == "rendering") {
            row.insert("status", "queued");
            row.insert("error", QStringLiteral("Interrupted by shutdown. Resume creates a new file if the previous output exists."));
        }
        restored.append(row);
    }
    *rows = restored;
    return true;
}
}
