#pragma once
#include <QObject>
#include <QImage>
#include <QMutex>
#include <QStringList>
#include <QVariantMap>
#include <QSet>
#include <memory>
#include <atomic>

namespace oma::gpu { class Context; }

// Suite boundary: straight float linear Rec.709 D65. The RAW engine owns
// camera input and developing; OCIO owns RGB presentation and standard output.
class ColourPipeline : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString configPath READ configPath NOTIFY changed)
    Q_PROPERTY(QString configName READ configName NOTIFY changed)
    Q_PROPERTY(QString configId READ configId NOTIFY changed)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString display READ display NOTIFY changed)
    Q_PROPERTY(QString view READ view NOTIFY changed)
    Q_PROPERTY(double exposure READ exposure NOTIFY changed)
    Q_PROPERTY(QStringList displays READ displays NOTIFY changed)
    Q_PROPERTY(QStringList views READ views NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool gpuEnabled READ gpuEnabled WRITE setGpuEnabled NOTIFY changed)
public:
    static ColourPipeline &instance();
    explicit ColourPipeline(QObject *parent = nullptr);
    ~ColourPipeline() override;
    QString configPath() const;
    QString configName() const;
    QString configId() const;
    QString version() const;
    QString display() const;
    QString view() const;
    double exposure() const;
    QStringList displays() const;
    QStringList views() const;
    QString error() const { return m_error; }
    bool loadSettings();
    bool gpuEnabled() const { return m_gpuEnabled.load(); }
    void setGpuEnabled(bool enabled);
    Q_INVOKABLE QVariantMap gpuBenchmark() const;
    QVariantMap textureBenchmark() const;
    // The engine's colour chain (exposure to sigmoid) as one resident GPU
    // pass; the same protocol as texture(): a negative cpuMs attempts admitted
    // GPU work, a positive one offers the native result for qualification.
    int chain(const struct oma_chain_stage_t *stages, int count, const float *input, float *output,
              int width, int height, double cpuMs, int (*cancelled)(void *), void *request);
    QVariantMap chainBenchmark() const;
    // Do not benchmark changing primary settings in the middle of a gesture.
    // Already qualified work remains usable; release can qualify the final grade.
    void setPrimaryGradeInteractive(const QObject *owner, bool active);

    int texture(const float *input, float *output, int width, int height,
                float amount, float sourceScale, double cpuMs,
                int (*cancelled)(void *), void *request);
    // Call after viewer/worker teardown and before QGuiApplication destruction.
    void releaseGpu();
    // Empty path selects the bundled immutable Oma Colour 1. Only SDR sRGB
    // displays are offered because that is the Qt surface's declared space.
    Q_INVOKABLE bool configure(const QString &path = {}, const QString &display = {},
                              const QString &view = {}, double exposure = 0, bool persist = true);
    Q_INVOKABLE QVariantMap inspect(const QString &path, const QString &display = {}) const;
    Q_INVOKABLE QString metadata() const;
    QImage present(const QImage &image) const; // selected view, still float sRGB
    static QImage linear(const QImage &image); // ICC input bridge; no view
    static QImage srgb(const QImage &image);   // standard encoded float; no view
    static QImage srgb8(const QImage &image);
    static QString bundledHash();
signals:
    void changed();
    void errorChanged();
private:
    struct State;
    std::shared_ptr<const State> state() const;
    mutable QMutex m_mutex;
    std::shared_ptr<const State> m_state;
    QString m_error;
    std::shared_ptr<oma::gpu::Context> m_gpu;
    std::shared_ptr<oma::gpu::Context> m_textureGpu;
    std::shared_ptr<oma::gpu::Context> m_chainGpu;
    QSet<const QObject *> m_primaryEditors;
    std::atomic_bool m_primaryInteractive{false};
    std::atomic_bool m_gpuEnabled{true};
};
