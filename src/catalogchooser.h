#pragma once
// The window OmaRAW shows when it has no catalog to work in: when the user
// has asked to choose one at startup, and when the remembered catalog cannot
// be opened — missing drive, another window already holding its locks, or a
// file the schema cannot read. Without it a launcher-started app would exit
// with a message nobody sees.
//
// It runs its own QML engine and event loop before the main window exists,
// and never touches the photo engine, so choosing here costs nothing that a
// failed open has not already cost.
#include <QObject>
#include <QString>

class Backend;

class CatalogChooser : public QObject {
    Q_OBJECT
    // Why the chooser is up. Empty when the user asked for it rather than
    // the app failing to open something.
    Q_PROPERTY(QString message READ message CONSTANT)
    Q_PROPERTY(QString failedPath READ failedPath CONSTANT)
public:
    CatalogChooser(Backend *backend, QString message, QString failedPath, QObject *parent = nullptr);

    // Shows the window and returns the catalog to open, or "" if the user
    // chose to quit or closed the window.
    QString run();

    QString message() const { return m_message; }
    QString failedPath() const { return m_failedPath; }

    // Called from the chooser window.
    Q_INVOKABLE void choose(const QString &fileOrUrl);
    Q_INVOKABLE void quit();

signals:
    void finished();

private:
    Backend *m_backend = nullptr;
    QString m_message, m_failedPath, m_chosen;
};
