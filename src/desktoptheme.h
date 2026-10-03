// Follows the Omarchy theme's ACCENT, and nothing else.
//
// ⚠️ The rest of the palette stays fixed on purpose. A photograph is read
// against its surround, so a surround that changes hue changes the picture:
// that is why every editor worth the name pins neutral greys around the
// image. The desktop therefore gets to colour the CHROME — buttons,
// selection, focus — and never the viewer, the pasteboard, the histogram or
// the mask colours. See the note atop src/qml/OmaRaw/Ui/Theme.qml.
//
// Three things this has to survive, all of them real Omarchy behaviour:
//   · a theme change REPLACES the directory: omarchy-theme-set stages into
//     `current/next-theme`, then `rm -rf current/theme && mv`. A watch on
//     the theme folder or on colors.toml dies with that rm, so `current`
//     itself — a stable directory — is watched and everything re-armed on
//     every event. Older layouts repointed a `current` SYMLINK instead,
//     which inotify follows, so its parent is watched too;
//   · the path does not exist at all on plain Arch, in CI or in a container,
//     where the app must still look finished — hence the fixed Oma accent;
//   · the value can be junk, so it is validated before it is believed.
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

class DesktopTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
    Q_PROPERTY(bool following READ following NOTIFY accentChanged)
public:
    explicit DesktopTheme(QObject *parent = nullptr);
    // The state directory as an argument, for tests: a fabricated theme tree
    // proves the reader, where whatever this box happens to be wearing proves
    // nothing and changes under the suite.
    explicit DesktopTheme(const QString &stateDir, QObject *parent = nullptr);

    QColor accent() const { return m_accent; }
    // False when there is no Omarchy to follow and the fallback is in use.
    bool following() const { return m_following; }

    // The Oma accent, used wherever a desktop theme cannot be read.
    static QColor fixedAccent();
    // ~/.local/state/omarchy/current
    static QString stateDir();
    // The theme's accent, or an invalid colour when it is absent or junk.
    static QColor readAccent(const QString &stateDir);

signals:
    void accentChanged();

private:
    void rearm();
    void reload();

    QString m_dir;
    QFileSystemWatcher m_watch;
    QColor m_accent;
    bool m_following = false;
};
