#include "catalogbackup.h"
#include <QDebug>
#include <QLockFile>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <memory>

namespace CatalogBackup {
namespace {
using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
Database open(const QString &path, int flags, QString *error) {
    sqlite3 *raw = nullptr;
    const int rc = sqlite3_open_v2(path.toUtf8().constData(), &raw, flags, nullptr);
    Database db(raw, sqlite3_close);
    if (rc != SQLITE_OK) { fail(error, QStringLiteral("%1: %2").arg(path, QString::fromUtf8(raw ? sqlite3_errmsg(raw) : "cannot open database"))); return {nullptr, sqlite3_close}; }
    sqlite3_busy_timeout(raw, 3000);
    return db;
}
bool integrity(const QString &path, QString *error) {
    auto db = open(path, SQLITE_OPEN_READONLY, error);
    if (!db) return false;
    sqlite3_stmt *statement = nullptr;
    bool ok = sqlite3_prepare_v2(db.get(), "PRAGMA integrity_check", -1, &statement, nullptr) == SQLITE_OK;
    if (ok) ok = sqlite3_step(statement) == SQLITE_ROW && QByteArray(reinterpret_cast<const char *>(sqlite3_column_text(statement, 0))) == "ok";
    sqlite3_finalize(statement);
    return ok || fail(error, QStringLiteral("Database integrity check failed: %1").arg(path));
}
bool catalogOnlySafe(const QString &path, QString *error) {
    auto db = open(path, SQLITE_OPEN_READONLY, error);
    if (!db) return false;
    sqlite3_stmt *statement = nullptr;
    // All OmaRAW schemas have this flag. Fail closed for an unknown or
    // damaged catalog rather than label missing Develop history complete.
    const int rc = sqlite3_prepare_v2(db.get(), "SELECT 1 FROM assets WHERE edited != 0 LIMIT 1", -1, &statement, nullptr);
    const int result = rc == SQLITE_OK ? sqlite3_step(statement) : SQLITE_ERROR;
    sqlite3_finalize(statement);
    if (result == SQLITE_DONE) return true;
    return fail(error, result == SQLITE_ROW
        ? QStringLiteral("The catalog contains Develop edits but engine-library.db is missing; restore that database before making a complete backup")
        : QStringLiteral("Cannot verify that this catalog is safe to back up without an engine database"));
}
QString digest(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? QString::fromLatin1(hash.result().toHex()) : QString();
}
const QStringList allowed = {QStringLiteral("catalog.db"), QStringLiteral("engine-library.db"),
    QStringLiteral("engine-config/data.db"), QStringLiteral("engine-config/darktablerc"),
    QStringLiteral("engine-library.db.undo-groups.json")};
bool managedMask(const QString &path) {
    static const QRegularExpression pattern(QStringLiteral("^masks/[0-9a-f]{64}\\.png$"));
    return pattern.match(path).hasMatch();
}
bool managedRepair(const QString &path) {
    static const QRegularExpression pattern(QStringLiteral("^repairs/[0-9a-f]{64}\\.orp$"));
    return pattern.match(path).hasMatch();
}
bool readManifest(const QString &path, QJsonObject *object, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return fail(error, QStringLiteral("Cannot read the backup manifest"));
    *object = QJsonDocument::fromJson(file.readAll()).object();
    if (object->value(QStringLiteral("omarawBackup")).toInt() != 1) return fail(error, QStringLiteral("This is not a supported OmaRAW backup"));
    return true;
}
bool publish(QTemporaryDir &staging, const QString &destination, QString *error) {
    if (QFileInfo::exists(destination) || !QDir().rename(staging.path(), destination))
        return fail(error, QStringLiteral("Cannot create %1; choose a new destination").arg(destination));
    staging.setAutoRemove(false);
    return true;
}
}

QString engineLibrary(const QString &catalog) { return QFileInfo(catalog).absolutePath() + QStringLiteral("/engine-library.db"); }
QString sharedEngineConfig(const QString &configBase) {
    // OmaRAW's own folder, beside omaraw.conf. Versions before 2026-09-24
    // used <config>/engine, a name any program could claim; that folder is
    // moved here once, when nothing has it open, and a link is left in its
    // place so an older build kept for rollback still finds the same data.
    const QString current = configBase + QStringLiteral("/omaraw/engine"), former = configBase + QStringLiteral("/engine");
    if (QFileInfo::exists(current)) return current;
    const QFileInfo was(former);
    // A link there is either the one left by the move (it leads here) or the
    // user's own, to a folder they keep elsewhere (synced, in dotfiles):
    // that one is used where it points, never moved.
    if (was.isSymLink())
        return was.exists() && was.canonicalFilePath() != QFileInfo(current).absoluteFilePath() && QFileInfo(was.canonicalFilePath()).isDir() ? former : current;
    if (!was.isDir()) return current;
    if (!QFileInfo::exists(former + QStringLiteral("/data.db")) && !QFileInfo::exists(former + QStringLiteral("/darktablerc")))
        return current;   // not ours: leave it alone
    // Two OmaRAWs starting together (two command-line runs) must not both
    // move it: one does, the other waits and then finds it moved.
    if (!QDir().mkpath(configBase + QStringLiteral("/omaraw"))) return former;
    QLockFile moving(configBase + QStringLiteral("/omaraw/.engine-move-lock"));
    moving.setStaleLockTime(30000);
    if (!moving.tryLock(10000)) return QFileInfo::exists(current) ? current : former;
    if (QFileInfo::exists(current)) return current;
    const QFileInfo still(former);
    if (still.isSymLink() || !still.isDir()) return current;
    {
        QLockFile inUse(former + QStringLiteral("/.omaraw-lock"));
        inUse.setStaleLockTime(0);
        if (!inUse.tryLock(0)) return former;   // an older OmaRAW has it open; move it next time
    }
    QFile::remove(former + QStringLiteral("/.omaraw-lock"));
    if (!QDir().rename(former, current)) return former;
    if (!QFile::link(current, former)) qWarning() << "engine settings moved to" << current << "but no link was left at" << former;
    return current;
}

QString engineConfig(const QString &catalog) {
    const QString local = QFileInfo(catalog).absolutePath() + QStringLiteral("/engine-config");
    return QDir(local).exists() ? local : sharedEngineConfig(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation));
}

