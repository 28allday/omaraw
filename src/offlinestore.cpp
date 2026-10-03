#include "offlinestore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <cstring>
#include <cstdlib>
#include <QtEndian>
#ifdef OMARAW_ENGINE
#include "engine/bridge.h"
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
// Rebuild older generators when requested with the original connected. Keep
// their payload readable while offline; a decoder update must not strand edits.
constexpr int smartGeneratorVersion = 2;
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
QByteArray statSignature(const struct stat &s) {
    if (!S_ISREG(s.st_mode)) return {};
    return QByteArray::number(s.st_dev) + ':' + QByteArray::number(s.st_ino) + ':' + QByteArray::number(s.st_size)
        + ':' + QByteArray::number(s.st_mtim.tv_sec) + ':' + QByteArray::number(s.st_mtim.tv_nsec)
        + ':' + QByteArray::number(s.st_ctim.tv_sec) + ':' + QByteArray::number(s.st_ctim.tv_nsec);
}
QByteArray signature(const QString &path) {
    struct stat s{};
    return ::stat(QFile::encodeName(path).constData(), &s) ? QByteArray() : statSignature(s);
}
QByteArray signature(int fd) {
    struct stat s{};
    return ::fstat(fd, &s) ? QByteArray() : statSignature(s);
}
QByteArray checksum(const QString &path, const std::atomic_bool *cancel = nullptr) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray before = signature(file.handle());
    if (before.isEmpty() || before != signature(path)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancel && cancel->load()) return {};
        const QByteArray part = file.read(1024 * 1024);
        if (part.isEmpty() && file.error() != QFile::NoError) return {};
        hash.addData(part);
    }
    return before == signature(file.handle()) && before == signature(path) ? hash.result().toHex() : QByteArray();
}
bool syncDirectory(const QString &path) {
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const bool ok = fd >= 0 && ::fsync(fd) == 0;
    if (fd >= 0) ::close(fd);
    return ok;
}
QString normalized(const QString &path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
}

OfflineStore &OfflineStore::instance() { static OfflineStore store; return store; }
OfflineStore &OfflineStore::previews() { static OfflineStore store(true); return store; }

bool OfflineStore::open(const QString &directory, QString *error) {
    QMutexLocker operation(&m_operation);
    const QString root = normalized(directory);
    QJsonObject entries;
    QString problem;
    QFile file(root + "/index.json");
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) problem = QStringLiteral("Cannot read offline working-copy index; existing files retained");
        else {
            QJsonParseError parse;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
            const QJsonObject obj = doc.object();
            if (parse.error != QJsonParseError::NoError || !doc.isObject() || obj.value("version").toInt() != 1
                || !obj.value("entries").isObject() || obj.value("entries").toObject().size() > 100000)
                problem = QStringLiteral("Damaged or unsupported offline working-copy index; existing files retained");
            else {
                entries = obj.value("entries").toObject();
                const QRegularExpression name("^[0-9a-f-]{36}\\.[A-Za-z0-9]{1,16}$"), digest("^[0-9a-f]{64}$");
                for (auto i = entries.begin(); i != entries.end(); ++i) {
                    const auto entry = i.value().toObject();
                    if (!QFileInfo(i.key()).isAbsolute() || !i.value().isObject()
                        || !name.match(entry.value("file").toString()).hasMatch()
                        || !digest.match(entry.value("sha256").toString()).hasMatch()
                        || entry.value("bytes").toInteger() <= 0
                        || (m_smart && (!entry.value("file").toString().endsWith(".omsp")
                            || !digest.match(entry.value("sourceSha256").toString()).hasMatch()
                            || entry.value("width").toInt() < 1 || entry.value("width").toInt() > 2560
                            || entry.value("height").toInt() < 1 || entry.value("height").toInt() > 2560))) {
                        problem = QStringLiteral("Invalid offline working-copy entry; existing files retained"); break;
                    }
                }
            }
        }
    }
    { QMutexLocker lock(&m_mutex); m_root = root; m_error = problem; m_entries = problem.isEmpty() ? entries : QJsonObject(); m_files.clear();
      for (auto it = m_entries.begin(); it != m_entries.end(); ++it) { const auto e = it.value().toObject(); m_files.insert(e.value("file").toString(), e); }
      m_verified.clear(); }
    if (error) *error = problem;
    return problem.isEmpty();
}

