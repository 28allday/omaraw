#include "backend.h"
#include "curatedpresets.h"
#include <QDirIterator>
#include <atomic>
#include <functional>
#include <QScopeGuard>
#include <QThread>
#include <memory>
#include <algorithm>
#include <QEventLoop>
#include "xmppreset.h"
#include "cameraprofiles.h"
#include <cmath>
#include <limits>
#include "catalogbackup.h"
#include "engineservice.h"
#include "aiservice.h"
#include "denoiseservice.h"
#include "metadata.h"
#include "sheets.h"
#include "thumbnailer.h"
#include "resourcebudget.h"
#include "snapshotpreview.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QSqlQuery>
#include <QSqlError>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QUuid>
#include <QDate>
#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QHash>
#include <QKeySequence>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QLockFile>
#include <QProcess>
#include <QTimer>
#include <QStorageInfo>
#include <algorithm>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>

// Each Backend owns a connection of its own, so two in one process (the
// tests, a future second window) never pull the rug from under each other.
static QString localPath(QString p) { if (p.startsWith(QLatin1String("file:"))) p = QUrl(p).toLocalFile(); return QDir::cleanPath(p); }

static QString catalogConnectionName() {
    static int n = 0;
    return QStringLiteral("catalog-%1").arg(++n);
}

Backend::Backend(QObject *parent) : QObject(parent), m_catalog(catalogConnectionName()), m_importer(this) {
    setupAutoTags();
    connect(&m_resources, &ResourceSettings::memoryChanged, this, [this] {
        const int mib = m_resources.effectiveMemoryMiB();
        { QMutexLocker lock(&m_snapMutex); m_snapImages.setMaxCost(ResourceBudget::forMemory(mib).snapshots * 1024); }
        if (m_engine) m_engine->setMemoryBudget(mib);
    });
    m_snapImages.setMaxCost(ResourceBudget::forMemory(m_resources.effectiveMemoryMiB()).snapshots * 1024);
    connect(&DisplayColour::instance(), &DisplayColour::changed, this, [this] {
        m_model.invalidateAll(); emit selectionChanged(); emit catalogChanged();
    });
    m_writeSidecars = QSettings().value(QStringLiteral("sidecars/write"), false).toBool();
    m_embedXmp = QSettings().value(QStringLiteral("sidecars/embed"), false).toBool();
    m_autoAdvance = QSettings().value(QStringLiteral("library/autoAdvance"), false).toBool();
    m_autoBackup = QSettings().value(QStringLiteral("catalog/autoBackup"), true).toBool();
    m_askCatalog = QSettings().value(QStringLiteral("catalog/askAtStartup"), false).toBool();
    m_recentKeywords = QSettings().value(QStringLiteral("library/recentKeywords")).toStringList();
    m_shortcutOverrides = QSettings().value(QStringLiteral("shortcuts/overrides")).toMap();
    m_reducedMotion = QSettings().value(QStringLiteral("access/reducedMotion"), false).toBool();
    m_labelNames = QSettings().value(QStringLiteral("labels/names")).toMap();
    m_highContrast = QSettings().value(QStringLiteral("access/highContrast"), false).toBool();
    m_colourCritical = QSettings().value(QStringLiteral("appearance/colourCritical"), false).toBool();
    m_favouritePresets = QSettings().value(QStringLiteral("develop/favouritePresets")).toStringList();
    m_recentPresets = QSettings().value(QStringLiteral("develop/recentPresets")).toStringList();
    // Watched folders: a burst of writes becomes one import once the disk
    // has been quiet for two seconds.
    m_watchTimer.setSingleShot(true);
    m_watchTimer.setInterval(2000);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &dir) { m_watchPending.insert(dir); m_watchTimer.start(); });
    connect(&m_watchTimer, &QTimer::timeout, this, &Backend::importWatched);
    m_savedFilters = QSettings().value(QStringLiteral("library/savedFilters")).toList();
    m_model.setCatalog(&m_catalog);
    m_model.setSelection(&m_selection, &m_currentId);
    connect(this, &Backend::selectionChanged, this, &Backend::saveBrowsingSession);
    connect(&m_importer, &Importer::progress, this, [this](int done, int total, const QString &file) {
        m_importProgress = total > 0 ? double(done) / total : 0;
        m_importStatus = total > 0 ? tr("Importing %1 of %2 — %3").arg(done).arg(total).arg(file)
                                   : tr("Scanning…");
        emit importChanged();
    });
    m_importRefresh.setSingleShot(true);
    m_importRefresh.setInterval(200);
    connect(&m_importRefresh, &QTimer::timeout, this, &Backend::refresh);
    connect(&m_importer, &Importer::assetsCommitted, this, [this] {
        // Only refresh committed rows. Coalesce bursts without restarting
        // the timer indefinitely during a continuous stream of copies.
        if (!m_importRefresh.isActive()) m_importRefresh.start();
    });
    connect(&m_importer, &Importer::runningChanged, this, [this] { emit importChanged(); });
    connect(&m_importer, &Importer::fileMoved, this, [this](const QString &from, const QString &to) {
        m_importMoves.insert({from, to}, m_importSerial);
        followMovedFile(from, to);
    });
    connect(&m_importer, &Importer::finished, this, [this](int imported, int skipped, int importId, const QString &importError) {
        m_importRefresh.stop();
        QStringList errors = m_importMoveErrors;
        if (!importError.isEmpty()) errors.prepend(importError);
        const QString err = errors.join(QLatin1Char('\n'));
        m_lastImportError = err;
        m_importProgress = 0;
        m_importStatus.clear();
        emit importChanged();
        if (!err.isEmpty()) setStatus(tr("Import failed: %1").arg(err));
        else {
            QStringList notes;
            if (m_importer.lastCopied()) notes << tr("%1 copied").arg(m_importer.lastCopied());
            if (m_importer.lastDuplicates()) notes << tr("%1 already in the catalog").arg(m_importer.lastDuplicates());
            if (m_importer.lastFailed()) notes << tr("%1 failed").arg(m_importer.lastFailed());
            if (m_importer.lastBackedUp()) notes << tr("%1 backed up").arg(m_importer.lastBackedUp());
            if (m_importer.lastBackupFailed()) notes << tr("%1 second copies failed").arg(m_importer.lastBackupFailed());
            const int other = skipped - m_importer.lastDuplicates() - m_importer.lastFailed();
            if (other > 0) notes << tr("%1 skipped").arg(other);
            if (m_importer.lastCancelled())
                setStatus(tr("Import stopped after %1 photos%2 — the rest of the folder was not imported").arg(imported)
                              .arg(notes.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(notes.join(QStringLiteral(", ")))));
            else
                setStatus(notes.isEmpty() ? tr("Imported %1 photos").arg(imported) : tr("Imported %1 photos (%2)").arg(imported).arg(notes.join(QStringLiteral(", "))));
        }
        refresh();
        m_catalog.rescanFolders();
        if (m_watchImporting) {
            // Only the rows this import made are new; the rest were re-read in place.
            m_watchImporting = false;
            const int fresh = err.isEmpty() ? m_catalog.assetIdsOfImport(importId).size() : 0;
            if (fresh) setStatus(tr("Watched folder: %1 new photo%2").arg(fresh).arg(fresh == 1 ? "" : "s"));
            refreshWatches();
        }
        // The card goes only after a clean verified run: a failed copy, a
        // checksum miss or a stop part-way keeps it in the reader.
        if (err.isEmpty() && !m_importer.lastCancelled() && m_runningImportOptions.eject && m_runningImportOptions.mode == QLatin1String("verify") && !m_importer.lastFailed() && !m_importer.lastBackupFailed()) {
            const QString summary = statusMessage();
            m_ejectNote = summary;
            const QString e = ejectVolumeOf(m_importSource);
            if (!e.isEmpty()) { m_ejectNote.clear(); setStatus(tr("%1 — could not eject: %2").arg(summary, e)); }
        }
        // The importer already marked new photos for their first edit before
        // publishing them. Do not mark one again if it was opened or reset
        // while the remaining photos were still importing.
        // An edit that came with a photo in its sidecar is its starting point.
        applyEditsFromSidecars(m_importer.lastSidecarEdits());
        // Presets with rules land on the new photos now.
        if (err.isEmpty() && imported > 0 && m_catalog.presetRules().size() > 0) {
            QVariantList ids;
            for (int id : m_catalog.assetIdsOfImport(importId)) ids << id;
            const int n = applyPresetRules(ids);
            if (n > 0) setStatus(tr("Imported %1 photos, auto-applying presets to %2").arg(imported).arg(n));
        }
        if (m_buildSmartAfterImport && m_engine && imported > 0) {
            QStringList paths;
            for (int id : m_catalog.assetIdsOfImport(importId)) paths << m_catalog.asset(id).path;
            m_engine->setSmartPreviews(paths, true);
        }
        m_buildSmartAfterImport = false;
        if (!m_lastImportError.isEmpty()) setStatus(tr("Import needs attention: %1").arg(m_lastImportError));
        m_autoTagTimer.start(0);
        emit importFinished(imported, skipped);
    });
}

Backend::~Backend() {
    stopAutoTags();
    cancelImportPreview();
    if (m_watchScan) { m_watchScanCancel = true; m_watchScan->wait(); delete m_watchScan; m_watchScan = nullptr; }
}

void Backend::setReducedMotion(bool on) {
    if (on == m_reducedMotion) return;
    m_reducedMotion = on;
    QSettings().setValue(QStringLiteral("access/reducedMotion"), on);
    emit accessibilityChanged();
}

void Backend::setHighContrast(bool on) {
    if (on == m_highContrast) return;
    m_highContrast = on;
    QSettings().setValue(QStringLiteral("access/highContrast"), on);
    emit accessibilityChanged();
}

void Backend::setColourCritical(bool on) {
    if (on == m_colourCritical) return;
    m_colourCritical = on;
    QSettings().setValue(QStringLiteral("appearance/colourCritical"), on);
    emit colourCriticalChanged();
}

void Backend::setShortcut(const QString &id, const QString &keys) {
    if (id.isEmpty()) return;
    m_shortcutOverrides[id] = keys;
    QSettings().setValue(QStringLiteral("shortcuts/overrides"), m_shortcutOverrides);
    emit shortcutsChanged();
}

void Backend::resetShortcut(const QString &id) {
    if (!m_shortcutOverrides.contains(id)) return;
    m_shortcutOverrides.remove(id);
    QSettings().setValue(QStringLiteral("shortcuts/overrides"), m_shortcutOverrides);
    emit shortcutsChanged();
}

void Backend::resetShortcuts() {
    if (m_shortcutOverrides.isEmpty()) return;
    m_shortcutOverrides.clear();
    QSettings().remove(QStringLiteral("shortcuts/overrides"));
    emit shortcutsChanged();
}

QString Backend::keyText(int key, int modifiers) const {
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_unknown || key == 0) return QString();
    const QKeyCombination combo(Qt::KeyboardModifiers(modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier)), Qt::Key(key));
    return QKeySequence(combo).toString(QKeySequence::PortableText);
}

void Backend::registerShortcuts(const QVariantMap &defaults) { m_shortcutDefaults = defaults; }

static QString normalKeys(QString k) {
    k = k.trimmed();
    if (k == QLatin1String("Del")) return QStringLiteral("Delete");
    if (k == QLatin1String("Shift+Del")) return QStringLiteral("Shift+Delete");
    return k;
}

QString Backend::shortcutHint(const QString &defaultKeys) const {
    const QString want = normalKeys(defaultKeys);
    if (want.isEmpty()) return QString();
    for (auto it = m_shortcutDefaults.constBegin(); it != m_shortcutDefaults.constEnd(); ++it) {
        if (normalKeys(it.value().toString()) != want) continue;
        const auto o = m_shortcutOverrides.constFind(it.key());
        return o == m_shortcutOverrides.constEnd() ? defaultKeys : o.value().toString();
    }
    return defaultKeys;
}

QString Backend::relinkPhoto(int id, const QString &fileOrUrl) {
    const AssetRecord r = m_catalog.asset(id);
    if (!r.id) return tr("no such photo");
    const QString to = localPath(fileOrUrl);
    const QFileInfo fi(to);
    if (!fi.isFile()) return tr("%1 is not a file").arg(to);
    if (r.size > 0 && fi.size() != r.size) return tr("%1 is a different size from %2").arg(fi.fileName(), r.filename);
    if (to == r.path) return QString();
    if (!m_catalog.movePath(r.path, to)) return m_catalog.lastError();
    if (m_engine) m_engine->moveImage(r.path, to);
    m_catalog.rescanFolders();
    m_model.invalidateAll();
    refresh();
    emit catalogChanged(); emit foldersChanged(); emit selectionChanged();
    setStatus(tr("Relinked %1 → %2").arg(r.filename, to));
    return QString();
}

// Slow file work (reading whole files, walking folders) off the window's
// thread, while the window keeps drawing and the status line says how far
// it has got. Returns true once `work` has run to its end; the caller then
// writes the catalog on this thread. One such job at a time. `work` must
// look at `cancel` often: the window closing (or a catalog switch) ends the
// nested loop early, and the job is then told to stop rather than holding
// the window, and every lock it has, until it finishes on its own.
bool Backend::waitOffThread(const std::function<void(std::atomic<int> &, const std::atomic<bool> &)> &work, const std::function<QString(int)> &progress) {
    if (m_offThread) { setStatus(tr("Another file job is still running")); return false; }
    m_offThread = true; m_offThreadCancel = false;
    emit fileJobChanged();
    const auto done = qScopeGuard([this] { m_offThread = false; emit fileJobChanged(); });
    std::atomic<int> step{0};
    QEventLoop loop;
    QThread *worker = QThread::create([&] { work(step, m_offThreadCancel); });
    connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer ticker; ticker.setInterval(250);
    connect(&ticker, &QTimer::timeout, this, [&] { setStatus(progress(step.load())); });
    worker->start(); ticker.start();
    if (!worker->isFinished()) loop.exec();
    // Still running: the application is quitting (quit() ends every event
    // loop, this one included). Stop it rather than wait for the whole job.
    if (!worker->isFinished()) m_offThreadCancel = true;
    worker->wait(); delete worker;
    if (m_offThreadCancel) { setStatus(tr("Stopped")); return false; }
    return true;
}

void Backend::cancelFileJob() { if (m_offThread) m_offThreadCancel = true; }

QVariantMap Backend::lastImportResult() const {
    return {{QStringLiteral("error"), m_lastImportError}, {QStringLiteral("copied"), m_importer.lastCopied()},
            {QStringLiteral("duplicates"), m_importer.lastDuplicates()}, {QStringLiteral("failed"), m_importer.lastFailed()},
            {QStringLiteral("backedUp"), m_importer.lastBackedUp()}, {QStringLiteral("backupFailed"), m_importer.lastBackupFailed()},
            {QStringLiteral("cancelled"), m_importer.lastCancelled()},
            {QStringLiteral("autoTagFailed"), m_catalog.failedAutoTagsForImport(m_catalog.lastImportId())}};
}

int Backend::relinkOfflineIn(const QString &folderOrUrl) {
    const QString folder = localPath(folderOrUrl);
    if (!QDir(folder).exists()) { setStatus(tr("%1 does not exist").arg(folder)); return 0; }
    // Every offline master, keyed by name + size; a file under the folder
    // that matches (and agrees on capture time when both know it) takes it.
    QHash<QString, QVector<int>> wanted;
    for (int id : m_catalog.assetIds(QStringLiteral("variant=0"), {}, QStringLiteral("id"))) {
        const AssetRecord r = m_catalog.asset(id);
        if (r.id && !QFile::exists(r.path)) wanted[r.filename.toLower() + QLatin1Char('|') + QString::number(r.size)] << id;
    }
    if (wanted.isEmpty()) { setStatus(tr("No photo is offline")); return 0; }
    // Walking the folder and reading each candidate's capture time can take
    // minutes on a large drive: that happens on a thread.
    struct Found { QString path, key, capturedAt; bool readable = false; };
    QVector<Found> found;
    const QSet<QString> keys(wanted.keyBegin(), wanted.keyEnd());
    if (!waitOffThread([&](std::atomic<int> &step, const std::atomic<bool> &cancel) {
            for (const QString &f : Importer::scan(folder, true, &cancel, &step)) {
                if (cancel) return;
                const QFileInfo fi(f);
                const QString key = fi.fileName().toLower() + QLatin1Char('|') + QString::number(fi.size());
                if (!keys.contains(key)) continue;
                AssetRecord meta;
                const bool readable = Metadata::read(f, meta);
                found.push_back({f, key, meta.capturedAt, readable});
            }
        }, [this](int seen) { return tr("Looking for offline photos: %1 files checked…").arg(seen); }))
        return 0;
    int done = 0;
    for (const Found &f : std::as_const(found)) {
        auto it = wanted.find(f.key);
        if (it == wanted.end() || it->isEmpty()) continue;
        for (int i = 0; i < it->size(); ++i) {
            const AssetRecord r = m_catalog.asset(it->at(i));
            // Relinked by hand (or back online) while the walk ran: leave it.
            if (!r.id || QFile::exists(r.path)) { it->removeAt(i--); continue; }
            if (f.readable && !r.capturedAt.isEmpty() && !f.capturedAt.isEmpty() && r.capturedAt != f.capturedAt) continue;
            if (m_catalog.assetExists(f.path) || !m_catalog.movePath(r.path, f.path)) continue;
            if (m_engine) m_engine->moveImage(r.path, f.path);
            it->removeAt(i);
            ++done;
            break;
        }
    }
    m_catalog.rescanFolders();
    m_model.invalidateAll();
    refresh();
    emit catalogChanged(); emit foldersChanged(); emit selectionChanged();
    int left = 0; for (const auto &v : std::as_const(wanted)) left += v.size();
    setStatus(left ? tr("Relinked %1 photo%2; %3 still offline").arg(done).arg(done == 1 ? "" : "s").arg(left)
                   : tr("Relinked %1 photo%2").arg(done).arg(done == 1 ? "" : "s"));
    return done;
}

int Backend::hashSelection() {
    QVector<int> ids; QStringList paths;
    for (int id : targets(0)) {
        const AssetRecord r = m_catalog.asset(id);
        if (!r.id || r.variant || !r.hash.isEmpty() || !QFile::exists(r.path)) continue;
        ids << id; paths << r.path;
    }
    // Reading hundreds of RAWs takes minutes: on a thread. The catalog is written here.
    QVector<QString> hashes(paths.size());
    if (!waitOffThread([&](std::atomic<int> &step, const std::atomic<bool> &cancel) {
                           for (int i = 0; i < paths.size() && !cancel; ++i) { hashes[i] = Importer::sha256(paths[i], &cancel); step.store(i + 1); } },
                       [this, total = paths.size()](int read) { return tr("Checksumming %1 of %2…").arg(read).arg(total); }))
        return 0;
    int n = 0;
    for (int i = 0; i < ids.size(); ++i) if (!hashes[i].isEmpty() && m_catalog.setHash(ids[i], hashes[i])) ++n;
    setStatus(tr("Checksummed %1 photo%2").arg(n).arg(n == 1 ? "" : "s"));
    return n;
}

int Backend::selectDuplicates() {
    QSet<int> shown;
    for (int id : m_model.ids()) shown.insert(id);
    m_selection.clear();
    m_currentId = 0;
    int groups = 0;
    for (const QVector<int> &g : m_catalog.duplicateGroups()) {
        int here = 0;
        for (int id : g) if (shown.contains(id)) { m_selection.insert(id); if (!m_currentId) m_currentId = id; ++here; }
        if (here > 1) ++groups;
    }
    m_model.invalidateAll();
    emit selectionChanged();
    setStatus(m_selection.isEmpty() ? tr("No two shown photos share a checksum (checksum them first: Image ▸ Duplicates ▸ Checksum Selection)")
                                    : tr("%1 photos in %2 duplicate group%3 selected").arg(m_selection.size()).arg(groups).arg(groups == 1 ? "" : "s"));
    return m_selection.size();
}

void Backend::watchHoldKeys(QObject *app) { if (app) app->installEventFilter(this); }

bool Backend::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (!ke->isAutoRepeat() && (ke->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            QString name;
            switch (ke->key()) {
            case Qt::Key_Backslash: name = QStringLiteral("backslash"); break;
            case Qt::Key_J: name = QStringLiteral("j"); break;
            case Qt::Key_M: name = QStringLiteral("m"); break;
            case Qt::Key_F: name = QStringLiteral("f"); break;
            default: break;
            }
            if (!name.isEmpty()) emit holdKey(name, event->type() == QEvent::KeyPress);
        }
    }
    return QObject::eventFilter(watched, event);
}

void Backend::setAutoBackup(bool on) {
    if (on == m_autoBackup) return;
    m_autoBackup = on;
    QSettings().setValue(QStringLiteral("catalog/autoBackup"), on);
    emit autoBackupChanged();
}

void Backend::setAskCatalogAtStartup(bool on) {
    if (on == m_askCatalog) return;
    m_askCatalog = on;
    QSettings().setValue(QStringLiteral("catalog/askAtStartup"), on);
    emit askCatalogAtStartupChanged();
}

// ---- Catalogs -------------------------------------------------------------
// A catalog owns the folder it sits in: CatalogBackup::engineConfig and
// engineLibrary derive the Develop databases from that folder, so two
// catalogs beside each other would fight over one engine library and its
// locks. Creating a catalog therefore creates a folder for it.

static QString catalogFileName() { return QStringLiteral("catalog.db"); }

void Backend::rememberCatalog(const QString &path) {
    const QString clean = QFileInfo(path).absoluteFilePath();
    if (clean.isEmpty()) return;
    QSettings settings;
    settings.setValue(QStringLiteral("catalog/activePath"), clean);
    QStringList recent = settings.value(QStringLiteral("catalog/recent")).toStringList();
    recent.removeAll(clean);
    recent.prepend(clean);
    while (recent.size() > 12) recent.removeLast();
    settings.setValue(QStringLiteral("catalog/recent"), recent);
}

QVariantList Backend::recentCatalogs() const {
    const QString active = catalogPath().isEmpty() ? QString() : QFileInfo(catalogPath()).absoluteFilePath();
    QStringList recent = QSettings().value(QStringLiteral("catalog/recent")).toStringList();
    if (!active.isEmpty() && !recent.contains(active)) recent.prepend(active);
    QVariantList rows;
    for (const QString &path : recent) {
        // A catalog on a disconnected drive is left out of the menu rather
        // than offered as something that cannot open. The stored list is not
        // rewritten, so it comes back with the drive.
        if (!QFileInfo(path).isFile()) continue;
        const QDir folder = QFileInfo(path).absoluteDir();
        rows.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("folder"), folder.absolutePath()},
            {QStringLiteral("name"), folder.dirName()},
            {QStringLiteral("current"), path == active},
        });
    }
    return rows;
}

void Backend::clearRecentCatalogs() {
    QSettings settings;
    const QString active = catalogPath().isEmpty() ? QString() : QFileInfo(catalogPath()).absoluteFilePath();
    settings.setValue(QStringLiteral("catalog/recent"), active.isEmpty() ? QStringList() : QStringList{active});
    setStatus(tr("Recent catalogs cleared"));
}

QString Backend::newCatalogParent() const {
    const QString last = QSettings().value(QStringLiteral("catalog/newParent")).toString();
    if (!last.isEmpty() && QFileInfo(last).isDir()) return last;
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!pictures.isEmpty() && QFileInfo(pictures).isDir()) return pictures;
    return QDir::homePath();
}

// Anything that would make a surprising folder name, and the separators that
// would put the catalog somewhere the user did not choose.
static QString catalogFolderName(const QString &name) {
    QString clean = name.simplified();
    clean.remove(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")));
    while (clean.startsWith(QLatin1Char('.'))) clean.remove(0, 1);
    return clean.trimmed();
}

QString Backend::localFile(const QString &fileOrUrl) const { return localPath(fileOrUrl); }
QUrl Backend::fileUrl(const QString &path) const { return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(localPath(path)); }

QVariantMap Backend::browseFolder(const QString &pathOrUrl) const {
    QString path = localPath(pathOrUrl);
    if (path == QLatin1String("~")) path = QDir::homePath();
    else if (path.startsWith(QLatin1String("~/"))) path = QDir::homePath() + path.mid(1);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isAbsolute() || !info.exists() || !info.isDir())
        return {{QStringLiteral("error"), tr("That folder could not be found. Check the path or reconnect the drive.")}};
    if (!info.isReadable() || !info.isExecutable())
        return {{QStringLiteral("error"), tr("You do not have permission to open that folder.")}};
    const QString absolute = QDir::cleanPath(info.absoluteFilePath());
    const QString parent = QFileInfo(absolute).absolutePath();
    return {{QStringLiteral("path"), absolute}, {QStringLiteral("url"), QUrl::fromLocalFile(absolute)},
            {QStringLiteral("parent"), QUrl::fromLocalFile(parent)}};
}

