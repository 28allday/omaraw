#include "importer.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QElapsedTimer>
#include <QUrl>
#include <QDateTime>
#include <QCryptographicHash>
#include "catalog.h"
#include "metadata.h"
#include "thumbnailer.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <algorithm>

Importer::Importer(QObject *parent) : QObject(parent) {
    // Keep card I/O and interactive decoding responsive. Library-size jobs
    // take priority over Loupe warming, with at most two background decodes.
    m_previews.setMaxThreadCount(qBound(1, Thumbnailer::threadCount(), 2));
    m_previews.setThreadPriority(QThread::LowPriority);
    m_previews.setObjectName(QStringLiteral("omaraw-import-previews"));
}

Importer::~Importer() {
    m_cancel = true;
    m_previewCancel = true;
    // QThread::create() runs a function, not an event loop, so quit() has
    // nothing to quit: only the cancel flag ends the run. The wait must be
    // unbounded — abandoning the thread leaves run() emitting progress into
    // an object that is already being destroyed.
    if (m_thread) { m_thread->wait(); delete m_thread; m_thread = nullptr; }
    m_previews.clear();
    m_previews.waitForDone();
}

QStringList Importer::scan(const QString &folder, bool recursive, const std::atomic<bool> *cancel, std::atomic<int> *walked) {
    QStringList out;
    const QStringList exts = Metadata::imageExtensions();
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        // A deep or unresponsive tree is walked before the first file is
        // read; without this the run cannot be stopped until the walk ends.
        if (cancel && cancel->load()) return {};
        const QString path = it.next();
        if (walked) walked->fetch_add(1);
        const QFileInfo fi(path);
        const QString name = fi.fileName();
        // AppleDouble resource forks and dotfiles are never photographs.
        if (name.startsWith(QLatin1Char('.'))) continue;
        if (!exts.contains(fi.suffix().toLower())) continue;
        out << path;
    }
    std::sort(out.begin(), out.end());
    return out;
}

ImportOptions ImportOptions::fromMap(const QVariantMap &m) {
    ImportOptions o;
    if (m.contains(QStringLiteral("mode"))) o.mode = m.value(QStringLiteral("mode")).toString();
    QString dest = m.value(QStringLiteral("destination")).toString();
    if (dest.startsWith(QLatin1String("file:"))) dest = QUrl(dest).toLocalFile();
    o.destination = dest;
    if (m.contains(QStringLiteral("subfolder"))) o.subfolder = m.value(QStringLiteral("subfolder")).toString();
    if (m.contains(QStringLiteral("rename"))) o.rename = m.value(QStringLiteral("rename")).toString();
    o.prefix = m.value(QStringLiteral("prefix")).toString().trimmed();
    if (m.contains(QStringLiteral("skipDuplicates"))) o.skipDuplicates = m.value(QStringLiteral("skipDuplicates")).toBool();
    QString backup = m.value(QStringLiteral("backup")).toString();
    if (backup.startsWith(QLatin1String("file:"))) backup = QUrl(backup).toLocalFile();
    o.backup = backup.trimmed();
    o.eject = m.value(QStringLiteral("eject")).toBool();
    o.hashDuplicates = m.value(QStringLiteral("hashDuplicates")).toBool();
    o.autoTag = m.value(QStringLiteral("autoTag")).toBool();
    o.autoStackSeconds = qBound(0, m.value(QStringLiteral("autoStackSeconds")).toInt(), 120);
    if (m.contains(QStringLiteral("recursive"))) o.recursive = m.value(QStringLiteral("recursive")).toBool();
    o.selectedOnly = m.contains(QStringLiteral("selectedFiles"));
    o.selectedFiles = m.value(QStringLiteral("selectedFiles")).toStringList();
    const auto metadata = m.value(QStringLiteral("metadata")).toMap();
    for (const char *key : {"creator", "copyright", "title", "caption", "keywords"})
        if (metadata.contains(QLatin1String(key))) o.metadata.insert(QLatin1String(key), metadata.value(QLatin1String(key)).toString().trimmed());
    return o;
}

