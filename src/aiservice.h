#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <QImage>
#include <QMutex>
#include <memory>

class EngineService;

// Inference is isolated from both Qt and the serial RAW engine. A draft is
// tied to one completed render; changing its photo/edit invalidates it.
class AiService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool creatingMask READ creatingMask NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString overlay READ overlay NOTIFY changed)
    Q_PROPERTY(QString result READ result NOTIFY changed)
    Q_PROPERTY(bool showResult READ showResult WRITE setShowResult NOTIFY changed)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY changed)
    Q_PROPERTY(bool useGpu READ useGpu WRITE setUseGpu NOTIFY changed)
    Q_PROPERTY(bool brushMode READ brushMode WRITE setBrushMode NOTIFY changed)
    Q_PROPERTY(bool objectBrush READ objectBrush WRITE setObjectBrush NOTIFY changed)
    Q_PROPERTY(double brushSize READ brushSize WRITE setBrushSize NOTIFY changed)
    Q_PROPERTY(double removalMargin READ removalMargin WRITE setRemovalMargin NOTIFY changed)
    Q_PROPERTY(bool canUndoSelection READ canUndoSelection NOTIFY changed)
    Q_PROPERTY(QVariantList points READ points NOTIFY changed)
    Q_PROPERTY(QVariantList box READ box NOTIFY changed)
public:
    explicit AiService(EngineService *engine);
    ~AiService() override;
    bool installed() const;
    bool busy() const { return m_busy; }
    bool creatingMask() const { return m_acceptingMask; }
    QString mode() const { return m_mode; }
    QString status() const { return m_status; }
    QString overlay() const { return m_overlay; }
    QString result() const;
    bool showResult() const { return m_showResult; }
    void setShowResult(bool value) { m_showResult = value; emit changed(); }
    QImage previewCopy() const { QMutexLocker lock(&m_mutex); return m_preview; }
    bool hasSelection() const { return !m_maskPath.isEmpty(); }
    bool useGpu() const { return m_gpu; }
    void setUseGpu(bool value);
    bool brushMode() const { return m_brushMode; }
    void setBrushMode(bool value) { if (!m_busy) { m_brushMode = value; emit changed(); } }
    bool objectBrush() const { return m_objectBrush; }
    void setObjectBrush(bool value) { if (!m_busy) { m_objectBrush = value; emit changed(); } }
    double brushSize() const { return m_brushSize; }
    void setBrushSize(double value);
    double removalMargin() const { return m_removalMargin; }
    void setRemovalMargin(double value);
    bool canUndoSelection() const { return !m_selectionUndo.isEmpty(); }
    QVariantList points() const { return m_points; }
    QVariantList box() const { return m_box; }
    Q_INVOKABLE void install();
    Q_INVOKABLE void start(const QString &mode);
    Q_INVOKABLE void point(double x, double y, bool subtract = false);
    Q_INVOKABLE void selectBox(double x0, double y0, double x1, double y1);
    Q_INVOKABLE void undoPoint();
    Q_INVOKABLE void brushStroke(const QVariantList &points, bool subtract = false);
    Q_INVOKABLE void refineEdges();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void invert();
    Q_INVOKABLE void acceptMask();
    Q_INVOKABLE void remove();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void saveCopy(const QString &fileOrUrl);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void saved(const QString &path);
    void operationFailed(const QString &message);
    void maskCreated();
private:
    QString home() const;
    bool prepareScripts();
    bool current() const;
    void stopProcess();
    void requestMask();
    void send(const QVariantMap &request);
    void readOutput();
    void failed(const QString &message);
    void setBusy(bool value);
    void rememberSelection();
    bool updateOverlay();
    bool writeSelection(const QImage &mask);
    struct Selection { QString mask; QVariantList points, box; bool inverted; };
    QList<Selection> m_selectionUndo;
    EngineService *m_engine;
    QProcess m_process;
    QTimer m_timeout, m_idle, m_resume;
    std::unique_ptr<QTemporaryDir> m_temp;
    QByteArray m_output, m_errors;
    mutable QMutex m_mutex;
    QImage m_preview;
    QVariantList m_points, m_box;
    QString m_mode, m_status, m_overlay, m_result, m_maskPath, m_action, m_savedPath;
    QString m_sourcePath, m_repairKey;
    bool m_savingCopy = false;
    int m_revision = -1, m_image = -1, m_request = 0;
    bool m_busy = false, m_installing = false, m_gpu = false, m_inverted = false, m_sourcePending = false;
    bool m_showResult = true;
    bool m_brushMode = false;
    bool m_objectBrush = true;
    bool m_repairRequired = false, m_stopping = false;
    bool m_acceptingMask = false;
    double m_brushSize = .015, m_removalMargin = .005;
};
