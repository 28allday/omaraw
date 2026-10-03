#include "desktoptheme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlEngine>
#include <QSet>

DesktopTheme::DesktopTheme(QObject *parent) : DesktopTheme(stateDir(), parent) {}

DesktopTheme::DesktopTheme(const QString &stateDir, QObject *parent)
    : QObject(parent), m_dir(stateDir), m_accent(fixedAccent())
{
    connect(&m_watch, &QFileSystemWatcher::fileChanged, this, [this] { reload(); });
    connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, [this] { reload(); });
    reload();
}

QColor DesktopTheme::fixedAccent() { return QColor(QStringLiteral("#27C2FF")); }

QString DesktopTheme::stateDir()
{
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
}

// A twenty line reader beats a TOML dependency, and matches what the
// first-party Omarchy apps do: skip blanks and comments, split on the first
// '=', strip one layer of quotes, and validate before believing it.
QColor DesktopTheme::readAccent(const QString &stateDir)
{
    QFile file(stateDir + QStringLiteral("/theme/colors.toml"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const int eq = line.indexOf('=');
        if (eq < 0 || line.left(eq).trimmed() != "accent") continue;
        QString value = QString::fromUtf8(line.mid(eq + 1).trimmed());
        if (value.size() >= 2 && (value.front() == QLatin1Char('"') || value.front() == QLatin1Char('\''))
            && value.back() == value.front())
            value = value.mid(1, value.size() - 2);
        const QColor colour = QColor::fromString(value);
        return colour.isValid() ? colour : QColor();
    }
    return {};
}

// ⚠️ Everything is re-armed on every event because a theme change destroys
// the paths below `current`: the theme folder and colors.toml are new files
// afterwards, and the watches on the old ones are gone. Only the two
// directories above them survive a switch, which is why they are watched at
// all — the parent of `current` covers the older layout, where `current` was
// a symlink that got repointed and inotify followed it to the old target.
void DesktopTheme::rearm()
{
    const QStringList want{ QFileInfo(m_dir).absolutePath(), m_dir,
                            m_dir + QStringLiteral("/theme"),
                            m_dir + QStringLiteral("/theme/colors.toml") };
    QStringList add;
    const QSet<QString> watched(m_watch.files().cbegin(), m_watch.files().cend());
    const QSet<QString> watchedDirs(m_watch.directories().cbegin(), m_watch.directories().cend());
    for (const QString &path : want)
        if (QFileInfo::exists(path) && !watched.contains(path) && !watchedDirs.contains(path))
            add += path;
    if (!add.isEmpty()) m_watch.addPaths(add);
    // A path that has gone is dropped by the watcher itself; one that is
    // watched but no longer wanted can only be a stale target of a moved
    // directory, so let it go rather than keep an inotify slot on it.
    QStringList drop;
    for (const QString &path : m_watch.files() + m_watch.directories())
        if (!want.contains(path)) drop += path;
    if (!drop.isEmpty()) m_watch.removePaths(drop);
}

void DesktopTheme::reload()
{
    rearm();
    const QColor found = readAccent(m_dir);
    const QColor want = found.isValid() ? found : fixedAccent();
    const bool follow = found.isValid();
    if (want == m_accent && follow == m_following) return;
    m_accent = want;
    m_following = follow;
    emit accentChanged();
}

// Registered at application startup rather than beside the image providers in
// main.cpp: the app builds a second QML engine for the catalogue chooser and
// the suite builds three more, and Ui/Theme.qml — a singleton every one of
// them imports — would fail to load wherever the registration was forgotten.
//
// An engine gets its own reader rather than sharing one process-wide (the
// difference from OmaVector's otherwise identical class). It costs four
// inotify watches and a 700 byte file, the app builds at most two engines,
// and in exchange the directory it reads stays a pure function of the
// environment at construction — which is what lets the suite point it at a
// fabricated theme tree instead of whatever this box happens to be wearing.
// Made on first use, so QGuiApplication exists and the watcher has an event
// loop; owned by the engine that asked for it.
static void registerDesktopTheme()
{
    qmlRegisterSingletonType<DesktopTheme>(
        "OmaRaw.Desktop", 1, 0, "OmarchyTheme",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new DesktopTheme; });
}
Q_COREAPP_STARTUP_FUNCTION(registerDesktopTheme)
