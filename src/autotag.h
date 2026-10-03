#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

namespace AutoTags {
QStringList keys();
QString label(const QString &key);
QString modelVersion();
QVariantList options();
QVariantList groups();
QStringList collectionKeys();
QStringList members(const QString &collection);
constexpr double threshold = 0.55;
// Private JSON-lines helper. Never opens a catalogue or starts the RAW engine.
int workerMain();
}

class AutoTagWorker : public QObject {
    Q_OBJECT
public:
    explicit AutoTagWorker(QObject *parent = nullptr);
    ~AutoTagWorker();
    bool busy() const { return m_active; }
    void scan(const QString &path);
    void stop();
signals:
    void completed(const QVariantMap &scores, const QString &error, bool fatal);
private:
    void receive();
    void fail(const QString &error);
    QProcess m_process;
    QTimer m_timeout, m_idle;
    QByteArray m_buffer, m_request;
    int m_serial = 0;
    bool m_active = false;
};
