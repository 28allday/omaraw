#include <algorithm>
#include "thumbnailer.h"
#include <QCoreApplication>
#include "metadata.h"
#include "displaycolour.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QRegularExpression>
#include <QRunnable>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>
#include <atomic>
#include <array>

namespace {

QMutex publicationMutex;
bool managedPreviewName(const QString &file) {
    static const QRegularExpression name(QStringLiteral("^(source-)?[0-9a-f]{24}\\.jpg$"));
    return name.match(file).hasMatch();
}
bool managedPreview(const QFileInfo &file) { return file.isFile() && !file.isSymLink() && managedPreviewName(file.fileName()); }

// Serialize cache publication for one photo, including its variants. RAW
// decoding happens outside this bounded set of locks, so unrelated previews
// can still use the whole pool.
QMutex &cacheMutex(const QString &cacheDir, const QString &path, qint64 mtime) {
    static std::array<QMutex, 64> mutexes;
    return mutexes[qHash(cacheDir + QLatin1Char('/') + Thumbnailer::cacheKey(path, mtime, 0)) % mutexes.size()];
}
QMutex &decodeMutex(const QString &cacheDir, const QString &path, qint64 mtime) {
    static std::array<QMutex, 64> mutexes;
    return mutexes[qHash(cacheDir + QLatin1Char('/') + Thumbnailer::cacheKey(path, mtime, 0)) % mutexes.size()];
}
bool saveThumbnail(const QString &path, const QImage &image) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QMutexLocker lock(&publicationMutex);
    QSaveFile output(path);
    return output.open(QIODevice::WriteOnly) && image.save(&output, "JPEG", 90) && output.commit();
}
QImage cachedThumbnail(const QString &cacheDir, const QString &path, qint64 mtime, int edge, int variant, bool original = false) {
    const QString file = Thumbnailer::filePath(cacheDir, path, mtime, edge, variant, original);
    const QString legacy = cacheDir + QLatin1Char('/') + QFileInfo(file).fileName();
    const QString readable = QFileInfo::exists(file) ? file : legacy;
    const QString edited = cacheDir + QLatin1Char('/') + Thumbnailer::cacheKey(path, mtime, 0, variant) + QStringLiteral(".jpg");
    const QFileInfo editedInfo(edited), fileInfo(readable);
    // Equal timestamps are possible when an edit follows a cached thumbnail
    // in the same filesystem clock tick. The authoritative edit wins ties.
    if (!original && editedInfo.exists() && (!fileInfo.exists() || editedInfo.lastModified() >= fileInfo.lastModified())) {
        QImage full(edited);
        if (!full.isNull()) {
            const int longEdge = qMax(full.width(), full.height());
            if (longEdge > edge) full = full.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            // A small Library render can serve the Loupe until a larger edit
            // arrives, but must not be stored as a full-sized Loupe thumbnail.
            if (longEdge >= qint64(edge) * 9 / 10) saveThumbnail(file, full);
            return full;
        }
    }
    QImage image(readable);
    if (!image.isNull() && readable == legacy && edge > 0) {
        QDir().mkpath(QFileInfo(file).absolutePath());
        QMutexLocker lock(&publicationMutex);
        // Move the encoded preview intact; migration must not introduce
        // another lossy JPEG generation. A failed move leaves it readable.
        QFile::rename(legacy, file);
    }
    // relatime filesystems may otherwise leave a frequently used preview's
    // access time unchanged for a day. Throttle explicit LRU touches.
    if (!image.isNull() && QFileInfo(file).lastRead().secsTo(QDateTime::currentDateTime()) >= 60) {
        QFile used(file);
        if (used.open(QIODevice::ReadOnly)) used.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileAccessTime);
    }
    // Imported photos already have grid and fitted Loupe sizes. Smaller
    // requests can use them without reopening the source/RAW. Original-only
    // requests only consult other original-only sizes.
    if (image.isNull()) {
        for (const int larger : {512, 2048}) {
            if (larger <= edge) continue;
            QImage full(Thumbnailer::filePath(cacheDir, path, mtime, larger, variant, original));
            if (full.isNull()) continue;
            image = qMax(full.width(), full.height()) > edge
                ? full.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation) : full;
            saveThumbnail(file, image);
            break;
        }
    }
    // A variant never rendered yet inherits the master's edited preview,
    // even if the master has not cached this particular requested size.
    if (!original && image.isNull() && variant) image = cachedThumbnail(cacheDir, path, mtime, edge, 0);
    return image;
}

