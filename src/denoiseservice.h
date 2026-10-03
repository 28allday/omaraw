#pragma once
#include <QObject>
#include <QImage>
#include <QMutex>
#include <QProcess>
#include <QPointF>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <memory>

class EngineService;
// A separate RAW operation: no develop settings are baked into its DNG.
class DenoiseService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(double strength READ strength WRITE setStrength NOTIFY changed)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY changed)
    Q_PROPERTY(QString actualBackend READ actualBackend NOTIFY changed)
    Q_PROPERTY(QString detectedSensor READ detectedSensor NOTIFY changed)
    Q_PROPERTY(QString actualModel READ actualModel NOTIFY changed)
    Q_PROPERTY(int isoOverride READ isoOverride WRITE setIsoOverride NOTIFY changed)
    Q_PROPERTY(double previewX READ previewX WRITE setPreviewX NOTIFY changed)
    Q_PROPERTY(double previewY READ previewY WRITE setPreviewY NOTIFY changed)
    Q_PROPERTY(QString before READ before NOTIFY changed)
    Q_PROPERTY(QString after READ after NOTIFY changed)
    Q_PROPERTY(QVariantMap previewArea READ previewArea NOTIFY previewAreaChanged)
public:
    explicit DenoiseService(EngineService *engine);
    ~DenoiseService() override;
    bool installed() const;
    bool busy() const { return m_busy; }
    bool canCancel() const { return m_busy && !m_finishingCopy; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    double strength() const { return m_strength; }
    void setStrength(double value);
    QString backend() const { return m_backend; }
    void setBackend(const QString &value);
    QString actualBackend() const { return m_actualBackend; }
    QString detectedSensor() const { return m_sensor; }
    QString actualModel() const { return m_model; }
    int isoOverride() const { return m_iso; }
    void setIsoOverride(int value);
    double previewX() const { return m_x; }
    double previewY() const { return m_y; }
    void setPreviewX(double value);
    void setPreviewY(double value);
    QString before() const;
    QString after() const;
    QImage previewCopy(bool after) const;
    QVariantMap previewArea() const { return m_previewArea; }
    Q_INVOKABLE void install();
    Q_INVOKABLE void preview();
    Q_INVOKABLE void previewAt(double x, double y);
    Q_INVOKABLE void apply();
    Q_INVOKABLE void saveCopy(const QString &fileOrUrl);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();
signals:
    void changed();
    void previewAreaChanged();
    void saved(const QString &path, const QString &source, int variant);
    void operationFailed(const QString &message);
private:
    QString home() const;
    bool prepareScripts();
    void start(const QString &action, const QString &destination = {}, bool keepPreview = false);
    void launch();
    void stop();
    void readOutput();
    void finished(int code, QProcess::ExitStatus exit);
    void fail(const QString &message);
    void clearPreview();
    bool mutableSetting();
    EngineService *m_engine;
    QProcess m_process;
    QTimer m_timeout;
    std::shared_ptr<QTemporaryDir> m_temp;
    QByteArray m_output, m_errors;
    QVariantMap m_request, m_answer;
    QVariantMap m_previewArea;
    QPointF m_pendingPosition;
    bool m_navigationPending = false;
    QVariantList m_previewValues, m_copyValues;
    QString m_status, m_destination, m_source, m_actualBackend;
    QString m_sensor, m_model, m_photoPath;
    QString m_backend = "auto";
    qint64 m_sourceSize = 0, m_sourceModified = 0;
    int m_image = -1, m_variant = 0, m_iso = 0, m_serial = 0, m_photoId = -1;
    double m_strength = .6, m_progress = 0, m_x = .5, m_y = .5;
    bool m_busy = false, m_installing = false, m_stopping = false, m_retried = false;
    bool m_repairRequired = false, m_finishingCopy = false;
    mutable QMutex m_mutex;
    QImage m_before, m_after;
};