QVariantMap ImportOptions::toMap() const {
    return {{QStringLiteral("mode"), mode}, {QStringLiteral("destination"), destination}, {QStringLiteral("subfolder"), subfolder},
            {QStringLiteral("rename"), rename}, {QStringLiteral("prefix"), prefix}, {QStringLiteral("skipDuplicates"), skipDuplicates},
            {QStringLiteral("backup"), backup}, {QStringLiteral("eject"), eject}, {QStringLiteral("hashDuplicates"), hashDuplicates}, {QStringLiteral("metadata"), metadata}, {QStringLiteral("recursive"), recursive}, {QStringLiteral("autoTag"), autoTag},
            {QStringLiteral("autoStackSeconds"), autoStackSeconds}};
}

QString Importer::targetPath(const ImportOptions &o, const QString &source, const QString &capturedAt, qint64 mtime, int seq) {
    const QFileInfo fi(source);
    QDate day = QDateTime::fromString(capturedAt, Qt::ISODate).date();
    if (!day.isValid()) day = QDateTime::fromSecsSinceEpoch(mtime > 0 ? mtime : fi.lastModified().toSecsSinceEpoch()).date();
    QString dir = o.destination;
    if (o.subfolder == QLatin1String("year")) dir += QLatin1Char('/') + day.toString(QStringLiteral("yyyy"));
    else if (o.subfolder == QLatin1String("year-date")) dir += QLatin1Char('/') + day.toString(QStringLiteral("yyyy")) + QLatin1Char('/') + day.toString(QStringLiteral("yyyy-MM-dd"));
    else if (o.subfolder == QLatin1String("date")) dir += QLatin1Char('/') + day.toString(QStringLiteral("yyyy-MM-dd"));
    QString name = fi.fileName();
    if (o.rename == QLatin1String("date-name")) name = day.toString(QStringLiteral("yyyyMMdd")) + QLatin1Char('_') + fi.fileName();
    else if (o.rename == QLatin1String("prefix-seq")) {
        const QString stem = o.prefix.isEmpty() ? QStringLiteral("IMG") : o.prefix;
        name = stem + QLatin1Char('-') + QStringLiteral("%1").arg(seq, 4, 10, QLatin1Char('0')) + (fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix());
    }
    return dir + QLatin1Char('/') + name;
}

QString Importer::sha256(const QString &path, const std::atomic<bool> *cancel) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!cancel) {
        if (!h.addData(&f)) return QString();
        return QString::fromLatin1(h.result().toHex());
    }
    // Read in blocks so a verified import of a large file on a slow card
    // reader still answers the cancel flag between them.
    QByteArray block(1 << 20, Qt::Uninitialized);
    for (;;) {
        if (cancel->load()) return QString();
        const qint64 read = f.read(block.data(), block.size());
        if (read < 0) return QString();
        if (read == 0) break;
        h.addData(QByteArrayView(block.constData(), read));
    }
    return QString::fromLatin1(h.result().toHex());
}

void Importer::start(const QString &catalogPath, const QString &folder, const ImportOptions &options) {
    if (m_running) return;
    m_running = true;
    m_cancel = false;
    emit runningChanged();
    const QString cacheDir = Thumbnailer::defaultCacheDir();
    m_thread = QThread::create([this, catalogPath, folder, options, cacheDir] { run(catalogPath, folder, options, cacheDir); });
    connect(m_thread, &QThread::finished, this, [this] {
        m_thread->deleteLater();
        m_thread = nullptr;
        m_running = false;
        emit runningChanged();
    });
    m_thread->start();
}

