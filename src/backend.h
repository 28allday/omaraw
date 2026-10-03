// The one object QML talks to. Owns the catalog, the browser model, the
// selection, the source/filter/sort state and the import worker.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include "displaycolour.h"
#include "colourpipeline.h"

#include "assetmodel.h"
#include "catalog.h"
#include "importer.h"
#include "autotag.h"
#include "metadata.h"

#include <QFileSystemWatcher>
#include <QImage>
#include <QTimer>
#include <QMutex>
#include <QObject>
#include <QHash>
#include <QCache>
#include "resourcesettings.h"
#include "catalogmaintenance.h"
#include <QQuickImageProvider>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class EngineService;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(DisplayColour* displayColour READ displayColour CONSTANT)
    Q_PROPERTY(ColourPipeline* colourPipeline READ colourPipeline CONSTANT)
    Q_PROPERTY(QString catalogPath READ catalogPath NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogName READ catalogName NOTIFY catalogChanged)
    Q_PROPERTY(bool catalogOpen READ catalogOpen NOTIFY catalogChanged)
    // A checksum or a search for offline photos is reading files (cancelFileJob stops it).
    Q_PROPERTY(bool fileJobBusy READ fileJobBusy NOTIFY fileJobChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY catalogChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QVariantList outputTemplates READ outputTemplates NOTIFY workflowPresetsChanged)
    Q_PROPERTY(QVariantList metadataImportPresets READ metadataImportPresets NOTIFY workflowPresetsChanged)

    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY sourceChanged)
    Q_PROPERTY(int sourceId READ sourceId NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY sourceChanged)

    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY filterChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY filterChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(int filterRating READ filterRating WRITE setFilterRating NOTIFY filterChanged)
    Q_PROPERTY(QString filterFlag READ filterFlag WRITE setFilterFlag NOTIFY filterChanged)
    Q_PROPERTY(QString filterLabel READ filterLabel WRITE setFilterLabel NOTIFY filterChanged)
    Q_PROPERTY(bool filterActive READ filterActive NOTIFY filterChanged)
    // The extra filters as one map (see whereFor); set the map to change them.
    Q_PROPERTY(QVariantMap filterExtra READ filterExtra WRITE setFilterExtra NOTIFY filterChanged)
    // Every "make model" in the catalog, for the camera filter.
    Q_PROPERTY(QStringList cameras READ cameras NOTIFY catalogChanged)
    // The last keywords typed, newest first, for one-click reuse.
    Q_PROPERTY(QStringList recentKeywords READ recentKeywords NOTIFY recentKeywordsChanged)

    // Collapsed stacks show only their top photo in the browser.
    Q_PROPERTY(bool collapseStacks READ collapseStacks WRITE setCollapseStacks NOTIFY filterChanged)
    Q_PROPERTY(QVariantList aiVersions READ aiVersions NOTIFY selectionChanged)
    Q_PROPERTY(int currentId READ currentId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QString currentFilename READ currentFilename NOTIFY selectionChanged)

    Q_PROPERTY(bool autoTagBusy READ autoTagBusy NOTIFY autoTagsChanged)
    Q_PROPERTY(bool autoTagEnabled READ autoTagEnabled WRITE setAutoTagEnabled NOTIFY autoTagsChanged)
    Q_PROPERTY(QVariantList autoTagOptions READ autoTagOptions CONSTANT)
    Q_PROPERTY(QVariantList autoTagGroups READ autoTagGroups CONSTANT)
    Q_PROPERTY(QVariantList autoTagCollections READ autoTagCollections NOTIFY autoTagSettingsChanged)
    Q_PROPERTY(bool autoTagPaused READ autoTagPaused WRITE setAutoTagPaused NOTIFY autoTagsChanged)
    Q_PROPERTY(int autoTagPending READ autoTagPending NOTIFY autoTagsChanged)
    Q_PROPERTY(QString autoTagStatus READ autoTagStatus NOTIFY autoTagsChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importChanged)
    Q_PROPERTY(double importProgress READ importProgress NOTIFY importChanged)
    Q_PROPERTY(QString importStatus READ importStatus NOTIFY importChanged)

    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(int thumbEdge READ thumbEdge WRITE setThumbEdge NOTIFY thumbEdgeChanged)
    // Write <file>.xmp beside the original on every rating/label/keyword change. Off by default.
    Q_PROPERTY(bool writeSidecars READ writeSidecars WRITE setWriteSidecars NOTIFY writeSidecarsChanged)
    // DNG, JPEG and TIFF carry their metadata and edit inside (the source editor's way); off = sidecars for everything.
    Q_PROPERTY(bool embedXmp READ embedXmp WRITE setEmbedXmp NOTIFY writeSidecarsChanged)
    // After a rating, flag or label lands on the one current photo by key
    // or menu, move to the next photo — culling without reaching for the arrows.
    Q_PROPERTY(bool autoAdvance READ autoAdvance WRITE setAutoAdvance NOTIFY autoAdvanceChanged)
    // A daily copy of the catalog into its backups folder, taken when the
    // catalog opens and the last one is older than a day; the newest seven stay.
    Q_PROPERTY(bool autoBackup READ autoBackup WRITE setAutoBackup NOTIFY autoBackupChanged)
    Q_PROPERTY(bool askCatalogAtStartup READ askCatalogAtStartup WRITE setAskCatalogAtStartup NOTIFY askCatalogAtStartupChanged)
    // Keyboard shortcuts the user changed: {id: "Ctrl+Shift+X"}; "" = unbound.
    Q_PROPERTY(QVariantMap shortcutOverrides READ shortcutOverrides NOTIFY shortcutsChanged)
    // Accessibility: zero-length animations, and stronger text and borders.
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY accessibilityChanged)
    Q_PROPERTY(ResourceSettings *resources READ resources CONSTANT)
    Q_PROPERTY(CatalogMaintenance *maintenance READ maintenance CONSTANT)
    Q_PROPERTY(bool highContrast READ highContrast WRITE setHighContrast NOTIFY accessibilityChanged)
    Q_PROPERTY(bool colourCritical READ colourCritical WRITE setColourCritical NOTIFY colourCriticalChanged)
    // Named sets of the browser's filters and sort: [{name, text, rating, flag, label, sortKey, sortDescending}].
    Q_PROPERTY(QVariantList savedFilters READ savedFilters NOTIFY savedFiltersChanged)