int Backend::requestPathListing(const QString &pathOrUrl, bool showHidden, bool foldersOnly, const QStringList &nameFilters) {
    const int request = ++m_folderListingRequest;
    const QString path = localPath(pathOrUrl);
    struct Listing { QVariantList entries; QString error; };
    const auto result = std::make_shared<Listing>();
    // Read actual paths, without parsing their #, ? or % characters as URLs.
    // A slow drive must not stop the interface. Results carry a request ID so
    // each picker can discard an older scan after navigating somewhere else.
    auto *worker = QThread::create([path, showHidden, foldersOnly, nameFilters, result] {
        const QFileInfo info(path);
        if (!info.isDir() || !info.isReadable() || !info.isExecutable()) {
            result->error = tr("This folder is no longer available. Check the path or reconnect the drive.");
            return;
        }
        QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable;
        if (!foldersOnly) filters |= QDir::Files;
        if (showHidden) filters |= QDir::Hidden;
        const auto entries = QDir(path).entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const auto &entry : entries) {
            if (entry.isDir() && !entry.isExecutable()) continue;
            if (!entry.isDir() && !nameFilters.isEmpty() && !QDir::match(nameFilters, entry.fileName())) continue;
            QVariantMap item{{QStringLiteral("name"), entry.fileName()},
                             {QStringLiteral("isDir"), entry.isDir()},
                             {QStringLiteral("url"), QUrl::fromLocalFile(entry.absoluteFilePath())}};
            const QString type = entry.suffix().toLower();
            if (!entry.isDir() && (type == "jpg" || type == "jpeg" || type == "png" || type == "tif" || type == "tiff")) {
                // Only the selected photograph is decoded, asynchronously by
                // the existing colour-managed provider. Ignore catalog edits.
                item["preview"] = Thumbnailer::sourceFor(entry.absoluteFilePath(), entry.lastModified().toSecsSinceEpoch(), 1600) + "&original=1";
                item["detail"] = type.toUpper() + QStringLiteral(" · ") + Metadata::formatSize(entry.size());
            }
            result->entries.append(item);
        }
    });
    connect(worker, &QThread::finished, this, [this, request, result] {
        emit pathListingReady(request, result->entries, result->error);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
    return request;
}

QVariantMap Backend::pickerFile(const QString &pathOrUrl, bool save, const QString &defaultSuffix) const {
    QString path = localPath(pathOrUrl);
    if (path.startsWith(QLatin1String("~/"))) path = QDir::homePath() + path.mid(1);
    if (!QFileInfo(path).isAbsolute()) return {{QStringLiteral("error"), tr("Choose a file using its full path.")}};
    if (save && QFileInfo(path).suffix().isEmpty() && !defaultSuffix.isEmpty())
        path += QLatin1Char('.') + (defaultSuffix.startsWith(QLatin1Char('.')) ? defaultSuffix.mid(1) : defaultSuffix);
    const QFileInfo info(path);
    if (info.isDir()) return {{QStringLiteral("error"), tr("That name belongs to a folder. Choose a file name instead.")}};
    if (!save && (!info.isFile() || !info.isReadable()))
        return {{QStringLiteral("error"), tr("That file could not be opened. Check the path and permissions.")}};
    const QFileInfo parent(info.absolutePath());
    if (save && (!parent.isDir() || !parent.isWritable() || !parent.isExecutable()
                 || (info.exists() && (!info.isFile() || !info.isWritable())) || (info.isSymLink() && !info.exists())))
        return {{QStringLiteral("error"), tr("This location cannot be written. Choose another folder or file name.")}};
    return {{QStringLiteral("path"), info.absoluteFilePath()}, {QStringLiteral("url"), QUrl::fromLocalFile(info.absoluteFilePath())},
            {QStringLiteral("parent"), QUrl::fromLocalFile(info.absolutePath())}, {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("exists"), info.exists()}};
}

QVariantMap Backend::createPickerFolder(const QString &parentOrUrl, const QString &name) const {
    const auto parent = browseFolder(parentOrUrl);
    if (parent.contains(QStringLiteral("error"))) return parent;
    if (name.trimmed().isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QChar::Null))
        return {{QStringLiteral("error"), tr("Enter a folder name without path separators.")}};
    const QDir directory(parent.value(QStringLiteral("path")).toString());
    if (QFileInfo::exists(directory.filePath(name))) return {{QStringLiteral("error"), tr("A file or folder already has that name.")}};
    if (!directory.mkdir(name)) return {{QStringLiteral("error"), tr("The folder could not be created. Check the location and permissions.")}};
    return browseFolder(directory.filePath(name));
}

QString Backend::proposedCatalogPath(const QString &parentOrUrl, const QString &name) const {
    const QString folder = catalogFolderName(name);
    if (folder.isEmpty()) return QString();
    return localPath(parentOrUrl) + QLatin1Char('/') + folder + QLatin1Char('/') + catalogFileName();
}

QString Backend::createCatalog(const QString &parentOrUrl, const QString &name) {
    const QString parent = localPath(parentOrUrl);
    const QString folderName = catalogFolderName(name);
    if (folderName.isEmpty()) { setStatus(tr("Give the catalog a name")); return QString(); }
    if (!QFileInfo(parent).isDir()) { setStatus(tr("Choose a folder to keep the catalog in")); return QString(); }
    const QString folder = parent + QLatin1Char('/') + folderName;
    const QString path = folder + QLatin1Char('/') + catalogFileName();
    if (QFileInfo::exists(path)) { setStatus(tr("There is already a catalog in %1").arg(folder)); return QString(); }
    // An existing folder is only used when it is empty: a catalog claims its
    // folder for engine databases, previews and backups.
    bool created = true;
    if (QFileInfo::exists(folder)) {
        if (!QFileInfo(folder).isDir()) { setStatus(tr("%1 is not a folder").arg(folder)); return QString(); }
        if (!QDir(folder).isEmpty()) { setStatus(tr("%1 already has files in it; choose another name").arg(folder)); return QString(); }
        created = false;
    } else if (!QDir().mkpath(folder)) {
        setStatus(tr("Could not create %1").arg(folder)); return QString();
    }
    // Build the schema now, in this window, so a folder that cannot be
    // written is reported here instead of failing after the relaunch.
    {
        // A connection name of its own: the default one belongs to the
        // catalog this window already has open.
        Catalog fresh(QStringLiteral("catalog-new"));
        if (!fresh.open(path)) {
            const QString error = fresh.lastError();
            fresh.close();
            QFile::remove(path);
            if (created) QDir().rmdir(folder);
            setStatus(tr("Could not create the catalog: %1").arg(error));
            return QString();
        }
        fresh.close();
    }
    QSettings().setValue(QStringLiteral("catalog/newParent"), parent);
    setStatus(tr("Created %1").arg(path));
    return path;
}

bool Backend::catalogAvailable(const QString &fileOrUrl) const {
    const QString path = localPath(fileOrUrl);
    if (!QFileInfo(path).isFile()) return false;
    // Only the locks this window does not already hold can be tested. Ordinary
    // catalogs share one engine settings folder, and catalogs in one folder
    // share an engine library, so those locks are usually this window's own;
    // a replacement window waits for them with --await-catalog instead.
    const QString mine = catalogPath();
    auto ours = [&mine](const QString &a, const QString &b) {
        return !mine.isEmpty() && QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
    };
    auto free = [](const QString &lockPath) {
        QLockFile lock(lockPath);
        lock.setStaleLockTime(0);
        return lock.tryLock(0);
    };
    if (!free(path + QStringLiteral(".omaraw-lock"))) return false;
    if (!ours(CatalogBackup::engineLibrary(path), CatalogBackup::engineLibrary(mine))
        && !free(CatalogBackup::engineLibrary(path) + QStringLiteral(".omaraw-lock"))) return false;
    const QString config = CatalogBackup::engineConfig(path);
    if (QFileInfo(config).isDir() && !ours(config, CatalogBackup::engineConfig(mine))
        && !free(QFileInfo(config).canonicalFilePath() + QStringLiteral("/.omaraw-lock"))) return false;
    return true;
}

bool Backend::switchCatalog(const QString &fileOrUrl) {
    // A checksum or a search for offline photos writes this catalog when it
    // ends; the window must not go while it reads.
    if (m_offThread) { setStatus(tr("Wait for the checksum or offline search to finish (or stop it) before opening another catalog")); return false; }
    const QString path = localPath(fileOrUrl);
    if (!QFileInfo(path).isFile()) { setStatus(tr("That catalog no longer exists: %1").arg(path)); return false; }
    if (!catalogPath().isEmpty()
        && QFileInfo(path).canonicalFilePath() == QFileInfo(catalogPath()).canonicalFilePath()) {
        setStatus(tr("That catalog is already open")); return false;
    }
    // Check the catalog is free before this window goes: if the replacement
    // could not open it, the user would lose the window they were working in
    // and land in the chooser. These locks belong to the catalog being opened,
    // never to the one this window holds, so taking them briefly is safe.
    // Check it can actually be opened before this window goes: otherwise the
    // user loses the window they were working in and lands in the chooser.
    if (!catalogAvailable(path)) {
        setStatus(tr("%1 is already open in another OmaRAW window")
                      .arg(QFileInfo(path).absoluteDir().dirName()));
        return false;
    }
    // The engine initialises once per process, so the window is relaunched
    // onto the chosen catalog. --await-catalog makes the new process wait for
    // the locks this one is about to drop, and --await-exit for this process
    // to be gone, since the engine's own locks outlive OmaRAW's.
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({QStringLiteral("--catalog"), path, QStringLiteral("--await-catalog"),
                          QStringLiteral("--await-exit"), QString::number(QCoreApplication::applicationPid())});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &key : {QStringLiteral("OMARAW_CATALOG"), QStringLiteral("OMARAW_SELFTEST"), QStringLiteral("OMARAW_DEMO"), QStringLiteral("OMARAW_GRAB")}) env.remove(key);
    process.setProcessEnvironment(env);
    if (!process.startDetached()) { setStatus(tr("Could not open %1").arg(path)); return false; }
    rememberCatalog(path);
    setStatus(tr("Opening %1…").arg(QFileInfo(path).absoluteDir().dirName()));
    // Leaving now is what lets the new window in: it is waiting on the locks
    // this one holds. Deferred so the caller returns and the window closes
    // through the ordinary shutdown that releases them.
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    return true;
}

void Backend::dailyBackup() {
    if (!m_autoBackup || !m_catalog.isOpen()) return;
    const QString key = QStringLiteral("catalog/lastCompleteBackup/") + QString::fromLatin1(QCryptographicHash::hash(m_catalog.path().toUtf8(), QCryptographicHash::Sha256).toHex());
    const QDateTime last = QSettings().value(key).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 24 * 3600) return;
    QString error;
    if (CatalogBackup::create(m_catalog.path(), m_catalog.backupPath(QStringLiteral("auto")) + QStringLiteral(".omaraw-backup"), &error).isEmpty()) {
        setStatus(tr("Daily backup failed: %1").arg(error)); return;
    }
    QSettings().setValue(key, QDateTime::currentDateTime());
    // The newest seven daily copies stay; pre-migration and manual ones are never pruned.
    QDir dir(m_catalog.backupDir());
    const QStringList autos = dir.entryList({QFileInfo(m_catalog.path()).completeBaseName() + QStringLiteral("-auto-*.omaraw-backup")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (int i = 0; i + 7 < autos.size(); ++i) {
        QString error;
        const QString folder = dir.filePath(autos[i]);
        if (!QFileInfo(folder).isSymLink() && CatalogBackup::verify(folder + QStringLiteral("/manifest.json"), &error)) QDir(folder).removeRecursively();
    }
}

QString Backend::backupCatalog() {
    if (!m_catalog.isOpen()) return QString();
    if (importing() || (m_engine && !m_engine->maintenanceReady()
        && (m_engine->available() || QFileInfo::exists(CatalogBackup::engineLibrary(m_catalog.path()))))) {
        setStatus(tr("Wait for importing, editing and exporting to finish before backing up")); return {};
    }
    QString error;
    const QString manifest = CatalogBackup::create(m_catalog.path(), m_catalog.backupPath(QStringLiteral("manual")) + QStringLiteral(".omaraw-backup"), &error);
    setStatus(manifest.isEmpty() ? tr("Backup failed: %1").arg(error) : tr("Catalog and Develop backup saved to %1").arg(QFileInfo(manifest).absolutePath()));
    return manifest;
}

QString Backend::restoreCatalogBackup(const QString &manifest, const QString &parentFolder) {
    const QString destination = localPath(parentFolder) + QStringLiteral("/OmaRAW-recovered-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::Id128).left(8);
    QString error;
    const QString path = CatalogBackup::restore(localPath(manifest), destination, &error);
    setStatus(path.isEmpty() ? tr("Restore failed: %1").arg(error) : tr("Recovered catalog saved to %1").arg(path));
    return path;
}

bool Backend::openCatalogWindow(const QString &fileOrUrl) {
    const QString path = localPath(fileOrUrl);
    if (!QFileInfo(path).isFile() || QFileInfo(path).canonicalFilePath() == QFileInfo(catalogPath()).canonicalFilePath()) {
        setStatus(tr("Choose another existing catalog")); return false;
    }
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({QStringLiteral("--catalog"), path});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &key : {QStringLiteral("OMARAW_CATALOG"), QStringLiteral("OMARAW_SELFTEST"), QStringLiteral("OMARAW_DEMO"), QStringLiteral("OMARAW_GRAB")}) env.remove(key);
    process.setProcessEnvironment(env);
    if (!process.startDetached()) { setStatus(tr("Could not open the recovered catalog")); return false; }
    return true;
}

QString Backend::checkCatalog() {
    if (!m_catalog.isOpen()) return QString();
    const QString r = m_catalog.integrityCheck();
    setStatus(r == QLatin1String("ok") ? tr("Catalog integrity: ok (%1 photos)").arg(m_catalog.count())
                                       : tr("Catalog integrity: %1").arg(r.section(QLatin1Char('\n'), 0, 0)));
    return r;
}

void Backend::startCatalogCheck() {
    if (m_catalog.isOpen()) m_maintenance.check(m_catalog.path(), CatalogBackup::engineConfig(m_catalog.path()));
}

QVariantMap Backend::catalogMaintenanceInfo() const {
    if (!m_catalog.isOpen()) return {};
    const QDir backups(m_catalog.backupDir());
    const auto entries = backups.entryInfoList({QFileInfo(m_catalog.path()).completeBaseName() + "-*.omaraw-backup"},
                                              QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Time);
    return {{"path", m_catalog.path()}, {"count", m_totalCount},
            {"size", Metadata::formatSize(QFileInfo(m_catalog.path()).size() + QFileInfo(m_catalog.path() + "-wal").size())},
            {"backupFolder", m_catalog.backupDir()},
            {"lastBackup", entries.isEmpty() ? QString() : entries.first().lastModified().toString("d MMM yyyy, HH:mm")}};
}

void Backend::revealBackups() const {
    if (!m_catalog.isOpen()) return;
    QDir().mkpath(m_catalog.backupDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_catalog.backupDir()));
}

QString Backend::licencesPath() const {
    // A package installs beside /usr/bin; a user-local test install keeps
    // the same layout under its own prefix.
    const QDir bin(QCoreApplication::applicationDirPath());
    for (const QString &candidate : {bin.absoluteFilePath("../share/licenses/omaraw"), QStringLiteral("/usr/share/licenses/omaraw")})
        if (QFileInfo(candidate).isDir()) return QDir(candidate).canonicalPath();
    return QString();
}

void Backend::revealLicences() const {
    const QString path = licencesPath();
    if (path.isEmpty()) { const_cast<Backend *>(this)->setStatus(tr("The licence notices are not installed with this build")); return; }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString Backend::documentationPathFor(const QString &binDir) {
    // Installed: the guide sits beside the binary the way the notices do.
    const QDir bin(binDir);
    for (const QString &candidate : {bin.absoluteFilePath(QStringLiteral("../share/doc/omaraw")), QStringLiteral("/usr/share/doc/omaraw")})
        if (QFileInfo(candidate + QStringLiteral("/USER-GUIDE.md")).isFile()) return QDir(candidate).canonicalPath();
    // Run from a build tree: the manual is in the source it was built from.
    // Walk up, and look into each sibling, which covers a build directory
    // beside the sources and the unpacked source of a package build.
    QDir up(binDir);
    const auto manualIn = [](const QString &directory) {
        for (const auto &name : {QStringLiteral("/docs/USER-GUIDE.md"), QStringLiteral("/README.md")}) {
            const QFileInfo file(directory + name);
            if (file.isFile()) return file.canonicalFilePath();
        }
        return QString();
    };
    for (int level = 0; level < 4; ++level) {
        const QString here = manualIn(up.absolutePath());
        if (!here.isEmpty()) return here;
        for (const QFileInfo &sibling : up.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString there = manualIn(sibling.absoluteFilePath());
            if (!there.isEmpty() && QFileInfo(sibling.absoluteFilePath() + QStringLiteral("/src/help/start.md")).isFile())
                return there;
        }
        if (!up.cdUp()) break;
    }
    return QString();
}

QString Backend::documentationPath() const { return documentationPathFor(QCoreApplication::applicationDirPath()); }

void Backend::revealDocumentation() const {
    const QString path = documentationPath();
    if (path.isEmpty()) { const_cast<Backend *>(this)->setStatus(tr("The user guide is not installed with this build")); return; }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void Backend::setAutoAdvance(bool on) {
    if (on == m_autoAdvance) return;
    m_autoAdvance = on;
    QSettings().setValue(QStringLiteral("library/autoAdvance"), on);
    emit autoAdvanceChanged();
}

// A cull landed by key or menu (id 0) on a single photo: step on, unless
// this was the last one shown.
void Backend::advanceAfterCull(int id) {
    if (!m_autoAdvance || id != 0 || m_selection.size() != 1 || !m_currentId) return;
    const int row = m_model.rowOf(m_currentId);
    if (row >= 0 && row + 1 < m_model.rowCount()) step(1);
}

void Backend::saveFilter(const QString &name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    QVariantMap f;
    f[QStringLiteral("name")] = n;
    f[QStringLiteral("text")] = m_filterText;
    f[QStringLiteral("rating")] = m_filterRating;
    f[QStringLiteral("flag")] = m_filterFlag;
    f[QStringLiteral("label")] = m_filterLabel;
    f[QStringLiteral("sortKey")] = m_sortKey;
    f[QStringLiteral("sortDescending")] = m_sortDescending;
    f[QStringLiteral("extra")] = m_filterExtra;
    QVariantList out;
    for (const QVariant &v : std::as_const(m_savedFilters)) if (v.toMap().value(QStringLiteral("name")).toString() != n) out << v;
    out << f;
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("name")).toString()) < 0; });
    m_savedFilters = out;
    QSettings().setValue(QStringLiteral("library/savedFilters"), m_savedFilters);
    emit savedFiltersChanged();
    setStatus(tr("Saved filter %1").arg(n));
}

bool Backend::applySavedFilter(const QString &name) {
    for (const QVariant &v : std::as_const(m_savedFilters)) {
        const QVariantMap f = v.toMap();
        if (f.value(QStringLiteral("name")).toString() != name) continue;
        // One refresh for the lot.
        m_filterText = f.value(QStringLiteral("text")).toString();
        m_filterRating = qBound(0, f.value(QStringLiteral("rating")).toInt(), 5);
        m_filterFlag = f.value(QStringLiteral("flag")).toString();
        m_filterLabel = f.value(QStringLiteral("label")).toString();
        m_sortKey = f.value(QStringLiteral("sortKey"), m_sortKey).toString();
        m_sortDescending = f.value(QStringLiteral("sortDescending"), m_sortDescending).toBool();
        m_filterExtra = f.value(QStringLiteral("extra")).toMap();
        emit filterChanged();
        refresh();
        setStatus(tr("Filter %1").arg(name));
        return true;
    }
    return false;
}

bool Backend::deleteSavedFilter(const QString &name) {
    QVariantList out;
    for (const QVariant &v : std::as_const(m_savedFilters)) if (v.toMap().value(QStringLiteral("name")).toString() != name) out << v;
    if (out.size() == m_savedFilters.size()) return false;
    m_savedFilters = out;
    QSettings().setValue(QStringLiteral("library/savedFilters"), m_savedFilters);
    emit savedFiltersChanged();
    return true;
}

void Backend::setWriteSidecars(bool on) {
    if (on == m_writeSidecars) return;
    m_writeSidecars = on;
    QSettings().setValue(QStringLiteral("sidecars/write"), on);
    emit writeSidecarsChanged();
    setStatus(on ? tr("Writing XMP sidecars beside originals") : tr("Sidecars off — metadata stays in the catalog"));
}

qint64 Backend::sidecarMtime(const QString &path) {
    QString sc = Metadata::existingSidecar(path);
    if (sc.isEmpty() && Metadata::embeddable(path) && QSettings().value(QStringLiteral("sidecars/embed"), false).toBool()) sc = path;
    return sc.isEmpty() ? 0 : QFileInfo(sc).lastModified().toSecsSinceEpoch();
}

QVariantMap Backend::sidecarReview(int id) const {
    QVariantMap m;
    const AssetRecord r = m_catalog.asset(id);
    if (!r.id) return m;
    const Metadata::Sidecar sc = Metadata::readSidecar(r.path, m_embedXmp);
    m[QStringLiteral("present")] = sc.present;
    if (!sc.present) { m[QStringLiteral("changed")] = false; return m; }
    QStringList differs;
    const QStringList mine = m_catalog.keywordsFor(id);
    if (sc.rating != r.rating) differs << QStringLiteral("rating");
    if (sc.label != r.label) differs << QStringLiteral("label");
    if (sc.title != r.title) differs << QStringLiteral("title");
    if (sc.caption != r.caption) differs << QStringLiteral("caption");
    if (sc.creator != r.creator) differs << QStringLiteral("creator");
    if (sc.copyright != r.copyright) differs << QStringLiteral("copyright");
    QStringList a = sc.keywords, b = mine; a.sort(Qt::CaseInsensitive); b.sort(Qt::CaseInsensitive);
    if (a != b) differs << QStringLiteral("keywords");
    m[QStringLiteral("rating")] = sc.rating;
    m[QStringLiteral("label")] = sc.label;
    m[QStringLiteral("keywords")] = sc.keywords;
    m[QStringLiteral("title")] = sc.title;
    m[QStringLiteral("caption")] = sc.caption;
    m[QStringLiteral("creator")] = sc.creator;
    m[QStringLiteral("copyright")] = sc.copyright;
    m[QStringLiteral("hasEdit")] = sc.hasEdit || (sc.hasCameraRaw && !sc.cameraRawOurs);
    m[QStringLiteral("differs")] = differs;
    // Changed = touched since we last read or wrote it, and not saying the same thing.
    m[QStringLiteral("changed")] = sidecarMtime(r.path) != r.sidecarSeen && !differs.isEmpty();
    return m;
}

void Backend::applySidecar(int id, const QString &field) {
    const AssetRecord r = m_catalog.asset(id);
    if (!r.id) return;
    const Metadata::Sidecar sc = Metadata::readSidecar(r.path, m_embedXmp);
    if (!sc.present) return;
    const bool all = field.isEmpty();
    if (all || field == QLatin1String("rating")) m_catalog.setRating({id}, sc.rating);
    if (all || field == QLatin1String("label")) m_catalog.setLabel({id}, sc.label);
    if (all || field == QLatin1String("title")) m_catalog.setTitle({id}, sc.title);
    if (all || field == QLatin1String("caption")) m_catalog.setCaption({id}, sc.caption);
    if (all || field == QLatin1String("creator")) m_catalog.setCreator({id}, sc.creator);
    if (all || field == QLatin1String("copyright")) m_catalog.setCopyright({id}, sc.copyright);
    if (all || field == QLatin1String("keywords")) {
        for (const QString &k : m_catalog.keywordsFor(id)) m_catalog.removeKeyword({id}, k);
        for (const QString &k : sc.keywords) m_catalog.addKeyword({id}, k);
    }
    // Nothing differs any more: this version of the sidecar is the one we know.
    if (sidecarReview(id).value(QStringLiteral("differs")).toStringList().isEmpty()) m_catalog.setSidecarSeen({id}, sidecarMtime(r.path));
    m_model.invalidate({id});
    emit selectionChanged();
    emit catalogChanged();
    setStatus(all ? tr("Took the sidecar's rating, label and keywords for %1").arg(r.filename) : tr("Took the sidecar's %1 for %2").arg(field, r.filename));
}

// Edits carried in by sidecars, onto their photos: OmaRAW's own exactly
// (and the photo needs no first edit then: this is its edit), the source editor's
// converted, on top of OmaRAW's first edit as the source editor's sit on its own
// defaults. The XMP settings with no OmaRAW counterpart are listed
// once in the status line.
int Backend::applyEditsFromSidecars(const QVector<Importer::SidecarEdit> &edits) {
    if (edits.isEmpty()) return 0;
    if (!m_engine || !m_engine->available()) { setStatus(tr("%1 photos carry an edit in their sidecar; it applies once the develop engine is running").arg(edits.size())); return 0; }
    QVariantList items;
    int xmp = 0;
    for (const Importer::SidecarEdit &e : edits) {
        if (e.exact) {
            m_catalog.clearAutoPending(e.id);
            items << QVariantMap{{"path", e.path}, {"variant", 0}, {"sidecar", e.file}, {"exact", true}};
            continue;
        }
        const XmpPreset::Result converted = XmpPreset::readSidecar(e.packet, e.raw);
        if (!converted.error.isEmpty() || converted.values.isEmpty()) continue;
        items << QVariantMap{{"path", e.path}, {"variant", 0}, {"values", converted.values}};
        ++xmp;
    }
    if (items.isEmpty()) return 0;
    m_engine->applySidecarEdits(items);
    setStatus(xmp ? tr("Applying the edits that came with %1 photos (%2 from XMP, converted approximately)").arg(items.size()).arg(xmp)
                        : tr("Applying the edits that came with %1 photos").arg(items.size()));
    return items.size();
}

