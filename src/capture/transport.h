#pragma once
#include <QObject>
#include <QImage>
#include <QStringList>
#include <QVariantList>

// Camera I/O lives on the capture thread. This boundary also lets tests
// exercise disconnects and partial transfers without operating a real camera.
class CaptureTransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
public slots:
    virtual void init() = 0;
    virtual void detect() = 0;
    virtual void open(const QString &model, const QString &port) = 0;
    virtual void close() = 0;
    virtual void readControls() = 0;
    virtual void setControl(const QString &name, const QString &value) = 0;
    virtual void shoot(const QString &directory, const QString &stem, bool deleteOnCamera) = 0;
    virtual void listen(const QString &directory, const QString &stem, bool deleteOnCamera) = 0;
    virtual void frame() = 0;
signals:
    void inited(bool ok, const QString &versionOrError);
    void detected(const QVariantList &cameras, const QString &error);
    void opened(bool ok, const QString &model, const QString &port, const QString &summary, bool capture, bool preview, bool config, const QString &error);
    void closed();
    void controls(const QVariantList &rows);
    void controlSet(const QString &name, const QString &value, const QString &error);
    void shot(const QStringList &paths, const QString &error);
    void listened(const QStringList &paths, const QString &error);
    void framed(const QImage &img, const QString &error);
};