QString OfflineStore::directory() const { QMutexLocker lock(&m_mutex); return m_root; }

QVariantMap OfflineStore::info(const QString &original) const {
    QJsonObject entry; QString root, problem;
    { QMutexLocker lock(&m_mutex); entry = m_entries.value(normalized(original)).toObject(); root = m_root; problem = m_error; }
    const QFileInfo copy(root + '/' + entry.value("file").toString());
    bool present = !entry.isEmpty() && copy.isFile() && !copy.isSymLink() && copy.size() == entry.value("bytes").toInteger();
    if (present) { QMutexLocker lock(&m_mutex); present = m_verified.value(copy.absoluteFilePath()) != '!' + signature(copy.absoluteFilePath()); }
    return {{"stored", !entry.isEmpty()}, {"available", present}, {"bytes", entry.value("bytes").toVariant()},
            {"path", entry.isEmpty() ? QString() : copy.absoluteFilePath()}, {"error", problem},
            {"width", entry.value("width").toInt()}, {"height", entry.value("height").toInt()}};
}

QString OfflineStore::verified(const QJsonObject &entry, QString *error) const {
    QString path;
    { QMutexLocker lock(&m_mutex); path = m_root + '/' + entry.value("file").toString(); }
    const QFileInfo file(path);
    const QByteArray stamp = signature(path);
    if (entry.isEmpty() || stamp.isEmpty() || file.isSymLink() || file.size() != entry.value("bytes").toInteger()) {
        fail(error, QStringLiteral("Offline working copy is missing or damaged")); return {};
    }
    {
        QMutexLocker lock(&m_mutex);
        if (m_verified.value(path) == stamp) return path;
        if (m_verified.value(path) == '!' + stamp) {
            fail(error, QStringLiteral("Offline working copy failed checksum verification; files retained")); return {};
        }
    }
    if (checksum(path) != entry.value("sha256").toString().toLatin1() || stamp != signature(path)) {
        { QMutexLocker lock(&m_mutex); m_verified.insert(path, '!' + stamp); }
        fail(error, QStringLiteral("Offline working copy failed checksum verification; files retained")); return {};
    }
    { QMutexLocker lock(&m_mutex); m_verified.insert(path, stamp); }
    return path;
}

QString OfflineStore::resolve(const QString &original, QString *error) const {
    if (error) error->clear();
    const QString path = normalized(original);
    const QFileInfo source(path);
    if (source.isFile() && source.isReadable()) return path;
    QJsonObject entry; QString problem;
    { QMutexLocker lock(&m_mutex); entry = m_entries.value(path).toObject(); problem = m_error; }
    if (!problem.isEmpty()) { fail(error, problem); return {}; }
    if (entry.isEmpty()) { fail(error, QStringLiteral("Original is unavailable and has no offline working copy")); return {}; }
    return verified(entry, error);
}

bool OfflineStore::save(const QJsonObject &entries, QString *error) {
    QString root;
    { QMutexLocker lock(&m_mutex); root = m_root; if (!m_error.isEmpty()) return fail(error, m_error); }
    if (root.isEmpty() || !QDir().mkpath(root)) return fail(error, QStringLiteral("Cannot create offline working-copy folder"));
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 64 * 1024 * 1024 || entries.size() > 100000) return fail(error, QStringLiteral("Offline working-copy index is full"));
    QSaveFile file(root + "/index.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()
        || ::fsync(file.handle()) || !file.commit()) return fail(error, QStringLiteral("Cannot save offline working-copy index: %1").arg(file.errorString()));
    { QMutexLocker lock(&m_mutex); m_entries = entries; m_files.clear();
      for (auto it = entries.begin(); it != entries.end(); ++it) { const auto e = it.value().toObject(); m_files.insert(e.value("file").toString(), e); }
    }
    // Even on a directory sync failure the published copy stays in place.
    return syncDirectory(root) || fail(error, QStringLiteral("Working-copy index saved, but directory sync failed; files retained"));
}

