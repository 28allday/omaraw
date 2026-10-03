// image://thumbs/<url-encoded path>?m=<mtime>&e=<maxEdge>
// Async provider: each request runs on this provider's own thread pool,
// reads the disk cache under ~/.cache/omaraw/thumbs or builds the preview
// and writes it there. The GUI thread never decodes a RAW.
//
// The pool is the provider's own, and small, on purpose. Building a preview
// decodes a RAW; on the global pool that is one decode per core, competing
// with the engine's processing pool for the same machine and holding up
// everything else that shares the global pool. See threadCount().
#pragma once

#include <QQuickAsyncImageProvider>
#include <QString>
#include <QThreadPool>
#include <atomic>

class Thumbnailer : public QQuickAsyncImageProvider {
public:
    explicit Thumbnailer(const QString &cacheDir = QString());
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    // `variant` (0 = master) keeps a virtual copy's edited thumbnail apart
    // from its master's; both fall back to the file's embedded preview.
    static QString cacheKey(const QString &path, qint64 mtime, int maxEdge, int variant = 0);
    // The source string the model hands QML.
    static QString sourceFor(const QString &path, qint64 mtime, int maxEdge, int rev = 0, int variant = 0);
    // Replace the cached thumbnail for a path (an edited render). Returns the file written.
    static QString store(const QString &cacheDir, const QString &path, qint64 mtime, int maxEdge, const QImage &img, int variant = 0);
    QString cacheDir() const { return m_cacheDir; }
    // Where an unconfigured provider keeps its cache. Callers that only want
    // the directory use this rather than building a whole provider.
    static QString defaultCacheDir();
    // Edited masters stay at the root; disposable sizes live in previews/.
    // Read legacy sizes until they have been migrated on access.
    static QString filePath(const QString &cacheDir, const QString &path, qint64 mtime, int edge, int variant = 0, bool original = false);
    static QString existingPath(const QString &cacheDir, const QString &path, qint64 mtime, int edge, int variant = 0);
    static QImage cached(const QString &cacheDir, const QString &path, qint64 mtime, int edge, int variant = 0);
    // Warm a Library/Loupe size on a background worker, preserving edits.
    static void prepare(const QString &cacheDir, const QString &path, qint64 mtime, int edge, const std::atomic<bool> *cancel = nullptr);
    static qint64 cacheBytes(const QString &cacheDir);
    // Concurrent preview builds: four, or OMARAW_THUMBNAIL_THREADS when the
    // environment sets it, so a measurement run can select the count.
    static int threadCount();
    // Keep the disk cache bounded: half-written files older than an hour go,
    // and above `maxBytes` the least recently used thumbnails go until the
    // cache is back under three quarters of it. Returns the files removed.
    static int prune(const QString &cacheDir, qint64 maxBytes);

private:
    QString m_cacheDir;
    QThreadPool m_pool;
};
