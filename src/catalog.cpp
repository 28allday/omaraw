#include "catalog.h"
#include "autotag.h"
#include "catalogbackup.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <functional>
#include <QSet>
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>
#include <QUuid>

Catalog::Catalog(const QString &connection) : m_connection(connection) {}

Catalog::~Catalog() { close(); }

bool Catalog::open(const QString &path) {
    close();
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return false;
    }
    m_path = path;
    const int v = currentVersion();
    if (v > schemaVersion()) {
        m_error = QStringLiteral("This catalog needs a newer OmaRAW version (schema %1; supported %2)").arg(v).arg(schemaVersion());
        close();
        return false;
    }
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    // An older catalog is copied before the schema moves on, so a migration
    // that goes wrong can never be the only copy of someone's edits.
    if (v > 0 && v < schemaVersion()) {
        const QString destination = backupPath(QStringLiteral("pre-v%1").arg(v));
        const QString library = CatalogBackup::engineLibrary(path);
        if (QFileInfo::exists(library) && QFileInfo::exists(CatalogBackup::engineConfig(path) + QStringLiteral("/data.db"))) {
            m_lastBackup = CatalogBackup::create(path, destination + QStringLiteral(".omaraw-backup"), &m_error);
            if (m_lastBackup.isEmpty()) { close(); return false; }
        } else {
            // A catalog brought from another machine or account: the engine's
            // own settings database is not here yet (it is made when the engine
            // first starts), so the full bundle cannot be made. The catalog and
            // its Develop history are what matter; copy both.
            if (!backupTo(destination)) { close(); return false; }
            if (QFileInfo::exists(library)
                && !CatalogBackup::snapshot(library, destination + QStringLiteral(".engine-library.db"), &m_error)) { close(); return false; }
        }
    }
    if (!migrate()) { close(); return false; }
    // Photos waiting for their automatic first edit. A table of its own,
    // made when missing rather than by a schema step, so a catalog opened here
    // still opens in the builds kept for rollback (they refuse a newer schema
    // but never look at this table).
    exec(QStringLiteral("CREATE TABLE IF NOT EXISTS auto_pending (asset_id INTEGER PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE)"));
    // Additive table: retained rollback builds can still open the catalog.
    if (!exec(QStringLiteral("CREATE TABLE IF NOT EXISTS ai_versions ("
            "sequence INTEGER PRIMARY KEY AUTOINCREMENT, "
            "asset_id INTEGER NOT NULL UNIQUE REFERENCES assets(id) ON DELETE CASCADE, "
            "family TEXT NOT NULL, source_id INTEGER REFERENCES assets(id) ON DELETE SET NULL, "
            "operation TEXT NOT NULL)")) ||
        !exec(QStringLiteral("CREATE INDEX IF NOT EXISTS ai_versions_family ON ai_versions(family)"))) {
        close(); return false;
    }
    if (!repairAiImports()) { close(); return false; }
    return true;
}

bool Catalog::repairAiImports() {
    // Older AI saves created a one-photo import, replacing the whole shoot in
    // Previous Import. Rejoin those versions to the original's import without
    // changing their files, edits, stacks or identities.
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT a.id,a.import_id,original.import_id FROM ai_versions v "
            "JOIN assets a ON a.id=v.asset_id "
            "JOIN ai_versions root ON root.family=v.family AND root.operation='original' "
            "JOIN assets original ON original.id=root.asset_id "
            "JOIN imports i ON i.id=original.import_id "
            "WHERE v.operation<>'original' AND a.import_id IS NOT original.import_id"))) {
        m_error = query.lastError().text(); return false;
    }
    struct Move { int id, oldImport, import; };
    QVector<Move> moves;
    while (query.next()) moves << Move{query.value(0).toInt(), query.value(1).toInt(), query.value(2).toInt()};
    query.finish();
    if (moves.isEmpty()) return true;
    if (!backupTo(backupPath(QStringLiteral("pre-ai-import-repair")))) return false;
    if (!m_db.transaction()) { m_error = m_db.lastError().text(); return false; }
    auto fail = [&] { m_error = query.lastError().text(); m_db.rollback(); return false; };
    QSet<int> oldImports;
    for (const auto &move : moves) {
        query.prepare(QStringLiteral("UPDATE assets SET import_id=? WHERE id=?"));
        query.addBindValue(move.import); query.addBindValue(move.id);
        if (!query.exec()) return fail();
        if (move.oldImport) oldImports.insert(move.oldImport);
    }
    // Keep the records, but empty AI-only batches must not remain Previous Import.
    for (int id : oldImports) {
        query.prepare(QStringLiteral("UPDATE imports SET count=(SELECT count(*) FROM assets WHERE import_id=?) WHERE id=?"));
        query.addBindValue(id); query.addBindValue(id);
        if (!query.exec()) return fail();
    }
    if (!m_db.commit()) { m_error = m_db.lastError().text(); m_db.rollback(); return false; }
    return true;
}

bool Catalog::addAiVersion(int sourceId, int resultId, const QString &operation) {
    if (sourceId == resultId || !asset(sourceId).id || !asset(resultId).id ||
        (operation != "denoise" && operation != "remove")) return false;
    if (!m_db.transaction()) return false;
    QSqlQuery query(m_db);
    auto fail = [&] { m_error = query.lastError().text(); m_db.rollback(); return false; };
    query.prepare("SELECT family FROM ai_versions WHERE asset_id=?"); query.addBindValue(sourceId);
    if (!query.exec()) return fail();
    QString family;
    if (query.next()) family = query.value(0).toString();
    else {
        family = QUuid::createUuid().toString(QUuid::WithoutBraces);
        query.prepare("INSERT INTO ai_versions(asset_id,family,operation) VALUES(?,?,'original')");
        query.addBindValue(sourceId); query.addBindValue(family);
        if (!query.exec()) return fail();
    }
    query.prepare("INSERT INTO ai_versions(asset_id,family,source_id,operation) VALUES(?,?,?,?)");
    query.addBindValue(resultId); query.addBindValue(family); query.addBindValue(sourceId); query.addBindValue(operation);
    if (!query.exec()) return fail();
    return m_db.commit();
}

QVariantList Catalog::aiVersions(int assetId) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT a.id,a.path,a.filename,a.variant,v.operation,v.source_id "
                  "FROM ai_versions v JOIN assets a ON a.id=v.asset_id "
                  "WHERE v.family=(SELECT family FROM ai_versions WHERE asset_id=?) ORDER BY v.sequence");
    query.addBindValue(assetId);
    QVariantList result;
    if (query.exec()) while (query.next()) result << QVariantMap{
        {"id",query.value(0)}, {"path",query.value(1)}, {"filename",query.value(2)},
        {"variant",query.value(3)}, {"operation",query.value(4)}, {"sourceId",query.value(5)}};
    return result;
}

void Catalog::setAutoPending(const QVector<int> &ids) {
    if (ids.isEmpty()) return;
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO auto_pending(asset_id) VALUES(?)"));
    for (int id : ids) { q.addBindValue(id); q.exec(); }
    m_db.commit();
}

bool Catalog::isAutoPending(int id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM auto_pending WHERE asset_id=?"));
    q.addBindValue(id);
    return q.exec() && q.next();
}

void Catalog::clearAutoPending(int id) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM auto_pending WHERE asset_id=?"));
    q.addBindValue(id);
    q.exec();
}

QString Catalog::backupDir() const {
    return QFileInfo(m_path).absolutePath() + QStringLiteral("/backups");
}

QString Catalog::backupPath(const QString &kind) const {
    return backupDir() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + QLatin1Char('-') + kind + QLatin1Char('-')
         + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) + QStringLiteral(".db");
}

bool Catalog::backupTo(const QString &dest) {
    if (!isOpen() || dest.isEmpty()) return false;
    if (QFileInfo(dest).absoluteFilePath() == QFileInfo(m_path).absoluteFilePath()
        || (!QFileInfo(dest).canonicalFilePath().isEmpty()
            && QFileInfo(dest).canonicalFilePath() == QFileInfo(m_path).canonicalFilePath())) {
        m_error = QStringLiteral("The backup destination is the open catalog");
        return false;
    }
    QDir().mkpath(QFileInfo(dest).absolutePath());
    // VACUUM accepts an empty file. Build the new copy beside its destination
    // before replacing anything: a failed snapshot must retain the last backup.
    QTemporaryFile staged(QFileInfo(dest).absolutePath() + QStringLiteral("/.catalog-backup-XXXXXX"));
    if (!staged.open()) { m_error = staged.errorString(); return false; }
    const QString stagingPath = staged.fileName();
    staged.close();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("VACUUM INTO ?"));
    q.addBindValue(stagingPath);
    if (!q.exec()) { m_error = q.lastError().text(); qWarning() << "catalog: backup failed:" << m_error; return false; }
    if (!staged.open()) { m_error = staged.errorString(); return false; }
    QSaveFile output(dest);
    if (!output.open(QIODevice::WriteOnly)) { m_error = output.errorString(); return false; }
    while (!staged.atEnd()) {
        const QByteArray bytes = staged.read(1024 * 1024);
        if (staged.error() != QFile::NoError || output.write(bytes) != bytes.size()) {
            m_error = staged.error() != QFile::NoError ? staged.errorString() : output.errorString();
            return false;
        }
    }
    if (!output.commit()) { m_error = output.errorString(); return false; }
    m_lastBackup = dest;
    return true;
}

QString Catalog::integrityCheck() const {
    QStringList out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("PRAGMA integrity_check"))) while (q.next()) { const QString l = q.value(0).toString(); if (l != QLatin1String("ok")) out << l; }
    else out << q.lastError().text();
    if (q.lastError().isValid() && !out.contains(q.lastError().text())) out << q.lastError().text();
    if (q.exec(QStringLiteral("PRAGMA foreign_key_check")))
        while (q.next()) out << QStringLiteral("%1 row %2 points at a missing %3 row").arg(q.value(0).toString()).arg(q.value(1).toLongLong()).arg(q.value(2).toString());
    if (q.lastError().isValid()) out << q.lastError().text();
    return out.isEmpty() ? QStringLiteral("ok") : out.join(QLatin1Char('\n'));
}

