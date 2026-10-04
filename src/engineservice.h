// The engine as QML sees it: one loaded image, a render that follows the
// history, a curated parameter list, the history list. All engine calls
// run on a worker thread; requests coalesce so a slider drag never queues
// more renders than the engine can keep up with.
#pragma once

#include <QImage>
#include <QCache>
#include <QRectF>
#include <QMutex>
#include <QPointer>
#include <QObject>
#include <QtNumeric>
#include <QElapsedTimer>
#include <QSet>
#include <QHash>
#include <QSize>
#include <QQuickImageProvider>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <memory>
class QTemporaryDir;

class EngineWorker;
class AiService;
class DenoiseService;
class ImageMatchService;

class EngineService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString version READ version NOTIFY availableChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QObject *ai READ ai CONSTANT)
    Q_PROPERTY(QObject *denoise READ denoise CONSTANT)
    Q_PROPERTY(QObject *imageMatch READ imageMatch CONSTANT)
    // Reduced-quality fitted previews: the engine's fast pipe while a photo
    // is shown fitted. Native detail tiles and exports keep full quality.
    Q_PROPERTY(bool reducedPreviews READ reducedPreviews WRITE setReducedPreviews NOTIFY reducedPreviewsChanged)
    Q_PROPERTY(bool exportFullResolution READ exportFullResolution WRITE setExportFullResolution NOTIFY exportFullResolutionChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY availableChanged)
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY sourceChanged)
    Q_PROPERTY(bool offlineBusy READ offlineBusy NOTIFY offlineChanged)
    Q_PROPERTY(double offlineProgress READ offlineProgress NOTIFY offlineChanged)
    Q_PROPERTY(QString offlineStatus READ offlineStatus NOTIFY offlineChanged)
    Q_PROPERTY(int imageId READ imageId NOTIFY imageChanged)
    Q_PROPERTY(QString imagePath READ imagePath NOTIFY imageChanged)
    // The loaded variant (engine version): 0 = master.
    Q_PROPERTY(int imageVariant READ imageVariant NOTIFY imageChanged)
    Q_PROPERTY(QVariantList creativeProfiles READ creativeProfiles NOTIFY creativeProfilesChanged)
    Q_PROPERTY(QVariantMap cameraProfileState READ cameraProfileState NOTIFY cameraProfileStateChanged)
    Q_PROPERTY(QVariantList params READ params NOTIFY paramsChanged)
    Q_PROPERTY(QVariantMap toolState READ toolState NOTIFY toolStateChanged)
    Q_PROPERTY(int scopeMode READ scopeMode WRITE setScopeMode NOTIFY scopeChanged)
    Q_PROPERTY(bool scopeExpanded READ scopeExpanded WRITE setScopeExpanded NOTIFY scopeChanged)
    Q_PROPERTY(QString scopeSource READ scopeSource NOTIFY scopeChanged)
    // Scope 4 is false colour: the picture itself is repainted by brightness
    // and the graph shows how much of it falls in each band.
    Q_PROPERTY(bool falseColour READ falseColour NOTIFY scopeChanged)
    Q_PROPERTY(QVariantList falseColourBands READ falseColourBands NOTIFY scopeChanged)
    Q_PROPERTY(bool rawClippingShown READ rawClippingShown WRITE setRawClippingShown NOTIFY rawClippingChanged)
    Q_PROPERTY(QString rawClippingSource READ rawClippingSource NOTIFY rawClippingChanged)
    Q_PROPERTY(QString rawClippingStatus READ rawClippingStatus NOTIFY rawClippingChanged)
    Q_PROPERTY(int retouchPreviewScale READ retouchPreviewScale WRITE setRetouchPreviewScale NOTIFY retouchPreviewChanged)
    Q_PROPERTY(QString retouchPreviewSource READ retouchPreviewSource NOTIFY retouchPreviewChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    // Changes with every change of params: a binding that reads it and then
    // paramsFor(op) re-evaluates with them, converting only that op's rows.
    Q_PROPERTY(int paramsVersion READ paramsVersion NOTIFY paramsChanged)
    // Local adjustments: exposure instances under a drawn radial or
    // gradient mask. Rows: {priority, name, shape (1 radial, 2 gradient),
    // cx, cy, radius, border, rotation, compression, opacity, enabled,
    // exposure, black}. Coordinates are fractions of the image.
    Q_PROPERTY(QVariantList locals READ locals NOTIFY localsChanged)
    // The local being edited (its priority), -1 for none; shared by panel and viewer.
    Q_PROPERTY(int activeLocal READ activeLocal WRITE setActiveLocal NOTIFY activeLocalChanged)
    // Retouch spots: rows {index, algorithm (1 clone, 2 heal, 3 blur, 4 fill),
    // cx, cy, radius, border, sx, sy, opacity, blurRadius, fillBrightness}.
    Q_PROPERTY(QVariantList spots READ spots NOTIFY spotsChanged)
    Q_PROPERTY(int activeSpot READ activeSpot WRITE setActiveSpot NOTIFY activeSpotChanged)
    // Tone curve (rgbcurve): {found, enabled, linked, channels: [{count, type,
    // xs, ys, samples[65]} x 3]}. Channel 0 is RGB while linked, else red.
    Q_PROPERTY(QVariantMap curve READ curve NOTIFY curveChanged)
    // Parametric curve (tonecurve L): {found, enabled, highlights, lights,
    // darks, shadows} each -100..100, plus xs/ys of the nodes.
    Q_PROPERTY(QVariantMap parametric READ parametric NOTIFY parametricChanged)
    // HSL by hue band (colorzones): {found, enabled, bands: [{name, colour,
    // hue, lum, sat, shift} x 8] (each -100..100), channels: [{xs, ys} x 3]}.
    Q_PROPERTY(QVariantMap zones READ zones NOTIFY zonesChanged)
    // Crop: {found, enabled, cx, cy, cw, ch (edge fractions), ratioN, ratioD}.
    Q_PROPERTY(QVariantMap crop READ crop NOTIFY cropChanged)
    // While the crop tool is open the render shows the whole frame.
    Q_PROPERTY(bool cropMode READ cropMode WRITE setCropMode NOTIFY cropModeChanged)
    Q_PROPERTY(bool geometryBusy READ geometryBusy NOTIFY geometryChanged)
    Q_PROPERTY(QString geometryStatus READ geometryStatus NOTIFY geometryChanged)
    Q_PROPERTY(int historyEnd READ historyEnd NOTIFY historyChanged)
    Q_PROPERTY(int renderMs READ renderMs NOTIFY renderedChanged)
    Q_PROPERTY(int renderWidth READ renderWidth NOTIFY renderedChanged)
    Q_PROPERTY(int renderHeight READ renderHeight NOTIFY renderedChanged)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY renderedChanged)
    // The tone curve's INPUT histogram (the render without rgbcurve), loaded
    // on request per render; empty until the first load or after a new image.
    Q_PROPERTY(QVariantList curveHistogram READ curveHistogram NOTIFY curveHistogramChanged)
    // image://engine/<imgid>?g=<generation> — changes on every render.
    Q_PROPERTY(QString renderSource READ renderSource NOTIFY renderedChanged)
    // Soft proof: the render pushed through an output profile for display.
    Q_PROPERTY(bool proofOn READ proofOn WRITE setProofOn NOTIFY proofChanged)
    Q_PROPERTY(QString proofProfile READ proofProfile WRITE setProofProfile NOTIFY proofChanged)
    Q_PROPERTY(QString proofProfileName READ proofProfileName NOTIFY proofChanged)
    Q_PROPERTY(int proofIntent READ proofIntent WRITE setProofIntent NOTIFY proofChanged)
    Q_PROPERTY(bool proofGamutWarning READ proofGamutWarning WRITE setProofGamutWarning NOTIFY proofChanged)
    Q_PROPERTY(bool proofing READ proofing NOTIFY proofChanged)
    // image://engine-original/<n>: the photo at history step 0, on request.
    Q_PROPERTY(QString originalSource READ originalSource NOTIFY originalChanged)
    Q_PROPERTY(int snapshotId READ snapshotId NOTIFY snapshotChanged)
    Q_PROPERTY(QString snapshotSource READ snapshotSource NOTIFY snapshotChanged)
    Q_PROPERTY(int snapshotFullWidth READ snapshotFullWidth NOTIFY snapshotChanged)
    Q_PROPERTY(int snapshotFullHeight READ snapshotFullHeight NOTIFY snapshotChanged)
    Q_PROPERTY(QString snapshotStatus READ snapshotStatus NOTIFY snapshotChanged)
    // 0 = fit the view; otherwise the render is view × zoom (long edge capped).
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    // The developed image's pixel size (after crop), for 1:1 zoom.
    Q_PROPERTY(int fullWidth READ fullWidth NOTIFY fullSizeChanged)
    Q_PROPERTY(int fullHeight READ fullHeight NOTIFY fullSizeChanged)
    Q_PROPERTY(QVariantList detailTiles READ detailTiles NOTIFY detailChanged)
    Q_PROPERTY(QVariantList originalDetailTiles READ originalDetailTiles NOTIFY detailChanged)
    Q_PROPERTY(int originalFullWidth READ originalFullWidth NOTIFY originalChanged)
    Q_PROPERTY(int originalFullHeight READ originalFullHeight NOTIFY originalChanged)
    Q_PROPERTY(bool detailBusy READ detailBusy NOTIFY detailChanged)
    Q_PROPERTY(bool detailLimited READ detailLimited NOTIFY detailChanged)
    // Batch export queue: rows {file, out, status, error}; status queued|rendering|done|failed|cancelled
    Q_PROPERTY(QVariantList exportQueue READ exportQueue NOTIFY exportQueueChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportQueueChanged)
    Q_PROPERTY(bool exportPaused READ exportPaused NOTIFY exportQueueChanged)
    Q_PROPERTY(QString exportQueueError READ exportQueueError NOTIFY exportQueueChanged)
    Q_PROPERTY(int exportDone READ exportDone NOTIFY exportQueueChanged)
    Q_PROPERTY(int exportFailed READ exportFailed NOTIFY exportQueueChanged)
