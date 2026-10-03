#pragma once
#include <QObject>
#include <QThreadPool>
#include <QTimer>

class ResourceSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(int systemMemoryMiB READ systemMemoryMiB CONSTANT)
    Q_PROPERTY(int maximumMemoryMiB READ maximumMemoryMiB CONSTANT)
    Q_PROPERTY(int memoryMiB READ memoryMiB WRITE setMemoryMiB NOTIFY memoryChanged)
    Q_PROPERTY(int effectiveMemoryMiB READ effectiveMemoryMiB NOTIFY memoryChanged)
    Q_PROPERTY(int diskCacheGiB READ diskCacheGiB WRITE setDiskCacheGiB NOTIFY diskCacheChanged)
    Q_PROPERTY(QString cachePath READ cachePath NOTIFY cacheChanged)
    Q_PROPERTY(QString cacheUsage READ cacheUsage NOTIFY cacheChanged)
    Q_PROPERTY(QString cacheStatus READ cacheStatus NOTIFY cacheChanged)
    Q_PROPERTY(bool cacheBusy READ cacheBusy NOTIFY cacheChanged)
public:
    explicit ResourceSettings(QObject *parent = nullptr);
    ~ResourceSettings();
    int systemMemoryMiB() const { return m_systemMiB; }
    int maximumMemoryMiB() const;
    int memoryMiB() const { return m_memoryMiB; } // zero = automatic
    int effectiveMemoryMiB() const;
    void setMemoryMiB(int mib);
    int diskCacheGiB() const { return m_diskGiB; }
    void setDiskCacheGiB(int gib);
    QString cachePath() const;
    QString cacheUsage() const;
    QString cacheStatus() const { return m_status; }
    bool cacheBusy() const { return m_busy; }
    void startMaintenance();
    Q_INVOKABLE void refreshCache();
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void revealCache();
signals:
    void memoryChanged();
    void diskCacheChanged();
    void cacheChanged();
private:
    void maintain(bool clear);
    int m_systemMiB = 4096, m_memoryMiB = 0, m_diskGiB = 4;
    qint64 m_used = -1, m_free = -1;
    bool m_busy = false, m_again = false, m_clearPending = false;
    QString m_status;
    QThreadPool m_pool;
    QTimer m_timer;
};