void Catalog::close() {
    if (m_db.isValid()) {
        if (m_db.isOpen()) m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
    }
    m_path.clear();
}

bool Catalog::exec(const QString &sql) {
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        m_error = q.lastError().text();
        qWarning() << "catalog:" << m_error << "in" << sql.left(80);
        return false;
    }
    return true;
}

int Catalog::currentVersion() const {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) return 0;
    return q.value(0).toInt();
}

bool Catalog::migrate() {
    const int v = currentVersion();
    if (v >= schemaVersion()) return true;
    // A table rebuild (v4) must run with foreign keys off, and that pragma
    // is a no-op inside a transaction; restore it whatever happens.
    const bool rebuild = v > 0 && v < 4;
    if (rebuild) exec(QStringLiteral("PRAGMA foreign_keys=OFF"));
    struct Restore { Catalog *c; bool on; ~Restore() { if (on) c->exec(QStringLiteral("PRAGMA foreign_keys=ON")); } } restore{this, rebuild};
    if (!m_db.transaction()) { m_error = m_db.lastError().text(); return false; }
    bool ok = true;
    if (v < 1) {
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE folders ("
            " id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, parent_id INTEGER,"
            " name TEXT NOT NULL, online INTEGER NOT NULL DEFAULT 1)"));
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE assets ("
            " id INTEGER PRIMARY KEY, folder_id INTEGER NOT NULL REFERENCES folders(id),"
            " path TEXT NOT NULL UNIQUE, filename TEXT NOT NULL, format TEXT NOT NULL,"
            " is_raw INTEGER NOT NULL DEFAULT 0, size INTEGER, mtime INTEGER,"
            " width INTEGER, height INTEGER, orientation INTEGER DEFAULT 1,"
            " captured_at TEXT, imported_at TEXT NOT NULL,"
            " make TEXT, model TEXT, lens TEXT, focal_length REAL, aperture REAL,"
            " shutter REAL, iso INTEGER, bit_depth INTEGER,"
            " rating INTEGER NOT NULL DEFAULT 0, flag INTEGER NOT NULL DEFAULT 0,"
            " label TEXT NOT NULL DEFAULT '', gps_lat REAL, gps_lon REAL,"
            " copyright TEXT, creator TEXT, edited INTEGER NOT NULL DEFAULT 0,"
            " import_id INTEGER)"));
        ok = ok && exec(QStringLiteral("CREATE INDEX assets_folder ON assets(folder_id)"));
        ok = ok && exec(QStringLiteral("CREATE INDEX assets_captured ON assets(captured_at)"));
        ok = ok && exec(QStringLiteral("CREATE INDEX assets_rating ON assets(rating)"));
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE keywords (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,"
            " parent_id INTEGER)"));
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE asset_keywords (asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,"
            " keyword_id INTEGER NOT NULL REFERENCES keywords(id) ON DELETE CASCADE,"
            " PRIMARY KEY (asset_id, keyword_id))"));
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE albums (id INTEGER PRIMARY KEY, name TEXT NOT NULL, parent_id INTEGER,"
            " smart_rule TEXT, sort INTEGER NOT NULL DEFAULT 0)"));
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE album_assets (album_id INTEGER NOT NULL REFERENCES albums(id) ON DELETE CASCADE,"
            " asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,"
            " position INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (album_id, asset_id))"));
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE imports (id INTEGER PRIMARY KEY, started_at TEXT NOT NULL,"
            " finished_at TEXT, source TEXT, count INTEGER NOT NULL DEFAULT 0)"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=1"));
    }
    if (v < 2) {
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN edit_rev INTEGER NOT NULL DEFAULT 0"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=2"));
    }
    if (v < 3) {
        ok = ok && exec(QStringLiteral("CREATE TABLE presets (id INTEGER PRIMARY KEY, name TEXT NOT NULL,"
                                       " params TEXT NOT NULL, created_at TEXT NOT NULL)"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=3"));
    }
    if (v < 4) {
        // Variants share a path, so UNIQUE(path) becomes UNIQUE(path, variant).
        // SQLite cannot drop a constraint: rebuild the table. Foreign keys
        // are off for the duration (set before the transaction) so the
        // DROP does not cascade into keywords and albums.
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE assets_v4 ("
            " id INTEGER PRIMARY KEY, folder_id INTEGER NOT NULL REFERENCES folders(id),"
            " path TEXT NOT NULL, filename TEXT NOT NULL, format TEXT NOT NULL,"
            " is_raw INTEGER NOT NULL DEFAULT 0, size INTEGER, mtime INTEGER,"
            " width INTEGER, height INTEGER, orientation INTEGER DEFAULT 1,"
            " captured_at TEXT, imported_at TEXT NOT NULL,"
            " make TEXT, model TEXT, lens TEXT, focal_length REAL, aperture REAL,"
            " shutter REAL, iso INTEGER, bit_depth INTEGER,"
            " rating INTEGER NOT NULL DEFAULT 0, flag INTEGER NOT NULL DEFAULT 0,"
            " label TEXT NOT NULL DEFAULT '', gps_lat REAL, gps_lon REAL,"
            " copyright TEXT, creator TEXT, edited INTEGER NOT NULL DEFAULT 0,"
            " import_id INTEGER, edit_rev INTEGER NOT NULL DEFAULT 0,"
            " variant INTEGER NOT NULL DEFAULT 0,"
            " variant_of INTEGER REFERENCES assets(id) ON DELETE CASCADE,"
            " variant_name TEXT NOT NULL DEFAULT '',"
            " UNIQUE(path, variant))"));
        ok = ok && exec(QStringLiteral(
            "INSERT INTO assets_v4 (id, folder_id, path, filename, format, is_raw, size, mtime, width, height,"
            " orientation, captured_at, imported_at, make, model, lens, focal_length, aperture, shutter, iso,"
            " bit_depth, rating, flag, label, gps_lat, gps_lon, copyright, creator, edited, import_id, edit_rev)"
            " SELECT id, folder_id, path, filename, format, is_raw, size, mtime, width, height,"
            " orientation, captured_at, imported_at, make, model, lens, focal_length, aperture, shutter, iso,"
            " bit_depth, rating, flag, label, gps_lat, gps_lon, copyright, creator, edited, import_id, edit_rev"
            " FROM assets"));
        ok = ok && exec(QStringLiteral("DROP TABLE assets"));
        ok = ok && exec(QStringLiteral("ALTER TABLE assets_v4 RENAME TO assets"));
        ok = ok && exec(QStringLiteral("CREATE INDEX assets_folder ON assets(folder_id)"));
        ok = ok && exec(QStringLiteral("CREATE INDEX assets_captured ON assets(captured_at)"));
        ok = ok && exec(QStringLiteral("CREATE INDEX assets_rating ON assets(rating)"));
        ok = ok && exec(QStringLiteral("CREATE INDEX assets_variant_of ON assets(variant_of)"));
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE snapshots (id INTEGER PRIMARY KEY,"
            " asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,"
            " name TEXT NOT NULL, created_at TEXT NOT NULL, history_end INTEGER NOT NULL DEFAULT 0,"
            " params TEXT NOT NULL, preview BLOB)"));
        ok = ok && exec(QStringLiteral("CREATE INDEX snapshots_asset ON snapshots(asset_id)"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=4"));
    }
    if (v < 5) {
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN stack_id INTEGER"));
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN stack_pos INTEGER NOT NULL DEFAULT 0"));
        ok = ok && exec(QStringLiteral("CREATE INDEX assets_stack ON assets(stack_id)"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=5"));
    }
    if (v < 6) {
        // The rule builder's own description of a smart album, beside the SQL it makes.
        ok = ok && exec(QStringLiteral("ALTER TABLE albums ADD COLUMN smart_json TEXT"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=6"));
    }
    if (v < 7) {
        // Presets applied on import when a photo matches: by camera, lens,
        // ISO range and format. The preset is named, so built-ins qualify.
        ok = ok && exec(QStringLiteral("CREATE TABLE preset_rules (id INTEGER PRIMARY KEY, preset TEXT NOT NULL,"
                                       " camera TEXT, lens TEXT, iso_min INTEGER NOT NULL DEFAULT 0, iso_max INTEGER NOT NULL DEFAULT 0,"
                                       " format TEXT, created_at TEXT NOT NULL)"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=7"));
    }
    if (v < 8) {
        // What an edit holds, for the card badges: 1 crop, 2 local, 4 retouch.
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN edit_flags INTEGER NOT NULL DEFAULT 0"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=8"));
    }
    if (v < 9) {
        // The move journal (brief: file safety). state 0 pending, 1 done.
        ok = ok && exec(QStringLiteral("CREATE TABLE file_ops (id INTEGER PRIMARY KEY, from_path TEXT NOT NULL, to_path TEXT NOT NULL,"
                                       " state INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL)"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=9"));
    }
    if (v < 10) {
        // When the sidecar was last read or written by us, to spot outside edits.
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN sidecar_seen INTEGER NOT NULL DEFAULT 0"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=10"));
    }
    if (v < 11) {
        // When the photo was last edited, for the Edit Time sort (edited rows start at their import time).
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN edited_at TEXT"));
        ok = ok && exec(QStringLiteral("UPDATE assets SET edited_at=imported_at WHERE edited=1"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=11"));
    }
    if (v < 12) {
        // Folders watched for new files.
        ok = ok && exec(QStringLiteral("ALTER TABLE folders ADD COLUMN watched INTEGER NOT NULL DEFAULT 0"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=12"));
    }
    if (v < 13) {
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN title TEXT NOT NULL DEFAULT ''"));
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN caption TEXT NOT NULL DEFAULT ''"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=13"));
    }
    if (v < 14) {
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN hash TEXT"));
        ok = ok && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS assets_hash ON assets(hash)"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=14"));
    }
    if (v < 15) {
        ok = ok && exec(QStringLiteral("ALTER TABLE assets ADD COLUMN exported_at TEXT"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=15"));
    }
    if (v < 16) {
        ok = ok && exec(QStringLiteral("ALTER TABLE albums ADD COLUMN cover_id INTEGER"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=16"));
    }
    if (v < 17) {
        ok = ok && exec(QStringLiteral("CREATE TABLE keyword_synonyms (keyword_id INTEGER NOT NULL REFERENCES keywords(id) ON DELETE CASCADE,"
                                       " synonym TEXT NOT NULL COLLATE NOCASE, PRIMARY KEY (keyword_id, synonym))"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=17"));
    }
    if (v < 18) {
        ok = ok && exec(QStringLiteral("CREATE INDEX idx_assets_gps ON assets(gps_lon,gps_lat) WHERE gps_lat IS NOT NULL AND gps_lon IS NOT NULL"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=18"));
    }
    if (v < 19) {
        ok = ok && exec(QStringLiteral("ALTER TABLE presets ADD COLUMN category TEXT NOT NULL DEFAULT ''"));
        ok = ok && exec(QStringLiteral("ALTER TABLE presets ADD COLUMN tags TEXT NOT NULL DEFAULT '[]'"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=19"));
    }
    if (v < 20) {
        for (const char *column : {"aperture_min", "aperture_max", "shutter_min", "shutter_max"})
            ok = ok && exec(QStringLiteral("ALTER TABLE preset_rules ADD COLUMN %1 REAL NOT NULL DEFAULT 0").arg(QLatin1String(column)));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=20"));
    }
    if (v < 21) {
        ok = ok && exec(QStringLiteral("CREATE TABLE asset_auto_tags (asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE, tag TEXT NOT NULL CHECK(tag IN ('people','cats','dogs')), confidence REAL NOT NULL DEFAULT 0 CHECK(confidence BETWEEN 0 AND 1), decision INTEGER NOT NULL DEFAULT 0 CHECK(decision IN (-1,0,1)), model TEXT NOT NULL DEFAULT '', PRIMARY KEY(asset_id,tag))"));
        ok = ok && exec(QStringLiteral("CREATE INDEX idx_auto_tags_subject ON asset_auto_tags(tag,decision,confidence,asset_id)"));
        ok = ok && exec(QStringLiteral("CREATE TABLE auto_tag_jobs (asset_id INTEGER PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE, state TEXT NOT NULL CHECK(state IN ('queued','done','error')), generation INTEGER NOT NULL DEFAULT 1, model TEXT NOT NULL DEFAULT '', error TEXT NOT NULL DEFAULT '')"));
        ok = ok && exec(QStringLiteral("CREATE INDEX idx_auto_tag_queue ON auto_tag_jobs(state,asset_id)"));
        ok = ok && exec(QStringLiteral("CREATE TABLE auto_tag_albums (tag TEXT PRIMARY KEY, album_id INTEGER REFERENCES albums(id) ON DELETE SET NULL)"));
        ok = ok && exec(QStringLiteral("PRAGMA user_version=21"));
    }
    if (v < 22) {
        ok = ok && exec(QStringLiteral("CREATE TABLE asset_auto_tags_expanded (asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE, tag TEXT NOT NULL CHECK(tag IN ('%1')), confidence REAL NOT NULL DEFAULT 0 CHECK(confidence BETWEEN 0 AND 1), decision INTEGER NOT NULL DEFAULT 0 CHECK(decision IN (-1,0,1)), model TEXT NOT NULL DEFAULT '', PRIMARY KEY(asset_id,tag))").arg(AutoTags::keys().join("','")));
        ok = ok && exec("INSERT INTO asset_auto_tags_expanded SELECT * FROM asset_auto_tags");
        ok = ok && exec("DROP TABLE asset_auto_tags");
        ok = ok && exec("ALTER TABLE asset_auto_tags_expanded RENAME TO asset_auto_tags");
        ok = ok && exec("CREATE INDEX idx_auto_tags_subject ON asset_auto_tags(tag,decision,confidence,asset_id)");
        ok = ok && exec("CREATE TABLE auto_tag_collection_choices (tag TEXT PRIMARY KEY, enabled INTEGER NOT NULL CHECK(enabled IN (0,1)))");
        ok = ok && exec("INSERT INTO auto_tag_collection_choices SELECT tag,album_id IS NOT NULL FROM auto_tag_albums");
        ok = ok && exec("INSERT OR IGNORE INTO auto_tag_collection_choices VALUES ('people',1),('group:animals',1),('group:transport',1),('group:sports',1),('group:food',1)");
        ok = ok && exec("PRAGMA user_version=22");
    }
    if (!ok) { m_db.rollback(); return false; }
    if (!m_db.commit()) { m_error = m_db.lastError().text(); return false; }
    return true;
}

int Catalog::folderIdFor(const QString &absPath) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM folders WHERE path=?"));
    q.addBindValue(absPath);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

int Catalog::ensureFolder(const QString &absPath) {
    const QString path = QDir::cleanPath(absPath);
    if (const int id = folderIdFor(path)) return id;
    int parent = 0;
    QDir d(path);
    if (!d.isRoot() && d.cdUp()) {
        // Only create parents down to the nearest one already catalogued;
        // otherwise a first import would register / and /home too.
        const QString parentPath = d.absolutePath();
        parent = folderIdFor(parentPath);
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO folders(path, parent_id, name) VALUES(?,?,?)"));
    q.addBindValue(path);
    q.addBindValue(parent ? QVariant(parent) : QVariant());
    q.addBindValue(QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName());
    if (!q.exec()) { m_error = q.lastError().text(); return 0; }
    return q.lastInsertId().toInt();
}

QVariantList Catalog::folderTree() const {
    // Rows ordered by path so a parent always precedes its children; depth
    // is counted against the shallowest ancestor present in the catalog.
    QVariantList out;
    // One pass over the assets for the per-folder counts; a folder's own
    // count then rolls up everything beneath it. (A LIKE subquery per
    // folder walked the whole table once per folder: half a second at
    // 100k rows and a hundred folders.)
    QHash<int, int> direct;
    {
        QSqlQuery c(m_db);
        if (c.exec(QStringLiteral("SELECT folder_id, COUNT(*) FROM assets GROUP BY folder_id"))) while (c.next()) direct[c.value(0).toInt()] = c.value(1).toInt();
    }
    struct F { int id; QString path, name; bool online, watched; };
    QList<F> folders;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, path, name, online, watched FROM folders ORDER BY path"));
    while (q.next()) folders << F{q.value(0).toInt(), q.value(1).toString(), q.value(2).toString(), q.value(3).toBool(), q.value(4).toBool()};
    QStringList roots;
    for (const F &f : folders) {
        int depth = 0;
        for (const QString &r : roots)
            if (f.path.startsWith(r + QLatin1Char('/'))) ++depth;
        roots << f.path;
        int count = 0;
        for (const F &g : folders) if (g.path == f.path || g.path.startsWith(f.path + QLatin1Char('/'))) count += direct.value(g.id);
        QVariantMap row;
        row[QStringLiteral("id")] = f.id;
        row[QStringLiteral("path")] = f.path;
        row[QStringLiteral("name")] = depth == 0 ? f.path : f.name;
        row[QStringLiteral("online")] = f.online && QDir(f.path).exists();
        row[QStringLiteral("count")] = count;
        row[QStringLiteral("watched")] = f.watched;
        row[QStringLiteral("depth")] = depth;
        out << row;
    }
    return out;
}

bool Catalog::setFolderWatched(int id, bool on) {
    return exec(QStringLiteral("UPDATE folders SET watched=%1 WHERE id=%2").arg(on ? 1 : 0).arg(id));
}

QStringList Catalog::watchedFolders() const {
    QStringList out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT path FROM folders WHERE watched=1 ORDER BY path"))) while (q.next()) out << q.value(0).toString();
    return out;
}

int Catalog::idForPath(const QString &path, int variant) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM assets WHERE path=? AND variant=?"));
    q.addBindValue(path);
    q.addBindValue(variant);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

QVector<int> Catalog::assetIdsForPath(const QString &path) const {
    QVector<int> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM assets WHERE path=? ORDER BY variant"));
    q.addBindValue(path);
    if (q.exec()) while (q.next()) out << q.value(0).toInt();
    return out;
}

bool Catalog::markEdited(int id, int flags) {
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (flags < 0) return exec(QStringLiteral("UPDATE assets SET edited=1, edit_rev=edit_rev+1, edited_at='%2' WHERE id=%1").arg(id).arg(now));
    return exec(QStringLiteral("UPDATE assets SET edited=1, edit_rev=edit_rev+1, edit_flags=%2, edited_at='%3' WHERE id=%1").arg(id).arg(flags).arg(now));
}

bool Catalog::assetExists(const QString &path) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM assets WHERE path=?"));
    q.addBindValue(path);
    return q.exec() && q.next();
}

int Catalog::upsertAsset(const AssetRecord &rec) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO assets(folder_id, path, filename, format, is_raw, size, mtime, width, height,"
        " orientation, captured_at, imported_at, make, model, lens, focal_length, aperture, shutter,"
        " iso, bit_depth, gps_lat, gps_lon, copyright, creator, import_id, rating, label, sidecar_seen, title, caption, hash)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(path, variant) DO UPDATE SET folder_id=excluded.folder_id, size=excluded.size,"
        " mtime=excluded.mtime, width=excluded.width, height=excluded.height,"
        " orientation=excluded.orientation, captured_at=excluded.captured_at, make=excluded.make,"
        " model=excluded.model, lens=excluded.lens, focal_length=excluded.focal_length,"
        " aperture=excluded.aperture, shutter=excluded.shutter, iso=excluded.iso,"
        " bit_depth=excluded.bit_depth, gps_lat=excluded.gps_lat, gps_lon=excluded.gps_lon,"
        " hash=COALESCE(excluded.hash, assets.hash)"));
    q.addBindValue(rec.folderId);
    q.addBindValue(rec.path);
    q.addBindValue(rec.filename);
    q.addBindValue(rec.format);
    q.addBindValue(rec.isRaw ? 1 : 0);
    q.addBindValue(rec.size);
    q.addBindValue(rec.mtime);
    q.addBindValue(rec.width);
    q.addBindValue(rec.height);
    q.addBindValue(rec.orientation);
    q.addBindValue(rec.capturedAt.isEmpty() ? QVariant() : QVariant(rec.capturedAt));
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(rec.make);
    q.addBindValue(rec.model);
    q.addBindValue(rec.lens);
    q.addBindValue(rec.focalLength);
    q.addBindValue(rec.aperture);
    q.addBindValue(rec.shutter);
    q.addBindValue(rec.iso);
    q.addBindValue(rec.bitDepth);
    q.addBindValue(rec.hasGps ? QVariant(rec.gpsLat) : QVariant());
    q.addBindValue(rec.hasGps ? QVariant(rec.gpsLon) : QVariant());
    q.addBindValue(rec.copyright);
    q.addBindValue(rec.creator);
    q.addBindValue(rec.importId);
    q.addBindValue(rec.sidecarRating >= 0 ? rec.sidecarRating : 0);
    q.addBindValue(rec.sidecarLabel.isEmpty() ? QStringLiteral("") : rec.sidecarLabel); // never NULL
    q.addBindValue(rec.sidecarSeen);
    q.addBindValue(rec.title.isNull() ? QStringLiteral("") : rec.title);       // a null QString binds NULL
    q.addBindValue(rec.caption.isNull() ? QStringLiteral("") : rec.caption);
    q.addBindValue(rec.hash.isEmpty() ? QVariant() : QVariant(rec.hash));
    if (!q.exec()) { m_error = q.lastError().text(); qWarning() << "upsert:" << m_error; return 0; }
    return idForPath(rec.path, 0);
}

static AssetRecord readAsset(QSqlQuery &q) {
    AssetRecord r;
    r.id = q.value(QStringLiteral("id")).toInt();
    r.folderId = q.value(QStringLiteral("folder_id")).toInt();
    r.path = q.value(QStringLiteral("path")).toString();
    r.filename = q.value(QStringLiteral("filename")).toString();
    r.format = q.value(QStringLiteral("format")).toString();
    r.isRaw = q.value(QStringLiteral("is_raw")).toBool();
    r.size = q.value(QStringLiteral("size")).toLongLong();
    r.mtime = q.value(QStringLiteral("mtime")).toLongLong();
    r.width = q.value(QStringLiteral("width")).toInt();
    r.height = q.value(QStringLiteral("height")).toInt();
    r.orientation = q.value(QStringLiteral("orientation")).toInt();
    r.capturedAt = q.value(QStringLiteral("captured_at")).toString();
    r.make = q.value(QStringLiteral("make")).toString();
    r.model = q.value(QStringLiteral("model")).toString();
    r.lens = q.value(QStringLiteral("lens")).toString();
    r.focalLength = q.value(QStringLiteral("focal_length")).toDouble();
    r.aperture = q.value(QStringLiteral("aperture")).toDouble();
    r.shutter = q.value(QStringLiteral("shutter")).toDouble();
    r.iso = q.value(QStringLiteral("iso")).toInt();
    r.bitDepth = q.value(QStringLiteral("bit_depth")).toInt();
    r.rating = q.value(QStringLiteral("rating")).toInt();
    r.flag = q.value(QStringLiteral("flag")).toInt();
    r.label = q.value(QStringLiteral("label")).toString();
    r.hasGps = !q.value(QStringLiteral("gps_lat")).isNull();
    r.gpsLat = q.value(QStringLiteral("gps_lat")).toDouble();
    r.gpsLon = q.value(QStringLiteral("gps_lon")).toDouble();
    r.copyright = q.value(QStringLiteral("copyright")).toString();
    r.title = q.value(QStringLiteral("title")).toString();
    r.hash = q.value(QStringLiteral("hash")).toString();
    r.caption = q.value(QStringLiteral("caption")).toString();
    r.creator = q.value(QStringLiteral("creator")).toString();
    r.importId = q.value(QStringLiteral("import_id")).toInt();
    r.edited = q.value(QStringLiteral("edited")).toBool();
    r.editRev = q.value(QStringLiteral("edit_rev")).toInt();
    r.editFlags = q.value(QStringLiteral("edit_flags")).toInt();
    r.sidecarSeen = q.value(QStringLiteral("sidecar_seen")).toLongLong();
    r.variant = q.value(QStringLiteral("variant")).toInt();
    r.variantOf = q.value(QStringLiteral("variant_of")).toInt();
    r.variantName = q.value(QStringLiteral("variant_name")).toString();
    r.stackId = q.value(QStringLiteral("stack_id")).toInt();
    r.stackPos = q.value(QStringLiteral("stack_pos")).toInt();
    r.stackCount = q.value(QStringLiteral("stack_count")).toInt();
    return r;
}

AssetRecord Catalog::asset(int id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT a.*, (SELECT COUNT(*) FROM assets b WHERE b.stack_id=a.stack_id) AS stack_count"
                             " FROM assets a WHERE a.id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) return readAsset(q);
    return AssetRecord();
}

QVector<int> Catalog::assetIds(const QString &whereSql, const QVariantList &binds,
                               const QString &orderSql) const {
    QVector<int> ids;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral("SELECT id FROM assets");
    if (!whereSql.isEmpty()) sql += QStringLiteral(" WHERE ") + whereSql;
    if (!orderSql.isEmpty()) sql += QStringLiteral(" ORDER BY ") + orderSql;
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    if (!q.exec()) { qWarning() << "assetIds:" << q.lastError().text() << sql; return ids; }
    while (q.next()) ids << q.value(0).toInt();
    return ids;
}

int Catalog::count(const QString &whereSql, const QVariantList &binds) const {
    QSqlQuery q(m_db);
    QString sql = QStringLiteral("SELECT COUNT(*) FROM assets");
    if (!whereSql.isEmpty()) sql += QStringLiteral(" WHERE ") + whereSql;
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

static QString idList(const QVector<int> &ids) {
    QStringList s;
    for (int id : ids) s << QString::number(id);
    return s.join(QLatin1Char(','));
}

bool Catalog::setRating(const QVector<int> &ids, int rating) {
    if (ids.isEmpty()) return true;
    rating = qBound(0, rating, 5);
    return exec(QStringLiteral("UPDATE assets SET rating=%1 WHERE id IN (%2)").arg(rating).arg(idList(ids)));
}

bool Catalog::setFlag(const QVector<int> &ids, int flag) {
    if (ids.isEmpty()) return true;
    flag = qBound(-1, flag, 1);
    return exec(QStringLiteral("UPDATE assets SET flag=%1 WHERE id IN (%2)").arg(flag).arg(idList(ids)));
}

bool Catalog::setLabel(const QVector<int> &ids, const QString &label) {
    if (ids.isEmpty()) return true;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE assets SET label=? WHERE id IN (%1)").arg(idList(ids)));
    q.addBindValue(label);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return true;
}

bool Catalog::removeAssets(const QVector<int> &ids) {
    if (ids.isEmpty()) return true;
    if (!m_db.transaction()) { m_error = m_db.lastError().text(); return false; }
    if (!deleteAssetRows(ids)) { m_db.rollback(); return false; }
    if (!m_db.commit()) { m_error = m_db.lastError().text(); m_db.rollback(); return false; }
    return true;
}

bool Catalog::deleteAssetRows(const QVector<int> &ids) {
    if (ids.isEmpty()) return true;
    // Deleting a master also deletes its virtual copies, which may be in
    // other stacks. Capture every affected stack before that cascade runs.
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT DISTINCT stack_id FROM assets WHERE stack_id IS NOT NULL "
                                  "AND (id IN (%1) OR variant_of IN (%1))").arg(idList(ids)))) {
        m_error = query.lastError().text(); return false;
    }
    QVector<int> stacks;
    while (query.next()) stacks << query.value(0).toInt();
    query.finish();
    if (!exec(QStringLiteral("DELETE FROM assets WHERE id IN (%1)").arg(idList(ids)))) return false;
    for (const int stack : stacks) {
        query.prepare("SELECT id FROM assets WHERE stack_id=? ORDER BY stack_pos,id"); query.addBindValue(stack);
        if (!query.exec()) { m_error = query.lastError().text(); return false; }
        QVector<int> members;
        while (query.next()) members << query.value(0).toInt();
        query.finish();
        for (int i = 0; i < members.size(); ++i) {
            // A lone remaining original should simply appear as a photo.
            const QString sql = members.size() == 1
                ? QStringLiteral("UPDATE assets SET stack_id=NULL, stack_pos=0 WHERE id=%1").arg(members[i])
                : QStringLiteral("UPDATE assets SET stack_pos=%1 WHERE id=%2").arg(i).arg(members[i]);
            if (!exec(sql)) return false;
        }
    }
    return true;
}

bool Catalog::removeFolder(int id) {
    const QString root = folderPath(id);
    if (root.isEmpty()) { m_error = QStringLiteral("no such folder"); return false; }
    // Subfolders by path, as folderTree() nests them; a LIKE pattern would
    // misread '_' and '%' in folder names.
    QVector<int> folders;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id, path FROM folders"))) { m_error = query.lastError().text(); return false; }
    while (query.next()) {
        const QString path = query.value(1).toString();
        if (path == root || path.startsWith(root + QLatin1Char('/'))) folders << query.value(0).toInt();
    }
    query.finish();
    QVector<int> assets;
    if (!query.exec(QStringLiteral("SELECT id FROM assets WHERE folder_id IN (%1)").arg(idList(folders)))) {
        m_error = query.lastError().text(); return false;
    }
    while (query.next()) assets << query.value(0).toInt();
    query.finish();
    if (!m_db.transaction()) { m_error = m_db.lastError().text(); return false; }
    if (!deleteAssetRows(assets)
        || !exec(QStringLiteral("DELETE FROM folders WHERE id IN (%1)").arg(idList(folders)))) {
        m_db.rollback(); return false;
    }
    if (!m_db.commit()) { m_error = m_db.lastError().text(); m_db.rollback(); return false; }
    return true;
}

QStringList Catalog::keywordsFor(int assetId) const {
    QStringList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT k.name FROM keywords k JOIN asset_keywords ak ON ak.keyword_id=k.id"
                             " WHERE ak.asset_id=? ORDER BY k.name"));
    q.addBindValue(assetId);
    if (q.exec()) while (q.next()) out << q.value(0).toString();
    return out;
}

// "  travel / italy /venice " → "travel/italy/venice": trimmed segments,
// empty ones dropped, so a path never has a blank level.
QString Catalog::normalizeKeyword(const QString &keyword) {
    QStringList parts;
    for (const QString &p : keyword.split(QLatin1Char('/'))) { const QString t = p.trimmed(); if (!t.isEmpty()) parts << t; }
    return parts.join(QLatin1Char('/'));
}

bool Catalog::addKeyword(const QVector<int> &ids, const QString &keyword) {
    const QString kw = normalizeKeyword(keyword);
    if (kw.isEmpty() || ids.isEmpty()) return false;
    // All the photos or none. A savepoint, not a transaction: the importer
    // calls this inside its own.
    QSqlQuery sp(m_db);
    if (!sp.exec(QStringLiteral("SAVEPOINT add_keyword"))) { m_error = sp.lastError().text(); return false; }
    const bool ok = addKeywordRows(ids, kw);
    if (!ok) sp.exec(QStringLiteral("ROLLBACK TO add_keyword"));
    sp.exec(QStringLiteral("RELEASE add_keyword"));
    return ok;
}

bool Catalog::addKeywordRows(const QVector<int> &ids, const QString &kw) {
    QSqlQuery q(m_db);
    // Every level of the path is a keyword of its own; the photo hangs on the leaf.
    const QStringList parts = kw.split(QLatin1Char('/'));
    for (int n = 1; n <= parts.size(); ++n) {
        q.prepare(QStringLiteral("INSERT OR IGNORE INTO keywords(name) VALUES(?)"));
        q.addBindValue(parts.mid(0, n).join(QLatin1Char('/')));
        if (!q.exec()) { m_error = q.lastError().text(); return false; }
    }
    q.prepare(QStringLiteral("SELECT id FROM keywords WHERE name=?"));
    q.addBindValue(kw);
    if (!q.exec() || !q.next()) return false;
    const int kid = q.value(0).toInt();
    for (int id : ids) {
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral("INSERT OR IGNORE INTO asset_keywords(asset_id, keyword_id) VALUES(?,?)"));
        ins.addBindValue(id);
        ins.addBindValue(kid);
        if (!ins.exec()) { m_error = ins.lastError().text(); return false; }
    }
    return true;
}

bool Catalog::setSidecarSeen(const QVector<int> &ids, qint64 mtime) {
    if (ids.isEmpty()) return true;
    return exec(QStringLiteral("UPDATE assets SET sidecar_seen=%1 WHERE id IN (%2)").arg(mtime).arg(idList(ids)));
}

bool Catalog::removeKeyword(const QVector<int> &ids, const QString &keyword) {
    if (ids.isEmpty()) return true;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM asset_keywords WHERE asset_id IN (%1) AND keyword_id IN"
                             " (SELECT id FROM keywords WHERE name=?)").arg(idList(ids)));
    q.addBindValue(keyword.trimmed());
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return true;
}

QVariantList Catalog::keywordTree() const {
    QVariantList out;
    // Exact counts (a photo once per subtree) come from a subquery per
    // keyword, fine for the few hundred keywords a catalog usually has;
    // past that, a one-pass roll-up of direct counts keeps the sidebar
    // quick, and a photo tagged at two levels of one branch counts twice.
    int keywords = 0;
    { QSqlQuery n(m_db); if (n.exec(QStringLiteral("SELECT COUNT(*) FROM keywords")) && n.next()) keywords = n.value(0).toInt(); }
    QSqlQuery q(m_db);
    if (keywords <= 400) {
        if (!q.exec(QStringLiteral(
            "SELECT k.id, k.name,"
            " (SELECT COUNT(DISTINCT ak.asset_id) FROM asset_keywords ak JOIN keywords c ON c.id=ak.keyword_id"
            "   WHERE c.name=k.name OR substr(c.name,1,length(k.name)+1) COLLATE NOCASE = k.name || '/') AS cnt"
            " FROM keywords k ORDER BY k.name COLLATE NOCASE"))) return out;
        while (q.next()) {
            const QString path = q.value(1).toString();
            const QStringList parts = path.split(QLatin1Char('/'));
            QVariantMap row;
            row[QStringLiteral("id")] = q.value(0).toInt();
            row[QStringLiteral("path")] = path;
            row[QStringLiteral("name")] = parts.last();
            row[QStringLiteral("depth")] = parts.size() - 1;
            row[QStringLiteral("count")] = q.value(2).toInt();
            out << row;
        }
        return out;
    }
    QHash<int, int> direct;
    { QSqlQuery c(m_db); if (c.exec(QStringLiteral("SELECT keyword_id, COUNT(*) FROM asset_keywords GROUP BY keyword_id"))) while (c.next()) direct[c.value(0).toInt()] = c.value(1).toInt(); }
    struct K { int id; QString path; };
    QList<K> all;
    if (!q.exec(QStringLiteral("SELECT id, name FROM keywords ORDER BY name COLLATE NOCASE"))) return out;
    while (q.next()) all << K{q.value(0).toInt(), q.value(1).toString()};
    for (const K &k : all) {
        int count = 0;
        for (const K &o : all) if (o.path == k.path || o.path.startsWith(k.path + QLatin1Char('/'))) count += direct.value(o.id);
        const QStringList parts = k.path.split(QLatin1Char('/'));
        QVariantMap row;
        row[QStringLiteral("id")] = k.id;
        row[QStringLiteral("path")] = k.path;
        row[QStringLiteral("name")] = parts.last();
        row[QStringLiteral("depth")] = parts.size() - 1;
        row[QStringLiteral("count")] = count;
        out << row;
    }
    return out;
}

int Catalog::keywordId(const QString &path) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM keywords WHERE name=?"));
    q.addBindValue(normalizeKeyword(path));
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

QString Catalog::keywordPath(int id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT name FROM keywords WHERE id=?"));
    q.addBindValue(id);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

bool Catalog::deleteKeyword(int id) {
    const QString path = keywordPath(id);
    if (path.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM keywords WHERE name=:path OR substr(name,1,length(:path)+1) COLLATE NOCASE = :path || '/'"));   // links cascade
    q.bindValue(QStringLiteral(":path"), path);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return q.numRowsAffected() > 0;
}

QStringList Catalog::cameras() const {
    QStringList out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT DISTINCT trim(make || ' ' || model) AS c FROM assets WHERE c<>'' ORDER BY c COLLATE NOCASE")))
        while (q.next()) out << q.value(0).toString();
    return out;
}

QStringList Catalog::keywordSynonyms(int keywordId) const {
    QStringList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT synonym FROM keyword_synonyms WHERE keyword_id=? ORDER BY synonym COLLATE NOCASE"));
    q.addBindValue(keywordId);
    if (q.exec()) while (q.next()) out << q.value(0).toString();
    return out;
}

bool Catalog::setKeywordSynonyms(int keywordId, const QStringList &synonyms) {
    if (keywordId <= 0) return false;
    if (!m_db.transaction()) return false;
    if (!exec(QStringLiteral("DELETE FROM keyword_synonyms WHERE keyword_id=%1").arg(keywordId))) { m_db.rollback(); return false; }
    QStringList seen;
    for (const QString &raw : synonyms) {
        const QString syn = raw.trimmed();
        if (syn.isEmpty() || seen.contains(syn, Qt::CaseInsensitive)) continue;
        seen << syn;
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("INSERT INTO keyword_synonyms(keyword_id, synonym) VALUES(?,?)"));
        q.addBindValue(keywordId); q.addBindValue(syn);
        if (!q.exec()) { m_error = q.lastError().text(); m_db.rollback(); return false; }
    }
    return m_db.commit();
}

QStringList Catalog::suggestKeywords(const QString &text, int limit) const {
    QStringList out;
    const QString t = text.trimmed();
    if (t.isEmpty()) return out;
    QSqlQuery q(m_db);
    // A name that starts with the text first, then any name or synonym holding it.
    q.prepare(QStringLiteral(
        "SELECT name FROM ("
        " SELECT k.name AS name, CASE WHEN k.name LIKE ? OR k.name LIKE ? THEN 0 WHEN k.name LIKE ? THEN 1 ELSE 2 END AS rank FROM keywords k"
        "  WHERE k.name LIKE ? OR k.id IN (SELECT keyword_id FROM keyword_synonyms WHERE synonym LIKE ?)"
        ") ORDER BY rank, name COLLATE NOCASE LIMIT ?"));
    q.addBindValue(t + QLatin1Char('%'));                                  // a top-level name that starts with it
    q.addBindValue(QStringLiteral("%/") + t + QLatin1Char('%'));           // or a level that does
    q.addBindValue(QLatin1Char('%') + t + QLatin1Char('%'));
    q.addBindValue(QLatin1Char('%') + t + QLatin1Char('%'));
    q.addBindValue(QLatin1Char('%') + t + QLatin1Char('%'));
    q.addBindValue(limit);
    if (q.exec()) while (q.next()) out << q.value(0).toString();
    return out;
}

QStringList Catalog::allKeywords() const {
    QStringList out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT name FROM keywords ORDER BY name")))
        while (q.next()) out << q.value(0).toString();
    return out;
}

int Catalog::createAlbum(const QString &name, const QString &smartRule, const QString &smartJson, int parentId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO albums(name, smart_rule, smart_json, parent_id) VALUES(?,?,?,?)"));
    q.addBindValue(name);
    q.addBindValue(smartRule.isEmpty() ? QVariant() : QVariant(smartRule));
    q.addBindValue(smartJson.isEmpty() ? QVariant() : QVariant(smartJson));
    q.addBindValue(parentId > 0 ? QVariant(parentId) : QVariant());
    if (!q.exec()) { m_error = q.lastError().text(); return 0; }
    return q.lastInsertId().toInt();
}

bool Catalog::setAlbumParent(int id, int parentId) {
    if (id <= 0 || id == parentId) return false;
    // Never under one of its own descendants.
    for (int p = parentId; p > 0;) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT parent_id FROM albums WHERE id=?"));
        q.addBindValue(p);
        if (!q.exec() || !q.next()) break;
        p = q.value(0).toInt();
        if (p == id) { m_error = QStringLiteral("an album cannot go inside its own child"); return false; }
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE albums SET parent_id=? WHERE id=?"));
    q.addBindValue(parentId > 0 ? QVariant(parentId) : QVariant());
    q.addBindValue(id);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return q.numRowsAffected() > 0;
}

bool Catalog::setAlbumCover(int id, int assetId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE albums SET cover_id=? WHERE id=?"));
    q.addBindValue(assetId > 0 ? QVariant(assetId) : QVariant());
    q.addBindValue(id);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return q.numRowsAffected() > 0;
}

QVector<int> Catalog::albumOrder(int albumId) const {
    QVector<int> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT asset_id FROM album_assets WHERE album_id=? ORDER BY position, asset_id"));
    q.addBindValue(albumId);
    if (q.exec()) while (q.next()) out << q.value(0).toInt();
    return out;
}

bool Catalog::setAlbumOrder(int albumId, const QVector<int> &assetIds) {
    if (!m_db.transaction()) return false;
    for (int i = 0; i < assetIds.size(); ++i) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("UPDATE album_assets SET position=? WHERE album_id=? AND asset_id=?"));
        q.addBindValue(i); q.addBindValue(albumId); q.addBindValue(assetIds[i]);
        if (!q.exec()) { m_error = q.lastError().text(); m_db.rollback(); return false; }
    }
    return m_db.commit();
}

bool Catalog::moveInAlbum(int albumId, int assetId, int delta) {
    QVector<int> order = albumOrder(albumId);
    const int i = order.indexOf(assetId);
    if (i < 0 || delta == 0) return false;
    const int j = qBound(0, i + delta, order.size() - 1);
    if (j == i) return false;
    order.move(i, j);
    return setAlbumOrder(albumId, order);
}

bool Catalog::moveInAlbumTo(int albumId, int assetId, int index) {
    QVector<int> order = albumOrder(albumId);
    const int i = order.indexOf(assetId);
    if (i < 0) return false;
    const int j = qBound(0, index, order.size() - 1);
    if (j == i) return false;
    order.move(i, j);
    return setAlbumOrder(albumId, order);
}

bool Catalog::markExported(int id) {
    return exec(QStringLiteral("UPDATE assets SET exported_at='%2' WHERE id=%1").arg(id).arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
}

QVariantList Catalog::albums(bool smart) const {
    QVariantList out;
    QSqlQuery q(m_db);
    q.prepare(smart
        ? QStringLiteral("SELECT id, name, smart_rule, smart_json, parent_id, cover_id FROM albums WHERE smart_rule IS NOT NULL AND id NOT IN (SELECT a.album_id FROM auto_tag_albums a JOIN auto_tag_collection_choices c ON c.tag=a.tag WHERE c.enabled=0 AND a.album_id IS NOT NULL) ORDER BY sort, name COLLATE NOCASE")
        : QStringLiteral("SELECT id, name, smart_rule, smart_json, parent_id, cover_id FROM albums WHERE smart_rule IS NULL ORDER BY sort, name COLLATE NOCASE"));
    if (!q.exec()) return out;
    QList<QVariantMap> rows;
    while (q.next()) {
        QVariantMap row;
        const int id = q.value(0).toInt();
        row[QStringLiteral("id")] = id;
        row[QStringLiteral("name")] = q.value(1).toString();
        const QString rule = q.value(2).toString();
        row[QStringLiteral("rule")] = rule;
        row[QStringLiteral("json")] = q.value(3).toString();
        row[QStringLiteral("parent")] = q.value(4).toInt();
        row[QStringLiteral("cover")] = q.value(5).toInt();
        row[QStringLiteral("count")] = smart ? count(rule)
            : count(QStringLiteral("id IN (SELECT asset_id FROM album_assets WHERE album_id=?)"), {id});
        rows << row;
    }
    // Parents first, each one's children right beneath it, by name at every level.
    QSet<int> ids;
    for (const QVariantMap &r : rows) ids.insert(r.value(QStringLiteral("id")).toInt());
    std::function<void(int, int)> walk = [&](int parent, int depth) {
        for (const QVariantMap &r : rows) {
            int p = r.value(QStringLiteral("parent")).toInt();
            if (!ids.contains(p)) p = 0;                 // an orphan (parent deleted or smart) sits at the top
            if (p != parent) continue;
            QVariantMap row = r; row[QStringLiteral("depth")] = depth;
            out << row;
            if (depth < 16) walk(r.value(QStringLiteral("id")).toInt(), depth + 1);
        }
    };
    walk(0, 0);
    return out;
}

bool Catalog::addToAlbum(int albumId, const QVector<int> &ids) {
    if (ids.isEmpty()) return true;
    // One atomic batch, including when a caller already owns a transaction.
    // Avoid a disk commit and a MAX(position) scan for every selected photo.
    QSqlQuery control(m_db);
    if (!control.exec(QStringLiteral("SAVEPOINT add_album_photos"))) { m_error = control.lastError().text(); return false; }
    const auto fail = [&](const QString &error) {
        m_error = error;
        control.exec(QStringLiteral("ROLLBACK TO add_album_photos"));
        control.exec(QStringLiteral("RELEASE add_album_photos"));
        return false;
    };
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM album_assets WHERE album_id=?"));
    q.addBindValue(albumId);
    if (!q.exec() || !q.next()) return fail(q.lastError().text());
    qint64 position = q.value(0).toLongLong();
    q.finish();
    if (!q.prepare(QStringLiteral("INSERT OR IGNORE INTO album_assets(album_id, asset_id, position) VALUES(?,?,?)")))
        return fail(q.lastError().text());
    for (int id : ids) {
        q.bindValue(0, albumId); q.bindValue(1, id); q.bindValue(2, position);
        if (!q.exec()) return fail(q.lastError().text());
        if (q.numRowsAffected() > 0) ++position;
    }
    q.finish();
    if (!control.exec(QStringLiteral("RELEASE add_album_photos"))) return fail(control.lastError().text());
    return true;
}

QVector<int> Catalog::albumAssetIds(int albumId) const {
    return assetIds(QStringLiteral("id IN (SELECT asset_id FROM album_assets WHERE album_id=?)"), {albumId});
}

int Catalog::createPreset(const QString &name, const QString &json, const QString &category, const QStringList &tags) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO presets(name, params, created_at, category, tags) VALUES(?,?,?,?,?)"));
    q.addBindValue(name);
    q.addBindValue(json);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(category.isEmpty() ? QStringLiteral("") : category);
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(tags)).toJson(QJsonDocument::Compact)));
    if (!q.exec()) { m_error = q.lastError().text(); return 0; }
    return q.lastInsertId().toInt();
}

bool Catalog::setPresetMetadata(int id, const QString &category, const QStringList &tags) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE presets SET category=?, tags=? WHERE id=?"));
    q.addBindValue(category.isEmpty() ? QStringLiteral("") : category);
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(tags)).toJson(QJsonDocument::Compact)));
    q.addBindValue(id);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return q.numRowsAffected() == 1;
}