public:
    QVariantList outputTemplates() const;
    QVariantList metadataImportPresets() const;
    Q_INVOKABLE bool saveWorkflowPreset(const QString &kind, const QString &name, const QVariantMap &values);
    Q_INVOKABLE bool deleteWorkflowPreset(const QString &kind, const QString &name);
    DisplayColour *displayColour() const { return &DisplayColour::instance(); }
    ColourPipeline *colourPipeline() const { return &ColourPipeline::instance(); }
    explicit Backend(QObject *parent = nullptr);
    ~Backend();

    // Default catalog location, or $OMARAW_CATALOG.
    static QString defaultCatalogPath();
    bool openCatalog(const QString &path);
    // GUI sessions resume per catalogue; headless jobs leave that state alone.
    void setRememberBrowsing(bool on) { m_rememberBrowsing = on; }
    // The engine, for variants (engine versions) and snapshot previews.
    void setEngine(EngineService *engine);
    // The automatic first edit (EngineService::firstEditValues) for the photo
    // open in the engine, if it was imported waiting for one. Develop calls
    // this once the photo has rendered; true when it was given.
    Q_INVOKABLE bool giveFirstEdit();
    Q_INVOKABLE void buildSelectionSmartPreviews(bool keep);
    Q_INVOKABLE void keepSelectionOffline(bool keep = true);

    QString catalogPath() const { return m_catalog.path(); }
    QString catalogName() const;
    bool catalogOpen() const { return m_catalog.isOpen(); }
    bool fileJobBusy() const { return m_offThread; }
    Q_INVOKABLE void cancelFileJob();
    // How the last import ended: error, copied, duplicates, failed, backedUp, backupFailed, cancelled.
    Q_INVOKABLE QVariantMap lastImportResult() const;
    int totalCount() const { return m_totalCount; }
    QString version() const;

    AssetModel *model() { return &m_model; }

    QString sourceKind() const { return m_sourceKind; }
    int sourceId() const { return m_sourceId; }
    QString sourceTitle() const { return m_sourceTitle; }

    QString sortKey() const { return m_sortKey; }
    void setSortKey(const QString &k);
    bool sortDescending() const { return m_sortDescending; }
    void setSortDescending(bool d);
    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &t);
    int filterRating() const { return m_filterRating; }
    void setFilterRating(int r);
    QString filterFlag() const { return m_filterFlag; }
    void setFilterFlag(const QString &f);
    QVariantMap filterExtra() const { return m_filterExtra; }
    void setFilterExtra(const QVariantMap &e);
    QStringList cameras() const { return m_catalog.cameras(); }
    QStringList recentKeywords() const { return m_recentKeywords; }
    QString filterLabel() const { return m_filterLabel; }
    void setFilterLabel(const QString &l);
    bool filterActive() const;

    int currentId() const { return m_currentId; }
    int selectedCount() const { return m_selection.size(); }
    QString currentFilename() const;

    bool importing() const { return m_importer.running(); }
    double importProgress() const { return m_importProgress; }
    QString importStatus() const { return m_importStatus; }
    QString statusMessage() const { return m_status; }
    int thumbEdge() const { return m_thumbEdge; }
    bool writeSidecars() const { return m_writeSidecars; }
    bool embedXmp() const { return m_embedXmp; }
    void setEmbedXmp(bool on);
    bool autoAdvance() const { return m_autoAdvance; }
    bool autoBackup() const { return m_autoBackup; }
    void setAutoBackup(bool on);
    bool askCatalogAtStartup() const { return m_askCatalog; }
    void setAskCatalogAtStartup(bool on);
    QVariantMap shortcutOverrides() const { return m_shortcutOverrides; }
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool on);
    ResourceSettings *resources() { return &m_resources; }
    CatalogMaintenance *maintenance() { return &m_maintenance; }
    Q_INVOKABLE void startCatalogCheck();
    Q_INVOKABLE QVariantMap catalogMaintenanceInfo() const;
    bool highContrast() const { return m_highContrast; }
    void setHighContrast(bool on);
    bool colourCritical() const { return m_colourCritical; }
    void setColourCritical(bool on);
    Q_INVOKABLE void setShortcut(const QString &id, const QString &keys);
    Q_INVOKABLE void resetShortcut(const QString &id);
    Q_INVOKABLE void resetShortcuts();
    // A pressed key + modifiers as the portable text a Shortcut takes ("Ctrl+Shift+K"); "" for a lone modifier.
    Q_INVOKABLE QString keyText(int key, int modifiers) const;
    // The window registers its table's defaults ({id: keys}) so a menu can
    // show the keys in force for the item whose default it names.
    Q_INVOKABLE void registerShortcuts(const QVariantMap &defaults);
    Q_INVOKABLE QString shortcutHint(const QString &defaultKeys) const;
    // Relink: one offline photo to a file you point at (its size must
    // match when the catalog knows it), or every offline photo the catalog
    // holds to files found under a folder by name, size and capture time.
    Q_INVOKABLE QString relinkPhoto(int id, const QString &fileOrUrl);
    Q_INVOKABLE int relinkOfflineIn(const QString &folderOrUrl);
    // Checksums: compute for the selection (files read once), then select
    // every master that shares a checksum with another; the count selected.
    Q_INVOKABLE int hashSelection();
    Q_INVOKABLE int selectDuplicates();
    // Watches the application's key events for the hold-to-preview keys:
    // holdKey(name, down) for Backslash, J, M and F, auto-repeats dropped.
    void watchHoldKeys(QObject *app);
    // A copy now, into the backups folder; the path, or "" with the status set.
    Q_INVOKABLE QString backupCatalog();
    Q_INVOKABLE QString restoreCatalogBackup(const QString &manifest, const QString &parentFolder);
    Q_INVOKABLE bool openCatalogWindow(const QString &path);
    // Catalogs the user has opened, newest first, missing files dropped:
    // rows {path, folder, name, current}. The active one is marked, not hidden.
    Q_INVOKABLE QVariantList recentCatalogs() const;
    Q_INVOKABLE void clearRecentCatalogs();
    // Remembers a catalog as the active one and at the top of the recents.
    static void rememberCatalog(const QString &path);
    // A folder to offer when creating a catalog: the parent of the last new
    // one, else Pictures, else home. Never inside an existing catalog folder.
    Q_INVOKABLE QString newCatalogParent() const;
    // A file dialog hands back a URL; the catalog dialogs show real paths.
    Q_INVOKABLE QString localFile(const QString &fileOrUrl) const;
    Q_INVOKABLE QUrl fileUrl(const QString &path) const;
    // Validation/navigation for the themed file and folder browsers.
    Q_INVOKABLE QVariantMap browseFolder(const QString &pathOrUrl) const;
    Q_INVOKABLE int requestPathListing(const QString &pathOrUrl, bool showHidden, bool foldersOnly, const QStringList &filters);
    Q_INVOKABLE QVariantMap pickerFile(const QString &pathOrUrl, bool save, const QString &defaultSuffix) const;
    Q_INVOKABLE QVariantMap createPickerFolder(const QString &parentOrUrl, const QString &name) const;
    // The folder a catalog of this name would occupy under this parent, so
    // the dialog can show the real destination before anything is created.
    Q_INVOKABLE QString proposedCatalogPath(const QString &parentOrUrl, const QString &name) const;
    // Creates <parent>/<name>/catalog.db and its schema. Catalogs sharing a
    // folder share engine data, so each one gets a folder of its own. The
    // new catalog's path, or "" with the reason on the status line.
    Q_INVOKABLE QString createCatalog(const QString &parentOrUrl, const QString &name);
    // Reopens this window on another catalog. The engine cannot be
    // reinitialised in place, so the window is relaunched onto it; the new
    // process waits for this one's locks. False (with a reason) if the
    // catalog cannot be used, before anything is closed.
    // Whether a catalog could be opened right now: it exists, and no other
    // OmaRAW window holds it or the engine data it does not share with this
    // window's catalog.
    Q_INVOKABLE bool catalogAvailable(const QString &fileOrUrl) const;
    Q_INVOKABLE bool switchCatalog(const QString &fileOrUrl);
    // SQLite's own check; "ok" or its findings, also on the status line.
    Q_INVOKABLE QString checkCatalog();
    Q_INVOKABLE void revealBackups() const;
    // The installed licence notices (package or user-local install).
    Q_INVOKABLE QString licencesPath() const;
    Q_INVOKABLE void revealLicences() const;
    // The installed documentation: the user guide and the help pages.
    // The lookup is a pure function of where the binary sits, so it can be
    // tested against a layout rather than against this machine.
    static QString documentationPathFor(const QString &binDir);
    Q_INVOKABLE QString documentationPath() const;
    Q_INVOKABLE void revealDocumentation() const;
    // The in-app help: sections in reading order (id, title, group) and a page as Markdown.
    Q_INVOKABLE QVariantList helpSections() const;
    Q_INVOKABLE QString helpText(const QString &id) const;
    void setAutoAdvance(bool on);
    QVariantList savedFilters() const { return m_savedFilters; }
    Q_INVOKABLE void saveFilter(const QString &name);
    Q_INVOKABLE bool applySavedFilter(const QString &name);
    Q_INVOKABLE bool deleteSavedFilter(const QString &name);
    void setWriteSidecars(bool on);
    // Write sidecars for the selection now (also used by the menu).
    Q_INVOKABLE int writeSidecarsForSelection();
    // A sidecar edited outside OmaRAW since the catalog last read or wrote
    // it: what it holds and which fields differ from the catalog.
    // {present, changed, rating, label, keywords, differs: [rating|label|keywords]}
    Q_INVOKABLE QVariantMap sidecarReview(int id) const;
    // Sidecar wins: the catalog takes the sidecar's rating, label and
    // keywords (or one `field`); catalog wins: the sidecar is rewritten.
    Q_INVOKABLE void applySidecar(int id, const QString &field = QString());
    Q_INVOKABLE void keepCatalogOverSidecar(int id);
    // Every shown photo whose sidecar changed outside becomes the selection; the count.
    Q_INVOKABLE int checkSidecars();
    // Each selected photo takes its sidecar's metadata and edit (the source editor's "Read Metadata from File").
    Q_INVOKABLE int readSidecarsForSelection();
    static qint64 sidecarMtime(const QString &path);
    void setThumbEdge(int e);

    // Pure: the WHERE clause for a source + filter, so the tests can pin
    // the browser's semantics without a window.
    static QString whereFor(const QString &kind, int id, const QString &text, int rating,
                            const QString &flag, const QString &label, QVariantList &binds);
    // With the extra filters: {type: ""|"raw"|"other", edited: ""|"yes"|"no",
    // gps: ""|"yes"|"no", camera: "" or an exact "make model"}.
    static QString whereFor(const QString &kind, int id, const QString &text, int rating,
                            const QString &flag, const QString &label, const QVariantMap &extra, QVariantList &binds);
    static QString orderFor(const QString &key, bool descending);

    // ── sources ────────────────────────────────────────────────────────
    Q_INVOKABLE void setSource(const QString &kind, int id = 0);
    Q_INVOKABLE QVariantList catalogSections() const;
    Q_INVOKABLE QVariantList folderTree() const { return m_catalog.folderTree(); }
    // Keywords as a tree ("Travel/Italy/Venice" is three levels); a keyword
    // is a source ("keyword", id) that shows its photos and its descendants'.
    Q_INVOKABLE QVariantList keywordTree() const { return m_catalog.keywordTree(); }
    Q_INVOKABLE int keywordId(const QString &path) const { return m_catalog.keywordId(path); }
    Q_INVOKABLE bool deleteKeyword(int id);
    Q_INVOKABLE QStringList keywordSynonyms(int id) const { return m_catalog.keywordSynonyms(id); }
    Q_INVOKABLE bool setKeywordSynonyms(int id, const QString &commaList);
    Q_INVOKABLE QStringList suggestKeywords(const QString &text) const { return m_catalog.suggestKeywords(text); }
    // Album rows with a `thumb` source for the cover (the album's chosen
    // photo, else its first member).
    Q_INVOKABLE QVariantList albums() const;
    Q_INVOKABLE bool setAlbumCover(int albumId, int assetId);
    // Custom order: valid while an album is the source and the sort is "custom";
    // moves the current photo (or `id`) earlier / later.
    Q_INVOKABLE bool moveInAlbum(int delta, int id = 0);
    // A drag in the grid: the photo lands at the given row of the shown order.
    Q_INVOKABLE bool moveInAlbumTo(int id, int row);
    Q_PROPERTY(bool customOrderAvailable READ customOrderAvailable NOTIFY sourceChanged)
    bool customOrderAvailable() const { return m_sourceKind == QLatin1String("album"); }
    Q_INVOKABLE QVariantList smartAlbums() const { return m_catalog.albums(true); }
    Q_INVOKABLE QVariantList recentImports() const { return m_catalog.recentImports(); }
    // Volumes: mark folders offline when their directory is gone (also on
    // open and after every import); relink a folder that moved.
    Q_INVOKABLE int rescanFolders();
    // Watch a catalogued folder: files that land in it are imported in
    // place a couple of seconds after the disk goes quiet.
    Q_INVOKABLE void setFolderWatched(int folderId, bool on);
    // Mounted volumes worth importing from: [{name, path, bytesTotal,
    // bytesFree, removable, dcim}] — the system and home volumes left out.
    Q_INVOKABLE QVariantList devices() const;
    Q_INVOKABLE QString relinkFolder(int folderId, const QString &newPathOrUrl);
    Q_INVOKABLE int createAlbum(const QString &name, int parentId = 0);
    Q_INVOKABLE bool setAlbumParent(int id, int parentId);
    // An export finished for this file: the catalog's Recently Exported follows.
    Q_INVOKABLE void markExported(const QString &path, int variant);
    // Colour labels can carry names ("Client", "Print"): {colour: name}; a missing one is the colour word.
    Q_PROPERTY(QVariantMap labelNames READ labelNames NOTIFY labelNamesChanged)
    QVariantMap labelNames() const { return m_labelNames; }
    Q_INVOKABLE QString labelName(const QString &colour) const;
    Q_INVOKABLE void setLabelName(const QString &colour, const QString &name);
    Q_INVOKABLE bool renameAlbum(int id, const QString &name);
    Q_INVOKABLE bool deleteAlbum(int id);
    Q_INVOKABLE void addSelectionToAlbum(int albumId);
    Q_INVOKABLE void removeSelectionFromAlbum(int albumId);
    // Smart albums from a rule map, never from typed SQL:
    // {match: "all"|"any", rating: 0..5, flag: ""|"pick"|"unflagged"|"reject",
    //  label: "", keyword: "", text: "", camera: "", lens: "", format: "",
    //  edited: ""|"yes"|"no", from: "YYYY-MM-DD", to: "YYYY-MM-DD", stacked: ""|"top"}
    static QString smartRuleSql(const QVariantMap &rule);
    static QString smartRuleSummary(const QVariantMap &rule);
    Q_INVOKABLE int createSmartAlbum(const QString &name, const QVariantMap &rule);
    Q_INVOKABLE bool updateSmartAlbum(int id, const QString &name, const QVariantMap &rule);
    // The rule map a smart album was made with ({} for hand-written ones).
    Q_INVOKABLE QVariantMap smartAlbumRule(int id) const;
    Q_INVOKABLE int smartRuleCount(const QVariantMap &rule) const;
    Q_INVOKABLE void addSelectionToQuickCollection();

    // ── import ─────────────────────────────────────────────────────────
    Q_INVOKABLE void importFolder(const QString &pathOrUrl);
    // Import with options {mode add|copy|move|verify, destination, subfolder
    // none|year|year-date|date, rename keep|date-name|prefix-seq, prefix,
    // skipDuplicates}; the last-used options persist.
    Q_INVOKABLE bool importFolderWith(const QString &pathOrUrl, const QVariantMap &options);
    Q_INVOKABLE int requestImportPreview(const QString &pathOrUrl, bool recursive);
    Q_INVOKABLE void cancelImportPreview();
    void openFromCommandLine(const QString &pathOrUrl);
    Q_INVOKABLE QVariantMap importOptions() const;
    // What a folder holds before importing: {folder, name, count, bytes}.
    Q_INVOKABLE QVariantMap importPreview(const QString &pathOrUrl) const;
    // A folder on a card or drive of its own (not the system or home volume) — the eject option applies.
    Q_INVOKABLE bool isRemovableSource(const QString &pathOrUrl) const;
    // Unmount and power off the volume a folder is on, over udisksctl; "" or the reason.
    Q_INVOKABLE QString ejectVolumeOf(const QString &pathOrUrl);
    // {exists, writable, bytesFree, bytesTotal} for a folder, for the export's validation.
    Q_INVOKABLE QVariantMap folderSpace(const QString &pathOrUrl) const;
    // Open the folder holding a file (or the folder itself) in the file manager.
    Q_INVOKABLE void revealPath(const QString &path) const;
    // One file that just arrived (a tethered capture): catalogued at once,
    // made current, and its folder becomes the source when it is not shown.
    Q_INVOKABLE int importCapture(const QString &path);
    Q_INVOKABLE void cancelImport() { m_importer.cancel(); }

    QVariantList aiVersions() const;
    Q_INVOKABLE bool openAiVersion(int id);
    Q_INVOKABLE bool openAiOriginal();
    Q_INVOKABLE bool openLatestAiVersion();

    // ── selection ──────────────────────────────────────────────────────
    // mode: 0 replace, 1 toggle (Ctrl), 2 extend (Shift)
    Q_INVOKABLE void select(int id, int mode = 0);
    Q_INVOKABLE void selectRow(int row, int mode = 0) { select(m_model.idAt(row), mode); }
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(int id) const { return m_selection.contains(id); }
    Q_INVOKABLE void step(int delta); // move current by delta rows, replace selection
    Q_INVOKABLE QVariantList selectedIds() const;
    // Compare: the other selected photo, else the next one in the browser (0 if none).
    Q_INVOKABLE int comparePartner() const;
    // Make `id` current without changing the selection set.
    Q_INVOKABLE void setCurrent(int id);
    Q_INVOKABLE QStringList selectedPaths() const;
    Q_INVOKABLE QStringList shownPaths() const;
    // Export rows [{path, variant, name}] for the selection or the whole browser.
    Q_INVOKABLE QVariantList exportItems(bool shown) const;

    // ── variants (virtual copies) ──────────────────────────────────────
    // A variant is a second catalog row on the same file with its own
    // engine history, rating, flag, label and keywords. `id` 0 = current.
    // createVariant is asynchronous (the engine copies the history); the
    // new row is selected when it lands.
    Q_INVOKABLE void createVariant(int id = 0);
    Q_INVOKABLE bool renameVariant(int id, const QString &name);
    // Make a variant the master: the two swap version numbers and the
    // siblings re-point. Asynchronous like createVariant.
    Q_INVOKABLE void promoteVariant(int id = 0);
    // Drop a variant from the catalog and the engine. Masters cannot be deleted this way.
    Q_INVOKABLE void deleteVariant(int id = 0);
    // The family of `id`: master first, then variants. Rows: id, variant, name, label, current.
    Q_INVOKABLE QVariantList variantsOf(int id) const;
    // "Copy 2" or the given name; "" on a master.
    static QString variantLabel(const AssetRecord &r);

    // ── snapshots ──────────────────────────────────────────────────────
    // A named capture of the engine's current settings for an asset plus a
    // preview of the render. Rows: id, name, createdAt, historyEnd, values, preview (image url).
    Q_INVOKABLE QVariantList snapshots(int assetId);
    // Capture the settled, matching engine state including all local edits.
    Q_INVOKABLE int saveSnapshot(int assetId, const QString &name);
    Q_INVOKABLE bool compareSnapshot(int id);
    Q_INVOKABLE bool renameSnapshot(int id, const QString &name);
    Q_INVOKABLE bool deleteSnapshot(int id);
    // Thread-safe: the image provider reads previews from here.
    QImage snapshotImage(int id) const;

    // ── print & contact sheets (PDF) ───────────────────────────────────
    // A contact sheet of the selection or everything shown, from the
    // Library thumbnails (edits included). Returns "" or a reason; the
    // page count lands in the status line.
    Q_INVOKABLE QString makeContactSheet(bool shown, const QString &pdfPath, const QVariantMap &options);
    // One photo per page from full renders already exported to `files`.
    Q_INVOKABLE QString makePrintSheet(const QStringList &files, const QString &pdfPath, const QVariantMap &options);
    Q_INVOKABLE QStringList sheetPapers() const;
    // Long-edge pixels a render needs for this paper (for the export step before a print sheet).
    Q_INVOKABLE int sheetLongEdge(const QVariantMap &options) const;

    // ── culling (apply to the selection; `id` 0 means the selection) ───
    Q_INVOKABLE void setRating(int rating, int id = 0);
    Q_INVOKABLE void setFlag(int flag, int id = 0);
    Q_INVOKABLE void toggleFlag(int flag, int id = 0);
    Q_INVOKABLE void setLabel(const QString &label, int id = 0);
    Q_INVOKABLE void toggleLabel(const QString &label, int id = 0);
    Q_INVOKABLE void addKeyword(const QString &keyword);
    Q_INVOKABLE void removeKeyword(const QString &keyword);
    Q_INVOKABLE QStringList currentKeywords() const;
    // Batch metadata: copyright and creator over the selection (or `id`).
    Q_INVOKABLE void setCopyright(const QString &text, int id = 0);
    Q_INVOKABLE void setTitle(const QString &text, int id = 0);
    Q_INVOKABLE void setCaption(const QString &text, int id = 0);
    Q_INVOKABLE void setCreator(const QString &text, int id = 0);
    // What the selection has in common: {count, copyright, copyrightMixed,
    // creator, creatorMixed, rating, ratingMixed, label, labelMixed}.
    Q_INVOKABLE QVariantMap selectionSummary() const;

    // ── stacks ─────────────────────────────────────────────────────────
    bool collapseStacks() const { return m_collapseStacks; }
    void setCollapseStacks(bool on);
    Q_INVOKABLE void stackSelection();
    Q_INVOKABLE void unstackSelection();
    // Groups the photos currently shown whose capture times sit within `seconds`.
    Q_INVOKABLE int autoStackShown(int seconds);
    Q_INVOKABLE void setStackTop(int id = 0);
    // Expand or collapse the stack `id` belongs to (current when 0).
    Q_INVOKABLE void toggleStack(int id = 0);
    Q_INVOKABLE bool isStackExpanded(int stackId) const { return m_expandedStacks.contains(stackId); }
    Q_INVOKABLE QStringList allKeywords() const { return m_catalog.allKeywords(); }
    Q_INVOKABLE void removeSelectionFromCatalog();
    // Safe deletion: the selected files (and sidecars) go to the system
    // trash and leave the catalog; the count moved, failures on the status line.
    Q_INVOKABLE int moveSelectionToTrash();
    // Acknowledge a sidecar that was touched outside but says nothing new.
    Q_INVOKABLE void reviewSidecar(int id);
    // On disk, journaled: the intent is written to the catalog first, the
    // file (and its sidecar) renamed, then the catalog, thumbnails and the
    // engine follow. "" on success, else the reason (also on the status line).
    // A name without an extension keeps the file's own.
    Q_INVOKABLE QString renamePhoto(int id, const QString &newName);
    // Every selected photo into a folder (a path or file: URL); "" or the first failure.
    Q_INVOKABLE QString moveSelectionTo(const QString &folderOrUrl);
    QString moveFile(int id, const QString &newPath);

    // ── inspector ──────────────────────────────────────────────────────
    Q_INVOKABLE QVariantMap info(int id) const;
    bool autoTagBusy() const { return m_autoTagWorker.busy(); }
    bool autoTagEnabled() const { return m_autoTagEnabled; }
    void setAutoTagEnabled(bool enabled);
    QVariantList autoTagOptions() const { return AutoTags::options(); }
    QVariantList autoTagGroups() const { return AutoTags::groups(); }
    QVariantList autoTagCollections() const { return m_catalog.autoTagCollections(); }
    Q_INVOKABLE bool setAutoTagCollectionEnabled(const QString &key, bool enabled);
    bool autoTagPaused() const { return m_autoTagPaused; }
    int autoTagPending() const { return m_catalog.pendingAutoTags(); }
    QString autoTagStatus() const { return m_autoTagStatus; }
    void setAutoTagPaused(bool paused);
    Q_INVOKABLE bool scanSelectionAutoTags();
    Q_INVOKABLE bool setCurrentAutoTag(const QString &tag, bool present);
    Q_INVOKABLE bool resetCurrentAutoTagCorrections();
    Q_INVOKABLE QString thumbSource(int id, int edge) const;
    // 3 × 64 bins from the cached preview (never the RAW): [r[], g[], b[]].
    Q_INVOKABLE QVariantList histogram(int id) const;
    static QVariantList histogramOf(const QImage &img);
    Q_INVOKABLE void revealInFileManager(int id) const;
    // The photo's GPS position on OpenStreetMap in the browser (no-op without GPS).
    Q_INVOKABLE void openInMap(int id) const;
    static QString mapUrl(double lat, double lon);

    Q_INVOKABLE void setStatus(const QString &m);
    // The folder camera profiles (DCP files, as film-look packs bring) are
    // found in when a XMP preset names one.
    Q_INVOKABLE QString cameraProfilesFolder() const;
    Q_INVOKABLE void setCameraProfilesFolder(const QString &folderOrUrl);

    // Develop presets: built-ins (id < 0) plus the catalog's. Rows include category and tags.
    Q_INVOKABLE QVariantList presets() const;
    // Favourites (a star on the row, sorted first) and the last five applied.
    Q_PROPERTY(QStringList favouritePresets READ favouritePresets NOTIFY presetsChanged)
    Q_PROPERTY(QStringList recentPresets READ recentPresets NOTIFY presetsChanged)
    QStringList favouritePresets() const { return m_favouritePresets; }
    QStringList recentPresets() const { return m_recentPresets; }
    Q_INVOKABLE void toggleFavouritePreset(const QString &name);
    Q_INVOKABLE void notePresetApplied(const QString &name);
    Q_INVOKABLE bool applyPreset(const QString &name);
    // Version 2 adds category/tags; legacy version 1 remains readable.
    Q_INVOKABLE int exportPresets(const QString &fileOrUrl);
    Q_INVOKABLE int importPresets(const QString &fileOrUrl);
    Q_INVOKABLE int importPresetFiles(const QStringList &filesOrUrls);
    Q_PROPERTY(QVariantList presetImportReport READ presetImportReport NOTIFY presetImportReportChanged)
    QVariantList presetImportReport() const { return m_presetImportReport; }
    Q_INVOKABLE int savePreset(const QString &name, const QVariantList &values, const QString &category = QString(), const QStringList &tags = {});
    Q_INVOKABLE int saveCurrentPreset(const QString &name, const QStringList &groups, const QString &category = QString(), const QStringList &tags = {});
    Q_INVOKABLE int saveCurrentModulePreset(const QString &name, const QString &operation, const QString &category = QString(), const QStringList &tags = {});
    Q_INVOKABLE bool setPresetMetadata(int id, const QString &category, const QStringList &tags);
    Q_INVOKABLE bool deletePreset(int id);
    static QVariantList builtinPresets();
    // Auto-apply: presets laid on photos that match a rule, on import or on
    // request. Rules: {id, preset, camera, lens, isoMin, isoMax, format, text}.
    Q_PROPERTY(int presetRuleCount READ presetRuleCount NOTIFY presetRulesChanged)
    int presetRuleCount() const { return m_catalog.presetRules().size(); }
    Q_INVOKABLE QVariantList presetRules() const;
    Q_INVOKABLE int createPresetRule(const QString &preset, const QVariantMap &rule);
    Q_INVOKABLE bool deletePresetRule(int id);
    static bool ruleMatches(const QVariantMap &rule, const AssetRecord &r);
    static QString ruleText(const QVariantMap &rule);
    // Unique matching preset names, ordered by each name's last matching rule.
    Q_INVOKABLE QStringList presetsMatching(int assetId) const;
    // Apply the last matching preset to each photo; returns how many photos matched.
    Q_INVOKABLE int applyPresetRules(const QVariantList &ids);

    // An edited render for `path` (variant) from the engine: replaces the
    // cached thumbnail, marks the asset edited, repaints its card.
    void applyEditedThumb(const QString &path, int variant, const QImage &render, int flags);