bool snapshot(const QString &source, const QString &destination, QString *error) {
    if (error) error->clear();
    if (QFileInfo::exists(destination)) return fail(error, QStringLiteral("Backup destination already exists: %1").arg(destination));
    auto src = open(source, SQLITE_OPEN_READONLY, error);
    if (!src) return false;
    auto dst = open(destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXCLUSIVE, error);
    if (!dst) return false;
    // SQLite's backup API includes committed WAL pages, unlike copying the
    // .db file. The caller holds OmaRAW's writers idle across the bundle.
    sqlite3_backup *backup = sqlite3_backup_init(dst.get(), "main", src.get(), "main");
    const int rc = backup ? sqlite3_backup_step(backup, -1) : SQLITE_ERROR;
    const int finished = backup ? sqlite3_backup_finish(backup) : SQLITE_ERROR;
    if (rc != SQLITE_DONE || finished != SQLITE_OK) {
        fail(error, QStringLiteral("Snapshot of %1 failed: %2").arg(source, QString::fromUtf8(sqlite3_errmsg(dst.get()))));
        dst.reset(); QFile::remove(destination); return false;
    }
    dst.reset();
    return integrity(destination, error);
}

QString create(const QString &catalog, const QString &destination, QString *error) {
    if (error) error->clear();
    if (QFileInfo::exists(destination)) { fail(error, QStringLiteral("Backup destination already exists")); return {}; }
    const QString parent = QFileInfo(destination).absolutePath();
    if (!QDir().mkpath(parent)) { fail(error, QStringLiteral("Cannot create the backups folder")); return {}; }
    QTemporaryDir staging(parent + QStringLiteral("/.backup-XXXXXX"));
    if (!staging.isValid()) { fail(error, QStringLiteral("Cannot create a temporary backup")); return {}; }
    QDir().mkpath(staging.path() + QStringLiteral("/engine-config"));
    const bool engine = QFileInfo::exists(engineLibrary(catalog));
    if (!engine && !catalogOnlySafe(catalog, error)) return {};
    const QString config = engineConfig(catalog);
    QStringList sources = {catalog};
    if (engine) sources << engineLibrary(catalog) << config + QStringLiteral("/data.db");
    // Engine configuration is optional for an engine never shut down yet.
    if (engine && QFileInfo::exists(config + QStringLiteral("/darktablerc"))) sources << config + QStringLiteral("/darktablerc");
    QJsonArray files;
    for (int i = 0; i < sources.size(); ++i) {
        const QString target = staging.path() + QLatin1Char('/') + allowed[i];
        const bool ok = i < 3 ? snapshot(sources[i], target, error) : QFile::copy(sources[i], target);
        if (!ok) { if (error && error->isEmpty()) *error = QStringLiteral("Could not copy engine configuration"); return {}; }
        const QString hash = digest(target);
        if (hash.isEmpty()) { fail(error, QStringLiteral("Could not checksum %1").arg(target)); return {}; }
        files.append(QJsonObject{{QStringLiteral("path"), allowed[i]}, {QStringLiteral("sha256"), hash}});
    }
    if(engine) {
        // Grouped Undo actions and the Lab master slider's history are saved
        // outside SQLite. They belong with the Develop database on restore.
        // Older catalogs and backups may not have this state yet.
        const QString groupName = QStringLiteral("engine-library.db.undo-groups.json");
        const QString groupSource = engineLibrary(catalog) + QStringLiteral(".undo-groups.json");
        if (QFileInfo::exists(groupSource)) {
            const QString target = staging.path() + QLatin1Char('/') + groupName;
            if (!QFile::copy(groupSource, target)) { fail(error, QStringLiteral("Could not back up Develop undo state")); return {}; }
            const QString hash = digest(target);
            if (hash.isEmpty()) { fail(error, QStringLiteral("Could not checksum Develop undo state")); return {}; }
            files.append(QJsonObject{{QStringLiteral("path"), groupName}, {QStringLiteral("sha256"), hash}});
        }
        const QString repairsRoot=QFileInfo(catalog).absolutePath()+QStringLiteral("/repairs");
        // History includes disabled and undone repairs: they must survive a
        // backup too. A missing folder must not become an apparently complete backup.
        auto db=open(engineLibrary(catalog),SQLITE_OPEN_READONLY,error);
        if(!db) return {};
        sqlite3_stmt *repairs=nullptr;
        sqlite3_stmt *schema=nullptr;
        sqlite3_prepare_v2(db.get(),"SELECT 1 FROM sqlite_master WHERE type='table' AND name='history'",-1,&schema,nullptr);
        const bool hasHistory=schema && sqlite3_step(schema)==SQLITE_ROW;
        sqlite3_finalize(schema);
        if(hasHistory && sqlite3_prepare_v2(db.get(),"SELECT op_params FROM history WHERE operation='omarawrepair'",-1,&repairs,nullptr)!=SQLITE_OK) {
            fail(error,QStringLiteral("Cannot inspect saved AI repairs"));return {};
        }
        QSet<QString> required;
        while(repairs && sqlite3_step(repairs)==SQLITE_ROW) {
            const QByteArray blob(static_cast<const char*>(sqlite3_column_blob(repairs,0)),sqlite3_column_bytes(repairs,0));
            const QString key=QString::fromLatin1(blob.left(64).split('\0').first());
            if(!key.isEmpty()) required.insert(key);
        }
        sqlite3_finalize(repairs);
        for(const auto &key:required) if(!managedRepair("repairs/"+key+".orp") || !QFileInfo::exists(repairsRoot+"/"+key+".orp")) {
            fail(error,QStringLiteral("A saved AI repair is missing; restore the repairs folder before backing up"));return {};
        }
        const QDir repairsDir(repairsRoot);
        for(const auto &info:repairsDir.entryInfoList({"*.orp"},QDir::Files,QDir::Name)) {
            const QString name="repairs/"+info.fileName();
            if(!managedRepair(name) || info.isSymLink() || files.size()>30000) {fail(error,QStringLiteral("Invalid saved AI repair"));return {};}
            QFile repair(info.absoluteFilePath());
            if(!repair.open(QIODevice::ReadOnly)) {fail(error,QStringLiteral("Cannot read saved AI repair"));return {};}
            const QByteArray header=repair.read(96),parent=header.mid(32,64).split('\0').first();
            if(header.size()!=96 || !header.startsWith("OREPAIR1") || (!parent.isEmpty() && !QFileInfo::exists(repairsRoot+"/"+QString::fromLatin1(parent)+".orp"))) {
                fail(error,QStringLiteral("The saved AI repair history is incomplete"));return {};
            }
            QDir().mkpath(staging.path()+"/repairs");
            const QString target=staging.path()+"/"+name;
            if(!QFile::copy(info.absoluteFilePath(),target)) {fail(error,QStringLiteral("Cannot back up saved AI repair"));return {};}
            const QString hash=digest(target);
            if(hash!=info.completeBaseName()) {fail(error,QStringLiteral("Saved AI repair checksum failed"));return {};}
            files.append(QJsonObject{{"path",name},{"sha256",hash}});
        }
        const QDir masks(QFileInfo(catalog).absolutePath()+QStringLiteral("/masks"));
        for(const auto &info:masks.entryInfoList({QStringLiteral("*.png")},QDir::Files,QDir::Name)) {
            const QString name=QStringLiteral("masks/")+info.fileName();
            if(!managedMask(name) || info.isSymLink()) {fail(error,QStringLiteral("Invalid managed mask: %1").arg(info.fileName()));return {};}
            if(files.size()>30000){fail(error,QStringLiteral("Too many managed mask files"));return {};}
            QDir().mkpath(staging.path()+QStringLiteral("/masks"));
            const QString target=staging.path()+QLatin1Char('/')+name;
            if(!QFile::copy(info.absoluteFilePath(),target)){fail(error,QStringLiteral("Could not back up imported mask"));return {};}
            const QString hash=digest(target);if(hash.isEmpty()){fail(error,QStringLiteral("Could not checksum imported mask"));return {};}
            files.append(QJsonObject{{QStringLiteral("path"),name},{QStringLiteral("sha256"),hash}});
        }
    }
    QJsonObject manifest{{QStringLiteral("omarawBackup"), 1}, {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("catalogSource"), QFileInfo(catalog).absoluteFilePath()}, {QStringLiteral("engine"), engine}, {QStringLiteral("files"), files}};
    QSaveFile file(staging.path() + QStringLiteral("/manifest.json"));
    const QByteArray bytes = QJsonDocument(manifest).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { fail(error, QStringLiteral("Could not save the backup manifest")); return {}; }
    if (!verify(file.fileName(), error) || !publish(staging, destination, error)) return {};
    return destination + QStringLiteral("/manifest.json");
}