bool Catalog::deletePreset(int id) {
    if (id <= 0 || !m_db.transaction()) return false;
    QSqlQuery rules(m_db), preset(m_db);
    rules.prepare(QStringLiteral("DELETE FROM preset_rules WHERE preset=(SELECT name FROM presets WHERE id=?)"));
    rules.addBindValue(id);
    preset.prepare(QStringLiteral("DELETE FROM presets WHERE id=?")); preset.addBindValue(id);
    if (!rules.exec() || !preset.exec() || preset.numRowsAffected() != 1) {
        m_error = rules.lastError().isValid() ? rules.lastError().text() : preset.lastError().text();
        m_db.rollback(); return false;
    }
    if (!m_db.commit()) { m_error = m_db.lastError().text(); m_db.rollback(); return false; }
    return true;
}

QVariantList Catalog::presets() const {
    QVariantList out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, name, params, category, tags FROM presets ORDER BY name COLLATE NOCASE"))) return out;
    while (q.next()) {
        QVariantMap row;
        row[QStringLiteral("id")] = q.value(0).toInt();
        row[QStringLiteral("name")] = q.value(1).toString();
        row[QStringLiteral("json")] = q.value(2).toString();
        row[QStringLiteral("category")] = q.value(3).toString();
        row[QStringLiteral("tags")] = QJsonDocument::fromJson(q.value(4).toString().toUtf8()).array().toVariantList();
        out << row;
    }
    return out;
}