QImage loadThumbnail(const QString &cacheDir, const QString &path, qint64 mtime, int edge,
                     int variant, bool original, const std::atomic<bool> *cancel) {
    const auto cancelled = [cancel] { return cancel && cancel->load(); };
    if (cancelled()) return {};
    {
        QMutexLocker lock(&cacheMutex(cacheDir, path, mtime));
        const QImage cached = cachedThumbnail(cacheDir, path, mtime, edge, variant, original);
        if (!cached.isNull()) return cached;
    }
    // Foreground requests and import warming must not decode the same photo
    // together. This separate lock leaves edit publication free to proceed.
    auto &decode = decodeMutex(cacheDir, path, mtime);
    while (!decode.tryLock(50)) if (cancelled()) return {};
    const auto unlock = qScopeGuard([&decode] { decode.unlock(); });
    if (cancelled()) return {};
    {
        QMutexLocker lock(&cacheMutex(cacheDir, path, mtime));
        const QImage cached = cachedThumbnail(cacheDir, path, mtime, edge, variant, original);
        if (!cached.isNull()) return cached;
    }
    const QImage decoded = Metadata::preview(path, edge, cancel);
    if (cancelled()) return {};
    QMutexLocker lock(&cacheMutex(cacheDir, path, mtime));
    // An edit may have finished while the source was decoding. Its preview
    // wins, including on a same-timestamp race.
    const QImage cached = cachedThumbnail(cacheDir, path, mtime, edge, variant, original);
    if (!cached.isNull()) return cached;
    if (!decoded.isNull()) saveThumbnail(Thumbnailer::filePath(cacheDir, path, mtime, edge, variant, original), decoded);
    return decoded;
}

class ThumbResponse : public QQuickImageResponse, public QRunnable {
public:
    ThumbResponse(const QString &id, const QString &cacheDir) : m_id(id), m_cacheDir(cacheDir) {
        setAutoDelete(false);
    }
    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
    QString errorString() const override { return m_error; }
    // Called from the QML pixmap reader while this may still be queued.
    // A fast scroll leaves a tail of tiles nobody is waiting for any more;
    // they must still finish, but they need not decode anything first.
    void cancel() override { m_cancelled.store(true); }
    void run() override {
        if (m_cancelled.load()) { emit finished(); return; }
        // id is "<encoded path>?m=<mtime>&e=<edge>"
        const int q = m_id.indexOf(QLatin1Char('?'));
        const QString path = QUrl::fromPercentEncoding(m_id.left(q).toUtf8());
        QUrlQuery query(q >= 0 ? m_id.mid(q + 1) : QString());
        const qint64 mtime = query.queryItemValue(QStringLiteral("m")).toLongLong();
        int edge = query.queryItemValue(QStringLiteral("e")).toInt();
        if (edge <= 0) edge = 512;
        const int variant = query.queryItemValue(QStringLiteral("v")).toInt();
        const bool original = query.queryItemValue(QStringLiteral("original")) == QLatin1String("1");
        m_image = loadThumbnail(m_cacheDir, path, mtime, edge, variant, original, &m_cancelled);
        if (m_image.isNull() && !m_cancelled.load()) m_error = QStringLiteral("no preview for %1").arg(path);
        m_image = DisplayColour::instance().convert(m_image);
        emit finished();
    }
private:
    QString m_id, m_cacheDir, m_error;
    QImage m_image;
    std::atomic<bool> m_cancelled{false};
};

} // namespace

int Thumbnailer::threadCount() {
    bool ok = false;
    const int forced = qEnvironmentVariableIntValue("OMARAW_THUMBNAIL_THREADS", &ok);
    if (ok && forced > 0) return qMin(forced, QThread::idealThreadCount());
    return qBound(1, QThread::idealThreadCount(), 4);
}

int Thumbnailer::prune(const QString &cacheDir, qint64 maxBytes) {
    QDir dir(cacheDir);
    if (!dir.exists()) return 0;
    int removed = 0;
    qint64 total = 0;
    struct Entry { QString path; qint64 size; QDateTime used, modified; };
    QVector<Entry> entries;
    const QDateTime hourAgo = QDateTime::currentDateTime().addSecs(-3600);
    for (const QFileInfo &f : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (f.suffix() == QLatin1String("part") && managedPreviewName(f.fileName().chopped(5))) {
            if (f.lastModified() < hourAgo && QFile::remove(f.filePath())) ++removed;
            continue;
        }
        if (!managedPreview(f)) continue;
        const QDateTime read = f.lastRead(), written = f.lastModified();
        entries.push_back({f.filePath(), f.size(), read.isValid() && read > written ? read : written, written});
        total += f.size();
    }
    if (total <= maxBytes) return removed;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.used < b.used; });
    const qint64 target = maxBytes / 4 * 3;
    for (const Entry &e : entries) {
        if (total <= target) break;
        QMutexLocker lock(&publicationMutex);
        const QFileInfo now(e.path);
        if (!now.isSymLink() && now.size() == e.size && now.lastModified() == e.modified
            && (maxBytes == 0 || now.lastRead() <= e.used)
            && QFile::remove(e.path)) { total -= e.size; ++removed; }
    }
    return removed;
}