signals:
    void autoTagSettingsChanged();
    void autoTagsChanged();
    void autoTagFailed(const QString &error);
    void pathListingReady(int request, const QVariantList &entries, const QString &error);
    void importPreviewReady(int request, const QVariantMap &preview, const QString &error);
    void workflowPresetsChanged();
    void catalogChanged();
    void sourceChanged();
    void filterChanged();
    void selectionChanged();
    void importChanged();
    void fileJobChanged();
    // A photo was handed to the application to look at: show it in Develop.
    void developRequested();
    // A photo's sidecar now carries its edit as well as its metadata.
    void sidecarWritten(int id);
    void statusChanged();
    void thumbEdgeChanged();
    // A finished edit replaced this asset's preview, including a batch member
    // that is not the current photo. Library viewers must reload its source.
    void previewChanged(int id);
    void writeSidecarsChanged();
    void autoAdvanceChanged();
    void autoBackupChanged();
    void askCatalogAtStartupChanged();
    void shortcutsChanged();
    void accessibilityChanged();
    void colourCriticalChanged();
    void labelNamesChanged();
    void holdKey(const QString &name, bool down);
    void recentKeywordsChanged();
    void savedFiltersChanged();
    void presetsChanged();
    void presetImportReportChanged();
    void presetRulesChanged();
    void importFinished(int imported, int skipped);
    void variantsChanged();
    void snapshotsChanged(int assetId);
    void foldersChanged();