public:
    explicit EngineService(QObject *parent = nullptr);
    ~EngineService();

    // Starts the worker and initialises darktable in the background.
    void start(const QString &prefix, const QString &configDir, const QString &cacheDir, const QString &library);
    void setMemoryBudget(int mib);
    int detailBudgetKiB() const { return m_detailBudgetKiB; }

    bool available() const { return m_available; }
    bool ready() const { return m_available && m_ready; }
    QString version() const { return m_version; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool maintenanceReady() const { return m_ready && !m_busy && !m_dirty && !m_exporting && !m_syncPending && !m_fileOperations && !m_detailPending; }
    int imageId() const { return m_imgid; }
    QString imagePath() const { return m_path; }
    QString rawCameraDatabase() const { return m_rawCameraDatabase; }
    int imageVariant() const { return m_variant; }
    QVariantList params() const { return m_params; }
    QVariantList history() const { return m_history; }
    int paramsVersion() const { return m_paramsVersion; }
    // One module's rows of params, from a per-module index rebuilt once per
    // change instead of every panel filtering the whole list.
    Q_INVOKABLE QVariantList paramsFor(const QString &op) const;
    int historyEnd() const { return m_historyEnd; }
    int renderMs() const { return m_renderMs; }
    int renderWidth() const { return m_image.width(); }
    int renderHeight() const { return m_image.height(); }
    QVariantList histogram() const { return m_histogram; }
    QVariantList curveHistogram() const { return m_curveHistogram; }
    Q_INVOKABLE void loadCurveHistogram();
    const QImage &image() const { return m_image; }
    QString renderSource() const;
    // The worker, for tests that must call the bridge on its thread.
    QObject *workerObject() const;
    bool offlineBusy() const { return m_offlineBusy; }
    double offlineProgress() const { return m_offlineProgress; }
    QString offlineStatus() const { return m_offlineStatus; }
    QString sourceKind() const { return m_sourceKind; }
    Q_INVOKABLE QVariantMap smartPreviewInfo(const QString &path) const;
    void setSmartPreviews(const QStringList &paths, bool keep);
    Q_INVOKABLE void refreshSource();
    Q_INVOKABLE QVariantMap offlineInfo(const QString &path) const;
    void setOfflineCopies(const QStringList &paths, bool keep);
    Q_INVOKABLE void cancelOfflineCopies();
    // Thread-safe copy for the image provider.
    QImage imageCopy() const { QMutexLocker l(&m_imageMutex); return m_image; }
    QImage plainCopy() const { QMutexLocker l(&m_imageMutex); return m_plain; }
    QObject *ai() const;
    QObject *denoise() const;
    int editRevision() const { return m_generation.load(); }
    void addAiSelection(const QImage &mask);
    void requestAiSource(int ticket, const QString &stem);
    void requestAiRefinementSource(int ticket, const QString &path);
    void requestAiRepairSource(int ticket, const QString &stem, const QImage &mask);
    void previewAiRepair(int ticket, const QString &patch);
    void applyAiRepair(const QString &key);
    void discardAiRepair(const QString &key);
    void exportAiRepair(int ticket, const QString &key, const QString &stem);
    void prepareAiCopy(int ticket, const QString &source, const QString &destination);
    void prepareDenoiseCopy(const QString &path, const QVariantList &values);
    void openDenoiseCopy(const QString &path);
    void renderDenoisePreview(int ticket, const std::shared_ptr<QTemporaryDir> &directory,
                              const QString &source, const QVariantList &values);
    QImage originalCopy() const { QMutexLocker l(&m_imageMutex); return m_original; }
    QImage viewCopy() const { QMutexLocker l(&m_imageMutex); return m_viewImage; }
    QImage linearCopy() const { QMutexLocker l(&m_imageMutex); return m_linearImage; }
    QImage originalViewCopy() const { QMutexLocker l(&m_imageMutex); return m_originalView; }
    bool proofOn() const { return m_proofOn; }
    void setProofOn(bool on);
    QString proofProfile() const { return m_proofProfile; }
    void setProofProfile(const QString &pathOrUrl);
    QString proofProfileName() const { return m_proofName; }
    int proofIntent() const { return m_proofIntent; }
    void setProofIntent(int i);
    bool proofGamutWarning() const { return m_proofGamut; }
    void setProofGamutWarning(bool on);
    bool proofing() const { return m_proofOn && !m_proofProfile.isEmpty(); }
    QString originalSource() const;
    // Renders the untouched photo at the view size; originalChanged fires when it is in.
    Q_INVOKABLE void renderOriginal();
    // Saved settings render on a disposable engine image; the live image and
    // its undo/redo cursor are never restored or edited for comparison.
    void compareSnapshot(int id, const QVariantList &values);
    Q_INVOKABLE void clearSnapshotComparison();
    int snapshotId() const { return m_snapshotId; }
    QString snapshotSource() const;
    int snapshotFullWidth() const { return m_snapshotFullW; }
    int snapshotFullHeight() const { return m_snapshotFullH; }
    QString snapshotStatus() const { return m_snapshotStatus; }
    QImage snapshotViewCopy() const { QMutexLocker l(&m_imageMutex); return m_snapshotView; }

    // The curated parameter table: group, op, field, label, suffix, decimals.
    static QVariantList curatedParams();
    QVariantMap toolState() const { return m_toolState; }
    Q_INVOKABLE void toolAction(const QString &operation, const QString &action, const QVariantMap &values = QVariantMap());
    int scopeMode() const { return m_scopeMode; }
    void setScopeMode(int mode);
    bool scopeExpanded() const { return m_scopeExpanded; }
    void setScopeExpanded(bool expanded);
    // The palette pill and its key: false colour from any graph, and back to that graph.
    Q_INVOKABLE void toggleFalseColour();
    QString scopeSource() const;
    QImage scopeCopy() const { QMutexLocker l(&m_imageMutex); return m_scopeImage; }
    bool falseColour() const { return m_scopeMode == 4; }
    QVariantList falseColourBands() const { return m_falseColourBands; }
    // The shown render, or a detail tile of it, in false colour.
    QImage falseColourView() const;
    QImage falseColourDetail(const QString &key) const;
    static QImage falseColourOf(const QImage &picture);   // the false-colour map of any picture on screen
    bool rawClippingShown() const { return m_rawClippingShown; }
    void setRawClippingShown(bool shown);
    QString rawClippingSource() const;
    QString rawClippingStatus() const { return m_rawClippingStatus; }
    QImage rawClippingCopy() const { QMutexLocker l(&m_imageMutex); return m_rawClippingImage; }
    int retouchPreviewScale() const { return m_retouchPreviewScale; }
    void setRetouchPreviewScale(int scale);
    QString retouchPreviewSource() const;
    QImage retouchPreviewCopy() const { QMutexLocker l(&m_imageMutex); return m_retouchPreviewImage; }

    Q_INVOKABLE void load(const QString &path, int variant = 0);
    // Variants live in the engine as versions of the same file. All three
    // are asynchronous; the outcome arrives on the matching signal.
    void duplicateImage(const QString &path, int variant);
    void removeImage(const QString &path, int variant);
    void swapVersions(const QString &path, int variantA, int variantB);
    // A catalogued folder moved: keep the engine's histories attached.
    void relinkFolder(const QString &oldDir, const QString &newDir);
    // One file renamed or moved on disk: the engine's rows for every version follow.
    void moveImage(const QString &oldPath, const QString &newPath);
    bool reducedPreviews() const { return m_reducedPreviews.load(); }
    bool exportFullResolution() const { return m_exportFullResolution.load(); }
    void setExportFullResolution(bool on);
    // Restores persisted engine preferences; call once before start().
    void loadSettings();
    void setReducedPreviews(bool on);
    Q_INVOKABLE void setViewSize(int w, int h);
    double zoom() const { return m_zoom; }
    void setZoom(double z);
    Q_INVOKABLE void setParam(const QString &op, const QString &field, double value);
    // Many fields in one history step's worth of work and one render: [{op, field, value}].
    // Exclusive preset selection. Generic applyValues remains a patch operation.
    Q_INVOKABLE void applyPresetValues(const QVariantList &values);
    Q_INVOKABLE void applyPresetValuesTo(const QVariantList &items, const QVariantList &values);
    Q_INVOKABLE void applyValues(const QVariantList &values);
    QObject *imageMatch() const;
    void requestImageMatch(int ticket,const QString &action,const QVariantMap &data,
                           const std::shared_ptr<std::atomic<int>> &cancel);
    // Whether a module is on, as the parameter table last read it.
    Q_INVOKABLE bool moduleEnabled(const QString &op) const;
    // All three coordinates travel together; subsequent samples share one undo step.
    Q_INVOKABLE void setPrimaryGrade(const QString &zone, double hue, double chroma,
                                    double level, bool separateEdit = true);
    // Continuous controls publish a live overview while native detail waits.
    // Owners can overlap; a stale release cannot end another control's edit.
    Q_INVOKABLE void setPreviewEditing(QObject *owner, bool active);
    bool previewEditing() const { return !m_previewEditors.isEmpty(); }
    Q_INVOKABLE void setPrimaryGradeEditing(QObject *owner, bool active);
    bool primaryGradeEditing() const { return previewEditing(); }

    // Current values of the curated parameters as [{op, field, value, enabled}]
    // (what a preset or snapshot saves). Disabled modules retain their
    // saved values and are switched off after their parameters are restored.
    // With `withLocals`, the local adjustments ride along as rows with
    // local:true; applyValues then rebuilds them (snapshots do, presets don't).
    Q_INVOKABLE QVariantList currentValues(bool withLocals = false) const;
    Q_INVOKABLE void resetParam(const QString &op, const QString &field);
    QVariantList locals() const { return m_locals; }
    int activeLocal() const { return m_activeLocal; }
    QVariantList shapes() const;
    // The developed image's pixel size (after crop), once the engine has read it.
    int fullWidth() const { return m_fullW; }
    int fullHeight() const { return m_fullH; }
    Q_INVOKABLE void setDetailView(double x, double y, double width, double height, double scale);
    QVariantList detailTiles() const;
    QVariantList originalDetailTiles() const;
    int originalFullWidth() const { return m_originalFullW; }
    int originalFullHeight() const { return m_originalFullH; }
    Q_INVOKABLE void setOriginalDetailView(double x, double y, double width, double height, double scale);
    bool detailBusy() const;
    bool detailLimited() const { return m_detailLimited; }
    QImage detailImage(const QString &key) const;
    QVariantList ranges() const;
    bool maskShown() const { return m_maskShown; }
    void setMaskShown(bool on);
    QString maskColour() const { return m_maskColour; }
    void setMaskColour(const QString &c) { if (c == m_maskColour) return; m_maskColour = c; emit maskChanged(); rebuildMask(); }
    double maskStrength() const { return m_maskStrength; }
    void setMaskStrength(double v) { if (qFuzzyCompare(v, m_maskStrength)) return; m_maskStrength = v; emit maskChanged(); rebuildMask(); }
    QString maskSource() const;
    QImage maskCopy() const { QMutexLocker l(&m_imageMutex); return m_mask; }
    bool clippingShown() const { return m_clippingShown; }
    void setClippingShown(bool on);
    QString clippingSource() const;
    double clippedHighlights() const { return m_clipHi; }
    double clippedShadows() const { return m_clipLo; }
    QImage clippingCopy() const { QMutexLocker l(&m_imageMutex); return m_clipping; }
    int activeShape() const { return m_activeShape; }
    void setActiveShape(int i) { if (i == m_activeShape) return; m_activeShape = i; emit activeShapeChanged(); }
    void setActiveLocal(int p) { if (p == m_activeLocal) return; m_activeLocal = p; emit activeLocalChanged(); }
    // shape 1 radial at the centre, 2 gradient from the top; the new one becomes active.
    // Shapes of the active local: index, shape, combine, inverse, geometry.
    Q_PROPERTY(QVariantList shapes READ shapes NOTIFY localsChanged)
    // The shape being dragged on the viewer, -1 for none.
    Q_PROPERTY(int activeShape READ activeShape WRITE setActiveShape NOTIFY activeShapeChanged)
    // Parametric ranges of the active local: luminance, hue, chroma.
    Q_PROPERTY(QVariantList ranges READ ranges NOTIFY localsChanged)
    // Mask preview: what the active local's mask actually covers, painted
    // over the render. Colour and strength are the reader's to set.
    Q_PROPERTY(bool maskShown READ maskShown WRITE setMaskShown NOTIFY maskChanged)
    Q_PROPERTY(QString maskColour READ maskColour WRITE setMaskColour NOTIFY maskChanged)
    Q_PROPERTY(double maskStrength READ maskStrength WRITE setMaskStrength NOTIFY maskChanged)
    Q_PROPERTY(QString maskSource READ maskSource NOTIFY maskSourceChanged)
    // Clipping indicators: where the render clips, highlights red and
    // shadows blue, over the picture; the shares are counted every render.
    Q_PROPERTY(bool clippingShown READ clippingShown WRITE setClippingShown NOTIFY clippingChanged)
    Q_PROPERTY(QString clippingSource READ clippingSource NOTIFY clippingChanged)
    Q_PROPERTY(double clippedHighlights READ clippedHighlights NOTIFY clippingChanged)
    Q_PROPERTY(double clippedShadows READ clippedShadows NOTIFY clippingChanged)

    Q_INVOKABLE void addLocal(int shape);
    Q_INVOKABLE void setLocalMask(int priority, double cx, double cy, double a, double b, double rotation, double opacity);
    Q_INVOKABLE void setLocalParam(int priority, const QString &field, double value);
    Q_INVOKABLE void setLocalColour(int priority, double warmth, double tint);
    Q_INVOKABLE void setLocalEnabled(int priority, bool on);
    Q_INVOKABLE void removeLocal(int priority);
    Q_INVOKABLE void addShape(int shape);
    // `nodes` is a list of {x, y} in 0..1; size and hardness are the brush's.
    Q_INVOKABLE void addBrushStroke(const QVariantList &nodes, double size, double hardness, double flow = 1.0);
    // Pen nodes contain x/y and inX/inY/outX/outY in the displayed frame.
    // A new local is created only when the user completes a closed path.
    Q_INVOKABLE void addPenPath(const QVariantList &nodes, double feather, bool newLocal);
    Q_INVOKABLE void setPenPath(int index, const QVariantList &nodes, double feather);
    // A completed on-image gesture gets its own undo step; slider updates coalesce.
    Q_INVOKABLE void setShape(int index, double cx, double cy, double a, double b, double rotation, double opacity, bool separateEdit = false);
    Q_INVOKABLE void setShapeCombine(int index, int combine, bool inverse);
    Q_INVOKABLE void removeShape(int index);
    // Housekeeping on the active local's shapes: a copy beside the original,
    // a new position in the combine order, bypass, a name, brush settings.
    Q_INVOKABLE void duplicateShape(int index);
    Q_INVOKABLE void moveShape(int from, int to);
    Q_INVOKABLE void setShapeEnabled(int index, bool on);
    Q_INVOKABLE void renameShape(int index, const QString &name);
    Q_INVOKABLE void setBrush(int index, double size, double hardness, double flow);
    Q_INVOKABLE void renameLocal(int priority, const QString &name);
    // Copy the active local (mask, ranges and adjustments) and paste it onto
    // whatever photo is loaded, as a new local. Shapes are in fractions of
    // the frame, so they land in the same relative place; the status line
    // says when the two photos are shaped differently.
    Q_PROPERTY(bool hasMaskClipboard READ hasMaskClipboard NOTIFY maskClipboardChanged)
    bool hasMaskClipboard() const { return !m_maskClipboard.isEmpty(); }
    Q_INVOKABLE void copyMask();
    Q_INVOKABLE void pasteMask();
    // Develop settings across photos. A settings group is a slice of the
    // saved state (see settingsGroups): the clipboard holds the chosen
    // groups of the open photo; paste lays them on the open photo; sync lays
    // them on a list of other photos [{path, variant}] on the worker, each
    // one re-rendered so its thumbnail follows.
    Q_PROPERTY(bool hasSettingsClipboard READ hasSettingsClipboard NOTIFY settingsClipboardChanged)
    Q_PROPERTY(int syncPending READ syncPending NOTIFY syncChanged)
    Q_PROPERTY(QStringList clipboardModules READ clipboardModules NOTIFY settingsClipboardChanged)
    bool hasSettingsClipboard() const { return !m_settingsClipboard.isEmpty(); }
    int syncPending() const { return m_syncPending; }
    Q_INVOKABLE static QVariantList settingsGroups();
    static QString groupOf(const QVariantMap &row);
    static QString moduleOf(const QVariantMap &row);
    Q_INVOKABLE QVariantList valuesFor(const QStringList &groups) const;
    Q_INVOKABLE QVariantList valuesForModule(const QString &operation) const;
    QStringList clipboardModules() const;
    Q_INVOKABLE bool copySettings(const QStringList &groups);
    Q_INVOKABLE bool copyModuleSettings(const QString &operation);
    Q_INVOKABLE bool pasteModuleSettings(const QString &operation);
    Q_INVOKABLE QVariantList settingsClipboard() const { return m_settingsClipboard; }
    Q_INVOKABLE void pasteSettings();
    Q_INVOKABLE void applyValuesTo(const QVariantList &items, const QVariantList &values);
    // The Library's quick adjust: move one field of each photo in `items` by
    // `delta` from wherever it stands, clamped to [lo, hi]. The open photo
    // takes the ordinary edit path; the others go to the worker like a sync,
    // sharing its counters and syncFinished.
    Q_INVOKABLE void nudgeValues(const QVariantList &items, const QString &op, const QString &field, double delta, double lo, double hi);
    Q_INVOKABLE void nudgeFigures(const QVariantList &items, const QString &op, const QString &field, double delta);
    // Sidecars: ask for a photo's edit (answered by sidecarEditReady), and
    // put edits read from sidecars onto their photos.
    void requestSidecarEdit(const QString &path, bool raw);
    Q_INVOKABLE void applySidecarEdits(const QVariantList &items);
    void sourceRewritten(const QString &path);
    QString sourceStamp(const QString &path, QString *kind = nullptr) const;
    // Lens profile: {found, cameraFound, lensFound, overridden, camera, lens,
    // exifCamera, exifLens, body} - what lensfun matched for the open photo;
    // body is the engine's own name for the camera (camera profiles match it).
    // Candidates are the database's lenses for the body's mount, loaded on
    // request; setLensOverride("") goes back to the file's own lens.
    Q_PROPERTY(QVariantMap lensProfile READ lensProfile NOTIFY lensProfileChanged)
    Q_PROPERTY(QStringList lensCandidates READ lensCandidates NOTIFY lensCandidatesChanged)
    QVariantMap lensProfile() const { return m_lensProfile; }
    QStringList lensCandidates() const { return m_lensCandidates; }
    Q_INVOKABLE void loadLensCandidates();
    Q_INVOKABLE void setLensOverride(const QString &lens);
    // Parametric range on the active local: channel 0 luminance, 1 hue,
    // 2 chroma; p0..p3 are the trapezoid nodes over 0..1.
    Q_INVOKABLE void setRange(int channel, bool active, bool inverse, double p0, double p1, double p2, double p3);
    Q_INVOKABLE void setMaskInverted(bool on);
    // Seed a range from the picture: the pixel at (fx, fy) of the frame,
    // read off the render the mask is measured against, becomes the middle
    // of a band a fifth wide with a short falloff either side.
    Q_INVOKABLE void pickRange(int channel, double fx, double fy);
    // Click/drag selection in native colour space; a new mask is created
    // only after the user finishes sampling. Existing shapes are preserved.
    Q_INVOKABLE void pickColourRange(double x, double y, double x2, double y2, bool newLocal);
    Q_INVOKABLE void setColourRange(double width, double softness);
    // Edge refinement of the active local's mask: blur and feather radii
    // in full-image pixels, guide 0 input / 1 output, curve -1..1.
    Q_INVOKABLE void setMaskRefine(double blur, double feather, int guide, double contrast, double brightness);
    QVariantList spots() const { return m_spots; }
    int activeSpot() const { return m_activeSpot; }
    void setActiveSpot(int i) { if (i == m_activeSpot) return; m_activeSpot = i; emit activeSpotChanged(); }
    Q_INVOKABLE void addSpot(int algorithm, double cx, double cy, double radius, double border, double sx, double sy);
    Q_INVOKABLE void setSpot(int index, double cx, double cy, double radius, double border, double sx, double sy, double opacity);
    Q_INVOKABLE void setSpotAlgorithm(int index, int algorithm, double blurRadius, double fillBrightness);
    Q_INVOKABLE void removeSpot(int index);
    QVariantMap curve() const { return m_curve; }
    Q_INVOKABLE void setCurve(int channel, const QVariantList &xs, const QVariantList &ys, int type);
    Q_INVOKABLE void setCurveLinked(bool linked);
    Q_INVOKABLE void resetCurve(int channel);
    QVariantMap parametric() const { return m_parametric; }
    // region 0 highlights, 1 lights, 2 darks, 3 shadows; value -100..100
    Q_INVOKABLE void setParametric(int region, double value);
    Q_INVOKABLE void resetParametric();
    // Lab colour curves, carried in `parametric.ab` (a, then b): nodes are
    // (value + 128) / 256, so 0.5 is neutral. channel 1 = a, 2 = b.
    Q_INVOKABLE void setLabCurve(int channel, const QVariantList &xs, const QVariantList &ys, int type);
    Q_INVOKABLE void resetLabCurve(int channel);
    // Lab colour as sliders over those two curves, in `parametric.lab`:
    // separation, greens, magentas, blues, yellows, tintA, tintB (each −100 to
    // 100) and `custom` when the curves were shaped by hand. See labcolour.h.
    Q_INVOKABLE void setLabColour(const QString &key, double value);
    Q_INVOKABLE void resetLabColour();
    QVariantMap zones() const { return m_zones; }
    // channel 0 lightness, 1 chroma, 2 hue; value -100..100
    Q_INVOKABLE void setZone(int band, int channel, double value);
    Q_INVOKABLE void resetZones();
    // One adjustment back to how it starts, whatever shape its settings take:
    // slider rows to their defaults, and the curve and band modules (which
    // have no rows) straightened. An adjustment that is off stays off.
    Q_INVOKABLE void resetModule(const QString &op);
    Q_INVOKABLE void resetTool(const QString &key);
    // The automatic first edit a new RAW gets, matching what the editors
    // people arrive from apply by default: capture sharpening, colour noise
    // out (grain kept), and the lens profile when the lens is recognised.
    // Empty for a photo that is not a RAW. See Backend::giveFirstEdit.
    QVariantList firstEditValues() const;
    bool toneMapperParked(const QString &op) const;
    QString rivalToneMapper(const QString &op) const;
    QImage meteredImage() const;
    // Waits until every job already queued on the engine's worker has run
    // (it runs one at a time, in order): exports and edits handed over
    // without a busy flag included. For the command line; never from the
    // interface thread while a render is expected to stay responsive.
    void drainWorker();
    // The eight bands: name, swatch colour, Lab hue in turns.
    static QVariantList zoneBands();
    QVariantMap crop() const { return m_crop; }
    bool cropMode() const { return m_cropMode; }
    void setCropMode(bool on);
    Q_INVOKABLE void setCrop(double cx, double cy, double cw, double ch, int ratioN, int ratioD, bool separateEdit = false);
    // Straighten along a line drawn on the render (fractions): the nearest
    // horizontal or vertical becomes level, on top of the current rotation.
    Q_INVOKABLE void straightenAlong(double x0, double y0, double x1, double y1);
    bool geometryBusy() const { return m_geometryBusy; }
    QString geometryStatus() const { return m_geometryStatus; }
    // 0 constrain, 1 level, 2 vertical, 3 horizontal, 4 both, 5 guides,
    // 6 reset. Crop policy -1 retains the current policy.
    Q_INVOKABLE void correctGeometry(int operation, int cropPolicy = -1, const QVariantList &guides = {});
    Q_INVOKABLE void setViewerActive(QObject *viewer, bool active);
    // White balance helpers: the camera's own, a neutral under the cursor
    // (fractions of the render), or the whole picture as grey.
    Q_INVOKABLE void whiteBalanceAsShot();
    Q_INVOKABLE void pickWhiteBalance(double fx, double fy);
    Q_INVOKABLE void autoWhiteBalance() { pickWhiteBalance(-1, -1); }
    // One exposure step that brings the render's mean to mid grey; undoable.
    Q_INVOKABLE void autoExposure();
    // True while Auto is still measuring renders and correcting.
    Q_INVOKABLE bool autoRunning() const { return m_auto.waiting || m_auto.requested; }
    // A drag is over (a slider released): the next edit is a History step of
    // its own even on the same control and even at once.
    Q_INVOKABLE void endGesture() { m_editKey.clear(); }
    // True while an undo group's marks or a held Undo/Redo are outstanding.
    Q_INVOKABLE bool historyBusy() const { return m_marksInFlight > 0 || m_heldSteps != 0; }
    // Text fields (a LUT file): the row's `text` follows.
    QVariantList creativeProfiles() const;
    QVariantMap cameraProfileState() const { return m_cameraProfileState; }
    Q_INVOKABLE void refreshCameraProfiles();
    Q_INVOKABLE bool importCameraProfile(const QString &source);
    Q_INVOKABLE bool selectCameraProfile(const QString &key);
    Q_INVOKABLE bool applyCameraLook();
    Q_INVOKABLE bool importCreativeProfile(const QString &source);
    Q_INVOKABLE bool selectCreativeProfile(const QString &key);
    Q_INVOKABLE QString creativeProfileFileStatus(const QString &path) const;
    Q_INVOKABLE QVariantMap creativeProfileDetails(const QString &path) const;
    Q_INVOKABLE void setParamString(const QString &op, const QString &field, const QString &value);
    Q_INVOKABLE void setModuleEnabled(const QString &op, bool on);
    Q_INVOKABLE void jumpHistory(int end);
    // One undo step for an action that adds several history items.
    Q_INVOKABLE void beginUndoGroup();
    Q_INVOKABLE void endUndoGroup();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Reset: back to the photo as it looked after import (a RAW keeps its
    // starting exposure, colour calibration and tone mapper, and the
    // automatic sharpening, colour noise reduction and lens profile).
    // resetToOriginal: the camera original, with nothing on. Both discard
    // the previous edit/redo history; full resets cannot be undone.
    Q_INVOKABLE void resetHistory() { resetTo(true); }
    Q_INVOKABLE void resetToOriginal() { resetTo(false); }
    // An ICC profile's description (the Output page shows a paper profile by it).
    Q_INVOKABLE QString proofProfileNameOf(const QString &path) const;
    Q_INVOKABLE void exportJpeg(const QString &path, int maxEdge, int quality);
    Q_INVOKABLE void refresh();

    QVariantList exportQueue() const { return m_queue; }
    bool exporting() const { return m_exporting; }
    bool exportPaused() const { return m_exportPaused; }
    QString exportQueueError() const { return m_queueError; }
    Q_INVOKABLE void pauseExport();
    Q_INVOKABLE void resumeExport();
    int exportDone() const { return m_exportDone; }
    int exportFailed() const { return m_exportFailed; }
    // JPEG export of many files into `folder`, sequential on the worker.
    Q_INVOKABLE void exportBatch(const QStringList &paths, const QString &folder, int maxEdge, int quality, const QString &suffix,
                                 const QString &format = QStringLiteral("jpeg"), int bpp = 8, int metaFlags = -1);
    // Same for [{path, variant, name}] rows: a variant's file is named after
    // its variant name (or -vN) so it never collides with the master's.
    // `finishing` (Finishing::Options as a map) sharpens and/or watermarks
    // each file after the engine wrote it; empty = leave the file alone.
    // The file name: `suffix` is appended to the original name, unless it
    // holds a token — then it is the whole name. Tokens: {name} {tag}
    // {seq} {date} {time} {camera} {rating} {folder}; see exportName.
    static QString exportName(const QString &pattern, const QVariantMap &item, int seq);
    Q_INVOKABLE QString exportNamePreview(const QString &pattern, const QVariantMap &item) const { return exportName(pattern, item, 1); }
    // Export options: {mode: long|short|width|height|megapixels|percent|none,
    // value} for the resize (maxEdge is the long-edge fallback when absent);
    // copyOriginal / copySidecar put the source file and its XMP beside the
    // export;
    // ppi (> 0) is written as the file's resolution for print.
    // exportSize reads the resize half → the (maxW, maxH) the engine gets
    // for a photo of w×h (orientation applied).
    static QSize exportSize(const QVariantMap &resize, int maxEdge, int w, int h, int orientation);
    // The output queue: failed and cancelled rows back to queued, a row
    // out, a queued row up or down, a copy of a row queued again.
    Q_INVOKABLE void retryFailed();
    Q_INVOKABLE void removeQueueRow(int index);
    Q_INVOKABLE void moveQueueRow(int index, int delta);
    Q_INVOKABLE void duplicateQueueRow(int index);
    Q_INVOKABLE void exportBatchItems(const QVariantList &items, const QString &folder, int maxEdge, int quality, const QString &suffix,
                                      const QString &format = QStringLiteral("jpeg"), int bpp = 8, int metaFlags = -1,
                                      const QVariantMap &finishing = QVariantMap(), const QVariantMap &options = QVariantMap());
    // Build darktable's flag word from the four user-facing choices.
    Q_INVOKABLE static int metaFlagsFor(bool exif, bool location, bool keywords, bool history);
    // Format modules the engine offers: [{id, label, ext, quality(bool), bpp(bool)}]
    Q_INVOKABLE QVariantList exportFormats() const;
    // Empty when the profile can be written; `bpp` is the file's bit depth.
    Q_INVOKABLE QString outputProfileError(int profile, const QString &path, int bpp = 16) const;
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void clearExportQueue();

signals:
    void cameraProfileStateChanged();
    void creativeProfilesChanged();
    void editRevisionChanged();
    void aiSourceReady(int ticket, const QString &path, const QString &error);
    void aiRefinementSourceReady(int ticket, const QString &path, const QString &error);
    void aiRepairSourceReady(int ticket, const QString &path, const QString &error);
    void aiRepairReady(int ticket, const QString &key, const QImage &preview, const QString &error);
    void aiMaskAccepted(bool ok, const QString &error);
    void aiCopyPrepared(int ticket, const QString &error);
    void denoiseCopyPrepared(const QString &path, const QString &error);
    void denoisePreviewReady(int ticket, const QImage &before, const QImage &after, const QString &error);
    void imageMatchReady(int ticket,const QString &action,const QVariantMap &result,const QString &error);
    void imageMatchApplied(const QString &path,int variant);
    void scopeChanged();
    void toolStateChanged();
    void rawClippingChanged();
    void retouchPreviewChanged();
    void sourceChanged();
    void offlineChanged();
    void offlineFinished(int done, int failed, bool cancelled);
    void detailChanged();
    void availableChanged();
    void statusChanged();
    void busyChanged();
    void reducedPreviewsChanged();
    void exportFullResolutionChanged();
    void imageChanged();
    void paramsChanged();
    void historyChanged();
    void localsChanged();
    void originalChanged();
    void snapshotChanged();
    void proofChanged();
    void activeLocalChanged();
    void activeShapeChanged();
    void maskChanged();
    void maskSourceChanged();
    void clippingChanged();
    void maskClipboardChanged();
    void settingsClipboardChanged();
    void lensProfileChanged();
    void lensCandidatesChanged();
    void curveHistogramChanged();
    // Undo, redo, a History row or Reset: every control's value is replaced.
    void historyJumped();
    // Discard deferred control input before replacing the current edit.
    void editStateReplaced();
    // Commit deferred control input before Auto chooses its metering frame.
    void autoExposureRequested();
    void syncChanged();
    // Every photo of an applyValuesTo call has been handled.
    void syncFinished(int done, int failed);
    void spotsChanged();
    void activeSpotChanged();
    void curveChanged();
    void zonesChanged();
    void parametricChanged();
    void cropChanged();
    void geometryChanged();
    void geometryCompleted(int operation, bool ok);
    void cropModeChanged();
    void renderedChanged();
    void exported(const QString &path, bool ok);
    // An edit or read the engine refused (the status line says it too).
    void engineFailed(const QString &error);
    // A photo's edit for its sidecar (has = the engine has edited it).
    void sidecarEditReady(const QString &path, bool has, const QByteArray &engineXmp, const QVariantMap &cameraRaw, const QString &error);
    // A queue row finished: the SOURCE it came from, for the catalog's Recently Exported.
    void sourceExported(const QString &source, int variant, bool ok);
    void exportQueueChanged();
    void zoomChanged();
    void fullSizeChanged();
    void exportBatchFinished(int done, int failed);
    // A render of `path` (variant) after the user changed its history.
    // `flags`: what the edit holds — 1 crop, 2 local adjustment, 4 retouch spot.
    void editedRender(const QString &path, int variant, const QImage &render, int flags);
    void imageDuplicated(const QString &path, int variant, int newVariant, const QString &error);
    void imageRemoved(const QString &path, int variant, const QString &error);
    void imageMoved(const QString &oldPath, const QString &newPath, const QString &error);
    void versionsSwapped(const QString &path, int variantA, int variantB, const QString &error);
    void folderRelinked(const QString &oldDir, const QString &newDir, const QString &error);

private:
    QString m_rawCameraDatabase;
    void resetTo(bool start);
    void resetModuleFields(const QString &op, const QStringList &fields = {});
    void jumpToHistory(int end);
    QVariantList withoutHiddenChanges(const QVariantList &rows) const;
    void editBoundary(const QString &key = QString());
    static QString groupKey(const QString &path, int variant);
    void storeGroups();
    void writeGroupsFile();
    void relocateGroups(const QString &oldPath, const QString &newPath, bool folder);
    void copyGroups(const QString &path, int from, int to, bool swap = false);
    void forgetGroups(const QString &path, int variant);
    void recordLabMaster(int end, double master);
    void pruneLabMasters(int end);
    bool m_labPending = false;   // a Lab slider step not yet read back
    QVariantMap m_savedGroups;   // photo → its history ends inside undo groups
    QString m_groupsPath;
    QString m_editKey;
    QElapsedTimer m_editClock;
    // Auto meters an ungraded linear reference, then writes one absolute EV.
    struct AutoRun {
        bool waiting = false, requested = false, restored = false, needsExposure = false;
        int imgid = -1;
        double from = 0;
    } m_auto;
    int m_autoSerial = 0;
    void cancelAuto();
    void continueAuto();
    void startAutoExposure();
    bool m_autoApplying = false;
    int m_renderedGeneration = -1, m_failedGeneration = -1;
    int m_requestedParams = 0, m_receivedParams = 0;
    // Where Auto last left a photo: pressing it again with nothing changed since is a no-op.
    struct { QString path; int variant = 0, end = -2; double exposure = 0; QString sourceStamp; } m_lastAuto;
    void requestRender(bool preserveDetails = false);
    void invalidatePreview(bool preserveDetails = false);
    void setStatus(const QString &s);
    void setBusy(bool b);

    QThread m_thread;
    EngineWorker *m_worker = nullptr;
    AiService *m_ai = nullptr;
    DenoiseService *m_denoise = nullptr;
    ImageMatchService *m_imageMatch = nullptr;
    QString m_sourceKind, m_sourceStamp;
    QStringList m_pendingSmartPreviews;
    bool m_smartJob = false;
    bool m_offlineBusy = false;
    double m_offlineProgress = 0;
    QString m_offlineStatus;
    std::atomic_bool m_offlineCancel{false};
    bool m_available = false, m_ready = false, m_busy = false;
    QString m_version, m_status, m_path;
    int m_imgid = -1;
    int m_variant = 0;
    int m_viewW = 1200, m_viewH = 800;
    double m_zoom = 0;
    QImage m_image, m_original;
    bool m_proofOn = false, m_proofGamut = false;
    int m_proofIntent = 1;
    QString m_proofProfile, m_proofName;
    QImage m_measured;   // what the histogram and scopes read (see rendered)
    void pushProof();
    int m_originalSerial = 0, m_originalGeneration = 0;
    bool m_originalRequested = false;
    int m_snapshotId = 0, m_snapshotEngineId = -1;
    int m_snapshotFullW = 0, m_snapshotFullH = 0, m_snapshotSerial = 0;
    std::atomic<int> m_snapshotGeneration{0};
    QVariantList m_snapshotValues;
    QImage m_snapshotView;
    QString m_snapshotStatus;
    void requestSnapshot();
    void requestOriginal();
    mutable QMutex m_imageMutex;
    int m_renderSerial = 0;
    QVariantList m_params, m_history, m_histogram, m_locals;
    QVariantList m_curveHistogram;
    int m_curveHistogramSerial = 0, m_paramsSerial = 0, m_paramsVersion = 0;
    void paramsTouched();
    mutable QHash<QString, QVariantList> m_byOp;
    mutable int m_byOpVersion = -1;
    bool m_curveHistogramInFlight = false;
    int m_activeLocal = -1;
    int m_activeShape = 0;
    QVariantMap m_maskClipboard;      // a local row, ready to rebuild
    QVariantList m_settingsClipboard; // saved-state rows of the chosen groups
    QVariantMap m_lensProfile;
    QVariantMap m_cameraProfileState;
    QStringList m_lensCandidates;
    int m_syncPending = 0, m_syncDone = 0, m_syncFailed = 0;
    QVariantList m_spots;
    QVariantMap m_toolState;
    int m_scopeMode = 0, m_scopeSerial = 0, m_scopeBefore = 0;
    bool m_scopeExpanded = false;
    // The tone mappers a print stock parked, to bring back when it goes.
    void nudge(const QVariantList &items, const QString &op, const QString &field, double delta, double lo, double hi, const QString &how);
    QStringList m_printHeld;
    bool m_basecurveOn = false;   // not a curated row; read with the params
    bool m_readIsJump = false;    // the next params read-back follows a jump
    int m_readJumpTarget = -1;
    bool printHandOff(const QString &op, const QString &field, const QVariant &value, bool on);
    QVariantList m_falseColourBands;
    QImage m_scopeImage;
    bool m_rawClippingShown = false;
    int m_rawClippingSerial = 0;
    QImage m_rawClippingImage;
    QString m_rawClippingStatus;
    void requestRawClipping();
    int m_retouchPreviewScale = -1, m_retouchPreviewSerial = 0;
    QImage m_retouchPreviewImage;
    void requestRetouchPreview();
    void rebuildScope();
    int m_activeSpot = -1;
    QVariantMap m_curve;
    QVariantMap m_zones;
    QVariantMap m_parametric;
    double m_labMaster = 0; bool m_labMasterKnown = false; int m_labImage = -1;
    void refreshLabColour();
    QVariantMap m_crop;
    bool m_cropMode = false;
    bool m_geometryBusy = false;
    QString m_geometryStatus;
    int m_fullW = 0, m_fullH = 0;      // the developed image's pixel size, for the preview's radii
    int m_clipW = 0, m_clipH = 0;     // the frame it was copied from
    // Mask preview, rebuilt whenever the mask or the render moves.
    void rebuildMask();
    void paintMask();
    bool m_maskShown = false;
    QString m_maskColour = QStringLiteral("#ff3b30");
    double m_maskStrength = 0.6;
    QImage m_mask;
    QImage m_viewImage, m_originalView; // float linear, transformed only by photo providers
    QImage m_linearImage;              // unproofed float render for saved comparisons
    QImage m_plain;                  // the untouched sRGB render (the shown one may be proofed)
    QImage m_clipping;               // the clipping overlay, built when shown
    bool m_clippingShown = false;
    int m_clippingSerial = 0;
    double m_clipHi = 0, m_clipLo = 0;
    void rebuildClipping();
    int m_maskSerial = 0;
    QImage m_maskCoverage;              // native float selection coverage
    int m_maskCoverageFor = -1;         // which local it belongs to
    int m_maskCoverageSerial = -1;      // the render serial it was made for
    bool m_maskCoveragePending = false;
    int m_historyEnd = 0, m_renderMs = 0;
    QSet<int> m_innerSteps;          // history ends inside an undo group
    int m_groupFrom = -1, m_undoDepth = 0, m_marksInFlight = 0;   // marks sent to the worker, not yet back
    int m_heldSteps = 0;   // Undo (+) / Redo (−) presses waiting for those marks
    void releaseHeldSteps();
    int m_jumpTarget = -1;           // the history end a pending jump will read back
    bool m_openFailed = false;       // the open photo could not be opened from its current source
    std::atomic_bool m_reducedPreviews{false};
    std::atomic_bool m_exportFullResolution{false};
    std::atomic<int> m_generation{0};
    int m_pending = 0;
    int m_fileOperations = 0;
    int m_detailBudgetKiB = 256 * 1024;
    mutable QCache<QString, QImage> m_detailCache{256 * 1024}; // KiB
    // Keep the last visible detail through tonal edits; both generations
    // share the existing 256 MiB budget. Replace the visible set together.
    QHash<QObject *, QMetaObject::Connection> m_previewEditors;
    bool m_interactivePreviewSized = false;
    bool m_previewParamsDeferred = false;
    QHash<QString, QImage> m_retainedDetailImages;
    QVariantList m_retainedDetailTiles[2];
    int m_retainedDetailRevision = -1;
    void retainVisibleDetails();
    void clearRetainedDetails();
    void releaseRetainedDetailsIfReady();
    struct DetailLayer {
        QRectF view;
        double requestedScale = 0, scale = 1;
        QList<QRect> wanted;
        int width = 0, height = 0;
    };
    DetailLayer m_detailLayers[2];
    QPointer<QObject> m_activeViewer;
    int m_originalFullW = 0, m_originalFullH = 0;
    std::atomic<int> m_detailGeneration{0};
    bool m_detailPending = false;
    QString m_pendingDetailKey;
    bool m_detailLimited = false;
    void invalidateDetails(bool preserve = false);
    void rebuildDetails();
    void renderNextDetail();
    QString detailKey(const QRect &rect, int layer = 0) const;
    QVariantList detailTilesFor(int layer) const;
    QVariantList plainDetailTilesFor(int layer) const;
    void setDetailLayer(int layer, double x, double y, double width, double height, double scale);
    bool m_dirty = false;
    void exportNext();
    bool saveExportQueue();
    void notifyExportQueue(bool persist = true);
    QString m_queuePath, m_queueError;
    bool m_exportPaused = false, m_queueUnreadable = false;
    QVariantList m_queue;
    int m_queueIndex = 0, m_exportDone = 0, m_exportFailed = 0;
    bool m_exporting = false, m_cancelExport = false;
    QString m_exportFolder, m_exportSuffix, m_exportFormat = QStringLiteral("jpeg");
    int m_exportBpp = 8;
    int m_exportMeta = -1;
    QVariantMap m_exportFinishing;
    QVariantMap m_exportOptions;
    QStringList m_formats;
    int m_exportEdge = 2048, m_exportQuality = 90;
};

// Serves the untouched render for before/after.
// Serves the mask preview overlay.
class MaskImageProvider : public QQuickImageProvider {
public:
    explicit MaskImageProvider(EngineService *svc) : QQuickImageProvider(QQuickImageProvider::Image), m_svc(svc) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    EngineService *m_svc;
};

class ClippingImageProvider : public QQuickImageProvider {
public:
    explicit ClippingImageProvider(EngineService *svc) : QQuickImageProvider(QQuickImageProvider::Image), m_svc(svc) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    EngineService *m_svc;
};

class EngineOriginalProvider : public QQuickImageProvider {
public:
    explicit EngineOriginalProvider(EngineService *svc) : QQuickImageProvider(QQuickImageProvider::Image), m_svc(svc) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    EngineService *m_svc;
};

// Serves the engine's latest render to QML Image items.
class EngineDetailProvider : public QQuickImageProvider {
public:
    explicit EngineDetailProvider(EngineService *service) : QQuickImageProvider(QQuickImageProvider::Image), m_service(service) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    EngineService *m_service;
};

class EngineImageProvider : public QQuickImageProvider {
public:
    explicit EngineImageProvider(EngineService *svc) : QQuickImageProvider(QQuickImageProvider::Image), m_svc(svc) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    EngineService *m_svc;
};