qint64 Thumbnailer::cacheBytes(const QString &cacheDir) {
    qint64 bytes = 0;
    for (const auto &file : QDir(cacheDir).entryInfoList(QDir::Files | QDir::NoSymLinks))
        if (managedPreview(file)) bytes += file.size();
    return bytes;
}
QString Thumbnailer::filePath(const QString &dir, const QString &path, qint64 mtime, int edge, int variant, bool original) {
    return dir + (edge > 0 ? QStringLiteral("/previews/") : QStringLiteral("/"))
        + (original ? QStringLiteral("source-") : QString()) + cacheKey(path, mtime, edge, variant) + QStringLiteral(".jpg");
}
QString Thumbnailer::existingPath(const QString &dir, const QString &path, qint64 mtime, int edge, int variant) {
    const QString current = filePath(dir, path, mtime, edge, variant);
    return QFileInfo::exists(current) ? current : dir + QLatin1Char('/') + QFileInfo(current).fileName();
}
QImage Thumbnailer::cached(const QString &dir, const QString &path, qint64 mtime, int edge, int variant) {
    QMutexLocker lock(&cacheMutex(dir, path, mtime));
    return cachedThumbnail(dir, path, mtime, edge, variant);
}
void Thumbnailer::prepare(const QString &dir, const QString &path, qint64 mtime, int edge, const std::atomic<bool> *cancel) {
    loadThumbnail(dir, path, mtime, qBound(1, edge, 8192), 0, false, cancel);
}

QString Thumbnailer::defaultCacheDir() {
    if (QCoreApplication::instance()) {
        const QString configured = QCoreApplication::instance()->property("omarawThumbnailCache").toString();
        if (!configured.isEmpty()) return configured;
    }
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/thumbs");
}

Thumbnailer::Thumbnailer(const QString &cacheDir) : m_cacheDir(cacheDir) {
    m_pool.setMaxThreadCount(threadCount());
    m_pool.setObjectName(QStringLiteral("omaraw-thumbnails"));
    if (m_cacheDir.isEmpty()) m_cacheDir = defaultCacheDir();
}

QQuickImageResponse *Thumbnailer::requestImageResponse(const QString &id, const QSize &) {
    auto *r = new ThumbResponse(id, m_cacheDir);
    m_pool.start(r);
    return r;
}

QString Thumbnailer::cacheKey(const QString &path, qint64 mtime, int maxEdge, int variant) {
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(path.toUtf8());
    h.addData(QByteArray::number(mtime));
    h.addData(QByteArray::number(maxEdge));
    // Rebuild old source/size thumbnails made from tiny embedded previews.
    // Keep edge-zero edited masters: these are the authoritative user edits.
    if (maxEdge > 0) h.addData(QByteArrayLiteral("preview-v2"));
    if (variant) h.addData(QByteArray("v") + QByteArray::number(variant));
    return QString::fromLatin1(h.result().toHex().left(24));
}

QString Thumbnailer::sourceFor(const QString &path, qint64 mtime, int maxEdge, int rev, int variant) {
    // Concatenation, not arg(): the percent-encoded path contains "%2F",
    // which a later arg() would happily treat as a placeholder. `r` is the
    // edit revision: it changes the URL so QML reloads, and nothing else.
    return QStringLiteral("image://thumbs/") + QString::fromUtf8(QUrl::toPercentEncoding(path))
           + QStringLiteral("?m=") + QString::number(mtime) + QStringLiteral("&e=") + QString::number(maxEdge)
           + (variant ? QStringLiteral("&v=") + QString::number(variant) : QString())
           + (rev ? QStringLiteral("&r=") + QString::number(rev) : QString())
           + QStringLiteral("&display=") + QString::number(DisplayColour::instance().revision());
}

QString Thumbnailer::store(const QString &cacheDir, const QString &path, qint64 mtime, int maxEdge, const QImage &img, int variant) {
    QMutexLocker lock(&cacheMutex(cacheDir, path, mtime));
    QDir().mkpath(cacheDir);
    // The edited render at its own size (up to the largest thumbnail), for
    // every other size to be made from; see ThumbResponse.
    {
        const QString edited = cacheDir + QLatin1Char('/') + cacheKey(path, mtime, 0, variant) + QStringLiteral(".jpg");
        QImage full = img;
        if (qMax(full.width(), full.height()) > 2048) full = full.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        saveThumbnail(edited, full);
    }
    const QString file = filePath(cacheDir, path, mtime, maxEdge, variant);
    QImage out = img;
    if (qMax(out.width(), out.height()) > maxEdge) out = out.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (saveThumbnail(file, out)) return file;
    return QString();
}