private:
    void setupAutoTags();
    void stopAutoTags();
    void pumpAutoTags();
    bool ensureAutoTagAlbums();
    AutoTagWorker m_autoTagWorker;
    QTimer m_autoTagTimer, m_autoTagRefresh;
    QVariantMap m_autoTagJob;
    bool m_autoTagPaused = false;
    bool m_autoTagEnabled = false;
    QString m_autoTagStatus;
    ResourceSettings m_resources;
    CatalogMaintenance m_maintenance;
    int m_folderListingRequest = 0;
    int m_firstEditChecked = 0;   // giveFirstEdit: the photo already looked at
    void refresh();
    void refreshCounts();
    void followMovedFile(const QString &from, const QString &to);
    void finishMovedFile(const QString &from, const QString &to, const QString &error);
    QString moveEngineError() const;
    QVector<int> targets(int id) const;
    // Keep the active AI version visible even when an album/filter excludes it.
    int m_aiFocusId = 0;
    void focusAiVersion(int id);
    QString sourceWhere(QVariantList &binds) const;   // the shown source + filters, stacks not collapsed
    // Slow file reading on a thread while the window stays live; one at a time.
    bool waitOffThread(const std::function<void(std::atomic<int> &, const std::atomic<bool> &)> &work, const std::function<QString(int)> &progress);
    int quickCollectionId();
    void selectionSetChanged(const QSet<int> &before);
    bool presetCaptureReady();

    Catalog m_catalog;
    AssetModel m_model;
    Importer m_importer;
    QTimer m_importRefresh;
    int m_importPreviewRequest = 0;
    std::shared_ptr<std::atomic<bool>> m_importPreviewCancel;
    int m_totalCount = 0;
    QString m_sourceKind = QStringLiteral("all");
    int m_sourceId = 0;
    QString m_sourceTitle;
    QString m_sortKey = QStringLiteral("captured");
    bool m_sortDescending = false;
    QString m_filterText;
    int m_filterRating = 0;
    QString m_filterFlag;
    QString m_filterLabel;
    QVariantMap m_filterExtra;
    QString m_browseWhere;
    QVariantList m_browseBinds;
    QStringList m_recentKeywords;
    QStringList m_favouritePresets, m_recentPresets;
    QVariantList m_presetImportReport;
    void noteRecentKeyword(const QString &keyword);
    QSet<int> m_selection;
    QSet<int> m_expandedStacks;
    bool m_collapseStacks = true;
    int m_currentId = 0;
    int m_anchorId = 0;
    bool m_rememberBrowsing = false, m_restoringBrowsing = false;
    QString browsingSessionKey() const;
    void saveBrowsingSession();
    void restoreBrowsingSession();
    double m_importProgress = 0;
    QString m_importStatus;
    QString m_status;
    int m_thumbEdge = 512;
    bool m_writeSidecars = false;
    bool m_embedXmp = false;
    bool m_autoAdvance = false;
    bool m_autoBackup = true;
    bool m_askCatalog = false;
    QVariantMap m_shortcutOverrides;
    bool m_reducedMotion = false, m_highContrast = false;
    bool m_colourCritical = false;
    QVariantMap m_shortcutDefaults;   // id → default keys, from the window
    QVariantMap m_labelNames;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dailyBackup();
    QVariantList m_savedFilters;
    void advanceAfterCull(int id);
    void syncSidecars(const QVector<int> &ids);
    Metadata::Sidecar sidecarData(const AssetRecord &r) const;
    bool writeSidecarFor(int id, bool withEdit, QString *error = nullptr);
    bool storeMetadata(const AssetRecord &r, const Metadata::SidecarEdit *edit, QString *error);
    int applyEditsFromSidecars(const QVector<Importer::SidecarEdit> &edits);
    QTimer m_sidecarTimer;         // edits settle before their sidecars are written
    QSet<int> m_sidecarDue;
    EngineService *m_engine = nullptr;
    bool m_buildSmartAfterImport = false;
    ImportOptions m_importOptions;
    ImportOptions m_runningImportOptions;   // what the import now running was started with
    QString m_captureFolder, m_captureCatalog; int m_captureImport = 0;   // the tethered session's import record
    bool m_offThread = false;   // a checksum or relink job is reading files
    std::atomic<bool> m_offThreadCancel{false};
    QString m_importSource;
    QString m_ejectNote;
    QString m_lastImportError;   // put in front of the eject's own status
    quint64 m_importSerial = 0;
    QHash<QPair<QString, QString>, quint64> m_importMoves;
    QStringList m_importMoveErrors;
    QFileSystemWatcher m_watcher;
    QTimer m_watchTimer;
    QSet<QString> m_watchPending;
    QHash<QString, QString> m_watchSettle;   // folder → what it looked like at the last check
    QThread *m_watchScan = nullptr;          // the walk that decides whether a folder has settled
    std::atomic<bool> m_watchScanCancel{false};
    bool m_watchImporting = false;
    void refreshWatches();
    void importWatched();      // for the import about to start
    mutable QMutex m_snapMutex;
    mutable QCache<int, QImage> m_snapImages{64 * 1024}; // KiB, decoded previews
    QString m_snapCatalogPath;                         // guarded by m_snapMutex
    quint64 m_snapRevision = 0;
};

// image://snapshots/<id> — a snapshot's stored preview.
class SnapshotImageProvider : public QQuickImageProvider {
public:
    explicit SnapshotImageProvider(Backend *b) : QQuickImageProvider(QQuickImageProvider::Image), m_backend(b) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    Backend *m_backend;
};
