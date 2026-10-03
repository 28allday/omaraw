#include "colourpipeline.h"
#include "colourbridge.h"
#include <OpenColorIO/OpenColorIO.h>
#include <oma/gpu/pipeline.h>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QCache>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUrl>
#include <lcms2.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace OCIO = OCIO_NAMESPACE;
namespace {
struct Bundle {
    QByteArray bytes;
    OCIO::ConstConfigRcPtr config;
    OCIO::ConstCPUProcessorRcPtr decode, encode;
    Bundle() {
        QFile file(QStringLiteral(":/colour/oma-colour-1.ocio"));
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Bundled Oma Colour 1 is missing.");
        bytes = file.readAll();
        std::istringstream stream(bytes.toStdString());
        config = OCIO::Config::CreateFromStream(stream);
        config->validate();
        decode = config->getProcessor("sRGB", "oma_interchange")->getDefaultCPUProcessor();
        encode = config->getProcessor("oma_interchange", "sRGB")->getDefaultCPUProcessor();
    }
};
const Bundle &bundle() { static const Bundle b; return b; }
QMutex engineErrorMutex;
QByteArray engineError;
std::atomic<unsigned long long> stageCalls{0};
QString sha(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString pathOf(const QString &path) {
    const QString p = path.startsWith("file:") ? QUrl(path).toLocalFile() : path.trimmed();
    return p.isEmpty() || p.startsWith("ocio://") ? p : QFileInfo(p).absoluteFilePath();
}
OCIO::ConstConfigRcPtr readConfig(const QString &path) {
    if (path.isEmpty()) return bundle().config;
    auto config = path.startsWith("ocio://")
        ? OCIO::Config::CreateFromBuiltinConfig(path.mid(7).toUtf8().constData())
        : OCIO::Config::CreateFromFile(path.toUtf8().constData());
    config->validate();
    return config;
}
QStringList displayNames(const OCIO::ConstConfigRcPtr &c) {
    QStringList names;
    for (int i = 0; i < c->getNumDisplays(); ++i) {
        const QString name = QString::fromUtf8(c->getDisplay(i));
        // Standard OCIO configs use "sRGB" or "sRGB - Display". A P3,
        // Rec.1886 or HDR output cannot be sent to our declared sRGB surface.
        if (name.compare("sRGB", Qt::CaseInsensitive) == 0
            || name.compare("sRGB - Display", Qt::CaseInsensitive) == 0) names << name;
    }
    return names;
}
QStringList viewNames(const OCIO::ConstConfigRcPtr &c, const QString &display) {
    QStringList names;
    for (int i = 0; i < c->getNumViews(display.toUtf8().constData()); ++i)
        names << QString::fromUtf8(c->getView(display.toUtf8().constData(), i));
    return names;
}
void apply(float *pixels, int w, int h, qsizetype stride, const OCIO::ConstCPUProcessorRcPtr &cpu) {
    // Three channels with a four-float pixel stride: alpha is coverage and
    // remains unchanged even if a user configuration contains alpha ops.
    OCIO::PackedImageDesc desc(pixels, w, h, 3, OCIO::BIT_DEPTH_F32,
                               sizeof(float), 4 * sizeof(float), stride);
    cpu->apply(desc);
}
QImage apply(QImage out, const OCIO::ConstCPUProcessorRcPtr &cpu) {
    if (!out.isNull()) apply(reinterpret_cast<float *>(out.bits()), out.width(), out.height(), out.bytesPerLine(), cpu);
    return out;
}
struct Profile {
    cmsHPROFILE handle;
    explicit Profile(const QByteArray &icc) : handle(cmsOpenProfileFromMem(icc.constData(), icc.size())) {}
    ~Profile() { if (handle) cmsCloseProfile(handle); }
};
QImage iccLinear(const QImage &image) {
    QImage out = image.convertToFormat(QImage::Format_RGBA32FPx4);
    const Profile source(image.colorSpace().iccProfile()), target(QColorSpace(QColorSpace::SRgbLinear).iccProfile());
    cmsHTRANSFORM t = source.handle && target.handle ? cmsCreateTransform(source.handle, TYPE_RGBA_FLT,
        target.handle, TYPE_RGBA_FLT, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOOPTIMIZE) : nullptr;
    if (!t) throw std::runtime_error("Cannot convert the photograph's ICC profile to linear Rec.709.");
    for (int y = 0; y < out.height(); ++y) cmsDoTransform(t, out.constScanLine(y), out.scanLine(y), out.width());
    cmsDeleteTransform(t);
    return out;
}
OCIO::MatrixTransformRcPtr colorants(cmsHPROFILE profile) {
    auto m = OCIO::MatrixTransform::Create();
    const auto *r = static_cast<const cmsCIEXYZ *>(cmsReadTag(profile, cmsSigRedColorantTag));
    const auto *g = static_cast<const cmsCIEXYZ *>(cmsReadTag(profile, cmsSigGreenColorantTag));
    const auto *b = static_cast<const cmsCIEXYZ *>(cmsReadTag(profile, cmsSigBlueColorantTag));
    if (!r || !g || !b) throw std::runtime_error("Standard output profile has no RGB matrix.");
    // Both ICC matrices use the D50 PCS, including the engine's Bradford
    // adaptation and profile quantisation. Pixels match the embedded profile.
    const double values[] = {r->X,g->X,b->X,0, r->Y,g->Y,b->Y,0, r->Z,g->Z,b->Z,0, 0,0,0,1};
    m->setMatrix(values);
    return m;
}
OCIO::ConstCPUProcessorRcPtr outputProcessor(int profile, const QByteArray &source, const QByteArray &target) {
    static QMutex mutex;
    static QHash<QString, OCIO::ConstCPUProcessorRcPtr> cache;
    const QString key = QString::number(profile) + sha(source) + sha(target);
    QMutexLocker lock(&mutex);
    if (cache.contains(key)) return cache.value(key);
    const Profile input(source), output(target);
    if (!input.handle || !output.handle) throw std::runtime_error("Missing standard output profile.");
    auto group = OCIO::GroupTransform::Create();
    group->appendTransform(colorants(input.handle));
    auto inverse = colorants(output.handle);
    inverse->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
    group->appendTransform(inverse);
    if (profile == 0) {
        // The engine's embedded sRGB ICC and Qt's interchange ICC need not
        // have identical D50 colourants. Convert primaries before encoding.
        group->appendTransform(bundle().config->getProcessor("oma_interchange", "sRGB")->createGroupTransform());
    } else if (profile == 1) {
        auto gamma = OCIO::ExponentTransform::Create();
        const double values[] = {1.0 / 2.19921875, 1.0 / 2.19921875, 1.0 / 2.19921875, 1};
        gamma->setValue(values);
        gamma->setNegativeStyle(OCIO::NEGATIVE_MIRROR);
        group->appendTransform(gamma);
    }
    // darktable's DT_COLORSPACE_PROPHOTO_RGB is LINEAR ProPhoto (gamma 1).
    const auto cpu = bundle().config->getProcessor(group)->getDefaultCPUProcessor();
    cache.insert(key, cpu);
    return cpu;
}
}

struct ColourPipeline::State {
    OCIO::ConstConfigRcPtr config;
    OCIO::ConstCPUProcessorRcPtr processor;
    oma::gpu::Pipeline gpuPipeline;
    QString path, display, view, id;
    double exposure = 0;
};
ColourPipeline &ColourPipeline::instance() { static ColourPipeline p; return p; }
ColourPipeline::ColourPipeline(QObject *parent) : QObject(parent), m_gpu(std::make_shared<oma::gpu::Context>()) { configure({}, {}, {}, 0, false); }
ColourPipeline::~ColourPipeline() = default;
std::shared_ptr<const ColourPipeline::State> ColourPipeline::state() const { QMutexLocker lock(&m_mutex); return m_state; }
QString ColourPipeline::configPath() const { auto s = state(); return s ? s->path : QString(); }
QString ColourPipeline::configName() const { auto s = state(); return s ? QString::fromUtf8(s->config->getName()) : QString(); }
QString ColourPipeline::configId() const { auto s = state(); return s ? s->id : QString(); }
QString ColourPipeline::version() const { return QString::fromLatin1(OCIO::GetVersion()); }
QString ColourPipeline::display() const { auto s = state(); return s ? s->display : QString(); }
QString ColourPipeline::view() const { auto s = state(); return s ? s->view : QString(); }
double ColourPipeline::exposure() const { auto s = state(); return s ? s->exposure : 0; }
QStringList ColourPipeline::displays() const { auto s = state(); return s ? displayNames(s->config) : QStringList(); }
QStringList ColourPipeline::views() const { auto s = state(); return s ? viewNames(s->config, s->display) : QStringList(); }
QString ColourPipeline::bundledHash() { return sha(bundle().bytes); }

bool ColourPipeline::configure(const QString &path, const QString &display, const QString &view, double ev, bool persist) {
    try {
        if (!std::isfinite(ev) || ev < -20 || ev > 20) throw std::runtime_error("Preview exposure must be between -20 and 20 stops.");
        OCIO::ClearAllCaches(); // explicitly applying also reloads config/LUT files
        auto next = std::make_shared<State>();
        next->path = pathOf(path);
        next->config = readConfig(next->path);
        const auto displays = displayNames(next->config);
        if (displays.isEmpty()) throw std::runtime_error("This configuration needs an SDR sRGB display (sRGB or sRGB - Display).");
        next->display = display.isEmpty() ? displays.first() : display;
        if (!displays.contains(next->display)) throw std::runtime_error("Select an sRGB display from this configuration.");
        next->view = view.isEmpty() ? QString::fromUtf8(next->config->getDefaultView(next->display.toUtf8().constData())) : view;
        if (!viewNames(next->config, next->display).contains(next->view)) throw std::runtime_error("The selected view is not in this configuration.");
        auto group = OCIO::GroupTransform::Create();
        auto gain = OCIO::MatrixTransform::Create();
        const double g = std::exp2(ev), diagonal[] = {g,0,0,0, 0,g,0,0, 0,0,g,0, 0,0,0,1};
        gain->setMatrix(diagonal);
        group->appendTransform(gain);
        const char *input = "oma_interchange";
        if (!next->config->hasRole(input)) {
            if (!next->config->hasRole(OCIO::ROLE_INTERCHANGE_SCENE))
                throw std::runtime_error("Configuration needs oma_interchange (linear Rec.709 D65) or aces_interchange (ACES2065-1).");
            group->appendTransform(bundle().config->getProcessor("oma_interchange", OCIO::ROLE_INTERCHANGE_SCENE)->createGroupTransform());
            input = OCIO::ROLE_INTERCHANGE_SCENE;
        }
        auto transform = OCIO::DisplayViewTransform::Create();
        transform->setSrc(input);
        transform->setDisplay(next->display.toUtf8().constData());
        transform->setView(next->view.toUtf8().constData());
        group->appendTransform(transform);
        const auto processor = next->config->getProcessor(group);
        next->processor = processor->getDefaultCPUProcessor();
        next->gpuPipeline.colour(processor);
        next->id = QString::fromUtf8(next->config->getCacheID());
        next->exposure = ev;
        { QMutexLocker lock(&m_mutex); m_state = next; }
        m_error.clear();
        if (persist) {
            QSettings settings;
            settings.setValue("colour/config", next->path);
            settings.setValue("colour/configId", next->id);
            settings.setValue("colour/display", next->display);
            settings.setValue("colour/view", next->view);
            settings.setValue("colour/exposure", ev);
        }
        emit changed(); emit errorChanged();
        return true;
    } catch (const std::exception &e) { m_error = QString::fromUtf8(e.what()); emit errorChanged(); return false; }
}
bool ColourPipeline::loadSettings() {
    QSettings settings;
    m_gpuEnabled = settings.value("gpu/compute", true).toBool();
    const QString path = pathOf(qEnvironmentVariable("OCIO", settings.value("colour/config").toString()));
    const bool same = path == pathOf(settings.value("colour/config").toString());
    if (configure(path, same ? settings.value("colour/display").toString() : QString(),
                  same ? settings.value("colour/view").toString() : QString(),
                  same ? settings.value("colour/exposure", 0).toDouble() : 0, false)) {
        if (!same || settings.value("colour/configId").toString().isEmpty()
            || settings.value("colour/configId").toString() == configId()) return true;
        m_error = tr("The saved OCIO configuration or its LUTs changed. Apply it again in Colour Management to adopt the change.");
    }
    const QString problem = m_error;
    if (!configure({}, {}, {}, 0, false)) return false;
    m_error = tr("Using Oma Colour 1. %1").arg(problem);
    emit errorChanged();
    return true;
}
void ColourPipeline::setGpuEnabled(bool enabled) {
    if (m_gpuEnabled.exchange(enabled) == enabled) return;
    QSettings().setValue("gpu/compute", enabled);
    emit changed();
}
QVariantMap ColourPipeline::gpuBenchmark() const {
    std::shared_ptr<oma::gpu::Context> gpu;
    { QMutexLocker lock(&m_mutex); gpu = m_gpu; }
    if (!gpu) return {{"passed", false}, {"reason", "GPU resources released"}};
    const auto result = gpu->lastBenchmark();
    return {{"passed", result.passed}, {"preferVulkan", result.preferVulkan},
            {"cpuMs", result.cpuMs}, {"vulkanMs", result.vulkanMs},
            {"maxError", result.maxError}, {"reason", QString::fromStdString(result.reason)}};
}
void ColourPipeline::releaseGpu() {
    std::shared_ptr<oma::gpu::Context> retired, texture, chain;
    { QMutexLocker lock(&m_mutex); retired.swap(m_gpu); texture.swap(m_textureGpu); chain.swap(m_chainGpu); }
    // Drain benchmarking and release Vulkan while loader/layer globals and
    // the GUI application still exist, rather than during static destruction.
}
QVariantMap ColourPipeline::inspect(const QString &path, const QString &display) const {
    try {
        const auto c = readConfig(pathOf(path));
        const auto ds = displayNames(c);
        const QString d = display.isEmpty() ? ds.value(0) : display;
        return {{"name", QString::fromUtf8(c->getName())}, {"displays", ds}, {"display", d},
                {"views", viewNames(c, d)}, {"view", QString::fromUtf8(c->getDefaultView(d.toUtf8().constData()))}};
    } catch (const std::exception &e) { return {{"error", QString::fromUtf8(e.what())}}; }
}
QString ColourPipeline::metadata() const {
    auto s = state();
    if (!s) return {};
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"schema", "org.oma.colour/1"}, {"engine", "ocio"},
        {"config", s->path.isEmpty() ? "oma-colour-1" : s->path}, {"configId", s->id},
        {"bundledHash", bundledHash()}, {"configName", QString::fromUtf8(s->config->getName())},
        {"display", s->display}, {"view", s->view}, {"exposure", s->exposure},
        {"interchange", "linear Rec.709 D65"}, {"viewBaked", false}}).toJson(QJsonDocument::Compact));
}
QImage ColourPipeline::linear(const QImage &image) {
    if (image.isNull()) return {};
    QImage out;
    if (image.colorSpace() == QColorSpace(QColorSpace::SRgbLinear)) out = image.convertToFormat(QImage::Format_RGBA32FPx4);
    else if (!image.colorSpace().isValid() || image.colorSpace() == QColorSpace(QColorSpace::SRgb))
        out = apply(image.convertToFormat(QImage::Format_RGBA32FPx4), bundle().decode);
    else out = iccLinear(image);
    out.setColorSpace(QColorSpace::SRgbLinear);
    return out;
}
QImage ColourPipeline::srgb(const QImage &image) {
    QImage out = apply(linear(image), bundle().encode);
    out.setColorSpace(QColorSpace::SRgb);
    return out;
}
QImage ColourPipeline::srgb8(const QImage &image) { return srgb(image).convertToFormat(QImage::Format_ARGB32); }
QImage ColourPipeline::present(const QImage &image) const {
    try {
        auto s = state();
        if (!s || image.isNull()) return {};
        const QImage input = linear(image);
        QImage out;
        std::shared_ptr<oma::gpu::Context> gpu;
        { QMutexLocker lock(&m_mutex); gpu = m_gpu; }
        if (gpu && m_gpuEnabled && qgetenv("OMA_GPU").toLower() != "cpu") {
            const oma::gpu::ImageView view{input.width(), input.height(), reinterpret_cast<const float *>(input.constBits()), size_t(input.bytesPerLine())};
            auto result = gpu->runAdaptive(view, s->gpuPipeline);
            auto storage = std::make_unique<oma::gpu::Image>(std::move(result.image));
            out = QImage(reinterpret_cast<uchar *>(storage->rgba.data()), storage->width, storage->height,
                         qsizetype(storage->width) * 16, QImage::Format_RGBA32FPx4,
                         [](void *p) { delete static_cast<oma::gpu::Image *>(p); }, storage.get());
            if (!out.isNull()) storage.release();
        } else out = apply(input, s->processor);
        out.setColorSpace(QColorSpace::SRgb);
        return out;
    } catch (const std::exception &e) { qWarning() << "OCIO preview:" << e.what(); return {}; }
}