int Importer::importFile(Catalog &cat, const QString &path, int importId, Metadata::Sidecar *sidecar) {
    AssetRecord rec;
    if (!Metadata::read(path, rec)) return 0;
    rec.folderId = cat.ensureFolder(QFileInfo(path).absolutePath());
    rec.importId = importId;
    const bool isNew = !cat.assetExists(rec.path);
    // A DNG, JPEG or TIFF may carry embedded XMP metadata:
    // read it like a sidecar when there is none.
    const Metadata::Sidecar sc = Metadata::readSidecar(path, true);
    if (sc.present) {
        rec.sidecarRating = sc.rating; rec.sidecarLabel = sc.label; rec.sidecarSeen = QFileInfo(sc.file).lastModified().toSecsSinceEpoch();
        if (!sc.title.isEmpty()) rec.title = sc.title;       // the sidecar's words win over the file's, on a new row
        if (!sc.caption.isEmpty()) rec.caption = sc.caption;
        if (!sc.creator.isEmpty()) rec.creator = sc.creator;
        if (!sc.copyright.isEmpty()) rec.copyright = sc.copyright;
    }
    const int id = rec.folderId ? cat.upsertAsset(rec) : 0;
    if (id && isNew) for (const QString &k : sc.keywords) cat.addKeyword({id}, k);
    if (sidecar) *sidecar = sc;
    return id;
}

// Copy or move one file (and its sidecar) to where the options say; the
// path it landed on, or "" with the reason in *why.
static QString unusedTarget(const QString &target) {
    const QFileInfo t(target);
    for (int n = 1; n < 1000; ++n) {
        const QString out = n == 1 ? target : t.absolutePath() + QLatin1Char('/') + t.completeBaseName()
            + QStringLiteral(" (%1)").arg(n) + (t.suffix().isEmpty() ? QString() : QLatin1Char('.') + t.suffix());
        // An orphaned sidecar must not become the new photo's metadata, and
        // a RAW with another extension may already use the same basename.
        QStringList paths = Metadata::sidecarNames(out); paths.prepend(out);
        bool occupied = false;
        for (const QString &path : paths) {
            const QFileInfo info(path);
            if (info.exists() || info.isSymLink()) { occupied = true; break; }
        }
        if (!occupied) return out;
    }
    return {};
}

static QString placeFile(const ImportOptions &o, const QString &source, const QString &target, QString *why,
                         const std::atomic<bool> *cancel = nullptr, Catalog *catalog = nullptr) {
    QDir().mkpath(QFileInfo(target).absolutePath());
    const QString out = unusedTarget(target);
    if (out.isEmpty()) { *why = QStringLiteral("No unused destination name is available"); return {}; }
    const bool move = o.mode == QLatin1String("move");
    const int journal = catalog && move ? catalog->journalMove(source, out) : 0;
    if (catalog && move && !journal) { *why = catalog->lastError(); return {}; }
    QFile src(source);
    const bool ok = move ? src.rename(out) : src.copy(out);
    if (!ok) { if (journal) catalog->journalDone(journal); *why = src.errorString(); return QString(); }
    if (o.mode == QLatin1String("verify")) {
        const QString a = Importer::sha256(source, cancel), b = Importer::sha256(out, cancel);
        // A cancelled or mismatched verify says nothing good about the copy,
        // so it does not stay. Discarding it is only safe while the source
        // still exists, which holds because `verify` copies and never moves.
        // Should the two ever be combined, `out` would be the one remaining
        // copy of a photograph: check rather than trust the mode names.
        if ((a.isEmpty() || a != b) && !move && QFileInfo::exists(source)) {
            QFile::remove(out);
            *why = cancel && cancel->load() ? QStringLiteral("cancelled") : QStringLiteral("copy did not verify");
            return QString();
        }
        if (a.isEmpty() || a != b) {
            // The source is gone, so the unverified file is all there is.
            *why = QStringLiteral("copy did not verify and the original is no longer there; %1 was kept").arg(out);
            return QString();
        }
    }
    QVector<QPair<QString, QString>> placedSidecars;
    const auto rollback = [&] {
        const bool restored = move ? QFile::rename(out, source) : QFile::remove(out);
        if (restored) {
            for (const auto &sc : placedSidecars) QFile::remove(sc.second);
            if (journal) catalog->journalDone(journal);
        }
        return restored;
    };
    for (const auto &sc : Metadata::sidecarsFollowing(source, out)) {
        QFile s(sc.first);
        // Keep the source sidecars until the photo and catalog have both
        // moved. A failed move can then restore the original without also
        // relying on every sidecar rename succeeding in the reverse direction.
        if (!s.copy(sc.second)) {
            *why = QStringLiteral("Could not transfer sidecar: %1").arg(s.errorString());
            rollback(); return {};
        }
        placedSidecars.append(sc);
    }
    if (journal && !catalog->movePath(source, out)) {
        *why = catalog->lastError(); rollback(); return {};
    }
    if (journal) catalog->journalDone(journal);
    if (move) for (const auto &sc : placedSidecars) QFile::remove(sc.first);
    return out;
}