// What the catalog says about a photo, as its sidecar carries it.
Metadata::Sidecar Backend::sidecarData(const AssetRecord &r) const {
    Metadata::Sidecar data;
    data.rating = r.rating; data.label = r.label; data.keywords = m_catalog.keywordsFor(r.id);
    data.title = r.title; data.caption = r.caption; data.creator = r.creator; data.copyright = r.copyright;
    return data;
}

// Where a photo's metadata goes: its sidecar, or, with in-file metadata on,
// inside a DNG, JPEG or TIFF (using embedded XMP).
bool Backend::storeMetadata(const AssetRecord &r, const Metadata::SidecarEdit *edit, QString *error) {
    const bool inFile = m_embedXmp && Metadata::embeddable(r.path);
    if (inFile ? !Metadata::writeEmbedded(r.path, sidecarData(r), edit, error) : !Metadata::writeSidecar(r.path, sidecarData(r), edit, error)) return false;
    if (inFile && m_engine) m_engine->sourceRewritten(r.path);
    m_catalog.setSidecarSeen({r.id}, sidecarMtime(r.path));
    return true;
}

void Backend::setEmbedXmp(bool on) {
    if (on == m_embedXmp) return;
    m_embedXmp = on;
    QSettings().setValue(QStringLiteral("sidecars/embed"), on);
    emit writeSidecarsChanged();
    setStatus(on ? tr("DNG, JPEG and TIFF files will carry their metadata and edit inside themselves")
                 : tr("Every photo's metadata goes to a sidecar; originals are not changed"));
}

// A photo's sidecar from the catalog, now. With `withEdit` the edit follows
// from the engine once it has read it (sidecarEditReady); otherwise, and
// until then, the develop sections already in the file stay as they are.
// A variant's metadata is its master's file: only the master writes.
bool Backend::writeSidecarFor(int id, bool withEdit, QString *error) {
    AssetRecord r = m_catalog.asset(id);
    if (!r.id) return false;
    if (r.variant) { r = m_catalog.asset(m_catalog.idForPath(r.path, 0)); if (!r.id) return false; }
    if (!storeMetadata(r, nullptr, error)) return false;
    if (withEdit && m_engine && m_engine->ready()) m_engine->requestSidecarEdit(r.path, r.isRaw);
    return true;
}

void Backend::keepCatalogOverSidecar(int id) {
    const AssetRecord r = m_catalog.asset(id);
    if (!r.id) return;
    QString err;
    if (!writeSidecarFor(id, false, &err)) { setStatus(tr("Sidecar not written for %1: %2").arg(r.filename, err)); return; }
    m_model.invalidate({id});
    emit selectionChanged();
    setStatus(tr("Sidecar of %1 rewritten from the catalog").arg(r.filename));
}

// The source editor's "Read Metadata from File": each selected photo takes what its
// sidecar says, its metadata and its edit, over the catalog's.
int Backend::readSidecarsForSelection() {
    QVector<Importer::SidecarEdit> edits;
    int n = 0;
    QSet<QString> seen;
    for (int id : targets(0)) {
        AssetRecord r = m_catalog.asset(id);
        if (!r.id || seen.contains(r.path)) continue;
        seen.insert(r.path);
        if (r.variant) r = m_catalog.asset(m_catalog.idForPath(r.path, 0));
        const Metadata::Sidecar sc = Metadata::readSidecar(r.path, m_embedXmp);
        if (!r.id || !sc.present) continue;
        applySidecar(r.id);
        ++n;
        if (sc.hasEdit || (sc.hasCameraRaw && !sc.cameraRawOurs)) edits.append({r.id, r.path, sc.file, sc.hasEdit, r.isRaw, sc.packet});
    }
    const int applied = applyEditsFromSidecars(edits);
    if (!applied) setStatus(n ? tr("Read %1 sidecar%2").arg(n).arg(n == 1 ? "" : "s") : tr("None of the selected photos has a sidecar"));
    emit selectionChanged();
    return n;
}

int Backend::checkSidecars() {
    QVector<int> changed;
    for (int id : m_model.ids()) if (sidecarReview(id).value(QStringLiteral("changed")).toBool()) changed << id;
    if (!changed.isEmpty()) {
        m_selection.clear();
        for (int id : changed) m_selection.insert(id);
        m_currentId = changed.first();
        m_model.invalidateAll();
        emit selectionChanged();
    }
    setStatus(changed.isEmpty() ? tr("No sidecar was changed outside OmaRAW")
                                : tr("%1 sidecar%2 changed outside OmaRAW — selected").arg(changed.size()).arg(changed.size() == 1 ? "" : "s"));
    return changed.size();
}

void Backend::syncSidecars(const QVector<int> &ids) {
    if (!m_writeSidecars) return;
    for (int id : ids) {
        const AssetRecord r = m_catalog.asset(id);
        QString err;
        if (r.id && !writeSidecarFor(id, false, &err))
            setStatus(tr("Sidecar not written for %1: %2").arg(r.filename, err));
    }
}

int Backend::writeSidecarsForSelection() {
    const QVector<int> t = targets(0);
    int n = 0;
    for (int id : t) {
        const AssetRecord r = m_catalog.asset(id);
        if (r.id && writeSidecarFor(id, true)) ++n;
    }
    setStatus(tr("Wrote %1 sidecar%2").arg(n).arg(n == 1 ? "" : "s"));
    emit selectionChanged();
    return n;
}

QString Backend::defaultCatalogPath() {
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("OMARAW_CATALOG"));
    if (!env.isEmpty()) return env;
    const QString active = QSettings().value(QStringLiteral("catalog/activePath")).toString();
    if (!active.isEmpty() && QFileInfo(active).isFile()) return active;
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/omaraw/catalog.db");
}

QString Backend::browsingSessionKey() const {
    const QFileInfo file(m_catalog.path());
    const QString path = file.canonicalFilePath().isEmpty() ? file.absoluteFilePath() : file.canonicalFilePath();
    return QStringLiteral("library/sessions/")
        + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}

void Backend::saveBrowsingSession() {
    if (!m_rememberBrowsing || m_restoringBrowsing || !m_catalog.isOpen()) return;
    QVariantList selected, expanded;
    QList<int> ids = m_selection.values(), stacks = m_expandedStacks.values();
    std::sort(ids.begin(), ids.end()); std::sort(stacks.begin(), stacks.end());
    for (int id : ids) selected << id;
    for (int id : stacks) expanded << id;
    const QVariantMap state{{"current", m_currentId}, {"selected", selected}, {"anchor", m_anchorId},
        {"source", m_sourceKind}, {"sourceId", m_sourceId}, {"sort", m_sortKey}, {"descending", m_sortDescending},
        {"text", m_filterText}, {"rating", m_filterRating}, {"flag", m_filterFlag}, {"label", m_filterLabel},
        {"extra", m_filterExtra}, {"collapseStacks", m_collapseStacks}, {"expandedStacks", expanded}};
    QSettings settings;
    const QString key = browsingSessionKey();
    if (settings.value(key).toMap() != state) settings.setValue(key, state);
}

void Backend::restoreBrowsingSession() {
    const QVariantMap state = QSettings().value(browsingSessionKey()).toMap();
    const AssetRecord current = m_catalog.asset(state.value("current").toInt());
    m_currentId = current.id;
    m_selection.clear();
    for (const QVariant &id : state.value("selected").toList())
        if (id.toInt() > 0) m_selection.insert(id.toInt()); // refresh drops deleted/hidden entries
    if (m_currentId) m_selection.insert(m_currentId);
    m_anchorId = state.value("anchor", m_currentId).toInt();
    m_sortKey = state.value("sort", "captured").toString();
    if (!(QStringList{"captured", "imported", "filename", "rating", "edited", "custom"}).contains(m_sortKey)) m_sortKey = "captured";
    m_sortDescending = state.value("descending", false).toBool();
    m_filterText = state.value("text").toString();
    m_filterRating = qBound(0, state.value("rating").toInt(), 5);
    m_filterFlag = state.value("flag").toString(); m_filterLabel = state.value("label").toString();
    m_filterExtra.clear();
    const QVariantMap extra = state.value("extra").toMap();
    for (const char *key : {"type", "edited", "gps", "camera"})
        if (!extra.value(QLatin1String(key)).toString().isEmpty()) m_filterExtra[QLatin1String(key)] = extra.value(QLatin1String(key)).toString();
    m_collapseStacks = state.value("collapseStacks", true).toBool();
    m_expandedStacks.clear();
    for (const QVariant &id : state.value("expandedStacks").toList()) if (id.toInt() > 0) m_expandedStacks.insert(id.toInt());
    if (current.stackId && current.stackPos > 0) m_expandedStacks.insert(current.stackId);
    QString source = state.value("source", "all").toString();
    if (!(QStringList{"all", "quick", "previousImport", "recentEdited", "recentAdded", "rejected",
                      "recentExported", "folder", "album", "smart", "keyword", "import"}).contains(source)) source = "all";
    m_sourceTitle.clear();
    emit filterChanged();
    setSource(source, qMax(0, state.value("sourceId").toInt()));
    // A collection may have been removed or its rules changed while closed.
    // Keep an existing last photo reachable instead of silently selecting row 0.
    if (m_sourceTitle.isEmpty() || (current.id && m_model.rowOf(current.id) < 0)) {
        m_filterText.clear(); m_filterRating = 0; m_filterFlag.clear(); m_filterLabel.clear(); m_filterExtra.clear();
        m_currentId = current.id; m_selection.clear();
        if (current.id) m_selection.insert(current.id);
        emit filterChanged();
        setSource(QStringLiteral("all"));
    }
    if (!m_currentId && m_model.rowCount() > 0) select(m_model.idAt(0), 0);
    if (!m_selection.contains(m_anchorId)) m_anchorId = m_currentId;
}

bool Backend::openCatalog(const QString &path) {
    saveBrowsingSession();
    m_restoringBrowsing = true;
    const auto restoring = qScopeGuard([this] { m_restoringBrowsing = false; saveBrowsingSession(); });
    stopAutoTags();
    m_aiFocusId = 0;
    if (m_engine) m_engine->clearSnapshotComparison();
    const bool ok = m_catalog.open(path);
    m_maintenance.setCatalog(ok ? m_catalog.path() : QString());
    m_firstEditChecked = 0;   // ids belong to a catalog
    { QMutexLocker lock(&m_snapMutex); m_snapImages.clear(); m_snapCatalogPath = ok ? m_catalog.path() : QString(); ++m_snapRevision; }
    if (!ok) setStatus(tr("Could not open catalog: %1").arg(m_catalog.lastError()));
    // Every catalog that opens becomes the one to reopen next launch and
    // joins the recents, however it was chosen: menu, recents or command line.
    if (ok) rememberCatalog(m_catalog.path());
    if (ok) m_catalog.rescanFolders(); // drives come and go between sessions
    if (ok) dailyBackup();
    if (ok) refreshWatches();
    if (ok) {
        // A rename or move interrupted last time: settle it from the disk.
        const int settled = m_catalog.replayMoves();
        const int open = m_catalog.pendingMoves().size();
        if (settled || open) setStatus(open ? tr("Settled %1 interrupted moves; %2 need a look (File ▸ Offline Originals ▸ Rescan Folders)").arg(settled).arg(open)
                                            : tr("Settled %1 interrupted moves").arg(settled));
    }
    m_selection.clear();
    m_currentId = 0;
    if (ok && m_rememberBrowsing) restoreBrowsingSession();
    else setSource(QStringLiteral("all"));
    emit catalogChanged();
    emit selectionChanged();
    if (ok) { ensureAutoTagAlbums(); m_autoTagTimer.start(0); }
    emit autoTagSettingsChanged();
    emit autoTagsChanged();
    return ok;
}

QString Backend::catalogName() const {
    const QString stem = QFileInfo(m_catalog.path()).completeBaseName();
    return stem.isEmpty() ? tr("No catalog") : (stem == QLatin1String("catalog") ? tr("OmaRAW Catalog") : stem);
}

QString Backend::currentFilename() const {
    return m_currentId ? m_catalog.asset(m_currentId).filename : QString();
}

QString Backend::version() const { return QStringLiteral(OMARAW_VERSION); }

// ── filter/sort ─────────────────────────────────────────────────────────

QString Backend::whereFor(const QString &kind, int id, const QString &text, int rating,
                          const QString &flag, const QString &label, QVariantList &binds) {
    return whereFor(kind, id, text, rating, flag, label, QVariantMap(), binds);
}

QString Backend::whereFor(const QString &kind, int id, const QString &text, int rating,
                          const QString &flag, const QString &label, const QVariantMap &extra, QVariantList &binds) {
    QStringList w;
    if (kind == QLatin1String("folder")) {
        w << QStringLiteral("folder_id IN (SELECT child.id FROM folders child JOIN folders parent ON parent.id=?"
                            " WHERE child.path=parent.path OR substr(child.path,1,length(parent.path)+1)=parent.path || '/')");
        binds << id;
    } else if (kind == QLatin1String("album") || kind == QLatin1String("quick")) {
        w << QStringLiteral("id IN (SELECT asset_id FROM album_assets WHERE album_id=?)");
        binds << id;
    } else if (kind == QLatin1String("smart")) {
        w << QStringLiteral("id IN (SELECT id FROM assets WHERE (SELECT smart_rule FROM albums WHERE id=?) IS NOT NULL)");
        binds << id;
    } else if (kind == QLatin1String("import")) {
        w << QStringLiteral("import_id=?");
        binds << id;
    } else if (kind == QLatin1String("keyword")) {
        w << QStringLiteral("id IN (SELECT ak.asset_id FROM asset_keywords ak JOIN keywords k ON k.id=ak.keyword_id"
                            " JOIN keywords parent ON parent.id=?"
                            " WHERE k.name=parent.name OR substr(k.name,1,length(parent.name)+1) COLLATE NOCASE = parent.name || '/')");
        binds << id;
    } else if (kind == QLatin1String("previousImport")) {
        w << QStringLiteral("import_id=(SELECT id FROM imports WHERE count>0 ORDER BY id DESC LIMIT 1)");
    } else if (kind == QLatin1String("recentAdded")) {
        w << QStringLiteral("imported_at >= datetime('now','localtime','-7 days')");
    } else if (kind == QLatin1String("recentExported")) {
        w << QStringLiteral("exported_at >= datetime('now','localtime','-7 days')");
    } else if (kind == QLatin1String("recentEdited")) {
        w << QStringLiteral("edited=1");
    } else if (kind == QLatin1String("rejected")) {
        w << QStringLiteral("flag=-1");
    } else if (kind == QLatin1String("offline")) {
        w << QStringLiteral("folder_id IN (SELECT id FROM folders WHERE online=0)");
    }
    // Rejected photos stay out of every ordinary view; the Rejected source
    // and an explicit reject filter are the two ways to see them.
    if (kind != QLatin1String("rejected") && flag != QLatin1String("reject") && flag != QLatin1String("any"))
        w << QStringLiteral("flag<>-1");
    if (!text.trimmed().isEmpty()) {
        w << QStringLiteral("(filename LIKE ? OR make LIKE ? OR model LIKE ? OR lens LIKE ? OR path LIKE ? OR title LIKE ? OR caption LIKE ?"
                            " OR id IN (SELECT asset_id FROM asset_keywords ak JOIN keywords k ON k.id=ak.keyword_id"
                            " WHERE k.name LIKE ? OR k.id IN (SELECT keyword_id FROM keyword_synonyms WHERE synonym LIKE ?)))");
        const QString like = QLatin1Char('%') + text.trimmed() + QLatin1Char('%');
        for (int i = 0; i < 9; ++i) binds << like;
    }
    if (rating > 0) { w << QStringLiteral("rating>=?"); binds << rating; }
    if (flag == QLatin1String("pick")) w << QStringLiteral("flag=1");
    else if (flag == QLatin1String("reject")) w << QStringLiteral("flag=-1");
    else if (flag == QLatin1String("unflagged")) w << QStringLiteral("flag=0");
    if (!label.isEmpty()) { w << QStringLiteral("label=?"); binds << label; }
    const QString type = extra.value(QStringLiteral("type")).toString(), edited = extra.value(QStringLiteral("edited")).toString(),
                  gps = extra.value(QStringLiteral("gps")).toString(), camera = extra.value(QStringLiteral("camera")).toString().trimmed();
    if (type == QLatin1String("raw")) w << QStringLiteral("is_raw=1");
    else if (type == QLatin1String("other")) w << QStringLiteral("is_raw=0");
    if (edited == QLatin1String("yes")) w << QStringLiteral("edited=1");
    else if (edited == QLatin1String("no")) w << QStringLiteral("edited=0");
    if (gps == QLatin1String("yes")) w << QStringLiteral("gps_lat IS NOT NULL");
    else if (gps == QLatin1String("no")) w << QStringLiteral("gps_lat IS NULL");
    if (!camera.isEmpty()) { w << QStringLiteral("trim(make || ' ' || model)=?"); binds << camera; }
    return w.join(QStringLiteral(" AND "));
}

QString Backend::orderFor(const QString &key, bool descending) {
    const QString dir = descending ? QStringLiteral(" DESC") : QString();
    // `variant` last so a virtual copy always sits right after its master.
    if (key == QLatin1String("imported")) return QStringLiteral("imported_at%1, variant_of IS NOT NULL, id%1").arg(dir);
    if (key == QLatin1String("filename")) return QStringLiteral("filename COLLATE NOCASE%1, variant").arg(dir);
    if (key == QLatin1String("rating")) return QStringLiteral("rating%1, captured_at, filename, variant").arg(dir);
    if (key == QLatin1String("edited")) return QStringLiteral("edited_at%1, captured_at, filename, variant").arg(dir);
    return QStringLiteral("captured_at%1, filename%1, variant").arg(dir);
}

QString Backend::sourceWhere(QVariantList &binds) const {
    if (m_sourceKind != QLatin1String("smart"))
        return whereFor(m_sourceKind, m_sourceId, m_filterText, m_filterRating, m_filterFlag, m_filterLabel, m_filterExtra, binds);
    // A smart album *is* its rule; the rule is trusted SQL written by
    // this app (never user text), combined with the normal filters.
    QString rule;
    for (const QVariant &r : m_catalog.albums(true))
        if (r.toMap().value(QStringLiteral("id")).toInt() == m_sourceId)
            rule = r.toMap().value(QStringLiteral("rule")).toString();
    QString where = whereFor(QStringLiteral("all"), 0, m_filterText, m_filterRating, m_filterFlag, m_filterLabel, m_filterExtra, binds);
    // A smart album whose rule has gone matches nothing, never everything.
    const QString own = rule.isEmpty() ? QStringLiteral("0") : QStringLiteral("(%1)").arg(rule);
    return where.isEmpty() ? own : own + QStringLiteral(" AND ") + where;
}

void Backend::refresh() {
    if (!m_catalog.isOpen()) { m_browseWhere.clear(); m_browseBinds.clear(); m_model.setIds({}); return; }
    QVariantList binds;
    QString where = sourceWhere(binds);
    if (m_collapseStacks) {
        // A collapsed stack is its top photo; expanded ones show every member.
        QStringList open;
        for (int s : m_expandedStacks) open << QString::number(s);
        const QString clause = QStringLiteral("(stack_id IS NULL OR stack_pos=0%1)")
            .arg(open.isEmpty() ? QString() : QStringLiteral(" OR stack_id IN (%1)").arg(open.join(QLatin1Char(','))));
        where = where.isEmpty() ? clause : where + QStringLiteral(" AND ") + clause;
    }
    QString order = orderFor(m_sortKey, m_sortDescending);
    if (m_sortKey == QLatin1String("custom") && m_sourceKind == QLatin1String("album") && m_sourceId)
        order = QStringLiteral("(SELECT position FROM album_assets aa WHERE aa.album_id=%1 AND aa.asset_id=assets.id)%2, id").arg(m_sourceId).arg(m_sortDescending ? QStringLiteral(" DESC") : QString());
    QVector<int> ids = m_catalog.assetIds(where, binds, order);
    if (m_aiFocusId && m_catalog.asset(m_aiFocusId).id && !ids.contains(m_aiFocusId))
        ids.prepend(m_aiFocusId);
    m_browseWhere = where; m_browseBinds = binds;
    m_model.setIds(ids);
    // Drop selection entries that left the view; keep current if visible.
    QSet<int> keep;
    for (int id : ids) if (m_selection.contains(id)) keep.insert(id);
    const bool selChanged = keep.size() != m_selection.size() || (m_currentId && !ids.contains(m_currentId));
    m_selection = keep;
    if (m_currentId && !ids.contains(m_currentId)) m_currentId = 0;
    if (!m_currentId && !ids.isEmpty() && m_selection.isEmpty()) {
        m_currentId = ids.first();
        m_selection.insert(m_currentId);
        m_anchorId = m_currentId;
        m_model.selectionChanged({m_currentId});
        emit selectionChanged();
    } else if (selChanged) {
        emit selectionChanged();
    }
    refreshCounts();
    saveBrowsingSession();
}

void Backend::refreshCounts() {
    m_totalCount = m_catalog.isOpen() ? m_catalog.count() : 0;
    emit catalogChanged();
}

void Backend::setSortKey(const QString &k) { if (k == m_sortKey) return; m_sortKey = k; emit filterChanged(); refresh(); }
void Backend::setSortDescending(bool d) { if (d == m_sortDescending) return; m_sortDescending = d; emit filterChanged(); refresh(); }
void Backend::setFilterText(const QString &t) { if (t == m_filterText) return; m_filterText = t; emit filterChanged(); refresh(); }
void Backend::setFilterRating(int r) { r = qBound(0, r, 5); if (r == m_filterRating) return; m_filterRating = r; emit filterChanged(); refresh(); }
void Backend::setFilterFlag(const QString &f) { if (f == m_filterFlag) return; m_filterFlag = f; emit filterChanged(); refresh(); }
void Backend::setFilterLabel(const QString &l) { if (l == m_filterLabel) return; m_filterLabel = l; emit filterChanged(); refresh(); }
void Backend::setFilterExtra(const QVariantMap &e) {
    QVariantMap clean;
    for (const char *k : {"type", "edited", "gps", "camera"}) { const QString v = e.value(QLatin1String(k)).toString().trimmed(); if (!v.isEmpty()) clean[QLatin1String(k)] = v; }
    if (clean == m_filterExtra) return;
    m_filterExtra = clean;
    emit filterChanged();
    refresh();
}

bool Backend::filterActive() const {
    return !m_filterText.isEmpty() || m_filterRating > 0 || !m_filterFlag.isEmpty() || !m_filterLabel.isEmpty() || !m_filterExtra.isEmpty();
}

void Backend::setThumbEdge(int e) {
    e = qBound(128, e, 2048);
    if (e == m_thumbEdge) return;
    m_thumbEdge = e;
    m_model.setThumbEdge(e);
    emit thumbEdgeChanged();
}

// ── sources ─────────────────────────────────────────────────────────────

void Backend::setSource(const QString &kind, int id) {
    m_aiFocusId = 0;
    m_sourceKind = kind;
    // The Quick Collection is an album the app owns; resolve (or make) it.
    m_sourceId = kind == QLatin1String("quick") ? quickCollectionId() : id;
    if (kind == QLatin1String("all")) m_sourceTitle = tr("All Photographs");
    else if (kind == QLatin1String("quick")) m_sourceTitle = tr("Quick Collection");
    else if (kind == QLatin1String("previousImport")) m_sourceTitle = tr("Previous Import");
    else if (kind == QLatin1String("recentEdited")) m_sourceTitle = tr("Recently Edited");
    else if (kind == QLatin1String("recentAdded")) m_sourceTitle = tr("Recently Added");
    else if (kind == QLatin1String("rejected")) m_sourceTitle = tr("Rejected");
    else if (kind == QLatin1String("recentExported")) m_sourceTitle = tr("Recently Exported");
    else if (kind == QLatin1String("folder")) {
        m_sourceTitle = QString();
        for (const QVariant &r : m_catalog.folderTree())
            if (r.toMap().value(QStringLiteral("id")).toInt() == id)
                m_sourceTitle = r.toMap().value(QStringLiteral("name")).toString();
    } else if (kind == QLatin1String("album") || kind == QLatin1String("smart")) {
        for (const QVariant &r : m_catalog.albums(kind == QLatin1String("smart")))
            if (r.toMap().value(QStringLiteral("id")).toInt() == id)
                m_sourceTitle = r.toMap().value(QStringLiteral("name")).toString();
    } else if (kind == QLatin1String("keyword")) {
        m_sourceTitle = m_catalog.keywordPath(id).section(QLatin1Char('/'), -1);
    } else if (kind == QLatin1String("import")) {
        for (const QVariant &r : m_catalog.recentImports(50))
            if (r.toMap().value(QStringLiteral("id")).toInt() == id)
                m_sourceTitle = tr("Import %1").arg(r.toMap().value(QStringLiteral("date")).toString());
    } else m_sourceTitle = kind;
    emit sourceChanged();
    refresh();
}

