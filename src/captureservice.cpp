#include "captureservice.h"
#include "displaycolour.h"
#include "capture/gphoto.h"
#include "capture/transport.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>

// ── worker (lives on m_thread) ──────────────────────────────────────────
class CaptureWorker : public CaptureTransport {
    Q_OBJECT
public slots:
    void init() {
        const int rc = oma_gp_init();
        emit inited(rc == 0, rc == 0 ? QString::fromUtf8(oma_gp_version()) : QString::fromUtf8(oma_gp_error()));
    }
    void detect() {
        char models[8][128], ports[8][64];
        const int n = oma_gp_detect(models, ports, 8);
        QVariantList out;
        for (int i = 0; i < n; ++i) {
            QVariantMap m;
            m[QStringLiteral("model")] = QString::fromUtf8(models[i]);
            m[QStringLiteral("port")] = QString::fromUtf8(ports[i]);
            out << m;
        }
        emit detected(out, n < 0 ? QString::fromUtf8(oma_gp_error()) : QString());
    }
    void open(const QString &model, const QString &port) {
        const int rc = oma_gp_open(model.toUtf8().constData(), port.toUtf8().constData());
        if (rc) { emit opened(false, model, port, QString(), false, false, false, QString::fromUtf8(oma_gp_error())); return; }
        emit opened(true, QString::fromUtf8(oma_gp_model()), port, QString::fromUtf8(oma_gp_summary()),
                    oma_gp_can_capture() != 0, oma_gp_can_preview() != 0, oma_gp_can_config() != 0, QString());
        readControls();
    }
    void close() { oma_gp_close(); emit closed(); }
    void readControls() {
        QVariantList out;
        const int n = oma_gp_property_count();
        for (int i = 0; i < n; ++i) {
            char name[128], label[256], value[256];
            int type = 4, ro = 0;
            const char *section = "";
            if (oma_gp_property(i, name, sizeof name, label, sizeof label, value, sizeof value, &type, &ro, &section) != 0) continue;
            QVariantMap m;
            m[QStringLiteral("name")] = QString::fromUtf8(name);
            m[QStringLiteral("label")] = QString::fromUtf8(label);
            m[QStringLiteral("value")] = QString::fromUtf8(value);
            m[QStringLiteral("type")] = type;
            m[QStringLiteral("readOnly")] = ro != 0 || type == 4;
            m[QStringLiteral("section")] = QString::fromUtf8(section);
            if (type == 0) {
                char buf[8192];
                if (oma_gp_property_choices(name, buf, sizeof buf) > 0) m[QStringLiteral("choices")] = QString::fromUtf8(buf).split(QLatin1Char('\n'));
                else m[QStringLiteral("choices")] = QStringList();
            } else if (type == 3) {
                float lo = 0, hi = 0, step = 0;
                oma_gp_property_range(name, &lo, &hi, &step);
                m[QStringLiteral("lo")] = double(lo); m[QStringLiteral("hi")] = double(hi); m[QStringLiteral("step")] = double(step);
            }
            out << m;
        }
        emit controls(out);
    }
    void setControl(const QString &name, const QString &value) {
        const int rc = oma_gp_set_property(name.toUtf8().constData(), value.toUtf8().constData());
        emit controlSet(name, value, rc == 0 ? QString() : QString::fromUtf8(oma_gp_error()));
        readControls();
    }
    void shoot(const QString &dir, const QString &stem, bool deleteOnCamera) {
        char paths[8192] = {0};
        const int n = oma_gp_capture(dir.toLocal8Bit().constData(), stem.toUtf8().constData(), deleteOnCamera ? 1 : 0, paths, sizeof paths);
        emit shot(QString::fromLocal8Bit(paths).split(QLatin1Char('\n'), Qt::SkipEmptyParts),
                  n < 0 || *oma_gp_error() ? QString::fromUtf8(oma_gp_error()) : QString());
    }
    void listen(const QString &dir, const QString &stem, bool deleteOnCamera) {
        char paths[8192] = {0};
        const int n = oma_gp_wait_for_files(400, dir.toLocal8Bit().constData(), stem.toUtf8().constData(), deleteOnCamera ? 1 : 0, paths, sizeof paths);
        emit listened(QString::fromLocal8Bit(paths).split(QLatin1Char('\n'), Qt::SkipEmptyParts),
                      n < 0 || *oma_gp_error() ? QString::fromUtf8(oma_gp_error()) : QString());
    }
    void frame() {
        uint8_t *jpeg = nullptr; size_t len = 0;
        if (oma_gp_preview(&jpeg, &len) != 0) { emit framed(QImage(), QString::fromUtf8(oma_gp_error())); return; }
        QImage img = QImage::fromData(jpeg, int(len), "JPEG");
        oma_gp_free(jpeg);
        emit framed(img.convertToFormat(QImage::Format_RGB32), img.isNull() ? QStringLiteral("frame did not decode") : QString());
    }

};

