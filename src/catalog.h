// The OmaRAW catalog: one SQLite file, WAL mode, versioned migrations.
// RAW originals are never touched; everything the user does to a photo
// (rating, flag, label, keywords, albums, edits later) lives here.
#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

struct AssetRecord {
    int id = 0;
    int folderId = 0;
    QString path;
    QString filename;
    QString format;        // "RAF", "NEF", "DNG", "JPEG", ...
    bool isRaw = false;
    qint64 size = 0;
    qint64 mtime = 0;
    int width = 0, height = 0;
    int orientation = 1;   // EXIF orientation, 1 = upright
    QString capturedAt;    // ISO 8601, local time, "" when unknown
    QString make, model, lens;
    double focalLength = 0, aperture = 0, shutter = 0; // shutter in seconds
    int iso = 0;
    int bitDepth = 0;
    int rating = 0;        // 0..5
    int flag = 0;          // -1 reject, 0 none, 1 pick
    QString label;         // "", red, orange, yellow, green, blue, purple
    double gpsLat = 0, gpsLon = 0;
    bool hasGps = false;
    QString copyright, creator;
    QString title, caption;   // dc:title / dc:description — the catalog's, written to sidecars and exports
    QString hash;             // SHA-256 of the file, "" until computed (import option or on demand)
    int importId = 0;
    bool edited = false;
    int editRev = 0;
    int editFlags = 0;     // 1 crop, 2 local adjustment, 4 retouch spot — what the edit holds
    // Variants (virtual copies): the same file with its own history and
    // metadata. `variant` is the engine's version number (0 = master);
    // variantOf is the master's id (0 on a master).
    int variant = 0;
    int variantOf = 0;
    QString variantName;
    // Stacks: photos grouped under a top (stackPos 0); stackCount is the
    // member count (0 when not stacked).
    int stackId = 0;
    int stackPos = 0;
    int stackCount = 0;
    // From a sidecar at import: applied only to a NEW row, never over catalog edits.
    int sidecarRating = -1;
    QString sidecarLabel;
    // The sidecar's mtime (epoch seconds) when the catalog last read or
    // wrote it; 0 = never. A newer file on disk was changed outside.
    qint64 sidecarSeen = 0;
};

class Catalog {
public:
    // `connection` names the QSqlDatabase; each thread needs its own.
    explicit Catalog(const QString &connection = QStringLiteral("catalog"));
    ~Catalog();

    // Opening a catalog written by an older schema copies it first (see
    // backupTo); the copy's path is in lastBackup() afterwards.
    bool open(const QString &path);
    // A consistent copy of the open catalog (VACUUM INTO), WAL and all.
    bool backupTo(const QString &dest);
    // <catalog dir>/backups; backups are named <stem>-<kind>-<stamp>.db.
    QString backupDir() const;
    QString backupPath(const QString &kind) const;
    QString lastBackup() const { return m_lastBackup; }
    // "ok", or SQLite's findings (integrity_check + foreign_key_check) one per line.
    QString integrityCheck() const;
    void close();
    bool isOpen() const { return m_db.isOpen(); }
    QString path() const { return m_path; }
    QString lastError() const { return m_error; }

    static int schemaVersion() { return 22; }
    int currentVersion() const;

    // Folders. Paths are absolute and canonical; parents are created on
    // demand so the tree is always connected.
    int ensureFolder(const QString &absPath);
    QVariantList folderTree() const; // depth-first rows: id, path, name, depth, count, online, watched
    // Watched folders: new files that land in one are imported in place.
    bool setFolderWatched(int id, bool on);
    QStringList watchedFolders() const;
    QString folderPath(int id) const;
    // Marks every folder online/offline by whether its directory exists;
    // returns how many are offline.
    int rescanFolders();
    // Points a folder (and its subfolders and every photo under them) at
    // a new location, e.g. after a drive moved. Fails when the new path
    // is already catalogued elsewhere.
    bool relinkFolder(int id, const QString &newAbsPath);
    // One file renamed or moved: every variant's row follows (path, filename,
    // folder). The folder is created in the catalog when new.
    bool movePath(const QString &oldPath, const QString &newPath);
    // The move journal: an intent is written before the disk is touched and
    // closed after the catalog followed, so a crash in between leaves a
    // pending row that replayMoves() settles from what the disk says.
    int journalMove(const QString &fromPath, const QString &toPath);
    bool journalDone(int id);
    QVariantList pendingMoves() const;   // [{id, from, to}]
    // Settles pending rows: the catalog follows whichever path exists on
    // disk; a row whose file is at both or neither stays pending. Returns
    // how many rows were settled.
    int replayMoves();