QVariantList Backend::catalogSections() const {
    auto row = [](const QString &kind, const QString &name, const QString &icon, int count, bool enabled = true) {
        QVariantMap m;
        m[QStringLiteral("kind")] = kind; m[QStringLiteral("name")] = name;
        m[QStringLiteral("icon")] = icon; m[QStringLiteral("count")] = count;
        m[QStringLiteral("enabled")] = enabled;
        return m;
    };
    QVariantList out;
    if (!m_catalog.isOpen()) return out;
    QVariantList b;
    out << row(QStringLiteral("all"), tr("All Photographs"), QStringLiteral("images"), m_catalog.count(QStringLiteral("flag<>-1")));
    int quick = 0;
    for (const QVariant &a : m_catalog.albums(false))
        if (a.toMap().value(QStringLiteral("name")).toString() == QLatin1String("__quick"))
            quick = a.toMap().value(QStringLiteral("count")).toInt();
    out << row(QStringLiteral("quick"), tr("Quick Collection"), QStringLiteral("plus"), quick);
    out << row(QStringLiteral("previousImport"), tr("Previous Import"), QStringLiteral("download"),
               m_catalog.count(Backend::whereFor(QStringLiteral("previousImport"), 0, {}, 0, {}, {}, b)));
    b.clear();
    out << row(QStringLiteral("recentEdited"), tr("Recently Edited"), QStringLiteral("clock"),
               m_catalog.count(QStringLiteral("edited=1 AND flag<>-1")));
    out << row(QStringLiteral("recentAdded"), tr("Recently Added"), QStringLiteral("clock"),
               m_catalog.count(Backend::whereFor(QStringLiteral("recentAdded"), 0, {}, 0, {}, {}, b)));
    b.clear();
    const int exported = m_catalog.count(Backend::whereFor(QStringLiteral("recentExported"), 0, {}, 0, {}, {}, b));
    if (exported) out << row(QStringLiteral("recentExported"), tr("Recently Exported"), QStringLiteral("upload"), exported);
    out << row(QStringLiteral("rejected"), tr("Rejected"), QStringLiteral("x"), m_catalog.count(QStringLiteral("flag=-1")));
    const int offline = m_catalog.count(QStringLiteral("folder_id IN (SELECT id FROM folders WHERE online=0)"));
    if (offline) out << row(QStringLiteral("offline"), tr("Offline"), QStringLiteral("unplug"), offline);
    return out;
}

int Backend::quickCollectionId() {
    for (const QVariant &a : m_catalog.albums(false))
        if (a.toMap().value(QStringLiteral("name")).toString() == QLatin1String("__quick"))
            return a.toMap().value(QStringLiteral("id")).toInt();
    return m_catalog.createAlbum(QStringLiteral("__quick"));
}

int Backend::createAlbum(const QString &name, int parentId) {
    const int id = m_catalog.createAlbum(name.trimmed(), QString(), QString(), parentId);
    if (id) { setStatus(tr("Created album \"%1\"").arg(name.trimmed())); emit catalogChanged(); }
    return id;
}

QVariantList Backend::albums() const {
    QVariantList out;
    for (const QVariant &v : m_catalog.albums(false)) {
        QVariantMap m = v.toMap();
        int cover = m.value(QStringLiteral("cover")).toInt();
        if (!cover) { const QVector<int> first = m_catalog.albumOrder(m.value(QStringLiteral("id")).toInt()); if (!first.isEmpty()) cover = first.first(); }
        const AssetRecord r = cover ? m_catalog.asset(cover) : AssetRecord();
        m[QStringLiteral("thumb")] = r.id ? Thumbnailer::sourceFor(r.path, r.mtime, m_thumbEdge, r.editRev, r.variant) : QString();
        out << m;
    }
    return out;
}

bool Backend::setAlbumCover(int albumId, int assetId) {
    if (!m_catalog.setAlbumCover(albumId, assetId)) return false;
    emit catalogChanged();
    setStatus(assetId ? tr("Album cover set") : tr("Album cover cleared"));
    return true;
}

bool Backend::moveInAlbum(int delta, int id) {
    if (m_sourceKind != QLatin1String("album") || !m_sourceId) return false;
    const int target = id ? id : m_currentId;
    if (!target || !m_catalog.moveInAlbum(m_sourceId, target, delta)) return false;
    if (m_sortKey != QLatin1String("custom")) { m_sortKey = QStringLiteral("custom"); m_sortDescending = false; emit filterChanged(); }
    refresh();
    return true;
}

bool Backend::moveInAlbumTo(int id, int row) {
    if (m_sourceKind != QLatin1String("album") || !m_sourceId || !id) return false;
    // The shown order is the album's when the sort is custom; a filter may
    // hide members, so the target is the album position of the photo shown at `row`.
    if (m_sortKey != QLatin1String("custom")) { m_sortKey = QStringLiteral("custom"); m_sortDescending = false; emit filterChanged(); refresh(); }
    const int at = m_model.idAt(qBound(0, row, qMax(0, m_model.rowCount() - 1)));
    const QVector<int> order = m_catalog.albumOrder(m_sourceId);
    const int index = at ? order.indexOf(at) : order.size() - 1;
    if (index < 0 || !m_catalog.moveInAlbumTo(m_sourceId, id, index)) return false;
    refresh();
    return true;
}

bool Backend::setAlbumParent(int id, int parentId) {
    if (!m_catalog.setAlbumParent(id, parentId)) { setStatus(tr("Could not move the album: %1").arg(m_catalog.lastError())); return false; }
    emit catalogChanged();
    return true;
}

void Backend::markExported(const QString &path, int variant) {
    const int id = m_catalog.idForPath(path, variant);
    if (!id) return;
    m_catalog.markExported(id);
    m_model.invalidate({id});
    refreshCounts();
}

QString Backend::labelName(const QString &colour) const {
    const QString n = m_labelNames.value(colour).toString().trimmed();
    if (!n.isEmpty()) return n;
    return colour.isEmpty() ? QString() : colour.left(1).toUpper() + colour.mid(1);
}

void Backend::setLabelName(const QString &colour, const QString &name) {
    if (colour.isEmpty()) return;
    const QString n = name.trimmed();
    if (n.isEmpty()) m_labelNames.remove(colour); else m_labelNames[colour] = n;
    QSettings().setValue(QStringLiteral("labels/names"), m_labelNames);
    emit labelNamesChanged();
}

void Backend::addSelectionToAlbum(int albumId) {
    if (!albumId) return;
    const QVector<int> ids = targets(0);
    if (!m_catalog.addToAlbum(albumId, ids)) setStatus(tr("Not added to the album: %1").arg(m_catalog.lastError()));
    else setStatus(tr("Added %1 to album").arg(ids.size()));
    emit catalogChanged();
    if (m_sourceKind == QLatin1String("album") && m_sourceId == albumId) refresh();
}

void Backend::addSelectionToQuickCollection() {
    const int id = quickCollectionId();
    const int n = targets(0).size();
    if (!m_catalog.addToAlbum(id, targets(0))) setStatus(tr("Not added to Quick Collection: %1").arg(m_catalog.lastError()));
    else setStatus(tr("Added %1 to Quick Collection").arg(n));
    emit catalogChanged();
    if (m_sourceKind == QLatin1String("quick")) refresh();
}

// ── import ──────────────────────────────────────────────────────────────

QVariantList Backend::outputTemplates() const { return QSettings().value("workflow/output").toList(); }
QVariantList Backend::metadataImportPresets() const { return QSettings().value("workflow/metadata").toList(); }

bool Backend::saveWorkflowPreset(const QString &kind, const QString &name, const QVariantMap &values) {
    const QString label = name.trimmed();
    if ((kind != "output" && kind != "metadata") || label.isEmpty() || label.size() > 80 || values.isEmpty()
        || QJsonDocument(QJsonObject::fromVariantMap(values)).toJson().size() > 65536) {
        setStatus(tr("Use a preset name of 1–80 characters and valid settings")); return false;
    }
    QSettings settings;
    const QString key = "workflow/" + kind;
    auto list = settings.value(key).toList();
    const QVariantMap row{{"name", label}, {"values", values}, {"version", 1}};
    bool replaced = false;
    for (auto &value : list) if (value.toMap().value("name").toString().compare(label, Qt::CaseInsensitive) == 0) { value = row; replaced = true; break; }
    if (!replaced) {
        if (list.size() >= 100) { setStatus(tr("Remove an unused preset before adding another")); return false; }
        list.append(row);
    }
    settings.setValue(key, list); settings.sync();
    if (settings.status() != QSettings::NoError) { setStatus(tr("Cannot save workflow presets")); return false; }
    emit workflowPresetsChanged();
    setStatus(tr("Saved %1").arg(label));
    return true;
}

bool Backend::deleteWorkflowPreset(const QString &kind, const QString &name) {
    if (kind != "output" && kind != "metadata") return false;
    QSettings settings;
    const QString key = "workflow/" + kind;
    auto list = settings.value(key).toList();
    const auto before = list.size();
    list.removeIf([&](const QVariant &value) { return value.toMap().value("name").toString() == name; });
    if (list.size() == before) return false;
    settings.setValue(key, list); settings.sync();
    if (settings.status() != QSettings::NoError) { setStatus(tr("Cannot remove the saved preset")); return false; }
    emit workflowPresetsChanged(); return true;
}

QVariantMap Backend::importOptions() const {
    QSettings s;
    ImportOptions o = ImportOptions::fromMap(s.value(QStringLiteral("import/options")).toMap());
    QVariantMap options = o.toMap();
    options["smartPreviews"] = s.value(QStringLiteral("import/smartPreviews"), false).toBool();
    options["autoTag"] = m_autoTagEnabled;
    return options;
}

QVariantMap Backend::importPreview(const QString &pathOrUrl) const {
    QString path = pathOrUrl;
    if (path.startsWith(QLatin1String("file:"))) path = QUrl(path).toLocalFile();
    QVariantMap out;
    const QFileInfo fi(path);
    out[QStringLiteral("folder")] = fi.absoluteFilePath();
    out[QStringLiteral("name")] = fi.fileName();
    const QStringList files = fi.isDir() ? Importer::scan(fi.absoluteFilePath()) : QStringList();
    qint64 bytes = 0;
    for (const QString &f : files) bytes += QFileInfo(f).size();
    out[QStringLiteral("count")] = files.size();
    out[QStringLiteral("bytes")] = bytes;
    out[QStringLiteral("size")] = Metadata::formatSize(double(bytes));
    return out;
}

void Backend::cancelImportPreview() {
    if (m_importPreviewCancel) m_importPreviewCancel->store(true);
}

int Backend::requestImportPreview(const QString &pathOrUrl, bool recursive) {
    cancelImportPreview();
    const int request = ++m_importPreviewRequest;
    const auto cancel = m_importPreviewCancel = std::make_shared<std::atomic<bool>>(false);
    const QString path = localPath(pathOrUrl), catalog = m_catalog.path();
    struct Result { QVariantMap preview; QString error; };
    const auto result = std::make_shared<Result>();
    auto *worker = QThread::create([path, catalog, recursive, cancel, result] {
        const QFileInfo folder(path);
        if (!folder.isDir() || !folder.isReadable() || !folder.isExecutable()) {
            result->error = tr("This source folder is unavailable. Reconnect the drive or choose another folder."); return;
        }
        QSet<QString> existing;
        const QString connection = QStringLiteral("import-preview-%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY")); db.setDatabaseName(catalog);
            if (!db.open()) result->error = tr("Cannot read the catalog for import review.");
            else {
                QSqlQuery query(db);
                if (!query.exec(QStringLiteral("SELECT path FROM assets"))) result->error = query.lastError().text();
                else while (!cancel->load() && query.next()) existing.insert(query.value(0).toString());
            }
        }
        QSqlDatabase::removeDatabase(connection);
        if (cancel->load() || !result->error.isEmpty()) return;
        const auto paths = Importer::scan(folder.absoluteFilePath(), recursive, cancel.get());
        const QString canonicalFolder = folder.canonicalFilePath();
        QVariantList files;
        qint64 bytes = 0;
        for (const auto &file : paths) {
            if (cancel->load()) return;
            const QFileInfo info(file);
            const qint64 mtime = info.lastModified().toSecsSinceEpoch();
            files << QVariantMap{{"path", file}, {"name", info.fileName()},
                {"relative", QDir(path).relativeFilePath(file)}, {"size", Metadata::formatSize(info.size())},
                {"type", info.suffix().toUpper()}, {"alreadyImported", existing.contains(info.absoluteFilePath())},
                {"unavailableReason", Importer::selectionError(canonicalFolder, file)},
                {"thumb", Thumbnailer::sourceFor(file, mtime, 320) + "&original=1"},
                {"preview", Thumbnailer::sourceFor(file, mtime, 1600) + "&original=1"}};
            bytes += info.size();
        }
        result->preview = {{"folder", folder.absoluteFilePath()}, {"name", folder.fileName()},
            {"count", files.size()}, {"bytes", bytes}, {"size", Metadata::formatSize(bytes)}, {"files", files}};
    });
    connect(worker, &QThread::finished, this, [this, request, cancel, result] {
        if (!cancel->load()) emit importPreviewReady(request, result->preview, result->error);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
    return request;
}

bool Backend::importFolderWith(const QString &pathOrUrl, const QVariantMap &options) {
    if (m_importer.running()) { setStatus(tr("An import is already running")); return false; }
    const ImportOptions o = ImportOptions::fromMap(options);
    if (o.selectedOnly && o.selectedFiles.isEmpty()) { setStatus(tr("Select at least one photo to import")); return false; }
    if (o.mode == QLatin1String("move") && !o.inPlace()) {
        const QString error = moveEngineError();
        if (!error.isEmpty()) { setStatus(error); return false; }
    }
    QSettings().setValue(QStringLiteral("import/options"), o.toMap());
    if (!o.inPlace()) {
        const QFileInfo d(o.destination);
        if (!d.isDir()) { setStatus(tr("Destination is not a folder: %1").arg(o.destination)); return false; }
        if (!d.isWritable()) { setStatus(tr("Destination is not writable: %1").arg(o.destination)); return false; }
    }
    if (!o.backup.isEmpty()) {
        const QFileInfo b(o.backup);
        if (!b.isDir()) { setStatus(tr("Second-copy folder is not a folder: %1").arg(o.backup)); return false; }
        if (!b.isWritable()) { setStatus(tr("Second-copy folder is not writable: %1").arg(o.backup)); return false; }
    }
    if(options.contains("autoTag")) setAutoTagEnabled(o.autoTag);
    m_importOptions = o;
    const bool smart = options.value("smartPreviews", false).toBool();
    QSettings().setValue(QStringLiteral("import/smartPreviews"), smart);
    importFolder(pathOrUrl);
    if (m_importer.running()) m_buildSmartAfterImport = smart;
    return m_importer.running();
}


// A card, a USB stick or drive: the kernel marks the disk removable, or it
// hangs off USB or an SD/MMC slot. A second internal drive is none of these
// and must never be ejected (powered off) after an import.
static bool hotplugBlockDevice(const QByteArray &device) {
    const QString name = QFileInfo(QFileInfo(QString::fromLocal8Bit(device)).canonicalFilePath()).fileName();
    if (name.isEmpty()) return false;
    const QString sys = QFileInfo(QStringLiteral("/sys/class/block/") + name).canonicalFilePath();
    if (!sys.startsWith(QLatin1String("/sys/devices/"))) return false;
    if (sys.contains(QLatin1String("/usb")) || name.startsWith(QLatin1String("mmcblk"))) return true;
    // A partition's own directory has no "removable"; its disk's does.
    for (QString dir = sys; dir.startsWith(QLatin1String("/sys/devices/")); dir = QFileInfo(dir).path()) {
        QFile flag(dir + QStringLiteral("/removable"));
        if (flag.open(QIODevice::ReadOnly)) return flag.readAll().trimmed() == "1";
    }
    return false;
}

bool Backend::isRemovableSource(const QString &pathOrUrl) const {
    const QStorageInfo here(localPath(pathOrUrl));
    if (!here.isValid() || !here.isReady()) return false;
    const QString home = QStorageInfo(QDir::homePath()).rootPath();
    return here.rootPath() != QStorageInfo::root().rootPath() && here.rootPath() != home && !here.device().isEmpty()
           && here.device().startsWith("/dev/") && hotplugBlockDevice(here.device());
}

// The whole disk a block device belongs to, so a card reader lets go of the
// card: /dev/sdb1 → /dev/sdb, /dev/mmcblk0p1 → /dev/mmcblk0, and an
// unpartitioned card (/dev/mmcblk0, /dev/sdb) is its own disk. Asked of
// sysfs, where a partition is a directory inside its disk's.
static QString wholeDiskOf(const QString &dev) {
    const QString name = QFileInfo(dev).fileName();
    const QString sys = QStringLiteral("/sys/class/block/") + name;
    if (!QFileInfo::exists(sys + QStringLiteral("/partition"))) return dev;
    const QString parent = QFileInfo(QFileInfo(sys).canonicalFilePath()).absolutePath();
    const QString disk = QFileInfo(parent).fileName();
    return disk.isEmpty() ? dev : QStringLiteral("/dev/") + disk;
}

// Unmounts and powers off the volume holding `pathOrUrl`. What can be known
// at once (not removable, no udisksctl) comes back as the error; the rest
// runs without holding the window and ends in the status line.
QString Backend::ejectVolumeOf(const QString &pathOrUrl) {
    if (!isRemovableSource(pathOrUrl)) return tr("not a removable volume");
    const QStorageInfo here(localPath(pathOrUrl));
    const QString dev = QString::fromLocal8Bit(here.device());
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("udisksctl"));
    if (tool.isEmpty()) return tr("udisksctl is not installed");
    const QString disk = wholeDiskOf(dev);
    // What the status said before (an import's summary) stays in front.
    const QString before = std::exchange(m_ejectNote, QString());
    const auto say = [this, before](const QString &m) { setStatus(before.isEmpty() ? m : before + QStringLiteral(" — ") + m); };
    auto *unmount = new QProcess(this);
    connect(unmount, &QProcess::finished, this, [say, unmount, tool, dev, disk, this](int code, QProcess::ExitStatus status) {
        unmount->deleteLater();
        if (status != QProcess::NormalExit || code != 0) {
            const QString why = QString::fromLocal8Bit(unmount->readAllStandardError()).trimmed();
            say(tr("Could not eject %1: %2").arg(dev, why.isEmpty() ? tr("unmount failed") : why));
            return;
        }
        auto *off = new QProcess(this);
        connect(off, &QProcess::finished, this, [say, off, dev, disk](int c, QProcess::ExitStatus st) {
            off->deleteLater();
            say(st == QProcess::NormalExit && c == 0 ? tr("Ejected %1").arg(disk) : tr("Unmounted %1; it can be removed").arg(dev));
        });
        connect(off, &QProcess::errorOccurred, this, [say, off, dev](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) { off->deleteLater(); say(tr("Unmounted %1; it can be removed").arg(dev)); }
        });
        off->start(tool, {QStringLiteral("power-off"), QStringLiteral("-b"), disk, QStringLiteral("--no-user-interaction")});
    });
    connect(unmount, &QProcess::errorOccurred, this, [say, unmount, dev](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) { unmount->deleteLater(); say(tr("Could not eject %1: udisksctl did not start").arg(dev)); }
    });
    unmount->start(tool, {QStringLiteral("unmount"), QStringLiteral("-b"), dev, QStringLiteral("--no-user-interaction")});
    say(tr("Ejecting %1…").arg(dev));
    return QString();
}

// What the desktop hands over ("omaraw %f"): a folder imports in place, as
// before; a photo is brought in on its own (never its whole folder) and
// shown in Develop.
void Backend::openFromCommandLine(const QString &pathOrUrl) {
    const QString path = localPath(pathOrUrl);
    const QFileInfo fi(path);
    if (fi.isDir()) { importFolder(path); return; }
    if (!m_catalog.isOpen()) { setStatus(tr("No catalog open")); return; }
    if (!fi.isFile()) { setStatus(tr("%1 does not exist").arg(path)); return; }
    if (!Metadata::imageExtensions().contains(fi.suffix().toLower())) { setStatus(tr("OmaRAW cannot open %1").arg(fi.fileName())); return; }
    int id = m_catalog.idForPath(fi.absoluteFilePath());
    if (!id) {
        const int importId = m_catalog.beginImport(fi.absoluteFilePath());
        Metadata::Sidecar sc;
        id = Importer::importFile(m_catalog, fi.absoluteFilePath(), importId, &sc);
        m_catalog.finishImport(importId, id ? 1 : 0);
        if (!id) { setStatus(tr("Could not add %1 to the catalog: %2").arg(fi.fileName(), m_catalog.lastError())); return; }
        if (QSettings().value(QStringLiteral("develop/autoFirstEdit"), true).toBool()) m_catalog.setAutoPending({id});
        if (m_autoTagEnabled) {
            if(m_catalog.queueAutoTags({id})) { m_autoTagTimer.start(0); emit autoTagsChanged(); }
            else setStatus(tr("Photo added; could not queue subject tags: %1").arg(m_catalog.lastError()));
        }
        if (sc.hasEdit || (sc.hasCameraRaw && !sc.cameraRawOurs))
            applyEditsFromSidecars({{id, fi.absoluteFilePath(), sc.file, sc.hasEdit, Metadata::isRawExtension(fi.suffix().toLower()), sc.packet}});
        m_catalog.rescanFolders();
        emit catalogChanged(); emit foldersChanged();
    }
    setSource(QStringLiteral("all"));
    // An explicit desktop/open-file request takes precedence over a restored
    // filter or collapsed stack that would hide the requested photo.
    if (m_model.rowOf(id) < 0) {
        m_filterText.clear(); m_filterRating = 0; m_filterFlag.clear(); m_filterLabel.clear(); m_filterExtra.clear();
        const int stack = m_catalog.asset(id).stackId;
        if (stack) m_expandedStacks.insert(stack);
        emit filterChanged();
    }
    refresh();
    select(id);
    emit developRequested();
}

void Backend::importFolder(const QString &pathOrUrl) {
    ImportOptions options = m_importOptions;
    options.autoTag = m_autoTagEnabled;
    options.autoFirstEdit = QSettings().value(QStringLiteral("develop/autoFirstEdit"), true).toBool();
    m_importOptions = ImportOptions();   // consume options even if validation fails
    if (!m_catalog.isOpen()) { setStatus(tr("No catalog open")); return; }
    QString path = pathOrUrl;
    if (path.startsWith(QLatin1String("file:"))) path = QUrl(path).toLocalFile();
    const QFileInfo fi(path);
    if (!fi.isDir()) { setStatus(tr("Not a folder: %1").arg(path)); return; }
    if (m_importer.running()) { setStatus(tr("An import is already running")); return; }
    if (options.mode == QLatin1String("move") && !options.inPlace()) {
        const QString error = moveEngineError();
        if (!error.isEmpty()) { setStatus(error); return; }
    }
    m_importProgress = 0;
    m_importStatus = tr("Scanning %1…").arg(fi.fileName());
    setStatus(m_importStatus);
    m_importSource = fi.absoluteFilePath();
    ++m_importSerial;
    m_importMoveErrors.clear();
    m_lastImportError.clear();
    m_buildSmartAfterImport = false;
    m_runningImportOptions = options;   // read again when it finishes
    m_importer.start(m_catalog.path(), fi.absoluteFilePath(), options);
    emit importChanged();
}

// ── selection ───────────────────────────────────────────────────────────

void Backend::selectionSetChanged(const QSet<int> &before) {
    QVector<int> changed;
    for (int id : before) if (!m_selection.contains(id)) changed << id;
    for (int id : m_selection) if (!before.contains(id)) changed << id;
    m_model.selectionChanged(changed);
    emit selectionChanged();
}

void Backend::select(int id, int mode) {
    if (!id) return;
    if (id != m_aiFocusId) m_aiFocusId = 0;
    const QSet<int> before = m_selection;
    const int prevCurrent = m_currentId;
    if (mode == 1) {
        if (m_selection.contains(id)) m_selection.remove(id); else m_selection.insert(id);
        m_currentId = m_selection.contains(id) ? id : (m_selection.isEmpty() ? 0 : m_currentId);
        m_anchorId = id;
    } else if (mode == 2 && m_anchorId) {
        const int a = m_model.rowOf(m_anchorId), b = m_model.rowOf(id);
        if (a >= 0 && b >= 0) {
            m_selection.clear();
            for (int r = qMin(a, b); r <= qMax(a, b); ++r) m_selection.insert(m_model.idAt(r));
        } else m_selection.insert(id);
        m_currentId = id;
    } else {
        m_selection.clear();
        m_selection.insert(id);
        m_currentId = id;
        m_anchorId = id;
    }
    if (prevCurrent != m_currentId) {
        m_model.selectionChanged({prevCurrent, m_currentId});
    }
    selectionSetChanged(before);
}

void Backend::selectAll() {
    const QSet<int> before = m_selection;
    for (int id : m_model.ids()) m_selection.insert(id);
    selectionSetChanged(before);
}

void Backend::clearSelection() {
    const QSet<int> before = m_selection;
    m_selection.clear();
    const int prev = m_currentId;
    m_currentId = 0;
    if (prev) m_model.selectionChanged({prev});
    selectionSetChanged(before);
}

void Backend::step(int delta) {
    if (m_model.rowCount() == 0) return;
    int row = m_model.rowOf(m_currentId);
    row = row < 0 ? 0 : qBound(0, row + delta, m_model.rowCount() - 1);
    select(m_model.idAt(row), 0);
}