// ── service ─────────────────────────────────────────────────────────────
CaptureService::CaptureService(QObject *parent, CaptureTransport *transport) : QObject(parent) {
    m_sessionFolder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/OmaRAW Capture");
    m_sessionName = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    m_worker = transport ? transport : new CaptureWorker;
    m_worker->setParent(nullptr);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CaptureTransport::inited, this, [this](bool ok, const QString &v) {
        m_available = ok; m_version = ok ? v : QString();
        setStatus(ok ? tr("Camera control ready — libgphoto2 %1").arg(v) : tr("Camera control unavailable: %1").arg(v));
        emit availableChanged();
        if (ok) refresh();
        else emit commandFinished(QStringLiteral("refresh"), v);
    });
    connect(m_worker, &CaptureTransport::detected, this, [this](const QVariantList &cams, const QString &err) {
        m_cameras = cams;
        emit camerasChanged();
        setBusy(false);
        if (!err.isEmpty()) setStatus(tr("Camera detection failed: %1").arg(err));
        else setStatus(cams.isEmpty() ? tr("No camera found. Connect one by USB, switch it on, and make sure nothing else has mounted it.")
                                      : tr("%1 camera%2 found").arg(cams.size()).arg(cams.size() == 1 ? "" : "s"));
        listen();
        emit commandFinished(QStringLiteral("refresh"), err);
    });
    connect(m_worker, &CaptureTransport::opened, this, [this](bool ok, const QString &model, const QString &port, const QString &summary, bool cap, bool prev, bool conf, const QString &err) {
        setBusy(false);
        if (!ok) {
            m_connected = false; m_model.clear(); m_port.clear(); m_summary.clear(); m_controls.clear();
            m_canCapture = m_canPreview = m_canConfig = false;
            setLiveView(false); emit connectionChanged(); emit controlsChanged();
            setStatus(tr("Could not connect to %1: %2").arg(model, err));
            m_controlsCommand.clear(); emit commandFinished(QStringLiteral("connectCamera"), err); return;
        }
        m_connected = true; m_model = model; m_port = port; m_summary = summary;
        m_canCapture = cap; m_canPreview = prev; m_canConfig = conf;
        emit connectionChanged();
        setStatus(tr("Connected to %1").arg(model));
        if (m_listenToBody) listen();
    });
    connect(m_worker, &CaptureTransport::closed, this, [this] {
        m_connected = false; m_disconnecting = false; m_model.clear(); m_port.clear(); m_summary.clear(); m_controls.clear();
        m_canCapture = m_canPreview = m_canConfig = false;
        setLiveView(false);
        emit connectionChanged(); emit controlsChanged();
        setBusy(false);
        setStatus(tr("Camera disconnected"));
        emit commandFinished(QStringLiteral("disconnectCamera"), {});
    });
    connect(m_worker, &CaptureTransport::controls, this, [this](const QVariantList &rows) {
        m_controls = rows; emit controlsChanged();
        if (!m_controlsCommand.isEmpty()) {
            const QString command = m_controlsCommand, error = m_controlError;
            m_controlsCommand.clear(); m_controlError.clear();
            emit commandFinished(command, error);
        }
    });
    connect(m_worker, &CaptureTransport::controlSet, this, [this](const QString &name, const QString &value, const QString &err) {
        m_controlError = err;
        setStatus(err.isEmpty() ? tr("%1 → %2").arg(name, value) : tr("Could not set %1: %2").arg(name, err));
    });
    connect(m_worker, &CaptureTransport::shot, this, [this](const QStringList &paths, const QString &err) {
        setBusy(false);
        if (paths.isEmpty()) {
            setStatus(tr("Capture failed: %1").arg(err.isEmpty() ? tr("no file came back") : err));
            if (m_listenToBody && !m_disconnecting) m_listenTimer.start(500);
            emit commandFinished(QStringLiteral("capture"), err.isEmpty() ? tr("No file came back") : err);
            return;
        }
        ++m_shotCount;
        m_lastCapture = paths.first();
        emit sessionChanged();
        setStatus(err.isEmpty() ? tr("Captured %1").arg(QFileInfo(paths.first()).fileName()) : tr("Saved %1 file(s); camera warning: %2").arg(paths.size()).arg(err));
        for (const QString &p : paths) emit captured(p);
        emit commandFinished(QStringLiteral("capture"), err);
        if (m_listenToBody) listen();
    });
    connect(m_worker, &CaptureTransport::listened, this, [this](const QStringList &paths, const QString &err) {
        m_listening = false;
        if (!paths.isEmpty()) {
            ++m_shotCount;
            m_lastCapture = paths.first();
            emit sessionChanged();
            setStatus(tr("Received %1 from the camera").arg(QFileInfo(paths.first()).fileName()));
            for (const QString &p : paths) emit captured(p);
        }
        if (!err.isEmpty()) {
            m_listenToBody = false; emit sessionChanged();
            setStatus(tr("Camera link: %1 — listening stopped; reconnect or enable Listen to Camera to retry").arg(err));
            emit commandFinished(QStringLiteral("listen"), err);
            return;
        }
        if (m_connected && m_listenToBody && !m_liveView && !m_busy && !m_disconnecting) m_listenTimer.start(150);
    });
    connect(m_worker, &CaptureTransport::framed, this, [this](const QImage &img, const QString &err) {
        m_frameInFlight = false;
        if (!m_liveView || !m_connected || m_disconnecting) {
            // Stopping live view may have deferred listening until this
            // last frame finished. Resume it once the transport is free.
            listen();
            return;
        }
        if (!img.isNull()) {
            m_frameFailures = 0;
            { QMutexLocker l(&m_frameMutex); m_frame = img; }
            ++m_frameSerial;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_lastFrameMs) m_fps = 0.8 * m_fps + 0.2 * (1000.0 / qMax<qint64>(1, now - m_lastFrameMs));
            m_lastFrameMs = now;
            emit frameChanged();
        } else {
            if (++m_frameFailures >= 3) setLiveView(false);
            setStatus(m_frameFailures >= 3 ? tr("Live view stopped after repeated errors: %1").arg(err) : tr("Live view: %1").arg(err));
        }
        if (m_liveView) m_frameTimer.start(30);
        emit commandFinished(QStringLiteral("preview"), img.isNull() ? (err.isEmpty() ? tr("No preview frame") : err) : QString());
    });
    m_frameTimer.setSingleShot(true);
    connect(&m_frameTimer, &QTimer::timeout, this, &CaptureService::requestFrame);
    m_listenTimer.setSingleShot(true);
    connect(&m_listenTimer, &QTimer::timeout, this, &CaptureService::listen);
    m_thread.start();
}