    // Assets
    int upsertAsset(const AssetRecord &rec); // returns id; keeps user fields on re-import
    bool assetExists(const QString &path) const;
    int idForPath(const QString &path, int variant = 0) const;
    QVector<int> assetIdsForPath(const QString &path) const;   // every variant of the file
    // Marks the asset edited and bumps its edit revision (thumbnail cache-buster).
    // flags < 0 leaves the stored edit flags alone.
    bool markEdited(int id, int flags = -1);
    AssetRecord asset(int id) const;
    QVector<int> assetIds(const QString &whereSql = QString(),
                          const QVariantList &binds = QVariantList(),
                          const QString &orderSql = QStringLiteral("captured_at, filename")) const;
    int count(const QString &whereSql = QString(), const QVariantList &binds = QVariantList()) const;

    // Automatic subject tags are catalogue-only; decisions survive rescanning.
    QVariantList autoTags(int id) const;
    QVariantList autoTagCollections() const;
    bool setAutoTagCollectionEnabled(const QString &key, bool enabled);
    QString autoTagScanStatus(int id) const;
    bool setAutoTagDecision(int id, const QString &tag, int decision);
    bool queueAutoTags(const QVector<int> &ids, bool force = false);
    QVariantMap nextAutoTagJob() const;
    int pendingAutoTags() const;
    int failedAutoTagsForImport(int importId) const;
    bool resetAutoTagDecisions(int id);
    bool finishAutoTagJob(int id, int generation, const QVariantMap &scores, const QString &error);
    bool ensureAutoTagAlbum(const QString &tag, const QString &name, const QString &rule, const QString &json);

    bool setRating(const QVector<int> &ids, int rating);
    bool setFlag(const QVector<int> &ids, int flag);
    bool setLabel(const QVector<int> &ids, const QString &label);
    bool setSidecarSeen(const QVector<int> &ids, qint64 mtime);
    bool removeAssets(const QVector<int> &ids); // catalog only, never the disk

    // Batch metadata over many rows.
    bool setCopyright(const QVector<int> &ids, const QString &text);
    bool setCreator(const QVector<int> &ids, const QString &text);
    bool setTitle(const QVector<int> &ids, const QString &text);
    bool setCaption(const QVector<int> &ids, const QString &text);

    // Persistent AI lineage is separate from user-managed stacks.
    bool addAiVersion(int sourceId, int resultId, const QString &operation);
    QVariantList aiVersions(int assetId) const;

    // Stacks. stackAssets groups `ids` (capture order; the first becomes
    // the top) into one new stack, merging any stacks they were in;
    // returns the stack id. autoStack groups the given ids whose capture
    // times fall within `seconds` of the previous one; returns stacks made.
    int stackAssets(const QVector<int> &ids);
    bool unstack(const QVector<int> &ids);
    int autoStack(const QVector<int> &ids, int seconds);
    bool setStackTop(int id);
    QVector<int> stackMembers(int stackId) const; // top first

    // Variants. A variant row copies the master's file and camera fields
    // and starts with its rating/flag/label/keywords; it lists as its own
    // asset everywhere. Returns the new id.
    int createVariant(int masterId, int version, const QString &name);
    QVector<int> variantsOf(int masterId) const; // masters only, ordered by version
    bool setVariantName(int id, const QString &name);
    // Make variant `id` the master: rows swap their version numbers and
    // every sibling re-points at the new master.
    bool promoteVariant(int id);

    // Develop snapshots: a named capture of an asset's settings plus a
    // small preview. Rows: id, assetId, name, createdAt, historyEnd, json.
    int createSnapshot(int assetId, const QString &name, int historyEnd, const QString &json, const QByteArray &previewData);
    bool renameSnapshot(int id, const QString &name);
    bool deleteSnapshot(int id);
    QVariantList snapshots(int assetId) const;
    QByteArray snapshotPreview(int id) const;