extern "C" void oma_colour_stage(const float *input, float *output, int w, int h,
                                   const float *matrix, const float *const curves[3],
                                   const float *coeffs, int lutsize, int curvesBefore) {
    try {
        if (!input || !output || w <= 0 || h <= 0) throw std::runtime_error("Invalid engine colour stage.");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(&curvesBefore), sizeof curvesBefore));
        if (matrix) hash.addData(QByteArrayView(reinterpret_cast<const char *>(matrix), 12 * sizeof(float)));
        bool nonlinear = false;
        for (int c = 0; c < 3; ++c) {
            const char enabled = curves && curves[c] && lutsize > 1 && curves[c][0] >= 0.f;
            hash.addData(QByteArrayView(&enabled, 1));
            if (enabled) {
                nonlinear = true;
                hash.addData(QByteArrayView(reinterpret_cast<const char *>(curves[c]), qsizetype(lutsize) * sizeof(float)));
                hash.addData(QByteArrayView(reinterpret_cast<const char *>(coeffs + c*3), 3 * sizeof(float)));
            }
        }
        static QMutex mutex;
        // Account for three 65536-float shapers; bounded across large catalogs.
        static QCache<QByteArray, OCIO::ConstCPUProcessorRcPtr> processors(32 * 1024);
        OCIO::ConstCPUProcessorRcPtr cpu;
        {
            QMutexLocker lock(&mutex);
            const QByteArray key = hash.result();
            if (const auto *cached = processors.object(key)) cpu = *cached;
            else {
                auto group = OCIO::GroupTransform::Create();
                OCIO::Lut1DTransformRcPtr shaper;
                if (nonlinear) {
                    // A half-domain LUT is sampled over negative and HDR values,
                    // not a [0,1] table that clips highlights. OCIO interpolates
                    // the shaper on float pixels; linear profiles use no LUT.
                    shaper = OCIO::Lut1DTransform::Create(65536, true);
                    shaper->setInterpolation(OCIO::INTERP_LINEAR);
                    for (unsigned i = 0; i < 65536; ++i) {
                        const int exponent = int((i >> 10) & 31), mantissa = int(i & 1023);
                        float x = exponent == 31 ? 65504.f : exponent == 0 ? std::ldexp(float(mantissa), -24)
                            : std::ldexp(1.f + float(mantissa)/1024.f, exponent - 15);
                        if (i & 32768) x = -x;
                        float values[3];
                        for (int c = 0; c < 3; ++c) {
                            if (!curves[c] || curves[c][0] < 0.f) { values[c] = x; continue; }
                            if (x < 1.f) {
                                const float position = std::max(0.f, x) * (lutsize - 1);
                                const int index = std::min(lutsize - 2, int(position));
                                const float f = position - index;
                                values[c] = curves[c][index] * (1.f - f) + curves[c][index + 1] * f;
                            } else values[c] = coeffs[c*3+1] * std::pow(x * coeffs[c*3], coeffs[c*3+2]);
                        }
                        shaper->setValue(i, values[0], values[1], values[2]);
                    }
                }
                if (shaper && curvesBefore) group->appendTransform(shaper);
                if (matrix) {
                    auto m = OCIO::MatrixTransform::Create();
                    double values[] = {matrix[0],matrix[1],matrix[2],0, matrix[4],matrix[5],matrix[6],0,
                                       matrix[8],matrix[9],matrix[10],0, 0,0,0,1};
                    m->setMatrix(values); group->appendTransform(m);
                }
                if (shaper && !curvesBefore) group->appendTransform(shaper);
                cpu = bundle().config->getProcessor(group)->getDefaultCPUProcessor();
                processors.insert(key, new OCIO::ConstCPUProcessorRcPtr(cpu), nonlinear ? 768 : 1);
            }
        }
        if (input != output) std::memcpy(output, input, size_t(w) * h * 4 * sizeof(float));
        apply(output, w, h, qsizetype(w) * 4 * sizeof(float), cpu);
        ++stageCalls;
    } catch (const std::exception &e) {
        { QMutexLocker lock(&engineErrorMutex); engineError = e.what(); }
        if (output && w > 0 && h > 0) std::fill(output, output + size_t(w)*h*4, 0.f);
    }
}
extern "C" int oma_colour_engine_check(int reset, char *error, int errorSize) {
    QMutexLocker lock(&engineErrorMutex);
    if (reset) { engineError.clear(); return 0; }
    if (engineError.isEmpty()) return 0;
    if (error && errorSize > 0) std::snprintf(error, size_t(errorSize), "OCIO engine transform: %s", engineError.constData());
    return -1;
}
extern "C" unsigned long long oma_colour_stage_calls(void) { return stageCalls.load(); }