int Catalog::createPresetRule(const QString &preset, const QVariantMap &rule) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO preset_rules(preset, camera, lens, iso_min, iso_max, format, created_at, aperture_min, aperture_max, shutter_min, shutter_max) VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(preset);
    q.addBindValue(rule.value(QStringLiteral("camera")).toString().trimmed());
    q.addBindValue(rule.value(QStringLiteral("lens")).toString().trimmed());
    q.addBindValue(rule.value(QStringLiteral("isoMin")).toInt());
    q.addBindValue(rule.value(QStringLiteral("isoMax")).toInt());
    q.addBindValue(rule.value(QStringLiteral("format")).toString().trimmed().toUpper());
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    for (const char *key : {"apertureMin", "apertureMax", "shutterMin", "shutterMax"}) q.addBindValue(rule.value(QLatin1String(key)).toDouble());
    if (!q.exec()) { m_error = q.lastError().text(); return 0; }
    return q.lastInsertId().toInt();
}

bool Catalog::deletePresetRule(int id) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM preset_rules WHERE id=?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

QVariantList Catalog::presetRules() const {
    QVariantList out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, preset, camera, lens, iso_min, iso_max, format, aperture_min, aperture_max, shutter_min, shutter_max FROM preset_rules ORDER BY id"))) return out;
    while (q.next()) {
        QVariantMap row;
        row[QStringLiteral("id")] = q.value(0).toInt();
        row[QStringLiteral("preset")] = q.value(1).toString();
        row[QStringLiteral("camera")] = q.value(2).toString();
        row[QStringLiteral("lens")] = q.value(3).toString();
        row[QStringLiteral("isoMin")] = q.value(4).toInt();
        row[QStringLiteral("isoMax")] = q.value(5).toInt();
        row[QStringLiteral("format")] = q.value(6).toString();
        row[QStringLiteral("apertureMin")] = q.value(7).toDouble(); row[QStringLiteral("apertureMax")] = q.value(8).toDouble();
        row[QStringLiteral("shutterMin")] = q.value(9).toDouble(); row[QStringLiteral("shutterMax")] = q.value(10).toDouble();
        out << row;
    }
    return out;
}