QVariantList Backend::selectedIds() const {
    QVariantList out;
    for (int id : m_model.ids()) if (m_selection.contains(id)) out << id;
    return out;
}

int Backend::comparePartner() const {
    if (!m_currentId) return 0;
    for (int id : m_model.ids()) if (id != m_currentId && m_selection.contains(id)) return id;
    const int row = m_model.rowOf(m_currentId);
    if (row >= 0 && row + 1 < m_model.rowCount()) return m_model.idAt(row + 1);
    if (row > 0) return m_model.idAt(row - 1);
    return 0;
}

void Backend::setCurrent(int id) {
    if (!id || id == m_currentId) return;
    const int prev = m_currentId;
    m_currentId = id;
    m_selection.insert(id);
    m_model.selectionChanged({prev, id});
    emit selectionChanged();
}

QStringList Backend::selectedPaths() const {
    QStringList out;
    for (int id : m_model.ids()) if (m_selection.contains(id)) out << m_catalog.asset(id).path;
    if (out.isEmpty() && m_currentId) out << m_catalog.asset(m_currentId).path;
    return out;
}

QStringList Backend::shownPaths() const {
    QStringList out;
    for (int id : m_model.ids()) out << m_catalog.asset(id).path;
    return out;
}

QVector<int> Backend::targets(int id) const {
    if (id) return {id};
    QVector<int> v;
    for (int s : m_selection) v << s;
    if (v.isEmpty() && m_currentId) v << m_currentId;
    return v;
}

// ── culling ─────────────────────────────────────────────────────────────

void Backend::setRating(int rating, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty()) return;
    if (!m_catalog.setRating(t, rating)) { setStatus(tr("The rating was not saved: %1").arg(m_catalog.lastError())); return; }
    syncSidecars(t);
    m_model.invalidate(t);
    emit selectionChanged();
    setStatus(rating ? tr("%1 star%2").arg(rating).arg(rating == 1 ? "" : "s") : tr("Unrated"));
    advanceAfterCull(id);
}

void Backend::setFlag(int flag, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty()) return;
    if (!m_catalog.setFlag(t, flag)) { setStatus(tr("The flag was not saved: %1").arg(m_catalog.lastError())); return; }
    m_model.invalidate(t);
    emit selectionChanged();
    setStatus(flag > 0 ? tr("Picked") : flag < 0 ? tr("Rejected") : tr("Unflagged"));
    if (flag < 0 && m_sourceKind != QLatin1String("rejected")) refresh();   // the reject leaves the view; the next photo takes its row
    else advanceAfterCull(id);
    emit catalogChanged();
}

void Backend::toggleFlag(int flag, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty()) return;
    const AssetRecord r = m_catalog.asset(t.first());
    setFlag(r.flag == flag ? 0 : flag, id);
}

void Backend::setLabel(const QString &label, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty()) return;
    if (!m_catalog.setLabel(t, label)) { setStatus(tr("The label was not saved: %1").arg(m_catalog.lastError())); return; }
    syncSidecars(t);
    m_model.invalidate(t);
    emit selectionChanged();
    if (!m_filterLabel.isEmpty()) refresh();
    else advanceAfterCull(id);
}

void Backend::toggleLabel(const QString &label, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty()) return;
    const AssetRecord r = m_catalog.asset(t.first());
    setLabel(r.label == label ? QString() : label, id);
}

void Backend::noteRecentKeyword(const QString &keyword) {
    const QString k = Catalog::normalizeKeyword(keyword);
    if (k.isEmpty()) return;
    m_recentKeywords.removeAll(k);
    m_recentKeywords.prepend(k);
    while (m_recentKeywords.size() > 12) m_recentKeywords.removeLast();
    QSettings().setValue(QStringLiteral("library/recentKeywords"), m_recentKeywords);
    emit recentKeywordsChanged();
}

void Backend::addKeyword(const QString &keyword) {
    const QVector<int> t = targets(0);
    if (t.isEmpty() || Catalog::normalizeKeyword(keyword).isEmpty()) return;
    if (!m_catalog.addKeyword(t, keyword)) { setStatus(tr("The keyword was not saved: %1").arg(m_catalog.lastError())); return; }
    noteRecentKeyword(keyword);
    syncSidecars(t);
    emit selectionChanged();
    emit catalogChanged();   // the keyword tree in the sidebar follows
}

bool Backend::setKeywordSynonyms(int id, const QString &commaList) {
    if (!m_catalog.setKeywordSynonyms(id, commaList.split(QLatin1Char(','), Qt::SkipEmptyParts))) return false;
    emit catalogChanged();
    setStatus(tr("Synonyms saved for %1").arg(m_catalog.keywordPath(id).section(QLatin1Char('/'), -1)));
    return true;
}

bool Backend::deleteKeyword(int id) {
    // The shown source goes with it when it is this keyword or one under it.
    bool shown = false;
    if (m_sourceKind == QLatin1String("keyword")) {
        const QString gone = m_catalog.keywordPath(id), cur = m_catalog.keywordPath(m_sourceId);
        shown = m_sourceId == id || cur.startsWith(gone + QLatin1Char('/'));
    }
    if (!m_catalog.deleteKeyword(id)) return false;
    if (shown) setSource(QStringLiteral("all"));
    emit selectionChanged();
    emit catalogChanged();
    return true;
}

void Backend::removeKeyword(const QString &keyword) {
    const QVector<int> t = targets(0);
    if (t.isEmpty()) return;
    if (!m_catalog.removeKeyword(t, keyword)) { setStatus(tr("The keyword was not removed: %1").arg(m_catalog.lastError())); return; }
    syncSidecars(t);
    emit selectionChanged();
    emit catalogChanged();
}

QStringList Backend::currentKeywords() const {
    return m_currentId ? m_catalog.keywordsFor(m_currentId) : QStringList();
}

void Backend::removeSelectionFromCatalog() {
    const QVector<int> t = targets(0);
    if (t.isEmpty()) return;
    if (!m_catalog.removeAssets(t)) { setStatus(tr("Nothing was removed from the catalog: %1").arg(m_catalog.lastError())); return; }
    m_selection.clear();
    m_currentId = 0;
    setStatus(tr("Removed %1 from the catalog (files untouched)").arg(t.size()));
    refresh();
    emit selectionChanged();
}

int Backend::moveSelectionToTrash() {
    // Masters only: variants share the file and go with it.
    QSet<QString> paths; QVector<int> ids;
    for (int id : targets(0)) { const AssetRecord r = m_catalog.asset(id); if (r.id && !paths.contains(r.path)) { paths.insert(r.path); ids << id; } }
    int moved = 0; QString firstError;
    QVector<int> gone;
    for (int id : ids) {
        const AssetRecord r = m_catalog.asset(id);
        if (QFile::exists(r.path) && !QFile::moveToTrash(r.path)) { if (firstError.isEmpty()) firstError = tr("could not move %1 to the trash").arg(r.filename); continue; }
        for (const QString &sc : Metadata::sidecarNames(r.path)) if (QFile::exists(sc)) QFile::moveToTrash(sc);
        ++moved;
        for (int vid : m_catalog.assetIdsForPath(r.path)) { gone << vid; if (m_engine) m_engine->removeImage(r.path, m_catalog.asset(vid).variant); }
    }
    // The files are in the trash already; should the catalog refuse (busy
    // with an import), try once more before saying so plainly.
    if (!gone.isEmpty() && !m_catalog.removeAssets(gone)) {
        QThread::msleep(500);
        if (!m_catalog.removeAssets(gone) && firstError.isEmpty())
            firstError = tr("the catalog could not be updated (%1); the trashed photos show as offline until removed from the catalog").arg(m_catalog.lastError());
    }
    m_selection.clear();
    m_currentId = 0;
    refresh();
    emit catalogChanged();
    emit selectionChanged();
    setStatus(firstError.isEmpty() ? tr("Moved %1 photo%2 to the trash").arg(moved).arg(moved == 1 ? "" : "s")
                                   : tr("Moved %1 to the trash; %2").arg(moved).arg(firstError));
    return moved;
}

void Backend::reviewSidecar(int id) {
    const AssetRecord r = m_catalog.asset(id);
    if (!r.id) return;
    const QVariantMap review = sidecarReview(id);
    if (!review.value(QStringLiteral("present")).toBool()) return;
    const qint64 mtime = sidecarMtime(r.path);
    // Touched outside but saying nothing new: that version is the one we know.
    if (mtime != r.sidecarSeen && review.value(QStringLiteral("differs")).toStringList().isEmpty()) { m_catalog.setSidecarSeen({id}, mtime); m_model.invalidate({id}); }
}

// ── rename and move on disk ─────────────────────────────────────────────

QString Backend::moveEngineError() const {
    if (!QFileInfo::exists(CatalogBackup::engineLibrary(m_catalog.path())) || (m_engine && m_engine->ready())) return {};
    return tr("The Develop engine is unavailable or still starting. Wait for it to be ready before moving photos with existing Develop data.");
}

QString Backend::moveFile(int id, const QString &newPath) {
    const AssetRecord r = m_catalog.asset(id);
    if (!r.id) return tr("no such photo");
    const QString from = r.path, to = QDir::cleanPath(newPath);
    if (from == to) return QString();
    const QString engineError = moveEngineError();
    if (!engineError.isEmpty()) return engineError;
    if (!QFile::exists(from)) return tr("%1 is offline").arg(r.filename);
    if (QFile::exists(to)) return tr("%1 already exists").arg(to);
    if (!QDir(QFileInfo(to).absolutePath()).exists()) return tr("%1 does not exist").arg(QFileInfo(to).absolutePath());
    if (m_catalog.assetExists(to)) return tr("%1 is already in the catalog").arg(to);
    const QStringList sourceSidecars = Metadata::sidecarNames(from);
    for (const QString &path : Metadata::sidecarNames(to)) {
        const QFileInfo existing(path);
        if ((existing.exists() || existing.isSymLink()) && !sourceSidecars.contains(path))
            return tr("sidecar %1 already exists").arg(path);
    }
    const auto sidecars = Metadata::sidecarsFollowing(from, to);
    const int op = m_catalog.journalMove(from, to);
    if (!op) return tr("could not journal the move: %1").arg(m_catalog.lastError());
    if (!QFile::rename(from, to)) { m_catalog.journalDone(op); return tr("could not rename %1").arg(r.filename); }
    QVector<QPair<QString, QString>> copiedSidecars;
    for (const auto &sc : sidecars) {
        if (sc.first == sc.second) continue;
        // Keep source metadata until both the photo and its catalog row have
        // moved, so rollback never depends on reverse sidecar renames.
        if (QFile::copy(sc.first, sc.second)) { copiedSidecars.append(sc); continue; }
        const bool restored = QFile::rename(to, from);
        if (restored) {
            for (const auto &copy : copiedSidecars) QFile::remove(copy.second);
            m_catalog.journalDone(op);
        }
        return restored ? tr("could not copy sidecar %1; the original photo and sidecars were left in place").arg(sc.first)
            : tr("could not copy sidecar %1; the photo is at %2 and the original sidecars remain at %3").arg(sc.first, to, QFileInfo(from).absolutePath());
    }
    if (!m_catalog.movePath(from, to)) {
        // Put the file back rather than leave the catalog pointing nowhere.
        const bool restored = QFile::rename(to, from);
        if (restored) for (const auto &sc : copiedSidecars) QFile::remove(sc.second);
        // Only close the journal once the disk and the catalog agree again.
        // If the file could not come back it is at `to` while the catalog
        // still says `from`, which is exactly the disagreement replayMoves()
        // settles at the next open — so the row has to stay pending.
        if (restored) m_catalog.journalDone(op);
        return restored ? tr("could not update the catalog: %1").arg(m_catalog.lastError())
                        : tr("could not update the catalog, and %1 could not be put back; it is now at %2 and will be settled when the catalog is next opened").arg(r.filename, to);
    }
    m_catalog.journalDone(op);
    for (const auto &sc : copiedSidecars) QFile::remove(sc.first);
    followMovedFile(from, to);
    return QString();
}

void Backend::followMovedFile(const QString &from, const QString &to) {
    // Thumbnails are keyed by path: carry every variant's over so edits keep their previews.
    const QString cache = Thumbnailer::defaultCacheDir();
    for (int vid : m_catalog.assetIdsForPath(to)) {
        const AssetRecord v = m_catalog.asset(vid);
        // The grid's size and the full edited render; every other size is made again from the latter.
        for (const int edge : {m_thumbEdge, 0}) {
            const QString oldKey = Thumbnailer::existingPath(cache, from, v.mtime, edge, v.variant);
            const QString newKey = Thumbnailer::filePath(cache, to, v.mtime, edge, v.variant);
            QDir().mkpath(QFileInfo(newKey).absolutePath());
            if (QFile::exists(oldKey)) { QFile::remove(newKey); QFile::rename(oldKey, newKey); }
        }
    }
    const QString error = moveEngineError();
    if (!error.isEmpty() || !m_engine) finishMovedFile(from, to, error);
    else m_engine->moveImage(from, to);
}

void Backend::finishMovedFile(const QString &from, const QString &to, const QString &error) {
    const auto pending = m_importMoves.find({from, to});
    const bool thisImport = pending != m_importMoves.end() && pending.value() == m_importSerial;
    if (pending != m_importMoves.end()) m_importMoves.erase(pending);
    if (error.isEmpty()) return;
    // Engine history is keyed by image ID and retained on a failed move;
    // some variants may already have followed. Do not pretend that an
    // asynchronous filesystem rollback could atomically repair this.
    const QString message = tr("The photo is at %1, but Develop data could not be fully relinked from %2: %3. "
                               "Edits remain in the engine database; resolve the storage error before editing this photo.").arg(to, from, error);
    if (thisImport) {
        m_importMoveErrors << message;
        if (!m_lastImportError.isEmpty()) m_lastImportError += QLatin1Char('\n');
        m_lastImportError += message;
    }
    setStatus(message);
}

QString Backend::renamePhoto(int id, const QString &newName) {
    const AssetRecord r = m_catalog.asset(id ? id : m_currentId);
    if (!r.id) return tr("no such photo");
    QString name = newName.trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char('/'))) return tr("not a file name");
    if (QFileInfo(name).suffix().isEmpty() && !QFileInfo(r.filename).suffix().isEmpty()) name += QLatin1Char('.') + QFileInfo(r.filename).suffix();
    const QString err = moveFile(r.id, QFileInfo(r.path).absolutePath() + QLatin1Char('/') + name);
    if (!err.isEmpty()) { setStatus(tr("Could not rename: %1").arg(err)); return err; }
    m_model.invalidateAll();
    refresh();
    emit catalogChanged(); emit foldersChanged(); emit selectionChanged();
    setStatus(tr("Renamed %1 → %2").arg(r.filename, name));
    return QString();
}

QString Backend::moveSelectionTo(const QString &folderOrUrl) {
    QString folder = folderOrUrl;
    if (folder.startsWith(QLatin1String("file:"))) folder = QUrl(folder).toLocalFile();
    folder = QDir::cleanPath(folder);
    if (!QDir(folder).exists()) { setStatus(tr("%1 does not exist").arg(folder)); return tr("%1 does not exist").arg(folder); }
    // Masters only: a variant shares its master's file and follows it.
    QSet<QString> paths;
    QVector<int> ids;
    for (int id : targets(0)) { const AssetRecord r = m_catalog.asset(id); if (r.id && !paths.contains(r.path)) { paths.insert(r.path); ids << m_catalog.idForPath(r.path, 0); } }
    int moved = 0; QString first;
    for (int id : ids) {
        if (!id) continue;
        const AssetRecord r = m_catalog.asset(id);
        const QString err = moveFile(id, folder + QLatin1Char('/') + r.filename);
        if (err.isEmpty()) ++moved; else if (first.isEmpty()) first = err;
    }
    m_catalog.rescanFolders();
    m_model.invalidateAll();
    refresh();
    emit catalogChanged(); emit foldersChanged(); emit selectionChanged();
    setStatus(first.isEmpty() ? tr("Moved %1 photos to %2").arg(moved).arg(folder) : tr("Moved %1 photos to %2; %3").arg(moved).arg(folder, first));
    return first;
}

// ── inspector ───────────────────────────────────────────────────────────

QVariantMap Backend::info(int id) const {
    QVariantMap m;
    if (!id) return m;
    const AssetRecord r = m_catalog.asset(id);
    if (!r.id) return m;
    m[QStringLiteral("id")] = r.id;
    m[QStringLiteral("filename")] = r.filename;
    m[QStringLiteral("path")] = r.path;
    m[QStringLiteral("folder")] = QFileInfo(r.path).absolutePath();
    m[QStringLiteral("format")] = r.format;
    m[QStringLiteral("isRaw")] = r.isRaw;
    m[QStringLiteral("size")] = Metadata::formatSize(r.size);
    m[QStringLiteral("captured")] = Metadata::formatCaptured(r.capturedAt, true);
    m[QStringLiteral("capturedIso")] = r.capturedAt;
    m[QStringLiteral("dimensions")] = r.width ? QStringLiteral("%1 × %2").arg(r.width).arg(r.height) : QString();
    m[QStringLiteral("megapixels")] = r.width ? QStringLiteral("%1 MP").arg(r.width * qint64(r.height) / 1e6, 0, 'f', 1) : QString();
    m[QStringLiteral("bitDepth")] = r.bitDepth ? QStringLiteral("%1 bit").arg(r.bitDepth) : QString();
    m[QStringLiteral("iso")] = Metadata::formatIso(r.iso);
    m[QStringLiteral("exposure")] = Metadata::formatExposure(r);
    m[QStringLiteral("shutter")] = Metadata::formatShutter(r.shutter);
    m[QStringLiteral("aperture")] = Metadata::formatAperture(r.aperture);
    m[QStringLiteral("focal")] = Metadata::formatFocal(r.focalLength);
    m[QStringLiteral("make")] = r.make;
    m[QStringLiteral("model")] = r.model;
    m[QStringLiteral("camera")] = (r.make + QLatin1Char(' ') + r.model).trimmed();
    m[QStringLiteral("lens")] = r.lens;
    m[QStringLiteral("rating")] = r.rating;
    m[QStringLiteral("flag")] = r.flag;
    m[QStringLiteral("label")] = r.label;
    m[QStringLiteral("hasGps")] = r.hasGps;
    m[QStringLiteral("gpsLatitude")] = r.gpsLat;
    m[QStringLiteral("gpsLongitude")] = r.gpsLon;
    m[QStringLiteral("latitude")] = r.hasGps ? QStringLiteral("%1° %2").arg(qAbs(r.gpsLat), 0, 'f', 4).arg(r.gpsLat >= 0 ? "N" : "S") : QString();
    m[QStringLiteral("longitude")] = r.hasGps ? QStringLiteral("%1° %2").arg(qAbs(r.gpsLon), 0, 'f', 4).arg(r.gpsLon >= 0 ? "E" : "W") : QString();
    m[QStringLiteral("copyright")] = r.copyright;
    m[QStringLiteral("creator")] = r.creator;
    m[QStringLiteral("title")] = r.title;
    m[QStringLiteral("caption")] = r.caption;
    m[QStringLiteral("offline")] = !QFile::exists(r.path);
    const QVariantMap workingCopy = m_engine ? m_engine->offlineInfo(r.path) : QVariantMap();
    m[QStringLiteral("offlineCopy")] = workingCopy.value("available", false);
    m[QStringLiteral("offlineCopyStored")] = workingCopy.value("stored", false);
    m[QStringLiteral("offlineCopyBytes")] = workingCopy.value("bytes");
    const QVariantMap smart = m_engine ? m_engine->smartPreviewInfo(r.path) : QVariantMap();
    m[QStringLiteral("smartPreview")] = smart.value("available", false);
    m[QStringLiteral("smartPreviewStored")] = smart.value("stored", false);
    m[QStringLiteral("smartPreviewBytes")] = smart.value("bytes");
    m[QStringLiteral("smartPreviewWidth")] = smart.value("width");
    m[QStringLiteral("smartPreviewHeight")] = smart.value("height");
    {
        const QVariantMap review = sidecarReview(id);
        const bool changed = review.value(QStringLiteral("changed")).toBool();
        m[QStringLiteral("sidecar")] = !review.value(QStringLiteral("present")).toBool() ? tr("No sidecar") : changed ? tr("Changed outside OmaRAW") : tr("XMP sidecar present");
        m[QStringLiteral("sidecarChanged")] = changed;
        m[QStringLiteral("sidecarReview")] = review;
    }
    m[QStringLiteral("keywords")] = m_catalog.keywordsFor(id);
    m[QStringLiteral("autoTags")] = m_catalog.autoTags(id);
    m[QStringLiteral("autoTagScan")] = m_catalog.autoTagScanStatus(id);
    m[QStringLiteral("edited")] = r.edited;
    m[QStringLiteral("thumb")] = Thumbnailer::sourceFor(r.path, r.mtime, 1024, r.editRev, r.variant);
    m[QStringLiteral("variant")] = r.variant;
    m[QStringLiteral("variantOf")] = r.variantOf;
    m[QStringLiteral("variantName")] = r.variantName;
    m[QStringLiteral("variantLabel")] = variantLabel(r);
    m[QStringLiteral("variantCount")] = m_catalog.variantsOf(r.variantOf ? r.variantOf : r.id).size();
    m[QStringLiteral("stackId")] = r.stackId;
    m[QStringLiteral("stackPos")] = r.stackPos;
    m[QStringLiteral("stackCount")] = r.stackId ? r.stackCount : 0;
    return m;
}

QString Backend::thumbSource(int id, int edge) const {
    const AssetRecord r = m_catalog.asset(id);
    return r.id ? Thumbnailer::sourceFor(r.path, r.mtime, edge, r.editRev, r.variant) : QString();
}

QVariantList Backend::histogramOf(const QImage &src) {
    QVariantList out;
    if (src.isNull()) return out;
    const QImage img = src.convertToFormat(QImage::Format_RGB32);
    QVector<int> r(64, 0), g(64, 0), b(64, 0);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            ++r[qRed(line[x]) >> 2]; ++g[qGreen(line[x]) >> 2]; ++b[qBlue(line[x]) >> 2];
        }
    }
    QVariantList rl, gl, bl;
    for (int i = 0; i < 64; ++i) { rl << r[i]; gl << g[i]; bl << b[i]; }
    out << QVariant(rl) << QVariant(gl) << QVariant(bl);
    return out;
}

QVariantList Backend::histogram(int id) const {
    const AssetRecord r = m_catalog.asset(id);
    if (!r.id) return QVariantList();
    // The 512 px grid thumbnail is what the browser already built; use it
    // when it is on disk, otherwise a small fresh preview.
    QImage img = Thumbnailer::cached(Thumbnailer::defaultCacheDir(), r.path, r.mtime, m_thumbEdge, r.variant);
    if (img.isNull()) img = Metadata::preview(r.path, 256);
    return histogramOf(img);
}

void Backend::revealInFileManager(int id) const {
    const AssetRecord r = m_catalog.asset(id);
    if (r.id) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(r.path).absolutePath()));
}

QString Backend::mapUrl(double lat, double lon) {
    if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) return {};
    return QStringLiteral("https://www.openstreetmap.org/?mlat=%1&mlon=%2#map=16/%1/%2")
        .arg(QString::number(lat, 'f', 6), QString::number(lon, 'f', 6));
}

void Backend::openInMap(int id) const {
    const AssetRecord r = m_catalog.asset(id ? id : m_currentId);
    const QString url = r.id && r.hasGps ? mapUrl(r.gpsLat, r.gpsLon) : QString();
    if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
}

void Backend::applyEditedThumb(const QString &path, int variant, const QImage &render, int flags) {
    const int id = m_catalog.idForPath(path, variant);
    if (!id || render.isNull()) return;
    const AssetRecord r = m_catalog.asset(id);
    Thumbnailer::store(Thumbnailer::defaultCacheDir(), r.path, r.mtime, m_thumbEdge, render, r.variant);
    m_catalog.markEdited(id, flags);
    m_model.invalidate({id});
    emit previewChanged(id);
    if (id == m_currentId) emit selectionChanged();
}

static QVariantMap pv(const char *op, const char *field, double value) {
    QVariantMap m;
    m[QStringLiteral("op")] = QString::fromLatin1(op);
    m[QStringLiteral("field")] = QString::fromLatin1(field);
    m[QStringLiteral("value")] = value;
    return m;
}