extern "C" const void *oma_colour_interchange_profile(int *size) {
    static const QByteArray icc = QColorSpace(QColorSpace::SRgbLinear).iccProfile();
    if (size) *size = icc.size();
    return icc.constData();
}

extern "C" int oma_colour_interchange(float *rgba, int w, int h, const double matrix[9],
                                       char *error, int errorSize) {
    try {
        if (!rgba || w <= 0 || h <= 0 || !matrix)
            throw std::runtime_error("Invalid native colour buffer.");
        for (int i = 0; i < 9; ++i) if (!std::isfinite(matrix[i]))
            throw std::runtime_error("Invalid native colour matrix.");
        static QMutex mutex;
        static QHash<QByteArray, OCIO::ConstCPUProcessorRcPtr> cache;
        const QByteArray key(reinterpret_cast<const char *>(matrix), 9 * sizeof(double));
        OCIO::ConstCPUProcessorRcPtr cpu;
        {
            QMutexLocker lock(&mutex);
            cpu = cache.value(key);
            if (!cpu) {
                int size = 0; const void *bytes = oma_colour_interchange_profile(&size);
                const Profile target(QByteArray(static_cast<const char *>(bytes), size));
                if (!target.handle) throw std::runtime_error("Missing interchange profile.");
                auto input = OCIO::MatrixTransform::Create();
                const double values[] = {matrix[0],matrix[1],matrix[2],0,
                    matrix[3],matrix[4],matrix[5],0, matrix[6],matrix[7],matrix[8],0, 0,0,0,1};
                input->setMatrix(values);
                auto output = colorants(target.handle);
                output->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
                auto group = OCIO::GroupTransform::Create();
                group->appendTransform(input); group->appendTransform(output);
                cpu = bundle().config->getProcessor(group)->getDefaultCPUProcessor();
                cache.insert(key, cpu);
            }
        }
        apply(rgba, w, h, qsizetype(w) * 4 * sizeof(float), cpu);
        return 0;
    } catch (const std::exception &e) {
        if (error && errorSize > 0) std::snprintf(error, size_t(errorSize), "%s", e.what());
        return -1;
    }
}