bool OfflineStore::create(const QString &original, const std::atomic_bool *cancel, QString *error,
                          const std::function<void(qint64, qint64)> &progress) {
    if (error) error->clear();
    if (m_smart) return fail(error, QStringLiteral("Use the Smart Preview builder for this store"));
    QMutexLocker operation(&m_operation);
    const QString source = normalized(original);
    QString root; QJsonObject entries;
    { QMutexLocker lock(&m_mutex); root = m_root; entries = m_entries; if (!m_error.isEmpty()) return fail(error, m_error); }
    if (cancel && cancel->load()) return fail(error, QStringLiteral("Cancelled; originals and existing copies retained"));
    const QFileInfo info(source);
    const QString extension = info.suffix();
    const QString physicalRoot = QFileInfo(root).canonicalFilePath();
    if (root.isEmpty() || source.startsWith(root + '/') || (!physicalRoot.isEmpty() && info.canonicalFilePath().startsWith(physicalRoot + '/'))
        || !info.isFile() || !info.isReadable() || info.size() <= 0)
        return fail(error, QStringLiteral("A readable original is required to create an offline working copy"));
    if (!QRegularExpression("^[A-Za-z0-9]{1,16}$").match(extension).hasMatch()) return fail(error, QStringLiteral("Unsupported original file extension"));
    if (!QDir().mkpath(root)) return fail(error, QStringLiteral("Cannot create offline working-copy folder"));
    QFile input(source);
    QTemporaryFile temporary(root + "/.copy-XXXXXX");
    if (!input.open(QIODevice::ReadOnly) || !temporary.open()) return fail(error, QStringLiteral("Cannot open original or working-copy destination"));
    const QByteArray before = signature(input.handle());
    if (before.isEmpty() || before != signature(source)) return fail(error, QStringLiteral("Original changed while opening; try again"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 done = 0;
    while (!input.atEnd()) {
        if (cancel && cancel->load()) return fail(error, QStringLiteral("Cancelled; originals and existing copies retained"));
        const QByteArray part = input.read(1024 * 1024);
        if ((part.isEmpty() && input.error() != QFile::NoError) || temporary.write(part) != part.size())
            return fail(error, QStringLiteral("Cannot finish offline copy; original retained"));
        hash.addData(part); done += part.size();
        if (progress) progress(done, info.size());
    }
    if (before != signature(input.handle()) || before != signature(source) || done != info.size())
        return fail(error, QStringLiteral("Original changed while copying; try again"));
    if (!temporary.flush() || ::fsync(temporary.handle())) return fail(error, QStringLiteral("Cannot flush offline working copy to disk"));
    const QByteArray digest = hash.result().toHex();
    if (checksum(temporary.fileName(), cancel) != digest) return fail(error, cancel && cancel->load()
        ? QStringLiteral("Cancelled; originals and existing copies retained") : QStringLiteral("Offline working copy did not verify; original retained"));
    const auto previous = entries.value(source).toObject();
    if (!previous.isEmpty()) {
        if (previous.value("sha256").toString().toLatin1() != digest)
            return fail(error, QStringLiteral("Original differs from the saved offline copy; both versions retained"));
        if (!verified(previous, nullptr).isEmpty()) return true;
    }
    if (cancel && cancel->load()) return fail(error, QStringLiteral("Cancelled; originals and existing copies retained"));
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces) + '.' + extension;
    const QString destination = root + '/' + name;
    if (::link(QFile::encodeName(temporary.fileName()).constData(), QFile::encodeName(destination).constData()))
        return fail(error, QStringLiteral("Cannot publish verified offline working copy"));
    if (!syncDirectory(root)) return fail(error, QStringLiteral("Copy retained, but directory sync failed"));
    const QJsonObject entry{{"file", name}, {"bytes", done}, {"sha256", QString::fromLatin1(digest)}};
    entries.insert(source, entry);
    // On an index-write failure retain the verified file for recovery.
    if (!save(entries, error)) return false;
    if (!previous.isEmpty() && previous.value("file") != entry.value("file")) QFile::remove(root + '/' + previous.value("file").toString());
    return true;
}

bool OfflineStore::remove(const QString &original, QString *error) {
    if (error) error->clear();
    QMutexLocker operation(&m_operation);
    const QString source = normalized(original);
    QJsonObject entries; QString root;
    { QMutexLocker lock(&m_mutex); entries = m_entries; root = m_root; if (!m_error.isEmpty()) return fail(error, m_error); }
    const auto entry = entries.value(source).toObject();
    if (entry.isEmpty()) return true;
    // A filename alone does not prove that the original came back intact.
    const QString physicalRoot = QFileInfo(root).canonicalFilePath();
    if ((!physicalRoot.isEmpty() && QFileInfo(source).canonicalFilePath().startsWith(physicalRoot + '/'))
        || checksum(m_smart ? OfflineStore::instance().resolve(source) : source) != entry.value(m_smart ? "sourceSha256" : "sha256").toString().toLatin1())
        return fail(error, m_smart ? QStringLiteral("Reconnect the matching original before discarding its Smart Preview") : QStringLiteral("Reconnect the matching original before removing its offline working copy"));
    entries.remove(source);
    if (!save(entries, error)) return false;
    const QString name = entry.value("file").toString();
    for (auto i = entries.begin(); i != entries.end(); ++i) if (i.value().toObject().value("file").toString() == name) return true;
    const QString path = root + '/' + name;
    if (QFile::exists(path) && !QFile::remove(path)) return fail(error, QStringLiteral("Working copy detached; its file could not be removed"));
    { QMutexLocker lock(&m_mutex); m_verified.remove(path); }
    return syncDirectory(root) || fail(error, QStringLiteral("Working copy removed; directory sync failed"));
}

bool OfflineStore::relink(const QString &oldPath, const QString &newPath, bool folder, QString *error) {
    if (error) error->clear();
    QMutexLocker operation(&m_operation);
    const QString oldName = normalized(oldPath), newName = normalized(newPath);
    if (oldName == newName) return true;
    QJsonObject entries;
    QString root;
    { QMutexLocker lock(&m_mutex); entries = m_entries; root = m_root; if (!m_error.isEmpty()) return fail(error, m_error); }
    const QString physicalRoot = QFileInfo(root).canonicalFilePath();
    if (newName == root || newName.startsWith(root + '/')
        || (!physicalRoot.isEmpty() && QFileInfo(newName).canonicalFilePath().startsWith(physicalRoot + '/')))
        return fail(error, QStringLiteral("Relink to an independent original, not a managed offline copy"));
    const auto originalEntries = entries;
    bool changed = false;
    for (auto i = originalEntries.begin(); i != originalEntries.end(); ++i) {
        if (i.key() != oldName && (!folder || !i.key().startsWith(oldName + '/'))) continue;
        const QString destination = newName + i.key().mid(oldName.size());
        if (entries.contains(destination) && entries.value(destination) != i.value())
            return fail(error, QStringLiteral("Relink would replace another offline working copy; both retained"));
        entries.insert(destination, i.value()); entries.remove(i.key()); changed = true;
    }
    return !changed || save(entries, error);
}

extern "C" void oma_offline_configure(const char *library) {
    const QString directory = normalized(QString::fromLocal8Bit(library) + ".offline");
    if (OfflineStore::instance().directory() == directory && !OfflineStore::previews().directory().isEmpty()) return;
    QString error;
    OfflineStore::instance().open(directory, &error);
    if (!error.isEmpty()) qWarning("%s", qPrintable(error));
    OfflineStore::previews().open(normalized(QString::fromLocal8Bit(library) + ".smart-previews"), &error);
    if (!error.isEmpty()) qWarning("%s", qPrintable(error));
}
extern "C" int oma_offline_resolve(const char *original, char *out, size_t size) {
    if (!original || !out || size < 2) return 0;
    const QString source = QString::fromLocal8Bit(original);
    QString resolved = OfflineStore::instance().resolve(source);
    if (resolved.isEmpty()) resolved = OfflineStore::previews().resolve(source);
    if (resolved.isEmpty() || resolved == source) return 0;
    const QByteArray path = QFile::encodeName(resolved);
    if (size_t(path.size()) >= size) return 0;
    std::memcpy(out, path.constData(), size_t(path.size()) + 1);
    return 1;
}


// Smart Previews use the same durable index and checksum verification as
// full working copies, in a separate store. The payload is an unedited,
// reduced sensor buffer; never a rendered JPEG or a replacement original.
bool OfflineStore::createPreview(const QString &original, int imageId, const std::atomic_bool *cancel, QString *error) {
    if (error) error->clear();
    if (!m_smart) return fail(error, QStringLiteral("Not a Smart Preview store"));
    QMutexLocker operation(&m_operation);
    const QString source = normalized(original);
    const QString input = OfflineStore::instance().resolve(source, error);
    if (input.isEmpty()) return false;
    QJsonObject entries; QString root;
    { QMutexLocker lock(&m_mutex); entries = m_entries; root = m_root; if (!m_error.isEmpty()) return fail(error, m_error); }
    if (root.isEmpty() || input.startsWith(root + '/') || !QDir().mkpath(root)) return fail(error, QStringLiteral("Cannot create Smart Preview folder"));
    const QByteArray before = signature(input), sourceHash = checksum(input, cancel);
    if (sourceHash.isEmpty()) return fail(error, QStringLiteral("Cannot read original, or Smart Preview job cancelled"));
    const auto previous = entries.value(source).toObject();
    if (!previous.isEmpty()) {
        if (previous.value("sourceSha256").toString().toLatin1() != sourceHash)
            return fail(error, QStringLiteral("Original differs from the saved Smart Preview; files retained"));
        if (previous.value("generatorVersion").toInt() == smartGeneratorVersion
            && !verified(previous, nullptr).isEmpty()) return true;
    }
    void *pixels = nullptr; size_t size = 0; int width = 0, height = 0;
#ifdef OMARAW_ENGINE
    if (cancel && cancel->load()) return fail(error, QStringLiteral("Smart Preview job cancelled"));
    if (oma_engine_smart_data(imageId, &pixels, &size, &width, &height))
        return fail(error, QString::fromUtf8(oma_engine_error()));
#else
    Q_UNUSED(imageId)
    return fail(error, QStringLiteral("Smart Previews require the photo engine"));
#endif
    const QByteArray compressed = qCompress(static_cast<const uchar *>(pixels), qsizetype(size), 6);
    std::free(pixels);
    if (compressed.isEmpty() || before != signature(input) || checksum(input, cancel) != sourceHash)
        return fail(error, QStringLiteral("Original changed, or Smart Preview job cancelled; previous preview retained"));
    if (cancel && cancel->load()) return fail(error, QStringLiteral("Smart Preview job cancelled"));
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".omsp";
    QSaveFile file(root + '/' + name);
    if (!file.open(QIODevice::WriteOnly) || file.write(compressed) != compressed.size() || !file.flush()
        || ::fsync(file.handle()) || !file.commit()) return fail(error, QStringLiteral("Cannot save Smart Preview"));
    const QByteArray digest = QCryptographicHash::hash(compressed, QCryptographicHash::Sha256).toHex();
    if (checksum(root + '/' + name) != digest || !syncDirectory(root)) return fail(error, QStringLiteral("Smart Preview could not be verified; file retained"));
    entries.insert(source, QJsonObject{{"file", name}, {"bytes", compressed.size()}, {"sha256", QString::fromLatin1(digest)},
        {"sourceSha256", QString::fromLatin1(sourceHash)}, {"width", width}, {"height", height},
        {"generatorVersion", smartGeneratorVersion}});
    if (!save(entries, error)) return false;
    if (!previous.isEmpty()) QFile::remove(root + '/' + previous.value("file").toString());
    return true;
}

QByteArray OfflineStore::readPreview(const QString &file) const {
    if (!m_smart) return {};
    QJsonObject entry;
    { QMutexLocker lock(&m_mutex);
      if (!m_error.isEmpty() || normalized(file) != m_root + '/' + QFileInfo(file).fileName()) return {};
      entry = m_files.value(QFileInfo(file).fileName());
    }
    if (entry.isEmpty() || verified(entry, nullptr).isEmpty()) return {};
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 128 * 1024 * 1024) return {};
    const QByteArray compressed = input.readAll();
    if (compressed.size() < 4 || QCryptographicHash::hash(compressed, QCryptographicHash::Sha256).toHex() != entry.value("sha256").toString().toLatin1()) return {};
    const quint32 size = qFromBigEndian<quint32>(compressed.constData());
    if (!size || size > 128 * 1024 * 1024) return {};
    return qUncompress(compressed);
}
extern "C" int oma_smart_read(const char *file, void **data, size_t *size) {
    if (!file || !data || !size) return 0;
    *data = nullptr; *size = 0;
    const QByteArray bytes = OfflineStore::previews().readPreview(QString::fromLocal8Bit(file));
    if (bytes.isEmpty()) return 0;
    *data = std::malloc(size_t(bytes.size()));
    if (!*data) return 0;
    std::memcpy(*data, bytes.constData(), size_t(bytes.size())); *size = size_t(bytes.size()); return 1;
}
