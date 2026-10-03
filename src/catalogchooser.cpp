#include "catalogchooser.h"
#include "backend.h"
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QUrl>
#include <QDebug>

CatalogChooser::CatalogChooser(Backend *backend, QString message, QString failedPath, QObject *parent)
    : QObject(parent), m_backend(backend), m_message(std::move(message)),
      m_failedPath(std::move(failedPath)) {}

void CatalogChooser::choose(const QString &fileOrUrl) {
    QString path = fileOrUrl;
    if (path.startsWith(QLatin1String("file:"))) path = QUrl(path).toLocalFile();
    path = QDir::cleanPath(path);
    if (path.isEmpty() || !QFileInfo(path).isFile()) return;
    m_chosen = path;
    emit finished();
}

void CatalogChooser::quit() {
    m_chosen.clear();
    emit finished();
}

QString CatalogChooser::run() {
    // A QML engine of its own: the main window's engine is only built once a
    // catalog is open, and its image providers need services that are not
    // running yet.
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, this,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &w : warnings) qWarning().noquote() << w.toString();
    });
    engine.addImportPath(QStringLiteral(":/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("chooser"), this);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), m_backend);
    engine.load(QUrl(QStringLiteral("qrc:/qml/OmaRaw/App/CatalogChooserWindow.qml")));
    if (engine.rootObjects().isEmpty()) {
        // Never leave the user without a way forward: if the chooser itself
        // cannot be shown, report it and let the caller fail as before.
        qCritical() << "Could not show the catalog chooser.";
        return QString();
    }

    QEventLoop loop;
    QObject::connect(this, &CatalogChooser::finished, &loop, &QEventLoop::quit);
    // Closing the window is the same as choosing to quit.
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
        QObject::connect(window, &QWindow::visibleChanged, &loop, [this, &loop](bool visible) {
            if (!visible) { m_chosen.clear(); loop.quit(); }
        });
    loop.exec();
    return m_chosen;
}