QVariantList Backend::builtinPresets() {
    // Honest looks on modules the engine ships; values are in module units.
    auto row = [](int id, const QString &name, const QVariantList &values) {
        QVariantMap m;
        m[QStringLiteral("id")] = id; m[QStringLiteral("name")] = name;
        m[QStringLiteral("builtin")] = true; m[QStringLiteral("values")] = values;
        m[QStringLiteral("category")] = tr("Built-in"); m[QStringLiteral("tags")] = QStringList();
        return m;
    };
    QVariantList out = CuratedPresets::presets();
    out << row(-1, tr("Neutral"), {pv("exposure", "exposure", 0.7), pv("exposure", "black", 0.0),
                                    pv("sigmoid", "middle_grey_contrast", 1.5), pv("sigmoid", "contrast_skewness", 0.0),
                                    pv("colorbalancergb", "saturation_global", 0.0), pv("colorbalancergb", "chroma_global", 0.0), pv("colorbalancergb", "vibrance", 0.0),
                                    pv("colorbalancergb", "contrast", 0.0), pv("grain", "strength", 0.0), pv("vignette", "brightness", 0.0),
                                    QVariantMap{{"op", "monochrome"}, {"enabled", false}}});
    out << row(-2, tr("Punchy"), {pv("sigmoid", "middle_grey_contrast", 2.0), pv("sigmoid", "contrast_skewness", 0.1),
                                   pv("colorbalancergb", "saturation_global", 0.15), pv("colorbalancergb", "vibrance", 0.2),
                                   pv("colorbalancergb", "contrast", 0.1)});
    out << row(-3, tr("Matte"), {pv("exposure", "black", -0.01), pv("sigmoid", "middle_grey_contrast", 1.2),
                                  pv("sigmoid", "contrast_skewness", -0.25), pv("colorbalancergb", "saturation_global", -0.1)});
    QVariantList mono{pv("colorbalancergb", "saturation_global", 0.0), pv("colorbalancergb", "chroma_global", 0.0), pv("colorbalancergb", "vibrance", 0.0),
                      pv("sigmoid", "middle_grey_contrast", 1.7), pv("monochrome", "size", 2.0), pv("monochrome", "highlights", 0.0)};
    for (const char *band : {"mix_red", "mix_orange", "mix_yellow", "mix_green", "mix_aqua", "mix_blue", "mix_purple", "mix_magenta"})
        mono << pv("monochrome", band, 0.0);
    // Primary grading follows monochrome. Keep its tonal settings but clear
    // any earlier split-tone colours when choosing a neutral B&W recipe.
    for (const char *field : {"lift_C", "gamma_C", "gain_C", "offset_C"})
        mono << pv("omarawgrade", field, 0.0);
    mono << QVariantMap{{"op", "monochrome"}, {"enabled", true}};
    out << row(-4, tr("Monochrome"), mono);
    out << row(-5, tr("Warm"), {pv("channelmixerrgb", "temperature", 4300.0)});
    out << row(-6, tr("Cool"), {pv("channelmixerrgb", "temperature", 6800.0)});
    out << row(-7, tr("Film grain"), {pv("grain", "strength", 35.0), pv("sigmoid", "contrast_skewness", -0.1)});
    // Split tone uses the same editable Lift/Gain wheels as Primary correction.
    out << row(-8, tr("Split tone"), {pv("omarawgrade", "lift_H", 220.0), pv("omarawgrade", "lift_C", 0.04), pv("omarawgrade", "lift_Y", 0),
                                       pv("omarawgrade", "gamma_H", 0), pv("omarawgrade", "gamma_C", 0), pv("omarawgrade", "gamma_Y", 1),
                                       pv("omarawgrade", "gain_H", 40.0), pv("omarawgrade", "gain_C", 0.06), pv("omarawgrade", "gain_Y", 1),
                                       pv("omarawgrade", "offset_H", 0), pv("omarawgrade", "offset_C", 0), pv("omarawgrade", "offset_Y", 0),
                                       pv("omarawgrade", "lum_mix", 100), pv("colorbalancergb", "saturation_global", -0.05)});
    return out;
}

QVariantList Backend::presets() const {
    QVariantList out = builtinPresets();
    for (const QVariant &v : m_catalog.presets()) {
        QVariantMap m = v.toMap();
        const QJsonDocument doc = QJsonDocument::fromJson(m.value(QStringLiteral("json")).toString().toUtf8());
        m[QStringLiteral("values")] = doc.array().toVariantList();
        m[QStringLiteral("builtin")] = false;
        m.remove(QStringLiteral("json"));
        out << m;
    }
    for (QVariant &v : out) { QVariantMap m = v.toMap(); m[QStringLiteral("favourite")] = m_favouritePresets.contains(m.value(QStringLiteral("name")).toString()); v = m; }
    return out;
}

void Backend::toggleFavouritePreset(const QString &name) {
    if (name.isEmpty()) return;
    if (m_favouritePresets.contains(name)) m_favouritePresets.removeAll(name); else m_favouritePresets << name;
    QSettings().setValue(QStringLiteral("develop/favouritePresets"), m_favouritePresets);
    emit presetsChanged();
}

void Backend::notePresetApplied(const QString &name) {
    if (name.isEmpty()) return;
    m_recentPresets.removeAll(name);
    m_recentPresets.prepend(name);
    while (m_recentPresets.size() > 5) m_recentPresets.removeLast();
    QSettings().setValue(QStringLiteral("develop/recentPresets"), m_recentPresets);
    emit presetsChanged();
}

static QStringList normalisedPresetTags(const QStringList &input) {
    QStringList tags; QSet<QString> seen;
    for (const QString &value : input) {
        const QString tag = value.simplified();
        if (tag.isEmpty() || seen.contains(tag.toCaseFolded())) continue;
        seen.insert(tag.toCaseFolded()); tags << tag;
    }
    return tags;
}

int Backend::exportPresets(const QString &fileOrUrl) {
    QString path = localPath(fileOrUrl);
    if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) path += QStringLiteral(".json");
    QJsonArray list;
    for (const QVariant &v : m_catalog.presets()) {
        const QVariantMap m = v.toMap();
        list << QJsonObject{{"name", m.value("name").toString()},
                           {"values", QJsonDocument::fromJson(m.value("json").toString().toUtf8()).array()},
                           {"category", m.value("category").toString()},
                           {"tags", QJsonArray::fromStringList(m.value("tags").toStringList())}};
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"omarawPresets", 2}, {"presets", list}}).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setStatus(tr("Could not write %1: %2").arg(path, file.errorString())); return -1;
    }
    setStatus(tr("Wrote %1 preset%2 to %3").arg(list.size()).arg(list.size() == 1 ? "" : "s").arg(path));
    return list.size();
}

QString Backend::cameraProfilesFolder() const { return CameraProfiles::folder(); }
void Backend::setCameraProfilesFolder(const QString &folderOrUrl) {
    const QString folder = folderOrUrl.isEmpty() ? QString() : localFile(folderOrUrl);
    CameraProfiles::setFolder(folder);
    if (m_engine) m_engine->refreshCameraProfiles();
    if (folder.isEmpty()) { setStatus(tr("Camera profiles folder cleared")); return; }
    // Said at once, so a folder picker that shows no files (they are not
    // folders) is not taken for an empty choice.
    int cameras = 0;
    const int profiles = CameraProfiles::profileCount(&cameras);
    setStatus(profiles == 0 ? tr("No camera profiles (.dcp files) in %1 or its folders").arg(QDir::toNativeSeparators(folder))
                            : tr("%1 camera profiles for %2 cameras available. Profiles folder: %3").arg(profiles).arg(cameras).arg(QDir::toNativeSeparators(folder)));
}

int Backend::importPresets(const QString &fileOrUrl) {
    const QString path = localPath(fileOrUrl);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("xmp") || suffix == QLatin1String("lrtemplate"))
        return importPresetFiles({fileOrUrl});
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { setStatus(tr("Could not read %1").arg(path)); return -1; }
    if (file.size() > 64 * 1024 * 1024) { setStatus(tr("Preset file is too large.")); return -1; }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonObject root = doc.object();
    // Early unversioned files used the version-1 row structure.
    const QJsonValue version = root.value("omarawPresets");
    const int format = version.isUndefined() ? 1 : version.toInt(-1);
    if (error.error != QJsonParseError::NoError || !doc.isObject() || !root.value("presets").isArray()) {
        setStatus(tr("%1 is not an OmaRAW preset file").arg(QFileInfo(path).fileName())); return -1;
    }
    if ((format != 1 && format != 2) || (!version.isUndefined() && version.toDouble(-1) != format)) {
        setStatus(tr("This preset file needs an unsupported format version.")); return -1;
    }
    QSet<QString> have;
    for (const QVariant &v : presets()) have.insert(v.toMap().value("name").toString().toCaseFolded());
    QVariantList pending; int skipped = 0;
    // Validate the whole document before making any catalogue changes.
    for (const QJsonValue &value : root.value("presets").toArray()) {
        const QJsonObject row = value.toObject();
        const QString name = row.value("name").toString().trimmed();
        const QJsonArray values = row.value("values").toArray();
        bool valid = value.isObject() && !name.isEmpty() && !values.isEmpty();
        for (const QJsonValue &setting : values) valid &= setting.isObject();
        if (format == 2) {
            valid &= row.value("category").isUndefined() || row.value("category").isString();
            valid &= row.value("tags").isUndefined() || row.value("tags").isArray();
            for (const QJsonValue &tag : row.value("tags").toArray()) valid &= tag.isString();
        }
        if (!valid) { setStatus(tr("The preset file contains an invalid preset; nothing was imported.")); return -1; }
        if (have.contains(name.toCaseFolded())) { ++skipped; continue; }
        pending << QVariantMap{{"name", name}, {"values", values.toVariantList()},
                               {"category", format == 2 ? row.value("category").toString().simplified() : QString()},
                               {"tags", format == 2 ? normalisedPresetTags(row.value("tags").toVariant().toStringList()) : QStringList()}};
        have.insert(name.toCaseFolded());
    }
    auto database = m_catalog.db();
    if (!pending.isEmpty()) {
        if (!database.transaction()) { setStatus(tr("Could not start preset import: %1").arg(database.lastError().text())); return -1; }
        for (const QVariant &value : pending) {
            const auto row = value.toMap();
            const QString json = QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(row.value("values").toList())).toJson(QJsonDocument::Compact));
            if (!m_catalog.createPreset(row.value("name").toString(), json, row.value("category").toString(), row.value("tags").toStringList())) {
                database.rollback(); setStatus(tr("Could not import presets: %1").arg(m_catalog.lastError())); return -1;
            }
        }
        if (!database.commit()) { database.rollback(); setStatus(tr("Could not finish preset import: %1").arg(database.lastError().text())); return -1; }
        emit presetsChanged();
    }
    const int added = pending.size();
    setStatus(skipped ? tr("Imported %1 preset%2, %3 already here").arg(added).arg(added == 1 ? "" : "s").arg(skipped)
                      : tr("Imported %1 preset%2").arg(added).arg(added == 1 ? "" : "s"));
    return added;
}

int Backend::importPresetFiles(const QStringList &filesOrUrls) {
    m_presetImportReport.clear();
    int added = 0, skipped = 0, failed = 0;
    if (filesOrUrls.size() > 1000) {
        setStatus(tr("Select at most 1000 preset files at a time."));
        m_presetImportReport << QVariantMap{{"name", tr("Import stopped")}, {"status", tr("Not imported")}, {"error", statusMessage()}};
        emit presetImportReportChanged(); return -1;
    }
    QSet<QString> names;
    qint64 bytesRemaining = 64 * 1024 * 1024;
    for (const auto &value : presets()) names.insert(value.toMap().value("name").toString().toCaseFolded());
    for (const QString &fileOrUrl : filesOrUrls) {
        const QString path = localPath(fileOrUrl), suffix = QFileInfo(path).suffix().toLower();
        QVariantMap report{{"file", path}, {"name", QFileInfo(path).fileName()}};
        const qint64 size = QFileInfo(path).size();
        if (size > bytesRemaining) {
            report["status"] = tr("Not imported");
            report["error"] = tr("This selection exceeds the 64 MiB preset import limit. Import a smaller batch.");
            m_presetImportReport << report; ++failed; continue;
        }
        bytesRemaining -= qMax(qint64(0), size);
        if (suffix == QLatin1String("json")) {
            const int count = importPresets(fileOrUrl);
            report["status"] = count < 0 ? tr("Not imported") : tr("%1 presets added").arg(count);
            report["error"] = count < 0 ? statusMessage() : QString();
            report["summary"] = statusMessage();
            if (count < 0) ++failed; else { added += count; if (!count) ++skipped; }
            for (const auto &value : presets()) names.insert(value.toMap().value("name").toString().toCaseFolded());
        } else if (suffix == QLatin1String("xmp") || suffix == QLatin1String("lrtemplate")) {
            QFile file(path); XmpPreset::Result preset;
            if (!file.open(QIODevice::ReadOnly)) preset.error = tr("Could not read this file.");
            else if (file.size() > 4 * 1024 * 1024) preset.error = tr("XMP presets must be smaller than 4 MiB.");
            else preset = XmpPreset::read(file.readAll(), path);
            if (!preset.name.isEmpty()) report["name"] = preset.name;
            report["converted"] = preset.converted; report["warnings"] = preset.warnings;
            report["approximate"] = true;
            if (preset.error.isEmpty() && names.contains(preset.name.toCaseFolded())) {
                report["status"] = tr("Already here; kept existing preset"); ++skipped;
            } else if (preset.error.isEmpty()) {
                const QString json = QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(preset.values)).toJson(QJsonDocument::Compact));
                if (m_catalog.createPreset(preset.name, json, preset.category, {QStringLiteral("XMP"), QStringLiteral("approximate")})) {
                    names.insert(preset.name.toCaseFolded()); ++added;
                    report["status"] = tr("Imported as an approximate look");
                } else preset.error = tr("Could not save this preset: %1").arg(m_catalog.lastError());
            }
            if (!preset.error.isEmpty()) { report["status"] = tr("Not imported"); report["error"] = preset.error; ++failed; }
        } else {
            report["status"] = tr("Not imported");
            report["error"] = tr("Choose OmaRAW JSON, XMP or LRTEMPLATE files. Extract ZIP packs first; camera profiles are not presets.");
            ++failed;
        }
        m_presetImportReport << report;
    }
    if (added) emit presetsChanged();
    setStatus(tr("Presets: %1 added, %2 files skipped, %3 files failed. See the import report.").arg(added).arg(skipped).arg(failed));
    emit presetImportReportChanged();
    return failed && !added && !skipped ? -1 : added;
}

QVariantList Backend::presetRules() const {
    QVariantList out;
    for (const QVariant &v : m_catalog.presetRules()) { QVariantMap m = v.toMap(); m[QStringLiteral("text")] = ruleText(m); out << m; }
    return out;
}

static bool presetIsoRange(const QVariantMap &rule, int &lo, int &hi) {
    const auto read = [&](const char *key, int &output) {
        if (!rule.contains(key)) { output = 0; return true; }
        const QVariant value = rule.value(key);
        bool converted = false; const double number = value.toDouble(&converted);
        if (!converted || value.metaType().id() == QMetaType::Bool || !std::isfinite(number)
            || number < 0 || number > 2147483647 || std::floor(number) != number) return false;
        output = int(number); return true;
    };
    return read("isoMin", lo) && read("isoMax", hi) && (!lo || !hi || lo <= hi);
}

static bool presetExposureRange(const QVariantMap &rule, const QString &key, double &lo, double &hi) {
    const auto read = [&](const QString &field, double &output) {
        if (!rule.contains(field)) { output = 0; return true; }
        const QVariant value = rule.value(field);
        bool converted = false; double number = value.toDouble(&converted);
        if (!converted && key == QLatin1String("shutter") && value.metaType().id() == QMetaType::QString) {
            const auto parts = value.toString().split('/');
            if (parts.size() == 2) {
                bool a = false, b = false;
                const double numerator = parts[0].toDouble(&a), denominator = parts[1].toDouble(&b);
                converted = a && b && std::isfinite(numerator) && std::isfinite(denominator) && numerator >= 0 && denominator > 0;
                if (converted) number = numerator / denominator;
            }
        }
        if (!converted || value.metaType().id() == QMetaType::Bool || !std::isfinite(number) || number < 0) return false;
        output = number; return true;
    };
    return read(key + "Min", lo) && read(key + "Max", hi) && (!lo || !hi || lo <= hi);
}

static bool exposureMatches(double value, double lo, double hi) {
    if (!lo && !hi) return true;
    if (!std::isfinite(value) || value <= 0) return false;
    // Exiv2 and LibRaw expose these measurements as floats. Keep inclusive
    // boundaries inclusive after their single-precision rounding.
    const auto tolerance = [&](double bound) { return 4 * std::numeric_limits<float>::epsilon() * std::max(std::abs(value), std::abs(bound)); };
    return (!lo || value >= lo - tolerance(lo)) && (!hi || value <= hi + tolerance(hi));
}

static QString presetShutterText(double seconds) {
    // The metadata formatter uses integer reciprocal/whole-second labels.
    // Avoid its integer conversion for extreme but finite user-entered bounds.
    if (seconds > 1.0 / 2147483647.0 && seconds < 2147483647.0) return Metadata::formatShutter(seconds);
    return QStringLiteral("%1 s").arg(seconds, 0, 'g', 6);
}

int Backend::createPresetRule(const QString &preset, const QVariantMap &rule) {
    int lo = 0, hi = 0;
    if (!presetIsoRange(rule, lo, hi)) { setStatus(tr("Enter a valid ISO range; the starting value must not exceed the ending value.")); return 0; }
    double apertureLo = 0, apertureHi = 0, shutterLo = 0, shutterHi = 0;
    if (!presetExposureRange(rule, "aperture", apertureLo, apertureHi) || !presetExposureRange(rule, "shutter", shutterLo, shutterHi)) {
        setStatus(tr("Enter valid aperture and shutter ranges, with the lower value first. Shutter values use seconds or fractions such as 1/125.")); return 0;
    }
    QString name;
    for (const auto &value : presets()) {
        const auto existing = value.toMap().value("name").toString();
        if (existing.compare(preset.trimmed(), Qt::CaseInsensitive) == 0) { name = existing; break; }
    }
    if (name.isEmpty()) { setStatus(tr("This preset is no longer available.")); return 0; }
    auto normalised = rule; normalised["isoMin"] = lo; normalised["isoMax"] = hi;
    normalised["apertureMin"] = apertureLo; normalised["apertureMax"] = apertureHi;
    normalised["shutterMin"] = shutterLo; normalised["shutterMax"] = shutterHi;
    const int id = m_catalog.createPresetRule(name, normalised);
    if (id) { setStatus(tr("\"%1\" will be applied to %2").arg(name, ruleText(normalised))); emit presetRulesChanged(); }
    else setStatus(tr("Could not save the preset rule: %1").arg(m_catalog.lastError()));
    return id;
}

bool Backend::deletePresetRule(int id) {
    const bool ok = m_catalog.deletePresetRule(id);
    if (ok) emit presetRulesChanged();
    return ok;
}

QString Backend::ruleText(const QVariantMap &rule) {
    QStringList parts;
    const QString camera = rule.value(QStringLiteral("camera")).toString().trimmed(), lens = rule.value(QStringLiteral("lens")).toString().trimmed();
    const QString format = rule.value(QStringLiteral("format")).toString().trimmed().toUpper();
    int lo = 0, hi = 0;
    if (!presetIsoRange(rule, lo, hi)) return tr("Invalid ISO range");
    if (!camera.isEmpty()) parts << camera;
    if (!lens.isEmpty()) parts << lens;
    if (!format.isEmpty()) parts << (format == QLatin1String("RAW") ? tr("any RAW") : format);
    if (lo > 0 && hi > 0) parts << tr("ISO %1–%2").arg(lo).arg(hi);
    else if (lo > 0) parts << tr("ISO %1 and above").arg(lo);
    else if (hi > 0) parts << tr("ISO up to %1").arg(hi);
    double apertureLo = 0, apertureHi = 0, shutterLo = 0, shutterHi = 0;
    if (!presetExposureRange(rule, "aperture", apertureLo, apertureHi) || !presetExposureRange(rule, "shutter", shutterLo, shutterHi)) return tr("Invalid exposure range");
    const auto appendRange = [&](double a, double b, const QString &first, const QString &last) {
        if (a > 0 && b > 0) parts << tr("%1 to %2").arg(first, last);
        else if (a > 0) parts << tr("%1 and above").arg(first);
        else if (b > 0) parts << tr("up to %1").arg(last);
    };
    appendRange(apertureLo, apertureHi, QStringLiteral("f/%1").arg(apertureLo, 0, 'g', 6), QStringLiteral("f/%1").arg(apertureHi, 0, 'g', 6));
    appendRange(shutterLo, shutterHi, presetShutterText(shutterLo), presetShutterText(shutterHi));
    return parts.isEmpty() ? tr("every photo") : parts.join(QStringLiteral(" · "));
}

bool Backend::ruleMatches(const QVariantMap &rule, const AssetRecord &r) {
    const QString camera = rule.value(QStringLiteral("camera")).toString().trimmed();
    if (!camera.isEmpty() && !(r.make + QLatin1Char(' ') + r.model).contains(camera, Qt::CaseInsensitive)) return false;
    const QString lens = rule.value(QStringLiteral("lens")).toString().trimmed();
    if (!lens.isEmpty() && !r.lens.contains(lens, Qt::CaseInsensitive)) return false;
    int lo = 0, hi = 0;
    if (!presetIsoRange(rule, lo, hi)) return false;
    if (lo > 0 && r.iso < lo) return false;
    if (hi > 0 && (r.iso <= 0 || r.iso > hi)) return false;
    double apertureLo = 0, apertureHi = 0, shutterLo = 0, shutterHi = 0;
    if (!presetExposureRange(rule, "aperture", apertureLo, apertureHi) || !presetExposureRange(rule, "shutter", shutterLo, shutterHi)
        || !exposureMatches(r.aperture, apertureLo, apertureHi) || !exposureMatches(r.shutter, shutterLo, shutterHi)) return false;
    const QString format = rule.value(QStringLiteral("format")).toString().trimmed().toUpper();
    if (!format.isEmpty()) {
        if (format == QLatin1String("RAW")) { if (!Metadata::isRawExtension(QFileInfo(r.path).suffix())) return false; }
        else if (r.format.compare(format, Qt::CaseInsensitive) != 0) return false;
    }
    return true;
}

QStringList Backend::presetsMatching(int assetId) const {
    QStringList out;
    const AssetRecord r = m_catalog.asset(assetId);
    if (!r.id) return out;
    for (const QVariant &v : m_catalog.presetRules()) {
        const QVariantMap rule = v.toMap();
        const QString name = rule.value(QStringLiteral("preset")).toString();
        if (ruleMatches(rule, r)) { out.removeAll(name); out << name; }
    }
    return out;
}

bool Backend::giveFirstEdit() {
    if (!m_engine || m_engine->imageId() < 0 || !m_engine->maintenanceReady()) return false;
    const int id = m_catalog.idForPath(m_engine->imagePath(), m_engine->imageVariant());
    if (!id || id == m_firstEditChecked) return false;   // asked after every render: once per photo
    m_firstEditChecked = id;
    if (!m_catalog.isAutoPending(id)) return false;
    // Once only, whatever happens next: a photo taken back to its original
    // with Reset stays there. (A fresh photo's history already holds the
    // engine's own automatic steps, so the history cannot say "untouched";
    // the mark can. Preset rules clear it when they give the starting point.)
    m_catalog.clearAutoPending(id);
    const QVariantList values = m_engine->firstEditValues();
    if (values.isEmpty()) return false;
    m_engine->applyValues(values);
    return true;
}

bool Backend::applyPreset(const QString &name) {
    if (!m_engine || m_engine->imageId()<0) return false;
    for (const auto &v : presets()) {
        const auto preset=v.toMap();
        if (preset.value("name").toString()!=name) continue;
        m_engine->applyPresetValues(preset.value("values").toList());
        notePresetApplied(name);
        setStatus(tr("Applied preset %1").arg(name));
        return true;
    }
    setStatus(tr("Preset not found: %1").arg(name)); return false;
}

int Backend::applyPresetRules(const QVariantList &ids) {
    const QVariantList all = presets();
    int n = 0;
    for (const QVariant &v : ids) {
        const int id = v.toInt();
        const QStringList names = presetsMatching(id);
        if (names.isEmpty()) continue;
        QVariantList values;
        // Preset selection is exclusive: the last matching rule wins.
        for (const QString &name : names)
            for (const QVariant &p : all) if (p.toMap().value(QStringLiteral("name")).toString() == name) values = p.toMap().value(QStringLiteral("values")).toList();
        if (values.isEmpty()) continue;
        ++n;
        m_catalog.clearAutoPending(id);   // the preset is its starting point instead
        const AssetRecord r = m_catalog.asset(id);
        if (m_engine) m_engine->applyPresetValuesTo({QVariantMap{{QStringLiteral("path"), r.path}, {QStringLiteral("variant"), r.variant}}}, values);
    }
    return n;
}

int Backend::savePreset(const QString &name, const QVariantList &values, const QString &category, const QStringList &tags) {
    if (name.trimmed().isEmpty() || values.isEmpty()) return 0;
    for (const auto &value : presets()) if (value.toMap().value("name").toString().compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
        setStatus(tr("A preset with this name already exists. Choose a new name.")); return 0;
    }
    const QString json = QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(values)).toJson(QJsonDocument::Compact));
    const int id = m_catalog.createPreset(name.trimmed(), json, category.simplified(), normalisedPresetTags(tags));
    if (id) { setStatus(tr("Saved preset \"%1\"").arg(name.trimmed())); emit presetsChanged(); }
    return id;
}

bool Backend::presetCaptureReady() {
    const auto asset = m_catalog.asset(m_currentId);
    if (!asset.id || !m_engine || m_engine->imageId() < 0 || m_engine->imagePath() != asset.path || m_engine->imageVariant() != asset.variant) {
        setStatus(tr("Open the selected photo in Develop before saving a preset.")); return false;
    }
    if (!m_engine->maintenanceReady() || m_engine->image().isNull()) {
        setStatus(tr("Wait for the current edit to finish before saving a preset.")); return false;
    }
    return true;
}