int Catalog::findDuplicate(const QString &filename, qint64 size, const QString &capturedAt) const {
    QSqlQuery q(m_db);
    if (capturedAt.isEmpty()) q.prepare(QStringLiteral("SELECT id FROM assets WHERE filename=? AND size=? AND variant=0 LIMIT 1"));
    else { q.prepare(QStringLiteral("SELECT id FROM assets WHERE filename=? AND size=? AND variant=0 AND captured_at=? LIMIT 1")); }
    q.addBindValue(filename);
    q.addBindValue(size);
    if (!capturedAt.isEmpty()) q.addBindValue(capturedAt);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

int Catalog::findByHash(const QString &hash) const {
    if (hash.isEmpty()) return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM assets WHERE hash=? AND variant=0 LIMIT 1"));
    q.addBindValue(hash);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

bool Catalog::setHash(int id, const QString &hash) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE assets SET hash=? WHERE id=?"));
    q.addBindValue(hash.isEmpty() ? QVariant() : QVariant(hash));
    q.addBindValue(id);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return true;
}

QList<QVector<int>> Catalog::duplicateGroups() const {
    QList<QVector<int>> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT hash, id FROM assets WHERE variant=0 AND hash IS NOT NULL AND hash<>''"
                               " AND hash IN (SELECT hash FROM assets WHERE variant=0 GROUP BY hash HAVING COUNT(*) > 1) ORDER BY hash, id"))) return out;
    QString last;
    while (q.next()) {
        const QString h = q.value(0).toString();
        if (h != last) { out << QVector<int>(); last = h; }
        out.last() << q.value(1).toInt();
    }
    return out;
}

