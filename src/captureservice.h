// Tethered capture as QML sees it: the cameras on the bus, one connected
// camera with its controls, live view, a session folder and a naming
// template, the shutter. Every libgphoto2 call runs on its own worker
// thread; the interface never waits on the camera.
#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariantList>

class CaptureTransport;

class CaptureService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString version READ version NOTIFY availableChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Cameras seen on the last refresh: [{model, port}]
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY camerasChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString cameraModel READ cameraModel NOTIFY connectionChanged)
    Q_PROPERTY(QString cameraPort READ cameraPort NOTIFY connectionChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY connectionChanged)
    Q_PROPERTY(bool canCapture READ canCapture NOTIFY connectionChanged)
    Q_PROPERTY(bool canPreview READ canPreview NOTIFY connectionChanged)
    Q_PROPERTY(bool canConfig READ canConfig NOTIFY connectionChanged)
    // Controls the camera exposes: [{name, label, value, type, readOnly, section, choices[], lo, hi, step}]
    Q_PROPERTY(QVariantList controls READ controls NOTIFY controlsChanged)
    Q_PROPERTY(bool liveView READ liveView WRITE setLiveView NOTIFY liveViewChanged)
    // image://liveview/<n> — changes with every frame.
    Q_PROPERTY(QString liveSource READ liveSource NOTIFY frameChanged)
    Q_PROPERTY(int liveWidth READ liveWidth NOTIFY frameChanged)
    Q_PROPERTY(int liveHeight READ liveHeight NOTIFY frameChanged)
    Q_PROPERTY(double liveFps READ liveFps NOTIFY frameChanged)
    // Session: where captures land and how they are named.
    Q_PROPERTY(QString sessionFolder READ sessionFolder WRITE setSessionFolder NOTIFY sessionChanged)
    Q_PROPERTY(QString sessionName READ sessionName WRITE setSessionName NOTIFY sessionChanged)
    Q_PROPERTY(QString nameTemplate READ nameTemplate WRITE setNameTemplate NOTIFY sessionChanged)
    Q_PROPERTY(int shotCount READ shotCount NOTIFY sessionChanged)
    Q_PROPERTY(QString nextName READ nextName NOTIFY sessionChanged)
    Q_PROPERTY(QString lastCapture READ lastCapture NOTIFY sessionChanged)
    Q_PROPERTY(bool deleteFromCamera READ deleteFromCamera WRITE setDeleteFromCamera NOTIFY sessionChanged)
    Q_PROPERTY(bool listenToBody READ listenToBody WRITE setListenToBody NOTIFY sessionChanged)
public:
    explicit CaptureService(QObject *parent = nullptr, CaptureTransport *transport = nullptr);
    ~CaptureService();

    // Starts the worker and libgphoto2; safe without a camera.
    void start();

    bool available() const { return m_available; }
    QString version() const { return m_version; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    QVariantList cameras() const { return m_cameras; }
    bool connected() const { return m_connected; }
    QString cameraModel() const { return m_model; }
    QString cameraPort() const { return m_port; }
    QString summary() const { return m_summary; }
    bool canCapture() const { return m_canCapture; }
    bool canPreview() const { return m_canPreview; }
    bool canConfig() const { return m_canConfig; }
    QVariantList controls() const { return m_controls; }
    bool liveView() const { return m_liveView; }
    void setLiveView(bool on);
    QString liveSource() const;
    int liveWidth() const { return m_frame.width(); }
    int liveHeight() const { return m_frame.height(); }
    double liveFps() const { return m_fps; }
    QImage frameCopy() const { QMutexLocker l(&m_frameMutex); return m_frame; }
    QString sessionFolder() const { return m_sessionFolder; }
    void setSessionFolder(const QString &pathOrUrl);
    QString sessionName() const { return m_sessionName; }
    void setSessionName(const QString &n);
    QString nameTemplate() const { return m_template; }
    void setNameTemplate(const QString &t);
    int shotCount() const { return m_shotCount; }
    QString nextName() const;
    QString lastCapture() const { return m_lastCapture; }
    bool deleteFromCamera() const { return m_deleteFromCamera; }
    void setDeleteFromCamera(bool on);
    bool listenToBody() const { return m_listenToBody; }
    bool listening() const { return m_listening; }
    void setListenToBody(bool on);

    // Expands a naming template: {session} {seq} {seq:N} {date} {time}
    // {model}; the extension comes from the camera file.
    static QString expandTemplate(const QString &tpl, const QString &session, int seq, const QString &model, const QDateTime &when);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectCamera(int index);
    Q_INVOKABLE void disconnectCamera();
    Q_INVOKABLE void capture();
    Q_INVOKABLE void setControl(const QString &name, const QString &value);
    Q_INVOKABLE void reloadControls();

signals:
    void availableChanged();
    void statusChanged();
    void busyChanged();
    void camerasChanged();
    void connectionChanged();
    void controlsChanged();
    void liveViewChanged();
    void frameChanged();
    void sessionChanged();
    // A file landed in the session folder (one signal per file).
    void captured(const QString &path);
    // Completion of a one-shot transport request (also consumed by the CLI).
    void commandFinished(const QString &command, const QString &error);

private:
    void setStatus(const QString &s);
    void setBusy(bool b);
    void requestFrame();
    void listen();

    QThread m_thread;
    CaptureTransport *m_worker = nullptr;
    bool m_available = false, m_busy = false, m_connected = false;
    bool m_canCapture = false, m_canPreview = false, m_canConfig = false;
    QString m_version, m_status, m_model, m_port, m_summary;
    QString m_controlsCommand, m_controlError;
    QVariantList m_cameras, m_controls;
    bool m_liveView = false;
    bool m_frameInFlight = false, m_listening = false;
    bool m_disconnecting = false;
    int m_frameFailures = 0;
    QImage m_frame;
    mutable QMutex m_frameMutex;
    int m_frameSerial = 0;
    double m_fps = 0;
    qint64 m_lastFrameMs = 0;
    QTimer m_frameTimer, m_listenTimer;
    QString m_sessionFolder, m_sessionName, m_template = QStringLiteral("{session}_{seq:4}"), m_lastCapture;
    int m_shotCount = 0;
    bool m_deleteFromCamera = false, m_listenToBody = true;
};

class LiveViewProvider : public QQuickImageProvider {
public:
    explicit LiveViewProvider(CaptureService *svc) : QQuickImageProvider(QQuickImageProvider::Image), m_svc(svc) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    CaptureService *m_svc;
};