static bool verifyFiles(const QString &manifest, const QJsonObject &object, QString *error) {
    const QString root = QFileInfo(manifest).absolutePath();
    QSet<QString> found;
    for (const QJsonValue &entry : object.value(QStringLiteral("files")).toArray()) {
        const QJsonObject row = entry.toObject();
        const QString name = row.value(QStringLiteral("path")).toString();
        const QString path = root + QLatin1Char('/') + name;
        const QFileInfo info(path);
        if ((!allowed.contains(name) && !managedMask(name) && !managedRepair(name)) || found.contains(name) || !info.isFile() || info.isSymLink()
            || !info.canonicalFilePath().startsWith(QFileInfo(root).canonicalFilePath() + QLatin1Char('/')))
            return fail(error, QStringLiteral("Invalid or missing backup file: %1").arg(name));
        found.insert(name);
        const QString hash = digest(path);
        if (hash.isEmpty() || hash != row.value(QStringLiteral("sha256")).toString()) return fail(error, QStringLiteral("Backup checksum failed: %1").arg(name));
        if (name.endsWith(QLatin1String(".db")) && !integrity(path, error)) return false;
    }
    if (!found.contains(QStringLiteral("catalog.db")) || (object.value(QStringLiteral("engine")).toBool()
        && (!found.contains(QStringLiteral("engine-library.db")) || !found.contains(QStringLiteral("engine-config/data.db")))))
        return fail(error, QStringLiteral("The backup is incomplete"));
    if (!object.value(QStringLiteral("engine")).toBool() && found.size() != 1) return fail(error, QStringLiteral("The backup manifest disagrees with its contents"));
    return true;
}