QVector<int> Catalog::assetIdsOfImport(int importId) const {
    QVector<int> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM assets WHERE import_id=? ORDER BY id"));
    q.addBindValue(importId);
    if (q.exec()) while (q.next()) out << q.value(0).toInt();
    return out;
}

int Catalog::beginImport(const QString &source) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO imports(started_at, source) VALUES(?,?)"));
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(source);
    if (!q.exec()) { m_error = q.lastError().text(); return 0; }
    return q.lastInsertId().toInt();
}

void Catalog::finishImport(int importId, int count) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE imports SET finished_at=?, count=? WHERE id=?"));
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(count);
    q.addBindValue(importId);
    q.exec();
}

QVariantList Catalog::recentImports(int limit) const {
    QVariantList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, started_at, source, count FROM imports WHERE count>0"
                             " ORDER BY id DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap row;
        row[QStringLiteral("id")] = q.value(0).toInt();
        row[QStringLiteral("date")] = q.value(1).toString().left(10);
        row[QStringLiteral("source")] = q.value(2).toString();
        row[QStringLiteral("count")] = q.value(3).toInt();
        out << row;
    }
    return out;
}

int Catalog::lastImportId() const {
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT id FROM imports WHERE count>0 ORDER BY id DESC LIMIT 1")) && q.next())
        return q.value(0).toInt();
    return 0;
}

