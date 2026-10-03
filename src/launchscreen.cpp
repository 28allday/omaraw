#include "launchscreen.h"
#include "offlineqml.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QPainter>
#include <oma/gpu/quick.h>

static void registerLaunchTypes() {
    qmlRegisterType<LaunchVideoSurface>("OmaRaw.Launch", 1, 0, "LaunchVideoSurface");
}
Q_COREAPP_STARTUP_FUNCTION(registerLaunchTypes)

LaunchVideoSurface::LaunchVideoSurface(QQuickItem *parent) : QQuickPaintedItem(parent) {
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        m_frame = frame.toImage();
        update();
    });
}

void LaunchVideoSurface::setPosterSource(const QString &source) {
    if (m_posterSource == source) return;
    m_posterSource = source;
    m_poster = QImage(source.startsWith("qrc:/") ? source.mid(3) : source);
    update(); emit posterSourceChanged();
}

void LaunchVideoSurface::paint(QPainter *painter) {
    const QImage &image = m_frame.isNull() ? m_poster : m_frame;
    painter->fillRect(boundingRect(), m_background);
    if (image.isNull()) return;
    const QSizeF size = QSizeF(image.size()).scaled(boundingRect().size(), Qt::KeepAspectRatio);
    const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    // Screen blends the supplied animation's black into the card surface.
    // The same composition is used for the still and decoded frames, including
    // the software scene graph, so buffering never reveals a black rectangle.
    painter->setCompositionMode(QPainter::CompositionMode_Screen);
    painter->drawImage(target, image);
}

QVariantList LaunchScreen::quotes() {
    QFile file(QStringLiteral(":/launch/quotes.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).array().toVariantList();
}

QVariantMap LaunchScreen::nextQuote(QSettings &settings) {
    const auto entries = quotes();
    QStringList ids;
    for (const auto &entry : entries) ids << entry.toMap().value("id").toString();
    if (ids.isEmpty()) return {};
    const QString last = settings.value("launch/lastQuote").toString();
    QStringList remaining = settings.value("launch/remainingQuotes").toStringList();
    remaining.removeDuplicates();
    remaining.removeIf([&](const QString &id) { return !ids.contains(id) || id == last; });
    if (remaining.isEmpty()) remaining = ids;
    QStringList choices = remaining;
    if (choices.size() > 1) choices.removeAll(last);
    const QString chosen = choices.at(QRandomGenerator::global()->bounded(int(choices.size())));
    remaining.removeAll(chosen);
    settings.setValue("launch/remainingQuotes", remaining);
    settings.setValue("launch/lastQuote", chosen);
    settings.sync();
    for (const auto &entry : entries) if (entry.toMap().value("id").toString() == chosen) return entry.toMap();
    return {};
}

int LaunchScreen::run() {
    const bool quitOnClose = QGuiApplication::quitOnLastWindowClosed();
    QGuiApplication::setQuitOnLastWindowClosed(false);
    const auto restore = qScopeGuard([quitOnClose] { QGuiApplication::setQuitOnLastWindowClosed(quitOnClose); });
    QSettings settings;
    QQmlApplicationEngine qml;
    OfflineQml::install(qml);
    qml.addImportPath(QStringLiteral(":/qml"));
    qml.setInitialProperties({{"version", QCoreApplication::applicationVersion()},
                              {"quote", nextQuote(settings)},
                              {"reduceMotion", settings.value("access/reducedMotion", false)}});
    qml.load(QUrl(QStringLiteral("qrc:/qml/OmaRaw/App/LaunchWindow.qml")));
    if (qml.rootObjects().isEmpty()) {
        qWarning("Could not show the launch screen; opening OmaRAW directly.");
        return Continue;
    }
    auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
    if (!window) return Continue;
    oma::gpu::guardQuickWindow(window);
    QEventLoop loop;
    QObject::connect(window, &QWindow::visibleChanged, &loop, [&loop](bool visible) { if (!visible) loop.quit(); });
    // Isolated visual QA hook. Normal launches never auto-dismiss.
    if (const QString grab = qEnvironmentVariable("OMARAW_LAUNCH_GRAB"); !grab.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("OMARAW_LAUNCH_GRAB_DELAY");
        QTimer::singleShot(delay > 0 ? delay : 2500, &loop, [window, grab, &loop] {
            const bool saved = window->grabWindow().save(grab);
            QFile metadata(grab + ".json");
            if (metadata.open(QIODevice::WriteOnly)) metadata.write(QJsonDocument::fromVariant(QVariantMap{
                {"version", window->property("version")}, {"quote", window->property("quote")},
                {"playbackReady", window->property("playbackReady")}}).toJson());
            loop.exit(saved ? 0 : 1);
        });
    }
    const int result = loop.exec();
    return window->property("confirmed").toBool() ? Continue : result;
}
