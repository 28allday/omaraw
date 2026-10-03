#pragma once
#include <QObject>
#include <QThreadPool>
#include <QString>

// Read-only integrity checks use their own SQLite connections on a worker.
class CatalogMaintenance : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString report READ report NOTIFY changed)
    Q_PROPERTY(bool passed READ passed NOTIFY changed)
public:
    explicit CatalogMaintenance(QObject *parent = nullptr);
    ~CatalogMaintenance();
    bool busy() const { return m_busy; }
    QString summary() const { return m_summary; }
    QString report() const { return m_report; }
    bool passed() const { return m_passed; }
    void setCatalog(const QString &path);
    void check(const QString &catalog, const QString &engineConfig);
    Q_INVOKABLE void copyReport() const;
signals:
    void changed();
private:
    QThreadPool m_pool;
    QString m_catalog, m_summary, m_report;
    bool m_busy = false, m_passed = false;
};
