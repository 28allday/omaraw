#include "resourcesettings.h"
#include "thumbnailer.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QStorageInfo>
#include <QUrl>
#include <unistd.h>

ResourceSettings::ResourceSettings(QObject *parent) : QObject(parent) {
    const qint64 pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) m_systemMiB = int(qMin<qint64>(pages * pageSize / (1024 * 1024), 1048576));
    const int saved = QSettings().value("performance/memoryMiB", 0).toInt();
    m_memoryMiB = saved == 0 ? 0 : qBound(1024, saved, maximumMemoryMiB());
    m_diskGiB = qBound(1, QSettings().value("performance/diskCacheGiB", 4).toInt(), 1024);
    m_pool.setMaxThreadCount(1);
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &ResourceSettings::refreshCache);
}
ResourceSettings::~ResourceSettings() { m_timer.stop(); m_pool.waitForDone(); }
int ResourceSettings::maximumMemoryMiB() const { return qBound(1024, (m_systemMiB * 3 / 4) / 256 * 256, 65536); }
int ResourceSettings::effectiveMemoryMiB() const {
    return m_memoryMiB ? m_memoryMiB : qBound(1024, (m_systemMiB / 4) / 256 * 256, qMin(8192, maximumMemoryMiB()));
}
void ResourceSettings::setMemoryMiB(int mib) {
    mib = mib == 0 ? 0 : qBound(1024, mib, maximumMemoryMiB());
    if (mib == m_memoryMiB) return;
    m_memoryMiB = mib; QSettings().setValue("performance/memoryMiB", mib); emit memoryChanged();
}
void ResourceSettings::setDiskCacheGiB(int gib) {
    gib = qBound(1, gib, 1024);
    if (gib == m_diskGiB) return;
    m_diskGiB = gib; QSettings().setValue("performance/diskCacheGiB", gib); emit diskCacheChanged(); refreshCache();
}
QString ResourceSettings::cachePath() const { return Thumbnailer::defaultCacheDir() + "/previews"; }
QString ResourceSettings::cacheUsage() const {
    if (m_used < 0) return tr("Calculating cache usage…");
    const QString used = QLocale().formattedDataSize(m_used);
    return m_free < 0 ? tr("%1 used").arg(used) : tr("%1 used · %2 free on disk").arg(used, QLocale().formattedDataSize(m_free));
}
void ResourceSettings::startMaintenance() { m_timer.start(); refreshCache(); }
void ResourceSettings::refreshCache() { maintain(false); }
void ResourceSettings::clearCache() { maintain(true); }
void ResourceSettings::revealCache() { QDir().mkpath(cachePath()); QDesktopServices::openUrl(QUrl::fromLocalFile(cachePath())); }
void ResourceSettings::maintain(bool clear) {
    if (m_busy) { m_again = true; m_clearPending |= clear; return; }
    const QString path = cachePath();
    const qint64 limit = clear ? 0 : qint64(m_diskGiB) << 30;
    m_busy = true; m_status = clear ? tr("Clearing preview cache…") : tr("Checking preview cache…"); emit cacheChanged();
    // Destructor joins this pool. Completion targets this object so pending
    // UI callbacks are discarded if the window closes during maintenance.
    m_pool.start([this, path, limit, clear] {
        const int removed = Thumbnailer::prune(path, limit);
        const qint64 used = Thumbnailer::cacheBytes(path);
        QString volume = path;
        while (!QFileInfo::exists(volume) && QFileInfo(volume).absolutePath() != volume)
            volume = QFileInfo(volume).absolutePath();
        QStorageInfo disk(volume);
        const qint64 free = disk.isValid() && disk.isReady() ? disk.bytesAvailable() : -1;
        QMetaObject::invokeMethod(this, [this, used, free, removed, clear, limit] {
            m_used = used; m_free = free; m_busy = false;
            m_status = used > limit ? tr("Some cached previews are in use or could not be removed. Cleanup will retry.")
                     : clear ? tr("Preview cache cleared. Previews rebuild when needed.")
                     : removed ? tr("Removed %1 older cached previews.").arg(removed) : QString();
            emit cacheChanged();
            if (m_again) { const bool clearNext = m_clearPending; m_again = m_clearPending = false; maintain(clearNext); }
        }, Qt::QueuedConnection);
    });
}