extern "C" int oma_colour_output(float *rgba, int w, int h, int profile,
                                  const void *source, int sourceSize, const void *target, int targetSize,
                                  int intent, char *error, int errorSize) {
    try {
        if (!rgba || w <= 0 || h <= 0 || profile < 0 || profile > 3 || intent < 0 || intent > 3)
            throw std::runtime_error("Invalid colour output request.");
        const QByteArray sourceIcc(static_cast<const char *>(source), sourceSize), targetIcc(static_cast<const char *>(target), targetSize);
        if (profile < 3) apply(rgba, w, h, qsizetype(w) * 4 * sizeof(float), outputProcessor(profile, sourceIcc, targetIcc));
        else {
            // Arbitrary LUT/device ICCs require Little CMS; there is no sRGB
            // clipping/quantisation between the float engine and this bridge.
            const Profile input(sourceIcc), output(targetIcc);
            cmsHTRANSFORM t = input.handle && output.handle ? cmsCreateTransform(input.handle, TYPE_RGBA_FLT,
                output.handle, TYPE_RGBA_FLT, intent, cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOOPTIMIZE) : nullptr;
            if (!t) throw std::runtime_error("Cannot create the custom ICC output transform.");
            for (int y = 0; y < h; ++y) cmsDoTransform(t, rgba + qsizetype(y)*w*4, rgba + qsizetype(y)*w*4, w);
            cmsDeleteTransform(t);
        }
        return 0;
    } catch (const std::exception &e) {
        if (error && errorSize > 0) std::snprintf(error, size_t(errorSize), "%s", e.what());
        return -1;
    }
}