// The second copy: the same subfolder and name under the backup folder,
// never overwriting, read back and compared when the import verifies.
static bool backupFile(const ImportOptions &o, const QString &placed, const QString &capturedAt, qint64 mtime, int seq, QString *why,
                       const std::atomic<bool> *cancel = nullptr) {
    ImportOptions b = o;
    b.destination = o.backup;
    // The primary copy already has its final name (including any collision
    // suffix). Applying date-name again would prefix the date a second time.
    if (!o.inPlace()) b.rename = QStringLiteral("keep");
    if (b.inPlace()) b.mode = QStringLiteral("copy");             // an in-place add still gets its second copy
    const QString target = Importer::targetPath(b, placed, capturedAt, mtime, seq);
    b.mode = o.mode == QLatin1String("verify") ? QStringLiteral("verify") : QStringLiteral("copy");
    return !placeFile(b, placed, target, why, cancel).isEmpty();
}

QString Importer::selectionError(const QString &canonicalFolder, const QString &path) {
    const QFileInfo file(path);
    if (canonicalFolder.isEmpty() || !file.isFile() || !file.isReadable())
        return tr("This photo is no longer available. Refresh the source folder.");
    if (!Metadata::imageExtensions().contains(file.suffix().toLower()))
        return tr("This file type cannot be imported.");
    const QString prefix = canonicalFolder.endsWith('/') ? canonicalFolder : canonicalFolder + '/';
    if (!file.canonicalFilePath().startsWith(prefix))
        return tr("This photo is outside the source folder. Browse its original folder to import it.");
    return {};
}