// ── variants ────────────────────────────────────────────────────────────

int Catalog::createVariant(int masterId, int version, const QString &name) {
    const AssetRecord m = asset(masterId);
    if (!m.id || version <= 0) return 0;
    const int root = m.variantOf ? m.variantOf : m.id; // a variant of a variant hangs off the master
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO assets(folder_id, path, filename, format, is_raw, size, mtime, width, height, orientation,"
        " captured_at, imported_at, make, model, lens, focal_length, aperture, shutter, iso, bit_depth,"
        " rating, flag, label, gps_lat, gps_lon, copyright, creator, edited, import_id, edit_rev,"
        " variant, variant_of, variant_name, title, caption, hash, edit_flags, edited_at, sidecar_seen)"
        " SELECT folder_id, path, filename, format, is_raw, size, mtime, width, height, orientation,"
        " captured_at, imported_at, make, model, lens, focal_length, aperture, shutter, iso, bit_depth,"
        " rating, flag, label, gps_lat, gps_lon, copyright, creator, edited, import_id, 1,"
        " ?, ?, ?, title, caption, hash, edit_flags, edited_at, sidecar_seen FROM assets WHERE id=?"));
    q.addBindValue(version);
    q.addBindValue(root);
    q.addBindValue(name.isNull() ? QStringLiteral("") : name); // a null QString binds NULL
    q.addBindValue(masterId);
    if (!q.exec()) { m_error = q.lastError().text(); qWarning() << "createVariant:" << m_error; return 0; }
    const int id = q.lastInsertId().toInt();
    exec(QStringLiteral("INSERT OR IGNORE INTO asset_keywords(asset_id, keyword_id)"
                        " SELECT %1, keyword_id FROM asset_keywords WHERE asset_id=%2").arg(id).arg(masterId));
    // Virtual copies of an AI result retain its route back to the source.
    q.prepare("INSERT INTO ai_versions(asset_id,family,source_id,operation) "
              "SELECT ?,family,?,'variant' FROM ai_versions WHERE asset_id=?");
    q.addBindValue(id); q.addBindValue(masterId); q.addBindValue(masterId);
    if (!q.exec()) m_error = q.lastError().text();
    return id;
}

QVector<int> Catalog::variantsOf(int masterId) const {
    return assetIds(QStringLiteral("variant_of=?"), {masterId}, QStringLiteral("variant"));
}

bool Catalog::setVariantName(int id, const QString &name) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE assets SET variant_name=? WHERE id=?"));
    q.addBindValue(name.isNull() ? QStringLiteral("") : name);
    q.addBindValue(id);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return true;
}

bool Catalog::promoteVariant(int id) {
    const AssetRecord v = asset(id);
    if (!v.id || !v.variantOf) return false;
    const AssetRecord m = asset(v.variantOf);
    if (!m.id) return false;
    if (!m_db.transaction()) return false;
    // Park the variant's version on -1 to step around UNIQUE(path, variant).
    bool ok = exec(QStringLiteral("UPDATE assets SET variant=-1, variant_of=NULL, variant_name='' WHERE id=%1").arg(id));
    ok = ok && exec(QStringLiteral("UPDATE assets SET variant_of=%1 WHERE variant_of=%2 AND id<>%1").arg(id).arg(m.id));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE assets SET variant=?, variant_of=?, variant_name=? WHERE id=?"));
    q.addBindValue(v.variant); q.addBindValue(id); q.addBindValue(v.variantName); q.addBindValue(m.id);
    ok = ok && q.exec();
    ok = ok && exec(QStringLiteral("UPDATE assets SET variant=0 WHERE id=%1").arg(id));
    if (!ok) { m_error = m_db.lastError().text(); m_db.rollback(); return false; }
    return m_db.commit();
}

// ── snapshots ───────────────────────────────────────────────────────────

int Catalog::createSnapshot(int assetId, const QString &name, int historyEnd, const QString &json, const QByteArray &previewData) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO snapshots(asset_id, name, created_at, history_end, params, preview) VALUES(?,?,?,?,?,?)"));
    q.addBindValue(assetId);
    q.addBindValue(name);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(historyEnd);
    q.addBindValue(json);
    q.addBindValue(previewData.isEmpty() ? QVariant(QMetaType(QMetaType::QByteArray)) : QVariant(previewData));
    if (!q.exec()) { m_error = q.lastError().text(); qWarning() << "createSnapshot:" << m_error; return 0; }
    return q.lastInsertId().toInt();
}

bool Catalog::renameSnapshot(int id, const QString &name) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE snapshots SET name=? WHERE id=?"));
    q.addBindValue(name);
    q.addBindValue(id);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return true;
}

bool Catalog::deleteSnapshot(int id) {
    return exec(QStringLiteral("DELETE FROM snapshots WHERE id=%1").arg(id));
}

QVariantList Catalog::snapshots(int assetId) const {
    QVariantList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, name, created_at, history_end, params, length(preview)>0 FROM snapshots WHERE asset_id=? ORDER BY id"));
    q.addBindValue(assetId);
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap row;
        row[QStringLiteral("id")] = q.value(0).toInt();
        row[QStringLiteral("name")] = q.value(1).toString();
        row[QStringLiteral("createdAt")] = q.value(2).toString();
        row[QStringLiteral("historyEnd")] = q.value(3).toInt();
        row[QStringLiteral("json")] = q.value(4).toString();
        row[QStringLiteral("hasPreview")] = q.value(5).toBool();
        out << row;
    }
    return out;
}

QByteArray Catalog::snapshotPreview(int id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT preview FROM snapshots WHERE id=?"));
    q.addBindValue(id);
    return q.exec() && q.next() ? q.value(0).toByteArray() : QByteArray();
}

// ── batch metadata ──────────────────────────────────────────────────────

static bool setText(QSqlDatabase &db, QString *err, const char *column, const QVector<int> &ids, const QString &text) {
    if (ids.isEmpty()) return true;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE assets SET %1=? WHERE id IN (%2)").arg(QLatin1String(column), idList(ids)));
    q.addBindValue(text.isNull() ? QStringLiteral("") : text);
    if (!q.exec()) { *err = q.lastError().text(); return false; }
    return true;
}
bool Catalog::setCopyright(const QVector<int> &ids, const QString &text) { return setText(m_db, &m_error, "copyright", ids, text); }
bool Catalog::setCreator(const QVector<int> &ids, const QString &text) { return setText(m_db, &m_error, "creator", ids, text); }
bool Catalog::setTitle(const QVector<int> &ids, const QString &text) { return setText(m_db, &m_error, "title", ids, text); }
bool Catalog::setCaption(const QVector<int> &ids, const QString &text) { return setText(m_db, &m_error, "caption", ids, text); }

// ── stacks ──────────────────────────────────────────────────────────────