bool verify(const QString &manifest, QString *error) {
    if (error) error->clear();
    QJsonObject object;
    return readManifest(manifest, &object, error) && verifyFiles(manifest, object, error);
}

QString restore(const QString &manifest, const QString &destination, QString *error) {
    if (error) error->clear();
    QJsonObject object;
    // Keep the exact manifest that was validated; re-reading a changed
    // manifest after verification could change the allowed restore paths.
    if (!readManifest(manifest, &object, error) || !verifyFiles(manifest, object, error)) return {};
    if (QFileInfo::exists(destination)) { fail(error, QStringLiteral("Restore needs a new folder; existing catalogs are never overwritten")); return {}; }
    const QString parent = QFileInfo(destination).absolutePath();
    if (!QDir().mkpath(parent)) { fail(error, QStringLiteral("Cannot create the restore folder")); return {}; }
    QTemporaryDir staging(parent + QStringLiteral("/.restore-XXXXXX"));
    if (!staging.isValid()) { fail(error, QStringLiteral("Cannot stage the restore")); return {}; }
    QDir().mkpath(staging.path() + QStringLiteral("/engine-config"));
    for (const QJsonValue &entry : object.value(QStringLiteral("files")).toArray()) {
        const QJsonObject row = entry.toObject();
        const QString name = row.value(QStringLiteral("path")).toString();
        const QString target = staging.path() + QLatin1Char('/') + name;
        if(managedMask(name))QDir().mkpath(staging.path()+QStringLiteral("/masks"));
        if(managedRepair(name))QDir().mkpath(staging.path()+QStringLiteral("/repairs"));
        if (!QFile::copy(QFileInfo(manifest).absolutePath() + QLatin1Char('/') + name, target)
            || digest(target) != row.value(QStringLiteral("sha256")).toString()) {
            fail(error, QStringLiteral("Could not restore and verify %1").arg(name)); return {};
        }
    }
    if (!publish(staging, destination, error)) return {};
    return destination + QStringLiteral("/catalog.db");
}
}