int Backend::saveCurrentPreset(const QString &name, const QStringList &groups, const QString &category, const QStringList &tags) {
    if (!presetCaptureReady()) return 0;
    QStringList known;
    for (const auto &value : EngineService::settingsGroups()) known << value.toMap().value("key").toString();
    if (groups.isEmpty() || std::any_of(groups.begin(), groups.end(), [&](const auto &group) { return !known.contains(group); })) {
        setStatus(tr("Choose the adjustment groups to include in the preset.")); return 0;
    }
    const auto values = m_engine->valuesFor(groups);
    if (values.isEmpty()) { setStatus(tr("The selected groups have no settings to save.")); return 0; }
    return savePreset(name, values, category, tags);
}

int Backend::saveCurrentModulePreset(const QString &name, const QString &operation, const QString &category, const QStringList &tags) {
    if (!presetCaptureReady()) return 0;
    const auto values = m_engine->valuesForModule(operation);
    if (values.isEmpty()) { setStatus(tr("This adjustment has no settings to save.")); return 0; }
    return savePreset(name, values, category, tags);
}

bool Backend::setPresetMetadata(int id, const QString &category, const QStringList &tags) {
    if (id <= 0) return false;
    if (!m_catalog.setPresetMetadata(id, category.simplified(), normalisedPresetTags(tags))) {
        setStatus(tr("Could not update preset organisation.")); return false;
    }
    emit presetsChanged();
    setStatus(tr("Updated preset organisation"));
    return true;
}

bool Backend::deletePreset(int id) {
    if (id <= 0) return false;
    const bool ok = m_catalog.deletePreset(id);
    if (ok) { emit presetsChanged(); emit presetRulesChanged(); }
    return ok;
}

void Backend::setStatus(const QString &m) {
    if (m == m_status) return;
    m_status = m;
    emit statusChanged();
}

// ── variants ────────────────────────────────────────────────────────────

QVariantList Backend::aiVersions() const {
    QVariantList versions = m_catalog.aiVersions(m_currentId);
    int number = 0;
    for (auto &entry : versions) {
        auto row = entry.toMap();
        const QString operation = row.value("operation").toString();
        row["label"] = operation == "original" ? tr("Original")
            : tr("%1 · version %2").arg(operation == "denoise" ? tr("AI denoise") : operation == "variant" ? tr("Edit variant") : tr("Object removal")).arg(++number);
        row["current"] = row.value("id").toInt() == m_currentId;
        entry = row;
    }
    return versions;
}

void Backend::focusAiVersion(int id) {
    m_aiFocusId = id;
    // Keep one photo in a collapsed stack while switching working versions.
    if (m_catalog.asset(id).stackId) m_catalog.setStackTop(id);
    select(id, 0);
    refresh();
}

bool Backend::openAiVersion(int id) {
    bool related = false;
    for (const auto &entry : aiVersions()) related |= entry.toMap().value("id").toInt() == id;
    if (!related || !m_engine) { setStatus(tr("Choose a version of this photo.")); return false; }
    if (m_engine->busy() || static_cast<AiService *>(m_engine->ai())->busy() ||
        static_cast<DenoiseService *>(m_engine->denoise())->busy()) {
        setStatus(tr("Wait for processing to finish before switching versions.")); return false;
    }
    const auto photo = m_catalog.asset(id);
    if (!QFileInfo::exists(photo.path)) {
        setStatus(tr("Reconnect or locate %1 to open this version.").arg(photo.filename)); return false;
    }
    static_cast<AiService *>(m_engine->ai())->cancel();
    focusAiVersion(id);
    m_engine->load(photo.path, photo.variant);
    setStatus(tr("Switched photo version. Edits on your other versions are kept."));
    return true;
}

bool Backend::openAiOriginal() {
    for (const auto &entry : aiVersions()) {
        const auto row = entry.toMap();
        if (row.value("operation") == "original") return openAiVersion(row.value("id").toInt());
    }
    return false;
}

bool Backend::openLatestAiVersion() {
    const auto versions = aiVersions();
    return !versions.isEmpty() && openAiVersion(versions.last().toMap().value("id").toInt());
}

void Backend::setEngine(EngineService *engine) {
    m_engine = engine;
    if (!engine) return;
    engine->setMemoryBudget(m_resources.effectiveMemoryMiB());
    // Library recovery may choose its cache location after Backend construction.
    m_resources.refreshCache();
    connect(static_cast<DenoiseService *>(engine->denoise()), &DenoiseService::saved, this,
            [this, engine](const QString &path, const QString &original, int variant) {
        const auto source = m_catalog.asset(m_catalog.idForPath(original, variant));
        QString metadataError;
        if (source.id) Metadata::writeEmbedded(path, sidecarData(source), nullptr, &metadataError);
        const QString folder = QFileInfo(path).absolutePath();
        // A processed version belongs to its original shoot, not a new import.
        const int import = source.id ? source.importId : m_catalog.beginImport(folder);
        const int id = Importer::importFile(m_catalog, path, import);
        if (!source.id) m_catalog.finishImport(import, id ? 1 : 0);
        if (!id) { setStatus(tr("DNG saved at %1, but could not be added to the catalog.").arg(path)); return; }
        m_catalog.clearAutoPending(id);
        m_catalog.markEdited(id); // Copied settings must satisfy edited-photo filters before selection.
        if (source.id) {
            if (!m_catalog.addAiVersion(source.id, id, "denoise")) metadataError += tr(" Could not link the original version.");
            m_catalog.stackAssets({source.id, id});
        }
        m_catalog.rescanFolders(); focusAiVersion(id);
        engine->openDenoiseCopy(path); emit catalogChanged(); emit foldersChanged();
        setStatus(metadataError.isEmpty() ? tr("AI denoise applied. Editing the DNG copy; the original is unchanged.")
                                         : tr("DNG saved; some catalog metadata could not be embedded: %1").arg(metadataError));
    });
    connect(static_cast<AiService *>(engine->ai()), &AiService::saved, this, [this, engine](const QString &path) {
        const auto source = m_catalog.asset(m_catalog.idForPath(engine->imagePath(), engine->imageVariant()));
        QString metadataError;
        if (source.id) Metadata::writeEmbedded(path, sidecarData(source), nullptr, &metadataError);
        const QString folder = QFileInfo(path).absolutePath();
        const int import = source.id ? source.importId : m_catalog.beginImport(folder);
        const int id = Importer::importFile(m_catalog, path, import);
        if (!source.id) m_catalog.finishImport(import, id ? 1 : 0);
        if (!id) { setStatus(tr("Saved %1, but could not add it to the catalog.").arg(path)); return; }
        m_catalog.clearAutoPending(id);
        m_catalog.markEdited(id);
        if (source.id) {
            if (!m_catalog.addAiVersion(source.id, id, "remove")) metadataError += tr(" Could not link the original version.");
            m_catalog.stackAssets({source.id, id});
        }
        m_catalog.rescanFolders(); focusAiVersion(id);
        engine->load(path, 0); emit catalogChanged(); emit foldersChanged();
        setStatus(metadataError.isEmpty() ? tr("Object removal applied. Keep editing; your original is in Photo versions.")
                                         : tr("Copy saved; catalog metadata could not be embedded: %1").arg(metadataError));
    });
    connect(engine, &EngineService::offlineChanged, this, [this, engine] { setStatus(engine->offlineStatus()); });
    connect(engine, &EngineService::sourceChanged, this, [this] { emit selectionChanged(); });
    connect(engine, &EngineService::offlineFinished, this, [this] { emit selectionChanged(); });
    connect(engine, &EngineService::imageMoved, this, &Backend::finishMovedFile);
    connect(engine, &EngineService::editedRender, this, &Backend::applyEditedThumb);
    connect(engine,&EngineService::imageMatchApplied,this,[this](const QString &path,int variant) {
        // A deliberate match is the starting look. Do not silently apply a
        // pending automatic first edit when this batch photo is opened later.
        const int id=m_catalog.idForPath(path,variant);
        if(id) {m_catalog.clearAutoPending(id);m_catalog.markEdited(id);}
    });
    // An edit, once it has settled, goes into the photo's sidecar too.
    m_sidecarTimer.setSingleShot(true);
    m_sidecarTimer.setInterval(3000);
    connect(&m_sidecarTimer, &QTimer::timeout, this, [this] {
        const QSet<int> due = std::exchange(m_sidecarDue, {});
        for (int id : due) { QString err; if (!writeSidecarFor(id, true, &err)) setStatus(tr("Sidecar not written: %1").arg(err)); }
    });
    connect(engine, &EngineService::editedRender, this, [this](const QString &path, int) {
        if (!m_writeSidecars) return;
        if (const int id = m_catalog.idForPath(path, 0)) { m_sidecarDue.insert(id); m_sidecarTimer.start(); }
    });
    connect(engine, &EngineService::sidecarEditReady, this, [this](const QString &path, bool has, const QByteArray &xmp, const QVariantMap &crs, const QString &error) {
        if (!has) { if (!error.isEmpty()) setStatus(tr("The edit was not written to the sidecar of %1: %2").arg(QFileInfo(path).fileName(), error)); return; }
        const AssetRecord r = m_catalog.asset(m_catalog.idForPath(path, 0));
        if (!r.id) return;
        const Metadata::SidecarEdit edit{xmp, crs};
        QString err;
        if (!storeMetadata(r, &edit, &err)) { setStatus(tr("Sidecar not written for %1: %2").arg(r.filename, err)); return; }
        emit sidecarWritten(r.id);
    });
    connect(engine, &EngineService::sourceExported, this, [this](const QString &src, int variant, bool ok) { if (ok) markExported(src, variant); });
    connect(engine, &EngineService::imageDuplicated, this, [this](const QString &path, int variant, int newVariant, const QString &err) {
        const int src = m_catalog.idForPath(path, variant);
        if (newVariant < 0 || !src) { setStatus(tr("Could not create a variant: %1").arg(err.isEmpty() ? tr("source left the catalog") : err)); return; }
        const int id = m_catalog.createVariant(src, newVariant, QString());
        if (!id) { setStatus(tr("Could not create a variant: %1").arg(m_catalog.lastError())); return; }
        // Start out looking like the source: copy its cached thumbnail.
        const AssetRecord r = m_catalog.asset(id);
        const QString dir = Thumbnailer::defaultCacheDir();
        for (const int edge : {m_thumbEdge, 0}) {
            const QString from = Thumbnailer::existingPath(dir, r.path, r.mtime, edge, variant);
            const QString to = Thumbnailer::filePath(dir, r.path, r.mtime, edge, r.variant);
            QDir().mkpath(QFileInfo(to).absolutePath());
            if (QFile::exists(from)) { QFile::remove(to); QFile::copy(from, to); }
        }
        setStatus(tr("Created %1 of %2").arg(variantLabel(r), r.filename));
        refresh();
        emit variantsChanged();
        if (m_model.rowOf(id) >= 0) select(id, 0);
    });
    connect(engine, &EngineService::imageRemoved, this, [this](const QString &path, int variant, const QString &err) {
        if (!err.isEmpty()) qWarning() << "engine remove" << path << variant << err;
    });
    connect(engine, &EngineService::versionsSwapped, this, [this](const QString &path, int a, int b, const QString &err) {
        Q_UNUSED(b)
        const int id = m_catalog.idForPath(path, a); // the variant being promoted (a > 0)
        if (!err.isEmpty() || !id) { setStatus(tr("Could not promote the variant: %1").arg(err)); return; }
        if (!m_catalog.promoteVariant(id)) { setStatus(tr("Could not promote the variant: %1").arg(m_catalog.lastError())); return; }
        setStatus(tr("Promoted to master"));
        m_model.invalidateAll();
        refresh();
        emit variantsChanged();
        emit selectionChanged();
    });
}

QString Backend::variantLabel(const AssetRecord &r) {
    if (!r.variant) return QString();
    return r.variantName.isEmpty() ? tr("Copy %1").arg(r.variant) : r.variantName;
}

void Backend::createVariant(int id) {
    const AssetRecord r = m_catalog.asset(id ? id : m_currentId);
    if (!r.id) return;
    if (!m_engine || !m_engine->ready()) { setStatus(tr("Variants need the engine")); return; }
    setStatus(tr("Creating a variant of %1…").arg(r.filename));
    m_engine->duplicateImage(r.path, r.variant);
}

bool Backend::renameVariant(int id, const QString &name) {
    const AssetRecord r = m_catalog.asset(id ? id : m_currentId);
    if (!r.id || !r.variant) return false;
    if (!m_catalog.setVariantName(r.id, name.trimmed())) return false;
    m_model.invalidate({r.id});
    emit variantsChanged();
    if (r.id == m_currentId) emit selectionChanged();
    return true;
}

void Backend::promoteVariant(int id) {
    const AssetRecord r = m_catalog.asset(id ? id : m_currentId);
    if (!r.id || !r.variant) return;
    if (!m_engine || !m_engine->ready()) { setStatus(tr("Variants need the engine")); return; }
    m_engine->swapVersions(r.path, r.variant, 0);
}

void Backend::deleteVariant(int id) {
    const AssetRecord r = m_catalog.asset(id ? id : m_currentId);
    if (!r.id || !r.variant) return;
    if (!m_catalog.removeAssets({r.id})) { setStatus(tr("The variant was not deleted: %1").arg(m_catalog.lastError())); return; }
    if (m_engine) m_engine->removeImage(r.path, r.variant);
    // Land on the master in ONE selection change (refresh keeps a current
    // row that is still shown), so Develop loads it once.
    m_selection.remove(r.id);
    if (m_currentId == r.id) { m_currentId = r.variantOf; m_anchorId = r.variantOf; m_selection.insert(r.variantOf); }
    setStatus(tr("Deleted %1 of %2").arg(variantLabel(r), r.filename));
    refresh();
    emit variantsChanged();
    emit selectionChanged();
}

QVariantList Backend::variantsOf(int id) const {
    QVariantList out;
    const AssetRecord r = m_catalog.asset(id ? id : m_currentId);
    if (!r.id) return out;
    QVector<int> ids{r.variantOf ? r.variantOf : r.id};
    ids += m_catalog.variantsOf(ids.first());
    for (int i : ids) {
        const AssetRecord a = m_catalog.asset(i);
        QVariantMap m;
        m[QStringLiteral("id")] = a.id;
        m[QStringLiteral("variant")] = a.variant;
        m[QStringLiteral("name")] = a.variantName;
        m[QStringLiteral("label")] = a.variant ? variantLabel(a) : tr("Master");
        m[QStringLiteral("current")] = a.id == m_currentId;
        m[QStringLiteral("edited")] = a.edited;
        m[QStringLiteral("thumb")] = Thumbnailer::sourceFor(a.path, a.mtime, m_thumbEdge, a.editRev, a.variant);
        out << m;
    }
    return out;
}

void Backend::keepSelectionOffline(bool keep) {
    if (!m_engine || !m_engine->ready()) { setStatus(tr("Wait for the engine before preparing offline copies")); return; }
    QStringList paths;
    for (int id : targets(0)) { const auto r = m_catalog.asset(id); if (r.id) paths << r.path; }
    m_engine->setOfflineCopies(paths, keep);
}

void Backend::buildSelectionSmartPreviews(bool keep) {
    if (!m_engine) return;
    QStringList paths;
    for (int id : targets(0)) { const auto r = m_catalog.asset(id); if (r.id) paths << r.path; }
    m_engine->setSmartPreviews(paths, keep);
}

QVariantList Backend::exportItems(bool shown) const {
    QVariantList out;
    QVector<int> ids;
    if (shown) ids = m_model.ids();
    else { for (int id : m_model.ids()) if (m_selection.contains(id)) ids << id; }
    for (int id : ids) {
        const AssetRecord r = m_catalog.asset(id);
        if (!r.id) continue;
        QVariantMap m;
        m[QStringLiteral("path")] = r.path;
        m[QStringLiteral("variant")] = r.variant;
        m[QStringLiteral("name")] = r.variantName;
        // What the export's name tokens read.
        m[QStringLiteral("capturedAt")] = r.capturedAt;
        m[QStringLiteral("make")] = r.make;
        m[QStringLiteral("model")] = r.model;
        m[QStringLiteral("rating")] = r.rating;
        m[QStringLiteral("title")] = r.title;
        m[QStringLiteral("caption")] = r.caption;
        m[QStringLiteral("copyright")] = r.copyright;
        m[QStringLiteral("creator")] = r.creator;
        m[QStringLiteral("keywords")] = m_catalog.keywordsFor(r.id);
        // For the resize modes and the validation.
        m[QStringLiteral("width")] = r.width;
        m[QStringLiteral("height")] = r.height;
        m[QStringLiteral("orientation")] = r.orientation;
        const bool copy = m_engine && m_engine->offlineInfo(r.path).value("available").toBool();
        m[QStringLiteral("offline")] = !QFile::exists(r.path) && !copy;
        m[QStringLiteral("offlineCopy")] = !QFile::exists(r.path) && copy;
        out << m;
    }
    return out;
}

QVariantMap Backend::folderSpace(const QString &pathOrUrl) const {
    QVariantMap m;
    const QString p = localPath(pathOrUrl);
    const QFileInfo fi(p);
    m[QStringLiteral("exists")] = fi.isDir();
    m[QStringLiteral("writable")] = fi.isDir() && fi.isWritable();
    const QStorageInfo st(p);
    m[QStringLiteral("bytesFree")] = st.isValid() && st.isReady() ? st.bytesAvailable() : qint64(-1);
    m[QStringLiteral("bytesTotal")] = st.isValid() && st.isReady() ? st.bytesTotal() : qint64(-1);
    return m;
}

void Backend::revealPath(const QString &path) const {
    const QFileInfo fi(localPath(path));
    if (!fi.exists()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()));
}

// ── snapshots ───────────────────────────────────────────────────────────

QVariantList Backend::snapshots(int assetId) {
    QVariantList out;
    for (const QVariant &v : m_catalog.snapshots(assetId)) {
        QVariantMap m = v.toMap();
        const int id = m.value(QStringLiteral("id")).toInt();
        m[QStringLiteral("values")] = QJsonDocument::fromJson(m.value(QStringLiteral("json")).toString().toUtf8()).array().toVariantList();
        m.remove(QStringLiteral("json"));
        m[QStringLiteral("preview")] = QStringLiteral("image://snapshots/%1?display=%2").arg(id).arg(DisplayColour::instance().revision());
        out << m;
    }
    return out;
}

int Backend::saveSnapshot(int assetId, const QString &name) {
    const AssetRecord r = m_catalog.asset(assetId ? assetId : m_currentId);
    if (!r.id) return 0;
    if (!m_engine || m_engine->imageId() < 0 || m_engine->imagePath() != r.path || m_engine->imageVariant() != r.variant) {
        setStatus(tr("Open this photo in Develop before saving a snapshot.")); return 0;
    }
    if (!m_engine->maintenanceReady()) { setStatus(tr("Wait for the current edit to finish before saving a snapshot.")); return 0; }
    const QVariantList values = m_engine->currentValues(true);
    const int historyEnd = m_engine->historyEnd();
    QImage preview = m_engine->linearCopy();
    if (values.isEmpty() || preview.isNull()) { setStatus(tr("Wait for a completed preview before saving a snapshot.")); return 0; }
    const QString json = QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(values)).toJson(QJsonDocument::Compact));
    // Preserve scene-linear highlights and exclude soft proofing from the
    // saved look. The chosen view and monitor transform are applied on display.
    if (qMax(preview.width(), preview.height()) > SnapshotPreview::MaxEdge)
        preview = preview.scaled(SnapshotPreview::MaxEdge, SnapshotPreview::MaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QByteArray stored = SnapshotPreview::encode(preview);
    if (!preview.isNull() && stored.isEmpty()) { setStatus(tr("Could not encode the snapshot preview.")); return 0; }
    const QString title = name.trimmed().isEmpty() ? tr("Snapshot %1").arg(m_catalog.snapshots(r.id).size() + 1) : name.trimmed();
    const int id = m_catalog.createSnapshot(r.id, title, historyEnd, json, stored);
    if (!id) { setStatus(tr("Could not save the snapshot: %1").arg(m_catalog.lastError())); return 0; }
    {
        QMutexLocker l(&m_snapMutex); ++m_snapRevision;
        if (!preview.isNull()) m_snapImages.insert(id, new QImage(preview), int((preview.sizeInBytes() + 1023) / 1024));
        else m_snapImages.remove(id);
    }
    setStatus(tr("Saved snapshot \"%1\"").arg(title));
    emit snapshotsChanged(r.id);
    return id;
}

bool Backend::renameSnapshot(int id, const QString &name) {
    if (id <= 0 || name.trimmed().isEmpty() || !m_catalog.renameSnapshot(id, name.trimmed())) return false;
    emit snapshotsChanged(m_currentId);
    return true;
}

bool Backend::compareSnapshot(int id) {
    const AssetRecord asset = m_catalog.asset(m_currentId);
    if (!m_engine || !m_engine->ready() || m_engine->imageId() < 0
        || m_engine->imagePath() != asset.path || m_engine->imageVariant() != asset.variant) return false;
    for (const QVariant &snapshot : m_catalog.snapshots(asset.id)) {
        const auto row = snapshot.toMap();
        if (row.value(QStringLiteral("id")).toInt() != id) continue;
        const QJsonDocument stored = QJsonDocument::fromJson(row.value(QStringLiteral("json")).toString().toUtf8());
        if (!stored.isArray() || stored.array().isEmpty()) return false;
        m_engine->compareSnapshot(id, stored.array().toVariantList());
        return true;
    }
    return false;
}

bool Backend::deleteSnapshot(int id) {
    if (id <= 0 || !m_catalog.deleteSnapshot(id)) return false;
    if (m_engine && m_engine->snapshotId() == id) m_engine->clearSnapshotComparison();
    { QMutexLocker l(&m_snapMutex); ++m_snapRevision; m_snapImages.remove(id); }
    emit snapshotsChanged(m_currentId);
    return true;
}

QImage Backend::snapshotImage(int id) const {
    QString path; quint64 revision;
    {
        QMutexLocker lock(&m_snapMutex);
        if (const auto *cached = m_snapImages.object(id)) return *cached;
        path = m_snapCatalogPath; revision = m_snapRevision;
    }
    const QImage image = SnapshotPreview::decode(SnapshotPreview::read(path, id));
    {
        QMutexLocker lock(&m_snapMutex);
        // A deleted/reused id or a different catalog must not receive a late
        // result from an earlier asynchronous image request.
        if (revision == m_snapRevision && !image.isNull())
            m_snapImages.insert(id, new QImage(image), int((image.sizeInBytes() + 1023) / 1024));
    }
    return image;
}

QImage SnapshotImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(requestedSize)
    const QImage img = m_backend->snapshotImage(id.section(QLatin1Char('?'), 0, 0).toInt());
    if (size) *size = img.size();
    return DisplayColour::instance().convert(img);
}

// ── print & contact sheets ──────────────────────────────────────────────

QString Backend::makeContactSheet(bool shown, const QString &pdfPath, const QVariantMap &options) {
    QVector<int> ids;
    if (shown) ids = m_model.ids();
    else { for (int id : m_model.ids()) if (m_selection.contains(id)) ids << id; }
    if (ids.isEmpty()) return tr("nothing to lay out");
    QList<QImage> images;
    QStringList captions;
    const QString dir = Thumbnailer::defaultCacheDir();
    for (int id : ids) {
        const AssetRecord r = m_catalog.asset(id);
        if (!r.id) continue;
        // The grid thumbnail carries the edit; otherwise a fresh preview.
        QImage img = Thumbnailer::cached(dir, r.path, r.mtime, m_thumbEdge, r.variant);
        if (img.isNull()) img = Metadata::preview(r.path, 800);
        images << img;
        captions << (r.variant ? r.filename + QStringLiteral(" · ") + variantLabel(r) : r.filename);
    }
    int pages = 0;
    const QString err = Sheets::contactSheet(images, captions, pdfPath, Sheets::Options::fromMap(options), &pages);
    setStatus(err.isEmpty() ? tr("Contact sheet: %1 photos on %2 page%3 → %4").arg(images.size()).arg(pages).arg(pages == 1 ? "" : "s").arg(QFileInfo(pdfPath).fileName())
                            : tr("Contact sheet failed: %1").arg(err));
    return err;
}

QString Backend::makePrintSheet(const QStringList &files, const QString &pdfPath, const QVariantMap &options) {
    QStringList captions;
    for (const QString &f : files) captions << QFileInfo(f).completeBaseName();
    int pages = 0;
    const QString err = Sheets::printSheet(files, captions, pdfPath, Sheets::Options::fromMap(options), &pages);
    setStatus(err.isEmpty() ? tr("Print layout: %1 page%2 → %3").arg(pages).arg(pages == 1 ? "" : "s").arg(QFileInfo(pdfPath).fileName())
                            : tr("Print layout failed: %1").arg(err));
    return err;
}

QStringList Backend::sheetPapers() const { return Sheets::Options::papers(); }

int Backend::sheetLongEdge(const QVariantMap &options) const { return Sheets::longEdgePixels(Sheets::Options::fromMap(options)); }

// ── batch metadata ──────────────────────────────────────────────────────

