#pragma once
#include <QVariantMap>
#include <QVariantList>
#include <QQuickPaintedItem>
#include <QVideoSink>
#include <QImage>
#include <QColor>

class QSettings;

// Qt's VideoOutput does not draw with the software scene graph. This small
// surface keeps one decoded frame and works with both software and Vulkan UI.
class LaunchVideoSurface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVideoSink *videoSink READ videoSink CONSTANT)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QString posterSource READ posterSource WRITE setPosterSource NOTIFY posterSourceChanged)
public:
    explicit LaunchVideoSurface(QQuickItem *parent = nullptr);
    Q_INVOKABLE QVideoSink *videoSink() { return &m_sink; }
    void paint(QPainter *painter) override;
    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor &color) { if (m_background != color) { m_background = color; update(); emit backgroundColorChanged(); } }
    QString posterSource() const { return m_posterSource; }
    void setPosterSource(const QString &source);
signals:
    void backgroundColorChanged();
    void posterSourceChanged();
private:
    QVideoSink m_sink;
    QImage m_frame;
    QImage m_poster;
    QColor m_background = Qt::black;
    QString m_posterSource;
};

namespace LaunchScreen {
// A separate event loop before any catalogue locks or engine workers exist.
// The video resources are destroyed before opening the editing interface.
constexpr int Continue = -1;
int run();
QVariantList quotes();
QVariantMap nextQuote(QSettings &settings);
}