    // Keywords
    QStringList keywordsFor(int assetId) const;
    // Keywords are paths: "Travel/Italy/Venice". Adding one makes sure its
    // ancestors exist as keywords of their own (the photo is linked to the
    // leaf only); the tree counts a keyword's photos with its descendants'.
    bool addKeyword(const QVector<int> &ids, const QString &keyword);
    bool removeKeyword(const QVector<int> &ids, const QString &keyword);
    // [{id, path, name (the leaf), depth, count}] ordered by path, parents first.
    QVariantList keywordTree() const;
    int keywordId(const QString &path) const;
    QString keywordPath(int id) const;
    // Removes the keyword and everything under it, from every photo.
    bool deleteKeyword(int id);
    static QString normalizeKeyword(const QString &keyword);
    // Synonyms: other words for a keyword; search finds a photo by them,
    // and typing one suggests the keyword. A keyword's synonyms go with it.
    QStringList keywordSynonyms(int keywordId) const;
    bool setKeywordSynonyms(int keywordId, const QStringList &synonyms);
    // Keywords (full paths) whose name or synonym contains `text`, best first, at most `limit`.
    QStringList suggestKeywords(const QString &text, int limit = 8) const;
    QStringList allKeywords() const;
    // Every distinct "make model", sorted; blanks left out.
    QStringList cameras() const;

    // Albums (plain; smart albums are stored rules)
    int createAlbum(const QString &name, const QString &smartRule = QString(), const QString &smartJson = QString(), int parentId = 0);
    // rows: id, name, rule, json, count, parent, depth — parents first, children beneath, each level by name
    QVariantList albums(bool smart) const;
    bool setAlbumParent(int id, int parentId);   // 0 = top level; refuses a cycle
    bool setAlbumCover(int id, int assetId);     // 0 = none
    // Custom order inside an album: positions renumbered 0..n-1 in the given
    // order; moveInAlbum shifts one member by `delta` places.
    bool setAlbumOrder(int albumId, const QVector<int> &assetIds);
    bool moveInAlbum(int albumId, int assetId, int delta);
    bool moveInAlbumTo(int albumId, int assetId, int index);   // to the given place in the order
    QVector<int> albumOrder(int albumId) const;  // asset ids by position, ties by id
    // When every variant of the file was last exported (ISO), for Recently Exported.
    bool markExported(int id);
    bool renameAlbum(int id, const QString &name);
    bool deleteAlbum(int id);
    bool setSmartAlbum(int id, const QString &name, const QString &smartRule, const QString &smartJson);
    bool addToAlbum(int albumId, const QVector<int> &ids);
    bool removeFromAlbum(int albumId, const QVector<int> &ids);
    QVector<int> albumAssetIds(int albumId) const;

    // Develop presets: name + JSON array of {op, field, value}. Built-ins are not stored.
    int createPreset(const QString &name, const QString &json, const QString &category = QString(), const QStringList &tags = {});
    bool setPresetMetadata(int id, const QString &category, const QStringList &tags);
    bool deletePreset(int id);
    QVariantList presets() const; // rows: id, name, json, category, tags
    // Auto-apply rules: a named preset for photos matching camera / lens
    // (contains), ISO/aperture/shutter ranges (0 = open; shutter in seconds)
    // and a format ("RAW" = any raw).
    int createPresetRule(const QString &preset, const QVariantMap &rule);
    bool deletePresetRule(int id);
    QVariantList presetRules() const; // rows: id, preset, camera, lens, isoMin, isoMax, format
    QVector<int> assetIdsOfImport(int importId) const;
    // Photos imported but not yet given their automatic first edit (Backend
    // gives it the first time Develop opens one).
    void setAutoPending(const QVector<int> &ids);
    bool isAutoPending(int id) const;
    void clearAutoPending(int id);
    // A master already catalogued with this file name and size (and capture
    // time when known) - the import's duplicate check. 0 when none.
    int findDuplicate(const QString &filename, qint64 size, const QString &capturedAt) const;
    int findByHash(const QString &hash) const;          // a master with this checksum, or 0
    bool setHash(int id, const QString &hash);
    // Masters sharing a checksum with another master: [[id, id, …], …].
    QList<QVector<int>> duplicateGroups() const;

    // Imports
    int beginImport(const QString &source);
    void finishImport(int importId, int count);
    QVariantList recentImports(int limit = 5) const;
    int lastImportId() const;

    QSqlDatabase &db() { return m_db; }
    const QSqlDatabase &db() const { return m_db; }

private:
    bool addKeywordRows(const QVector<int> &ids, const QString &kw);
    bool migrate();
    bool repairAiImports();
    bool exec(const QString &sql);
    int folderIdFor(const QString &absPath) const;

    QString m_connection;
    QString m_path;
    QString m_lastBackup;
    QString m_error;
    QSqlDatabase m_db;
};
