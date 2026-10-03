#pragma once
#include <QObject>
#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QVariantMap>
#include <QWindow>
#include <atomic>

// Display conversion is applied only when serving photograph textures.
// The stored images, histograms, snapshots and exports keep their colour.
class DisplayColour : public QObject {
    Q_OBJECT
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY changed)
    Q_PROPERTY(QString screenName READ screenName NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(qreal windowScale READ windowScale NOTIFY windowScaleChanged)
public:
    static DisplayColour &instance();
    void setWindow(QWindow *window);
    int mode() const { return m_mode; } // 0 system, 1 manual/unmanaged display, 2 sRGB
    void setMode(int mode);
    QString profile() const { return m_profile; }
    void setProfile(const QString &path);
    QString screenName() const { return m_screenName; }
    QString status() const { return m_status; }
    int revision() const { return m_revision.load(); }
    qreal windowScale() const { return m_window ? m_window->devicePixelRatio() : 1.0; }
    QImage convert(const QImage &image) const;
    static QImage transform(const QImage &image, const QByteArray &icc);
    static bool matchesScreen(const QVariantMap &device, const QString &name, const QString &model, const QString &serial);
public slots:
    void refresh();
signals:
    void changed();
    void windowScaleChanged();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    explicit DisplayColour(QObject *parent = nullptr);
    void applyProfile(const QString &path, const QString &label);
    void install(const QByteArray &bytes, const QString &status);
    void querySystemProfile(int generation, const QString &name, const QString &model, const QString &serial);
    QPointer<QWindow> m_window;
    QString m_key, m_screenName, m_profile, m_status;
    int m_mode = 0, m_generation = 0;
    std::atomic<int> m_revision{0};
    mutable QMutex m_mutex;
    QByteArray m_icc;
};