int Catalog::stackAssets(const QVector<int> &idsIn) {
    if (idsIn.size() < 2) return 0;
    // Everything in the same stacks as the given rows joins too.
    QSet<int> all;
    for (int id : idsIn) {
        all.insert(id);
        const AssetRecord r = asset(id);
        if (r.stackId) for (int m : stackMembers(r.stackId)) all.insert(m);
    }
    QVector<int> ids = assetIds(QStringLiteral("id IN (%1)").arg(idList(QVector<int>(all.begin(), all.end()))), {},
                                QStringLiteral("captured_at, filename, variant"));
    if (ids.size() < 2) return 0;
    QSqlQuery q(m_db);
    int stack = 1;
    if (q.exec(QStringLiteral("SELECT COALESCE(MAX(stack_id), 0) + 1 FROM assets")) && q.next()) stack = q.value(0).toInt();
    if (!m_db.transaction()) return 0;
    for (int i = 0; i < ids.size(); ++i) {
        if (!exec(QStringLiteral("UPDATE assets SET stack_id=%1, stack_pos=%2 WHERE id=%3").arg(stack).arg(i).arg(ids[i]))) { m_db.rollback(); return 0; }
    }
    if (!m_db.commit()) return 0;
    return stack;
}

bool Catalog::unstack(const QVector<int> &ids) {
    if (ids.isEmpty()) return true;
    // Dissolve every stack touched, then renumber nothing: a stack is all or nothing.
    return exec(QStringLiteral("UPDATE assets SET stack_id=NULL, stack_pos=0 WHERE stack_id IN"
                               " (SELECT DISTINCT stack_id FROM assets WHERE id IN (%1) AND stack_id IS NOT NULL)").arg(idList(ids)));
}

int Catalog::autoStack(const QVector<int> &idsIn, int seconds) {
    if (idsIn.isEmpty() || seconds <= 0) return 0;
    // Unstacked rows only, in capture order; a run of neighbours within
    // `seconds` of the previous shot becomes one stack.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, captured_at FROM assets WHERE id IN (%1) AND stack_id IS NULL AND captured_at IS NOT NULL"
                             " ORDER BY captured_at, filename, variant").arg(idList(idsIn)));
    if (!q.exec()) { m_error = q.lastError().text(); return 0; }
    QVector<QVector<int>> runs;
    QDateTime last;
    while (q.next()) {
        const int id = q.value(0).toInt();
        const QDateTime t = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
        if (!t.isValid()) continue;
        if (!runs.isEmpty() && last.isValid() && last.secsTo(t) <= seconds) runs.last() << id;
        else runs << QVector<int>{id};
        last = t;
    }
    int made = 0;
    for (const QVector<int> &run : runs) if (run.size() >= 2 && stackAssets(run)) ++made;
    return made;
}

bool Catalog::setStackTop(int id) {
    const AssetRecord r = asset(id);
    if (!r.id || !r.stackId) return false;
    QVector<int> members = stackMembers(r.stackId);
    members.removeAll(id);
    members.prepend(id);
    if (!m_db.transaction()) return false;
    for (int i = 0; i < members.size(); ++i)
        if (!exec(QStringLiteral("UPDATE assets SET stack_pos=%1 WHERE id=%2").arg(i).arg(members[i]))) { m_db.rollback(); return false; }
    return m_db.commit();
}

QVector<int> Catalog::stackMembers(int stackId) const {
    return stackId ? assetIds(QStringLiteral("stack_id=?"), {stackId}, QStringLiteral("stack_pos, captured_at, filename")) : QVector<int>();
}

// ── volumes ─────────────────────────────────────────────────────────────

QString Catalog::folderPath(int id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT path FROM folders WHERE id=?"));
    q.addBindValue(id);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

int Catalog::rescanFolders() {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, path, online FROM folders"))) return 0;
    int offline = 0;
    QVector<QPair<int, bool>> changes;
    while (q.next()) {
        const bool exists = QDir(q.value(1).toString()).exists();
        if (!exists) ++offline;
        if (exists != q.value(2).toBool()) changes << qMakePair(q.value(0).toInt(), exists);
    }
    for (const auto &c : changes) exec(QStringLiteral("UPDATE folders SET online=%1 WHERE id=%2").arg(c.second ? 1 : 0).arg(c.first));
    return offline;
}

bool Catalog::relinkFolder(int id, const QString &newAbsPath) {
    const QString oldPath = folderPath(id);
    const QString newPath = QDir::cleanPath(newAbsPath);
    if (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath) { m_error = QStringLiteral("nothing to relink"); return false; }
    if (folderIdFor(newPath)) { m_error = QStringLiteral("%1 is already in the catalog").arg(newPath); return false; }
    if (!m_db.transaction()) return false;
    // Exact folder plus everything beneath it, in both tables.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE folders SET path = :new || substr(path,length(:old)+1), online=1"
                             " WHERE path = :old OR substr(path,1,length(:old)+1) = :old || '/'"));
    q.bindValue(QStringLiteral(":new"), newPath); q.bindValue(QStringLiteral(":old"), oldPath);
    bool ok = q.exec();
    if (!ok) m_error = q.lastError().text();
    QSqlQuery a(m_db);
    a.prepare(QStringLiteral("UPDATE assets SET path = :new || substr(path,length(:old)+1) WHERE substr(path,1,length(:old)+1) = :old || '/'"));
    a.bindValue(QStringLiteral(":new"), newPath); a.bindValue(QStringLiteral(":old"), oldPath);
    if (ok && !a.exec()) { ok = false; m_error = a.lastError().text(); }
    QSqlQuery n(m_db);
    n.prepare(QStringLiteral("UPDATE folders SET name=? WHERE id=?"));
    n.addBindValue(QFileInfo(newPath).fileName().isEmpty() ? newPath : QFileInfo(newPath).fileName()); n.addBindValue(id);
    if (ok && !n.exec()) { ok = false; m_error = n.lastError().text(); }
    if (!ok) { m_db.rollback(); return false; }
    return m_db.commit();
}

bool Catalog::movePath(const QString &oldPath, const QString &newPath) {
    const QString from = QDir::cleanPath(oldPath), to = QDir::cleanPath(newPath);
    if (from.isEmpty() || to.isEmpty() || from == to) { m_error = QStringLiteral("nothing to move"); return false; }
    if (assetExists(to)) { m_error = QStringLiteral("%1 is already in the catalog").arg(to); return false; }
    if (!m_db.transaction()) return false;
    const int folder = ensureFolder(QFileInfo(to).absolutePath());
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE assets SET path=?, filename=?, folder_id=? WHERE path=?"));
    q.addBindValue(to); q.addBindValue(QFileInfo(to).fileName()); q.addBindValue(folder); q.addBindValue(from);
    if (!folder || !q.exec()) { m_error = q.lastError().text(); m_db.rollback(); return false; }
    if (q.numRowsAffected() == 0) { m_error = QStringLiteral("%1 is not in the catalog").arg(from); m_db.rollback(); return false; }
    return m_db.commit();
}

int Catalog::journalMove(const QString &fromPath, const QString &toPath) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO file_ops(from_path, to_path, state, created_at) VALUES(?,?,0,?)"));
    q.addBindValue(QDir::cleanPath(fromPath)); q.addBindValue(QDir::cleanPath(toPath));
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) { m_error = q.lastError().text(); return 0; }
    return q.lastInsertId().toInt();
}

bool Catalog::journalDone(int id) {
    return exec(QStringLiteral("UPDATE file_ops SET state=1 WHERE id=%1").arg(id));
}

QVariantList Catalog::pendingMoves() const {
    QVariantList out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, from_path, to_path FROM file_ops WHERE state=0 ORDER BY id"))) return out;
    while (q.next()) {
        QVariantMap m; m[QStringLiteral("id")] = q.value(0).toInt(); m[QStringLiteral("from")] = q.value(1).toString(); m[QStringLiteral("to")] = q.value(2).toString();
        out << m;
    }
    return out;
}

int Catalog::replayMoves() {
    int settled = 0;
    for (const QVariant &v : pendingMoves()) {
        const QVariantMap m = v.toMap();
        const int id = m.value(QStringLiteral("id")).toInt();
        const QString from = m.value(QStringLiteral("from")).toString(), to = m.value(QStringLiteral("to")).toString();
        const bool atFrom = QFile::exists(from), atTo = QFile::exists(to);
        if (atFrom == atTo) continue;                       // both or neither: not ours to guess
        if (atTo && assetExists(from)) { if (!movePath(from, to)) continue; }   // the rename landed, the catalog did not follow
        if (atFrom && assetExists(to)) { if (!movePath(to, from)) continue; }   // the catalog moved ahead of a rename that never happened
        if (journalDone(id)) ++settled;
    }
    return settled;
}

// ── album management ────────────────────────────────────────────────────

bool Catalog::renameAlbum(int id, const QString &name) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE albums SET name=? WHERE id=?"));
    q.addBindValue(name); q.addBindValue(id);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return true;
}

bool Catalog::deleteAlbum(int id) {
    // Children go with it, level by level; album_assets cascade.
    QVector<int> kids;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM albums WHERE parent_id=?"));
    q.addBindValue(id);
    if (q.exec()) while (q.next()) kids << q.value(0).toInt();
    for (int k : kids) deleteAlbum(k);
    q.prepare("UPDATE auto_tag_collection_choices SET enabled=0 WHERE tag IN (SELECT tag FROM auto_tag_albums WHERE album_id=?)");
    q.bindValue(0,id);
    if (!q.exec()) { m_error=q.lastError().text(); return false; }
    return exec(QStringLiteral("DELETE FROM albums WHERE id=%1").arg(id));
}

bool Catalog::setSmartAlbum(int id, const QString &name, const QString &smartRule, const QString &smartJson) {
    if (smartRule.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE albums SET name=?, smart_rule=?, smart_json=? WHERE id=?"));
    q.addBindValue(name); q.addBindValue(smartRule); q.addBindValue(smartJson.isEmpty() ? QVariant() : QVariant(smartJson)); q.addBindValue(id);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    return true;
}

bool Catalog::removeFromAlbum(int albumId, const QVector<int> &ids) {
    if (ids.isEmpty()) return true;
    return exec(QStringLiteral("DELETE FROM album_assets WHERE album_id=%1 AND asset_id IN (%2)").arg(albumId).arg(idList(ids)));
}