void Backend::setCopyright(const QString &text, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty() || !m_catalog.setCopyright(t, text.trimmed())) return;
    m_model.invalidate(t);
    emit selectionChanged();
    setStatus(tr("Copyright set on %1 photo%2").arg(t.size()).arg(t.size() == 1 ? "" : "s"));
}

void Backend::setTitle(const QString &text, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty() || !m_catalog.setTitle(t, text.trimmed())) return;
    syncSidecars(t);
    m_model.invalidate(t);
    emit selectionChanged();
    setStatus(tr("Title set on %1 photo%2").arg(t.size()).arg(t.size() == 1 ? "" : "s"));
}

void Backend::setCaption(const QString &text, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty() || !m_catalog.setCaption(t, text.trimmed())) return;
    syncSidecars(t);
    m_model.invalidate(t);
    emit selectionChanged();
    setStatus(tr("Caption set on %1 photo%2").arg(t.size()).arg(t.size() == 1 ? "" : "s"));
}

void Backend::setCreator(const QString &text, int id) {
    const QVector<int> t = targets(id);
    if (t.isEmpty() || !m_catalog.setCreator(t, text.trimmed())) return;
    m_model.invalidate(t);
    emit selectionChanged();
    setStatus(tr("Creator set on %1 photo%2").arg(t.size()).arg(t.size() == 1 ? "" : "s"));
}

QVariantMap Backend::selectionSummary() const {
    QVariantMap m;
    const QVector<int> t = targets(0);
    m[QStringLiteral("count")] = t.size();
    QString copyright, creator, label, title, caption; int rating = 0;
    bool cMixed = false, crMixed = false, lMixed = false, rMixed = false, tMixed = false, capMixed = false;
    for (int i = 0; i < t.size(); ++i) {
        const AssetRecord r = m_catalog.asset(t[i]);
        if (i == 0) { copyright = r.copyright; creator = r.creator; label = r.label; rating = r.rating; title = r.title; caption = r.caption; continue; }
        if (r.title != title) tMixed = true;
        if (r.caption != caption) capMixed = true;
        if (r.copyright != copyright) cMixed = true;
        if (r.creator != creator) crMixed = true;
        if (r.label != label) lMixed = true;
        if (r.rating != rating) rMixed = true;
    }
    m[QStringLiteral("copyright")] = cMixed ? QString() : copyright; m[QStringLiteral("copyrightMixed")] = cMixed;
    m[QStringLiteral("title")] = tMixed ? QString() : title; m[QStringLiteral("titleMixed")] = tMixed;
    m[QStringLiteral("caption")] = capMixed ? QString() : caption; m[QStringLiteral("captionMixed")] = capMixed;
    m[QStringLiteral("creator")] = crMixed ? QString() : creator; m[QStringLiteral("creatorMixed")] = crMixed;
    m[QStringLiteral("label")] = lMixed ? QString() : label; m[QStringLiteral("labelMixed")] = lMixed;
    m[QStringLiteral("rating")] = rMixed ? 0 : rating; m[QStringLiteral("ratingMixed")] = rMixed;
    return m;
}

// ── stacks ──────────────────────────────────────────────────────────────

void Backend::setCollapseStacks(bool on) {
    if (on == m_collapseStacks) return;
    m_collapseStacks = on;
    emit filterChanged();
    refresh();
}

void Backend::stackSelection() {
    QVector<int> t;
    for (int id : m_model.ids()) if (m_selection.contains(id)) t << id;
    if (t.size() < 2) { setStatus(tr("Select at least two photos to stack")); return; }
    const int stack = m_catalog.stackAssets(t);
    if (!stack) { setStatus(tr("Could not stack: %1").arg(m_catalog.lastError())); return; }
    m_expandedStacks.remove(stack);
    const QVector<int> members = m_catalog.stackMembers(stack);
    m_selection = {members.first()};
    m_currentId = members.first();
    m_anchorId = m_currentId;
    setStatus(tr("Stacked %1 photos").arg(members.size()));
    refresh();
    emit selectionChanged();
}

void Backend::unstackSelection() {
    const QVector<int> t = targets(0);
    if (t.isEmpty()) return;
    QSet<int> stacks;
    for (int id : t) { const int s = m_catalog.asset(id).stackId; if (s) stacks.insert(s); }
    if (stacks.isEmpty()) { setStatus(tr("Nothing here is stacked")); return; }
    if (!m_catalog.unstack(t)) { setStatus(tr("Not unstacked: %1").arg(m_catalog.lastError())); return; }
    for (int s : stacks) m_expandedStacks.remove(s);
    setStatus(tr("Unstacked %1 stack%2").arg(stacks.size()).arg(stacks.size() == 1 ? "" : "s"));
    refresh();
    emit selectionChanged();
}

int Backend::autoStackShown(int seconds) {
    // Over everything the source holds, not just the collapsed view.
    QVariantList binds;
    const QString where = sourceWhere(binds);
    const QVector<int> ids = m_catalog.assetIds(where, binds, orderFor(QStringLiteral("captured"), false));
    const int made = m_catalog.autoStack(ids, seconds);
    setStatus(made ? tr("Made %1 stack%2 from shots within %3 s of each other").arg(made).arg(made == 1 ? "" : "s").arg(seconds)
                   : tr("No shots within %1 s of each other to stack").arg(seconds));
    if (made) { refresh(); emit selectionChanged(); }
    return made;
}

void Backend::setStackTop(int id) {
    const int target = id ? id : m_currentId;
    if (!target || !m_catalog.setStackTop(target)) return;
    setStatus(tr("Stack top set"));
    refresh();
    emit selectionChanged();
}

void Backend::toggleStack(int id) {
    const AssetRecord r = m_catalog.asset(id ? id : m_currentId);
    if (!r.id || !r.stackId) return;
    if (m_expandedStacks.contains(r.stackId)) {
        m_expandedStacks.remove(r.stackId);
        // Land on the top so the selection does not vanish with the members.
        const QVector<int> members = m_catalog.stackMembers(r.stackId);
        m_selection = {members.first()}; m_currentId = members.first(); m_anchorId = m_currentId;
    } else {
        m_expandedStacks.insert(r.stackId);
    }
    refresh();
    m_model.invalidateAll();
    emit selectionChanged();
}

// ── volumes ─────────────────────────────────────────────────────────────

void Backend::refreshWatches() {
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    QStringList dirs;
    for (const QString &d : m_catalog.watchedFolders()) if (QDir(d).exists()) dirs << d;
    if (!dirs.isEmpty()) m_watcher.addPaths(dirs);
}

void Backend::setFolderWatched(int folderId, bool on) {
    if (!m_catalog.setFolderWatched(folderId, on)) return;
    refreshWatches();
    emit foldersChanged();
    setStatus(on ? tr("Watching %1 for new photos").arg(m_catalog.folderPath(folderId)) : tr("No longer watching %1").arg(m_catalog.folderPath(folderId)));
}

void Backend::importWatched() {
    if (m_watchPending.isEmpty() || m_watchScan) return;
    if (m_importer.running()) { m_watchTimer.start(); return; }   // after the running import
    // A file being copied in is announced when it appears, not when it is
    // finished, and no further event comes. So a folder must look the same
    // on two checks in a row, with nothing written in the last two seconds,
    // before anything in it is read: otherwise a half-copied RAW would be
    // catalogued truncated. The walk can be long (an archive, a network
    // share), so it happens on a thread; every folder waiting is looked at.
    const QStringList dirs(m_watchPending.cbegin(), m_watchPending.cend());
    m_watchPending.clear();
    struct Look { bool gone = false; QString signature; qint64 newest = 0; };
    auto looks = std::make_shared<QHash<QString, Look>>();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_watchScan = QThread::create([dirs, looks, now, cancel = &m_watchScanCancel] {
        for (const QString &dir : dirs) {
            Look look;
            if (!QDir(dir).exists()) { look.gone = true; looks->insert(dir, look); continue; }
            qint64 bytes = 0, files = 0, latest = 0;
            QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext() && !*cancel) {
                it.next();
                const QFileInfo f = it.fileInfo();
                const qint64 m = f.lastModified().toMSecsSinceEpoch();
                bytes += f.size(); ++files; latest = qMax(latest, m);
                // A file dated ahead of the clock (a camera on another time
                // zone, copied with its date kept) says nothing about
                // whether writing has stopped: only the signature can.
                if (m <= now + 1000) look.newest = qMax(look.newest, m);
            }
            look.signature = QStringLiteral("%1/%2/%3").arg(files).arg(bytes).arg(latest);
            looks->insert(dir, look);
        }
    });
    connect(m_watchScan, &QThread::finished, this, [this, looks] {
        m_watchScan->deleteLater(); m_watchScan = nullptr;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QStringList ready;
        for (auto it = looks->cbegin(); it != looks->cend(); ++it) {
            if (it->gone) { m_watchSettle.remove(it.key()); continue; }
            const bool quiet = now - it->newest > 2000;
            if (m_watchSettle.value(it.key()) == it->signature && quiet) ready << it.key();
            else { m_watchSettle.insert(it.key(), it->signature); m_watchPending.insert(it.key()); }
        }
        // One import at a time: the first settled folder now, the others
        // stay settled and go as soon as it is done.
        std::sort(ready.begin(), ready.end());
        if (!ready.isEmpty() && !m_importer.running()) {
            const QString dir = ready.takeFirst();
            m_watchSettle.remove(dir);
            // In place, skipping what the catalog holds; the user's own
            // import choices are not touched.
            const ImportOptions keep = m_importOptions;
            ImportOptions o; o.mode = QStringLiteral("add"); o.skipDuplicates = true;
            m_importOptions = o;
            m_watchImporting = true;
            importFolder(dir);
            m_importOptions = keep;
        }
        for (const QString &dir : std::as_const(ready)) m_watchPending.insert(dir);
        if (!m_watchPending.isEmpty()) m_watchTimer.start();
    });
    m_watchScan->start();
}

QVariantList Backend::devices() const {
    QVariantList out;
    const QString root = QStorageInfo::root().rootPath(), home = QStorageInfo(QDir::homePath()).rootPath();
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady()) continue;
        const QString dev = QString::fromLocal8Bit(v.device());
        if (!dev.startsWith(QLatin1String("/dev/")) || dev.startsWith(QLatin1String("/dev/loop"))) continue;
        const QString path = v.rootPath();
        if (path == root || path == home || path.startsWith(QLatin1String("/boot")) || path.startsWith(QLatin1String("/efi"))) continue;
        QVariantMap m;
        m[QStringLiteral("name")] = v.displayName().isEmpty() || v.displayName() == path ? QFileInfo(path).fileName() : v.displayName();
        if (m.value(QStringLiteral("name")).toString().isEmpty()) m[QStringLiteral("name")] = path;
        m[QStringLiteral("path")] = path;
        m[QStringLiteral("bytesTotal")] = v.bytesTotal();
        m[QStringLiteral("bytesFree")] = v.bytesAvailable();
        m[QStringLiteral("removable")] = isRemovableSource(path);
        m[QStringLiteral("dcim")] = QDir(path + QStringLiteral("/DCIM")).exists();
        out << m;
    }
    return out;
}

int Backend::rescanFolders() {
    const int offline = m_catalog.rescanFolders();
    m_model.invalidateAll();
    refreshCounts();
    emit foldersChanged();
    setStatus(offline ? tr("%1 folder%2 offline").arg(offline).arg(offline == 1 ? " is" : "s are") : tr("Every folder is online"));
    return offline;
}

QString Backend::relinkFolder(int folderId, const QString &newPathOrUrl) {
    QString newPath = newPathOrUrl;
    if (newPath.startsWith(QLatin1String("file:"))) newPath = QUrl(newPath).toLocalFile();
    newPath = QDir::cleanPath(newPath);
    const QString oldPath = m_catalog.folderPath(folderId);
    if (oldPath.isEmpty()) return tr("no such folder");
    if (!QDir(newPath).exists()) return tr("%1 does not exist").arg(newPath);
    if (!m_catalog.relinkFolder(folderId, newPath)) { setStatus(tr("Could not relink: %1").arg(m_catalog.lastError())); return m_catalog.lastError(); }
    // The engine keeps every history attached by moving its film roll.
    if (m_engine) m_engine->relinkFolder(oldPath, newPath);
    m_catalog.rescanFolders();
    // How many photos actually turned up at the new place.
    int found = 0, total = 0;
    QVariantList folderBinds;
    const QString folderWhere = whereFor(QStringLiteral("folder"), folderId, {}, 0, QStringLiteral("any"), {}, folderBinds);
    for (int id : m_catalog.assetIds(folderWhere, folderBinds)) { ++total; if (QFile::exists(m_catalog.asset(id).path)) ++found; }
    m_model.invalidateAll();
    refresh();
    emit catalogChanged();
    emit foldersChanged();
    emit selectionChanged();
    setStatus(found == total ? tr("Relinked %1 → %2: all %3 photos found").arg(oldPath, newPath).arg(total)
                             : tr("Relinked %1 → %2: %3 of %4 photos found").arg(oldPath, newPath).arg(found).arg(total));
    return QString();
}

// ── tethered capture ────────────────────────────────────────────────────

int Backend::importCapture(const QString &path) {
    if (!m_catalog.isOpen() || !QFile::exists(path)) return 0;
    // One import record per session folder, so Previous Import is the session.
    const QString folder = QFileInfo(path).absolutePath();
    // Kept per catalog: a session's import record belongs to the catalog it was made in.
    if (folder != m_captureFolder || !m_captureImport || m_captureCatalog != m_catalog.path()) {
        m_captureImport = m_catalog.beginImport(folder); m_captureFolder = folder; m_captureCatalog = m_catalog.path();
    }
    const int id = Importer::importFile(m_catalog, path, m_captureImport);
    if (!id) { setStatus(tr("Could not catalogue %1").arg(QFileInfo(path).fileName())); return 0; }
    const bool tagsQueued=!m_autoTagEnabled || m_catalog.queueAutoTags({id});
    if(m_autoTagEnabled && tagsQueued) { m_autoTagTimer.start(0); emit autoTagsChanged(); }
    m_catalog.finishImport(m_captureImport, m_catalog.count(QStringLiteral("import_id=?"), {m_captureImport}));
    m_catalog.rescanFolders();
    // Show the session folder unless the capture is already in view.
    refresh();
    if (m_model.rowOf(id) < 0) setSource(QStringLiteral("folder"), m_catalog.ensureFolder(folder));
    select(id, 0);
    emit catalogChanged();
    emit foldersChanged();
    setStatus(tr("Captured %1 into the catalog").arg(QFileInfo(path).fileName()));
    if(!tagsQueued) setStatus(tr("Photo captured; could not queue subject tags: %1").arg(m_catalog.lastError()));
    return id;
}

// ── albums and smart albums ─────────────────────────────────────────────

bool Backend::renameAlbum(int id, const QString &name) {
    if (id <= 0 || name.trimmed().isEmpty() || !m_catalog.renameAlbum(id, name.trimmed())) return false;
    if ((m_sourceKind == QLatin1String("album") || m_sourceKind == QLatin1String("smart")) && m_sourceId == id) m_sourceTitle = name.trimmed();
    emit catalogChanged(); emit sourceChanged();
    return true;
}

bool Backend::deleteAlbum(int id) {
    if (id <= 0 || !m_catalog.deleteAlbum(id)) return false;
    if ((m_sourceKind == QLatin1String("album") || m_sourceKind == QLatin1String("smart")) && m_sourceId == id) setSource(QStringLiteral("all"));
    setStatus(tr("Album deleted (photos untouched)"));
    emit autoTagSettingsChanged();
    emit catalogChanged();
    return true;
}

void Backend::removeSelectionFromAlbum(int albumId) {
    if (!albumId) return;
    if (!m_catalog.removeFromAlbum(albumId, targets(0))) setStatus(tr("Not removed from the album: %1").arg(m_catalog.lastError()));
    emit catalogChanged();
    if (m_sourceKind == QLatin1String("album") && m_sourceId == albumId) refresh();
}

static QString sqlQuote(const QString &s) { QString q = s; q.replace(QLatin1Char('\''), QStringLiteral("''")); return QLatin1Char('\'') + q + QLatin1Char('\''); }
static QString sqlLike(const QString &s) { return QStringLiteral("LIKE ") + sqlQuote(QLatin1Char('%') + s.trimmed() + QLatin1Char('%')); }

QString Backend::smartRuleSql(const QVariantMap &r) {
    QStringList w;
    const int rating = r.value(QStringLiteral("rating")).toInt();
    if (rating > 0) w << QStringLiteral("rating>=%1").arg(qBound(1, rating, 5));
    const QString flag = r.value(QStringLiteral("flag")).toString();
    if (flag == QLatin1String("pick")) w << QStringLiteral("flag=1");
    else if (flag == QLatin1String("unflagged")) w << QStringLiteral("flag=0");
    else if (flag == QLatin1String("reject")) w << QStringLiteral("flag=-1");
    const QString label = r.value(QStringLiteral("label")).toString();
    if (!label.isEmpty()) w << QStringLiteral("label=%1").arg(sqlQuote(label));
    const QString subject = r.value(QStringLiteral("autoTag")).toString();
    if (!subject.isEmpty()) {
        const auto members=AutoTags::members(subject);
        if (members.isEmpty()) w << QStringLiteral("0=1");
        else {
            QStringList quoted; for(const auto &key:members) quoted << sqlQuote(key);
            w << QStringLiteral("COALESCE(variant_of,id) IN (SELECT asset_id FROM asset_auto_tags WHERE tag IN (%1) AND (decision=1 OR (decision=0 AND confidence>=%2)))")
                .arg(quoted.join(','), QString::number(AutoTags::threshold, 'f', 2));
        }
    }
    const QString keyword = r.value(QStringLiteral("keyword")).toString().trimmed();
    if (!keyword.isEmpty()) w << QStringLiteral("id IN (SELECT asset_id FROM asset_keywords ak JOIN keywords k ON k.id=ak.keyword_id WHERE k.name %1)").arg(sqlLike(keyword));
    const QString text = r.value(QStringLiteral("text")).toString().trimmed();
    if (!text.isEmpty()) w << QStringLiteral("(filename %1 OR path %1)").arg(sqlLike(text));
    const QString camera = r.value(QStringLiteral("camera")).toString().trimmed();
    if (!camera.isEmpty()) w << QStringLiteral("(make %1 OR model %1)").arg(sqlLike(camera));
    const QString lens = r.value(QStringLiteral("lens")).toString().trimmed();
    if (!lens.isEmpty()) w << QStringLiteral("lens %1").arg(sqlLike(lens));
    const QString format = r.value(QStringLiteral("format")).toString().trimmed();
    if (format == QLatin1String("raw")) w << QStringLiteral("is_raw=1");
    else if (!format.isEmpty()) w << QStringLiteral("format=%1").arg(sqlQuote(format.toUpper()));
    const QString edited = r.value(QStringLiteral("edited")).toString();
    if (edited == QLatin1String("yes")) w << QStringLiteral("edited=1"); else if (edited == QLatin1String("no")) w << QStringLiteral("edited=0");
    const QString from = r.value(QStringLiteral("from")).toString().trimmed(), to = r.value(QStringLiteral("to")).toString().trimmed();
    if (QDate::fromString(from, Qt::ISODate).isValid()) w << QStringLiteral("captured_at>=%1").arg(sqlQuote(from));
    if (QDate::fromString(to, Qt::ISODate).isValid()) w << QStringLiteral("captured_at<%1").arg(sqlQuote(QDate::fromString(to, Qt::ISODate).addDays(1).toString(Qt::ISODate)));
    if (r.value(QStringLiteral("stacked")).toString() == QLatin1String("top")) w << QStringLiteral("stack_id IS NOT NULL AND stack_pos=0");
    if (w.isEmpty()) return QStringLiteral("1=1");
    const QString glue = r.value(QStringLiteral("match")).toString() == QLatin1String("any") ? QStringLiteral(" OR ") : QStringLiteral(" AND ");
    return QLatin1Char('(') + w.join(glue) + QLatin1Char(')');
}

QString Backend::smartRuleSummary(const QVariantMap &r) {
    QStringList parts;
    if (const QString subject = r.value(QStringLiteral("autoTag")).toString(); !subject.isEmpty()) parts << tr("automatic tag %1").arg(AutoTags::label(subject));
    if (const int rating = r.value(QStringLiteral("rating")).toInt()) parts << tr("%1+ stars").arg(rating);
    const QString flag = r.value(QStringLiteral("flag")).toString();
    if (!flag.isEmpty()) parts << flag;
    if (const QString l = r.value(QStringLiteral("label")).toString(); !l.isEmpty()) parts << l;
    if (const QString k = r.value(QStringLiteral("keyword")).toString().trimmed(); !k.isEmpty()) parts << tr("keyword %1").arg(k);
    if (const QString t = r.value(QStringLiteral("text")).toString().trimmed(); !t.isEmpty()) parts << tr("name %1").arg(t);
    if (const QString c = r.value(QStringLiteral("camera")).toString().trimmed(); !c.isEmpty()) parts << c;
    if (const QString l = r.value(QStringLiteral("lens")).toString().trimmed(); !l.isEmpty()) parts << l;
    if (const QString f = r.value(QStringLiteral("format")).toString().trimmed(); !f.isEmpty()) parts << f.toUpper();
    if (const QString e = r.value(QStringLiteral("edited")).toString(); !e.isEmpty()) parts << (e == QLatin1String("yes") ? tr("edited") : tr("unedited"));
    if (const QString f = r.value(QStringLiteral("from")).toString().trimmed(); !f.isEmpty()) parts << tr("from %1").arg(f);
    if (const QString t = r.value(QStringLiteral("to")).toString().trimmed(); !t.isEmpty()) parts << tr("to %1").arg(t);
    if (r.value(QStringLiteral("stacked")).toString() == QLatin1String("top")) parts << tr("stack tops");
    if (parts.isEmpty()) return tr("every photo");
    return parts.join(r.value(QStringLiteral("match")).toString() == QLatin1String("any") ? tr(" or ") : tr(", "));
}

int Backend::createSmartAlbum(const QString &name, const QVariantMap &rule) {
    if (name.trimmed().isEmpty()) return 0;
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(rule)).toJson(QJsonDocument::Compact));
    const int id = m_catalog.createAlbum(name.trimmed(), smartRuleSql(rule), json);
    if (id) { setStatus(tr("Smart album \"%1\": %2").arg(name.trimmed(), smartRuleSummary(rule))); emit catalogChanged(); }
    return id;
}

bool Backend::updateSmartAlbum(int id, const QString &name, const QVariantMap &rule) {
    if (id <= 0 || name.trimmed().isEmpty()) return false;
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(rule)).toJson(QJsonDocument::Compact));
    if (!m_catalog.setSmartAlbum(id, name.trimmed(), smartRuleSql(rule), json)) return false;
    if (m_sourceKind == QLatin1String("smart") && m_sourceId == id) { m_sourceTitle = name.trimmed(); emit sourceChanged(); refresh(); }
    emit catalogChanged();
    return true;
}

QVariantMap Backend::smartAlbumRule(int id) const {
    for (const QVariant &a : m_catalog.albums(true)) {
        const QVariantMap m = a.toMap();
        if (m.value(QStringLiteral("id")).toInt() == id) return QJsonDocument::fromJson(m.value(QStringLiteral("json")).toString().toUtf8()).object().toVariantMap();
    }
    return QVariantMap();
}

int Backend::smartRuleCount(const QVariantMap &rule) const {
    return m_catalog.count(smartRuleSql(rule) + QStringLiteral(" AND flag<>-1"));
}

// ── help ────────────────────────────────────────────────────────────────
// The pages live in src/help/*.md and ship in the resources; this table is
// their order and grouping. Every DockHeader's helpSection must name one.
QVariantList Backend::helpSections() const {
    struct S { const char *id, *title, *group; };
    static const S sections[] = {
        {"start", "Welcome and workflow", "Start"},
        {"shortcuts", "Keyboard", "Start"},
        {"library", "Library", "Library"},
        {"import", "Importing", "Library"},
        {"culling", "Culling and rating", "Library"},
        {"organising", "Organising", "Library"},
        {"develop", "Develop", "Develop"},
        {"develop-prepare", "Prepare", "Develop"},
        {"develop-basic", "Light & tone", "Develop"},
        {"develop-colour", "Colour & look", "Develop"},
        {"develop-grading", "Grading", "Develop"},
        {"develop-curve", "Curve", "Develop"},
        {"develop-detail", "Detail", "Develop"},
        {"develop-film", "Effects and film", "Develop"},
        {"develop-lens", "Lens & geometry", "Develop"},
        {"develop-local", "Masks", "Develop"},
        {"develop-retouch", "Retouch", "Develop"},
        {"capture", "Capture", "Capture"},
        {"output", "Output", "Output"},
        {"print", "Print and contact sheets", "Output"},
        {"preferences", "Preferences and colour", "Settings"},
    };
    QVariantList out;
    for (const S &s : sections)
        out << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(s.id)}, {QStringLiteral("title"), tr(s.title)}, {QStringLiteral("group"), tr(s.group)}};
    return out;
}

QString Backend::helpText(const QString &id) const {
    static const QRegularExpression safe(QStringLiteral("^[a-z-]+$"));
    if (!safe.match(id).hasMatch()) return QString();
    QFile f(QStringLiteral(":/help/") + id + QStringLiteral(".md"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}