void Importer::run(const QString &catalogPath, const QString &folder, const ImportOptions &options, const QString &cacheDir) {
    m_copied = 0; m_duplicates = 0; m_failed = 0; m_backedUp = 0; m_backupFailed = 0; m_cancelled = false;
    m_sidecarEdits.clear();
    Catalog cat(QStringLiteral("import-%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId())));
    if (!cat.open(catalogPath)) {
        emit finished(0, 0, 0, cat.lastError());
        return;
    }
    QStringList files;
    if (options.selectedOnly) {
        const QString source = QFileInfo(folder).canonicalFilePath();
        if (source.isEmpty()) { emit finished(0, 0, 0, tr("The source folder is no longer available")); return; }
        for (const QString &entry : options.selectedFiles) {
            if (m_cancel) break;
            const QFileInfo file(entry);
            const QString error = selectionError(source, entry);
            if (!error.isEmpty()) {
                emit finished(0, 0, 0, tr("%1\n%2").arg(error, entry));
                return;
            }
            files << file.absoluteFilePath();
        }
        files.removeDuplicates();
        if (files.isEmpty()) { m_cancelled = m_cancel.load(); emit finished(0, 0, 0, tr("No photos selected for import")); return; }
    } else files = scan(folder, options.recursive, &m_cancel);
    const int importId = cat.beginImport(folder);
    if (!importId) { emit finished(0, 0, 0, cat.lastError()); return; }
    int imported = 0, skipped = 0, seq = 0;
    emit progress(0, files.size(), QString());
    int sinceCommit = 0;
    int pendingImported = 0;
    qsizetype committedSidecarEdits = 0;
    QString error;
    bool open = false;
    QElapsedTimer sinceCommitTime; sinceCommitTime.start();
    // The catalog allows one writer at a time, and the window's own writes
    // (a rating, a keyword) wait at most a few seconds. So the import never
    // holds a transaction across slow file work: copying, verifying,
    // hashing or backing up a file happens with none open. Not even a read
    // one: a read inside a transaction pins its snapshot, and should the
    // window write meanwhile, the first write after the copy could not
    // proceed at all (SQLITE_BUSY_SNAPSHOT) and the copied file would go
    // uncatalogued. Writes take the lock up front (BEGIN IMMEDIATE), which
    // waits its turn instead.
    const auto begin = [&] {
        if (open) return true;
        QSqlQuery q(cat.db());
        open = q.exec(QStringLiteral("BEGIN IMMEDIATE"));
        if (!open) error = q.lastError().text();
        committedSidecarEdits = m_sidecarEdits.size();
        sinceCommitTime.restart();
        return open;
    };
    const auto release = [&] {
        bool ok = true;
        const bool hasAssets = pendingImported > 0;
        bool counted = true;
        if (open && hasAssets) {
            // Previous Import and its sidebar count can show this batch
            // before it finishes, but only with rows committed alongside it.
            QSqlQuery count(cat.db());
            count.prepare(QStringLiteral("UPDATE imports SET count=? WHERE id=?"));
            count.addBindValue(imported); count.addBindValue(importId);
            counted = count.exec();
            if (!counted) error = count.lastError().text();
        }
        if (open && (!counted || !cat.db().commit())) {
            if (counted) error = cat.db().lastError().text();
            cat.db().rollback();
            imported -= pendingImported;
            m_sidecarEdits.resize(committedSidecarEdits);
            ok = false;
        }
        open = false; sinceCommit = 0;
        pendingImported = 0;
        if (ok && hasAssets) emit assetsCommitted();
        return ok;
    };
    QString pendingHash;
    for (int i = 0; i < files.size(); ++i) {
        if (m_cancel) break;
        pendingHash.clear();
        QString path = files[i];
        const bool wantsBackup = !options.backup.isEmpty();
        if ((!options.inPlace() || options.hashDuplicates || wantsBackup) && !release()) break;
        if (!options.inPlace() || options.skipDuplicates || wantsBackup) {
            AssetRecord meta;
            const bool readable = Metadata::read(path, meta);
            if (readable && options.skipDuplicates && cat.findDuplicate(meta.filename, meta.size, meta.capturedAt) > 0 && !cat.assetExists(path)) {
                ++m_duplicates; ++skipped;
                continue;
            }
            // By checksum: the same bytes under any name. The hash is kept on the new row.
            if (readable && options.skipDuplicates && options.hashDuplicates && !cat.assetExists(path)) {
                const QString h = sha256(path, &m_cancel);
                if (m_cancel) break;
                if (!h.isEmpty() && cat.findByHash(h) > 0) { ++m_duplicates; ++skipped; continue; }
                pendingHash = h;
            }
            if (readable && !options.inPlace()) {
                QString why;
                const bool moveExisting = options.mode == QLatin1String("move") && cat.assetExists(meta.path);
                const QString placed = placeFile(options, meta.path, targetPath(options, path, meta.capturedAt, meta.mtime, ++seq), &why, &m_cancel,
                                                 moveExisting ? &cat : nullptr);
                // A cancelled copy is neither a failure nor a skip: the run
                // simply stops here, before this file is counted either way.
                if (m_cancel && placed.isEmpty()) break;
                if (placed.isEmpty()) { ++m_failed; ++skipped; continue; }
                if (moveExisting) {
                    emit fileMoved(meta.path, placed);
                }
                ++m_copied;
                path = placed;
            } else if (readable && wantsBackup) {
                ++seq;
            }
            if (readable && wantsBackup) {
                QString why;
                const bool backedUp = backupFile(options, path, meta.capturedAt, meta.mtime, seq, &why, &m_cancel);
                // The primary copy/move has landed; catalogue it even if
                // cancellation stopped its second copy.
                if (backedUp) ++m_backedUp; else ++m_backupFailed;
            }
        }
        if (!begin()) break;
        const bool newAsset = !cat.assetExists(path);
        Metadata::Sidecar sc;
        const int id = importFile(cat, path, importId, &sc);
        if (id && newAsset && options.autoTag && !cat.queueAutoTags({id})) {
            error=cat.lastError(); cat.db().rollback(); imported-=pendingImported;
            m_sidecarEdits.resize(committedSidecarEdits); pendingImported=0; open=false; break;
        }
        if (id && newAsset && options.autoFirstEdit) {
            // Already inside the import transaction. setAutoPending() owns
            // its own transaction, so use the same insert here without
            // committing the other pending files early.
            QSqlQuery pending(cat.db());
            pending.prepare(QStringLiteral("INSERT OR IGNORE INTO auto_pending(asset_id) VALUES(?)"));
            pending.addBindValue(id);
            if (!pending.exec()) {
                error = pending.lastError().text();
                cat.db().rollback();
                imported -= pendingImported;
                m_sidecarEdits.resize(committedSidecarEdits);
                pendingImported = 0;
                open = false;
                break;
            }
        }
        if (id && newAsset && (sc.hasEdit || (sc.hasCameraRaw && !sc.cameraRawOurs)))
            m_sidecarEdits.append({id, path, sc.file, sc.hasEdit, Metadata::isRawExtension(QFileInfo(path).suffix().toLower()), sc.packet});
        if (id && newAsset && !options.metadata.isEmpty()) {
            const auto text = [&](const char *key) { return options.metadata.value(QLatin1String(key)).toString(); };
            if (!text("creator").isEmpty()) cat.setCreator({id}, text("creator"));
            if (!text("copyright").isEmpty()) cat.setCopyright({id}, text("copyright"));
            if (!text("title").isEmpty()) cat.setTitle({id}, text("title"));
            if (!text("caption").isEmpty()) cat.setCaption({id}, text("caption"));
            for (const auto &keyword : text("keywords").split(',', Qt::SkipEmptyParts)) cat.addKeyword({id}, keyword.trimmed());
        }
        if (id && !pendingHash.isEmpty()) cat.setHash(id, pendingHash);
        pendingHash.clear();
        if (id) {
            ++imported; ++pendingImported;
            // Copy/verification/backup and metadata reads are complete. Decode
            // from the final path while this worker imports the next photo.
            const qint64 mtime = QFileInfo(path).lastModified().toSecsSinceEpoch();
            m_previews.start([this, cacheDir, path, mtime] {
                Thumbnailer::prepare(cacheDir, path, mtime, 512, &m_previewCancel);
                if (!m_previewCancel.load())
                    m_previews.start([this, cacheDir, path, mtime] {
                        Thumbnailer::prepare(cacheDir, path, mtime, 2048, &m_previewCancel);
                    }, -1);
            });
        } else ++skipped;
        // Bounded transactions: a crash loses at most 50 files of work and
        // the GUI's reads never wait on one giant write.
        ++sinceCommit;
        if ((imported == 1 || sinceCommit >= 50 || sinceCommitTime.elapsed() > 500) && !release()) break;
        if (i % 5 == 0 || i == files.size() - 1) emit progress(i + 1, files.size(), QFileInfo(path).fileName());
    }
    release();
    m_cancelled = m_cancel.load();
    cat.finishImport(importId, imported);
    emit finished(imported, skipped, importId, error);
}
