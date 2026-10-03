// Durable, verified full working copies and reduced Smart Previews, stored
// separately from the evictable cache and attached to original photo paths.
#pragma once
#include "offlinebridge.h"
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <functional>

class OfflineStore {
public:
    explicit OfflineStore(bool smart = false) : m_smart(smart) {}
    static OfflineStore &instance();
    static OfflineStore &previews();
    bool createPreview(const QString &original, int imageId, const std::atomic_bool *cancel, QString *error);
    QByteArray readPreview(const QString &file) const;
    bool open(const QString &directory, QString *error);
    QVariantMap info(const QString &original) const;
    // Return the readable original, otherwise a checksum-verified copy.
    // Failure returns an empty path and leaves both files untouched.
    QString resolve(const QString &original, QString *error = nullptr) const;
    bool create(const QString &original, const std::atomic_bool *cancel, QString *error,
                const std::function<void(qint64, qint64)> &progress = {});
    bool remove(const QString &original, QString *error);
    bool relink(const QString &oldPath, const QString &newPath, bool folder, QString *error);
    QString directory() const;

private:
    bool save(const QJsonObject &entries, QString *error);
    QString verified(const QJsonObject &entry, QString *error) const;
    mutable QMutex m_mutex;
    QMutex m_operation;
    const bool m_smart;
    QString m_root, m_error;
    QJsonObject m_entries;
    QHash<QString, QJsonObject> m_files;
    mutable QHash<QString, QByteArray> m_verified;
};
