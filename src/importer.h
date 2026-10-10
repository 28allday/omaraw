// Folder import on a worker thread: walk, read metadata, upsert. Files
// stay in place by default; copy, move and verified copy lay them under a
// destination with date subfolders and a rename pattern first. Progress
// and the result come back as queued signals; the GUI thread never blocks.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include "metadata.h"

class Catalog;
struct AssetRecord;

struct ImportOptions {
    QString mode = QStringLiteral("add");        // add | copy | move | verify (copy, then hashes must match)
    QString destination;                          // folder for copy / move / verify
    QString subfolder = QStringLiteral("none");  // none | year | year-date | date, by capture date (file date when unknown)
    QString rename = QStringLiteral("keep");     // keep | date-name (YYYYMMDD_name) | prefix-seq (prefix-0001)
    QString prefix;
    bool skipDuplicates = true;                   // a photo with the same name, size and capture time already catalogued
    bool hashDuplicates = false;                  // …or the same checksum, whatever its name: every file is read once more
    QString backup;                               // a second copy of every imported file lands here too ("" = none), same subfolders and names
    bool eject = false;                           // power the source's card or drive off when the import is done
    int autoStackSeconds = 0;                     // shots within this many seconds of the previous become a stack (0 = off)
    QVariantMap metadata;                         // explicit values for new catalog rows only; keywords are additive
    bool recursive = true;
    // An explicit empty selection means import nothing, never the whole folder.
    // Selection is per run and is deliberately omitted from saved options.
    bool selectedOnly = false;
    QStringList selectedFiles;
    // Runtime policy from the window, not a saved file-placement option.
    // Mark new rows before they become browsable during a streaming import.
    bool autoFirstEdit = false;
    bool autoTag = false;                         // optional offline subject scan of new imports
    static ImportOptions fromMap(const QVariantMap &m);
    QVariantMap toMap() const;
    bool inPlace() const { return mode == QLatin1String("add") || destination.isEmpty(); }
};

class Importer : public QObject {
    Q_OBJECT
public:
    explicit Importer(QObject *parent = nullptr);
    ~Importer();

    // Files the walk would import, in order; static so tests can check the
    // filter without a catalog. A set `cancel` abandons the walk and
    // returns nothing.
    // `walked` (if given) counts every file looked at, for progress on a big tree.
    static QStringList scan(const QString &folder, bool recursive = true, const std::atomic<bool> *cancel = nullptr, std::atomic<int> *walked = nullptr);
    // Shared by Browse and selected imports. Empty means selectable; linked
    // files must resolve inside the canonical source folder in every mode.
    static QString selectionError(const QString &canonicalFolder, const QString &path);
    // Catalogues one file (metadata, folder, sidecar); returns its id or 0.
    // `sidecar`, when given, receives what the photo's sidecar held.
    static int importFile(Catalog &cat, const QString &path, int importId, Metadata::Sidecar *sidecar = nullptr);
    // New photos whose sidecar carries an edit (OmaRAW's own, or the source editor's),
    // from the last run: the window applies them once the engine can.
    struct SidecarEdit { int id = 0; QString path, file; bool exact = false, raw = false; QByteArray packet; };
    QVector<SidecarEdit> lastSidecarEdits() const { return m_sidecarEdits; }

    // Where a file lands under the options: the destination, the date
    // subfolder and the renamed file. `seq` numbers a prefix-seq run.
    static QString targetPath(const ImportOptions &o, const QString &source, const QString &capturedAt, qint64 mtime, int seq);
    // With a `cancel` flag the file is read in blocks and a set flag gives
    // an empty result, so a verified import of a large file stays stoppable.
    static QString sha256(const QString &path, const std::atomic<bool> *cancel = nullptr);

    bool running() const { return m_running; }
    void start(const QString &catalogPath, const QString &folder, const ImportOptions &options = ImportOptions());
    void cancel() { m_cancel = true; }
    // Counts from the last run, beside imported / skipped.
    int lastCopied() const { return m_copied; }
    int lastDuplicates() const { return m_duplicates; }
    int lastFailed() const { return m_failed; }
    int lastBackedUp() const { return m_backedUp; }
    int lastBackupFailed() const { return m_backupFailed; }
    // The last run was stopped before it reached the end of the folder.
    bool lastCancelled() const { return m_cancelled; }

signals:
    // A successful transaction made new/updated rows visible to the window.
    void assetsCommitted();
    // A catalogued original and all its variants now live at the new path.
    void fileMoved(const QString &from, const QString &to);
    void progress(int done, int total, const QString &currentFile);
    void finished(int imported, int skipped, int importId, const QString &error);
    void runningChanged();

private:
    void run(const QString &catalogPath, const QString &folder, const ImportOptions &options, const QString &cacheDir);

    std::atomic<bool> m_cancel{false};
    // Accepted photos keep warming after import finishes (or is stopped).
    // Shutdown cancels decodes and drops queued work before joining the pool.
    std::atomic<bool> m_previewCancel{false};
    QThreadPool m_previews;
    // Written by run() on the worker, read by the GUI thread from the
    // queued `finished` handler.
    std::atomic<int> m_copied{0}, m_duplicates{0}, m_failed{0}, m_backedUp{0}, m_backupFailed{0};
    std::atomic<bool> m_cancelled{false};
    QVector<SidecarEdit> m_sidecarEdits;   // written by run(), read after `finished`
    bool m_running = false;
    QThread *m_thread = nullptr;
};