CaptureService::~CaptureService() {
    QMetaObject::invokeMethod(m_worker, "close", Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void CaptureService::start() {
    setStatus(tr("Starting camera control…"));
    QMetaObject::invokeMethod(m_worker, "init", Qt::QueuedConnection);
}

void CaptureService::setStatus(const QString &s) { if (s == m_status) return; m_status = s; emit statusChanged(); }
void CaptureService::setBusy(bool b) { if (b == m_busy) return; m_busy = b; emit busyChanged(); }

void CaptureService::refresh() {
    if (!m_available || m_busy) return;
    setBusy(true);
    setStatus(tr("Looking for cameras…"));
    QMetaObject::invokeMethod(m_worker, "detect", Qt::QueuedConnection);
}

void CaptureService::connectCamera(int index) {
    if (!m_available || index < 0 || index >= m_cameras.size() || m_busy) return;
    const QVariantMap c = m_cameras[index].toMap();
    m_controlsCommand = QStringLiteral("connectCamera"); m_controlError.clear();
    setBusy(true);
    setStatus(tr("Connecting to %1…").arg(c.value(QStringLiteral("model")).toString()));
    QMetaObject::invokeMethod(m_worker, "open", Qt::QueuedConnection,
                              Q_ARG(QString, c.value(QStringLiteral("model")).toString()), Q_ARG(QString, c.value(QStringLiteral("port")).toString()));
}

void CaptureService::disconnectCamera() {
    if (!m_connected) return;
    m_disconnecting = true;
    m_listenTimer.stop();
    setLiveView(false);
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "close", Qt::QueuedConnection);
}

void CaptureService::reloadControls() {
    if (m_connected) {
        m_controlsCommand = QStringLiteral("reloadControls"); m_controlError.clear();
        QMetaObject::invokeMethod(m_worker, "readControls", Qt::QueuedConnection);
    }
}

void CaptureService::setControl(const QString &name, const QString &value) {
    if (!m_connected) return;
    m_controlsCommand = QStringLiteral("setControl"); m_controlError.clear();
    // Optimistic: the row shows the new value while the camera catches up.
    for (int i = 0; i < m_controls.size(); ++i) {
        QVariantMap m = m_controls[i].toMap();
        if (m.value(QStringLiteral("name")) == name) { m[QStringLiteral("value")] = value; m_controls[i] = m; }
    }
    emit controlsChanged();
    QMetaObject::invokeMethod(m_worker, "setControl", Qt::QueuedConnection, Q_ARG(QString, name), Q_ARG(QString, value));
}

void CaptureService::setLiveView(bool on) {
    if (on == m_liveView) return;
    if (on && (!m_connected || !m_canPreview)) return;
    m_liveView = on;
    emit liveViewChanged();
    if (on) { m_frameFailures = 0; m_listenTimer.stop(); m_fps = 0; m_lastFrameMs = 0; requestFrame(); }
    else { m_frameTimer.stop(); if (m_connected && m_listenToBody) listen(); }
}

void CaptureService::requestFrame() {
    if (!m_liveView || !m_connected || m_disconnecting || m_frameInFlight || m_listening || m_busy) { if (m_liveView && !m_disconnecting && (m_listening || m_busy)) m_frameTimer.start(50); return; }
    m_frameInFlight = true;
    QMetaObject::invokeMethod(m_worker, "frame", Qt::QueuedConnection);
}

// While idle, keep an ear on the camera: a press of the body's shutter
// button delivers the file here like a remote capture would.
void CaptureService::listen() {
    if (!m_connected || !m_listenToBody || m_liveView || m_busy || m_listening || m_frameInFlight || m_disconnecting) return;
    if (!QDir().mkpath(m_sessionFolder) || !QFileInfo(m_sessionFolder).isWritable()) {
        setStatus(tr("Capture folder is not writable: %1").arg(m_sessionFolder));
        emit commandFinished(QStringLiteral("listen"),m_status); return;
    }
    m_listening = true;
    QMetaObject::invokeMethod(m_worker, "listen", Qt::QueuedConnection,
                              Q_ARG(QString, m_sessionFolder), Q_ARG(QString, nextName()), Q_ARG(bool, m_deleteFromCamera));
}

void CaptureService::capture() {
    if (!m_connected || !m_canCapture || m_busy || m_disconnecting) return;
    if (!QDir().mkpath(m_sessionFolder) || !QFileInfo(m_sessionFolder).isWritable()) {
        setStatus(tr("Capture folder is not writable: %1").arg(m_sessionFolder));
        emit commandFinished(QStringLiteral("capture"),m_status); return;
    }
    setBusy(true);
    m_listenTimer.stop();
    setStatus(tr("Capturing…"));
    // A pending listen finishes on its own (≤ 400 ms); the shoot queues behind it.
    QMetaObject::invokeMethod(m_worker, "shoot", Qt::QueuedConnection,
                              Q_ARG(QString, m_sessionFolder), Q_ARG(QString, nextName()), Q_ARG(bool, m_deleteFromCamera));
}

QString CaptureService::liveSource() const {
    return m_frame.isNull() ? QString() : QStringLiteral("image://liveview/%1").arg(m_frameSerial);
}

QImage LiveViewProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(id) Q_UNUSED(requestedSize)
    const QImage img = m_svc->frameCopy();
    if (size) *size = img.size();
    return DisplayColour::instance().convert(img);
}

void CaptureService::setSessionFolder(const QString &pathOrUrl) {
    QString p = pathOrUrl;
    if (p.startsWith(QLatin1String("file:"))) p = QUrl(p).toLocalFile();
    if (p.trimmed().isEmpty()) return;
    p = QDir::cleanPath(QFileInfo(p).absoluteFilePath());
    if (p == m_sessionFolder) return;
    m_sessionFolder = p;
    m_shotCount = 0;
    emit sessionChanged();
}

void CaptureService::setSessionName(const QString &n) {
    if (n == m_sessionName) return;
    m_sessionName = n;
    m_shotCount = 0;
    emit sessionChanged();
}

void CaptureService::setNameTemplate(const QString &t) {
    if (t == m_template) return;
    m_template = t.isEmpty() ? QStringLiteral("{session}_{seq:4}") : t;
    emit sessionChanged();
}

void CaptureService::setDeleteFromCamera(bool on) { if (on == m_deleteFromCamera) return; m_deleteFromCamera = on; emit sessionChanged(); }
void CaptureService::setListenToBody(bool on) {
    if (on == m_listenToBody) return;
    m_listenToBody = on;
    emit sessionChanged();
    if (on) listen(); else m_listenTimer.stop();
}

QString CaptureService::expandTemplate(const QString &tpl, const QString &session, int seq, const QString &model, const QDateTime &when) {
    QString out = tpl;
    // {seq:N} first, then the plain tokens.
    int i = 0;
    while ((i = out.indexOf(QLatin1String("{seq:"), i)) >= 0) {
        const int end = out.indexOf(QLatin1Char('}'), i);
        if (end < 0) break;
        const int width = qBound(1, out.mid(i + 5, end - i - 5).toInt(), 8);
        out.replace(i, end - i + 1, QStringLiteral("%1").arg(seq, width, 10, QLatin1Char('0')));
    }
    out.replace(QLatin1String("{seq}"), QStringLiteral("%1").arg(seq, 4, 10, QLatin1Char('0')));
    out.replace(QLatin1String("{session}"), session.trimmed().isEmpty() ? QStringLiteral("capture") : session.trimmed());
    out.replace(QLatin1String("{date}"), when.toString(QStringLiteral("yyyyMMdd")));
    out.replace(QLatin1String("{time}"), when.toString(QStringLiteral("HHmmss")));
    QString m = model; m.replace(QLatin1Char(' '), QLatin1Char('-'));
    out.replace(QLatin1String("{model}"), m);
    // A file name, not a path.
    out.replace(QLatin1Char('/'), QLatin1Char('-'));
    static const QString bad = QStringLiteral("\\:*?\"<>|");
    for (const QChar c : bad) out.remove(c);
    return out.isEmpty() ? QStringLiteral("capture") : out;
}

QString CaptureService::nextName() const {
    return expandTemplate(m_template, m_sessionName, m_shotCount + 1, m_model, QDateTime::currentDateTime());
}

#include "captureservice.moc"
