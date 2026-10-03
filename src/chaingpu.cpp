// The application side of the engine's colour chain: the run from exposure
// to sigmoid as one fused Vulkan pass over the whole image, in bands, written
// straight into the engine's output buffer. Admission follows the Texture
// stage: the first run is native and its result qualifies the GPU path on a
// background worker (error against the native pixels, and time); later runs
// use the GPU once it has won. Everything else stays native.
#include "chainbridge.h"
#include "colourpipeline.h"
#include <oma/gpu/pipeline.h>
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace {
std::atomic<unsigned long long> attempts{0}, offers{0}, served{0};
std::atomic_bool chainCancelled{false};
std::atomic<unsigned long long> primaryAttempts{0}, primaryOffers{0}, primaryServed{0};

std::vector<oma::gpu::ChainStage> convert(const oma_chain_stage_t *stages, int count) {
    std::vector<oma::gpu::ChainStage> out;
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        const oma_chain_stage_t &s = stages[i];
        oma::gpu::ChainStage c;
        c.kind = s.kind; c.black = s.black; c.scale = s.scale;
        if (s.kind == OMA_CHAIN_PRIMARY_GRADE) {
            std::copy(s.illuminant, s.illuminant + 3, c.lift.begin());
            std::copy(s.saturation, s.saturation + 3, c.gamma.begin());
            std::copy(s.lightness, s.lightness + 3, c.gain.begin());
            std::copy(s.grey, s.grey + 3, c.offset.begin());
            c.master = {s.illuminant[3], s.saturation[3], s.lightness[3], s.grey[3]};
            std::copy(s.mix, s.mix + 3, c.luma.begin());
            c.lumMix = s.mix[3];
        }
        std::copy(s.coeffs, s.coeffs + 4, c.coeffs.begin());
        c.blueMapping = s.blue_mapping != 0; c.clipping = s.clipping != 0;
        std::copy(s.matrix, s.matrix + 12, c.matrix.begin()); std::copy(s.lmatrix, s.lmatrix + 12, c.lmatrix.begin());
        c.adaptation = s.adaptation; c.version = s.version; c.clip = s.clip != 0; c.applyGrey = s.apply_grey != 0;
        std::copy(s.rgb_to_xyz, s.rgb_to_xyz + 12, c.rgbToXyz.begin()); std::copy(s.xyz_to_rgb, s.xyz_to_rgb + 12, c.xyzToRgb.begin()); std::copy(s.mix, s.mix + 12, c.mix.begin());
        std::copy(s.illuminant, s.illuminant + 4, c.illuminant.begin()); std::copy(s.saturation, s.saturation + 4, c.saturation.begin());
        std::copy(s.lightness, s.lightness + 4, c.lightness.begin()); std::copy(s.grey, s.grey + 4, c.grey.begin());
        c.p = s.p; c.gamut = s.gamut;
        c.method = s.method; c.whiteTarget = s.white_target; c.blackTarget = s.black_target; c.paperExposure = s.paper_exposure;
        c.filmFog = s.film_fog; c.filmPower = s.film_power; c.paperPower = s.paper_power; c.huePreservation = s.hue_preservation;
        std::copy(s.pipe_to_base, s.pipe_to_base + 12, c.pipeToBase.begin()); std::copy(s.base_to_rendering, s.base_to_rendering + 12, c.baseToRendering.begin());
        std::copy(s.rendering_to_pipe, s.rendering_to_pipe + 12, c.renderingToPipe.begin());
        out.push_back(c);
    }
    return out;
}
}

int ColourPipeline::chain(const oma_chain_stage_t *stages, int count, const float *input, float *output,
                          int w, int h, double cpuMs, int (*cancelled)(void *), void *request) {
    if (count <= 0 || w <= 0 || h <= 0) return 0;
    // Protocol counters: the engine asked, or offered its native result.
    if (cpuMs >= 0) ++offers; else ++attempts;
    const bool primary = std::any_of(stages, stages + count, [](const auto &s) { return s.kind == OMA_CHAIN_PRIMARY_GRADE; });
    if (primary) { if (cpuMs >= 0) ++primaryOffers; else ++primaryAttempts; }
    if (!gpuEnabled() || qEnvironmentVariable("OMA_GPU") == "cpu") return 0;
    if (primary && cpuMs >= 0 && m_primaryInteractive.load()) return 0;
    // The primary wheels shown but at rest describe identity stages
    // (the kernels in gpu/src/chain.h test the same values). A chain of
    // nothing else is a copy, not a GPU round trip; there is nothing to
    // qualify either.
    const bool resting = std::all_of(stages, stages + count, [](const auto &s) {
        if (s.kind == OMA_CHAIN_PRIMARY_GRADE) {
            for (int c = 0; c < 3; ++c) if (s.illuminant[c] != 0.f || s.saturation[c] != 1.f || s.lightness[c] != 1.f || s.grey[c] != 0.f) return false;
            return true;
        }
        return false;
    });
    if (resting) {
        if (cpuMs >= 0) return 0;
        if (input != output) std::memcpy(output, input, size_t(w) * size_t(h) * 16);
        ++served; if (primary) ++primaryServed;
        return 1;
    }

    try {
        std::shared_ptr<oma::gpu::Context> gpu;
        {
            QMutexLocker lock(&m_mutex);
            if (!m_gpu) return 0; // resources have been released for shutdown
            // No second Vulkan instance where the first found no device.
            if (!m_gpu->capabilities().vulkan) return 0;
            if (!m_chainGpu) {
                oma::gpu::Options options;
                options.maxError = .002; options.meanError = .0001;
                options.memoryBudgetBytes = 768ULL * 1024 * 1024;
                m_chainGpu = std::make_shared<oma::gpu::Context>(options);
            }
            gpu = m_chainGpu;
        }
        oma::gpu::Pipeline pipeline; pipeline.chain(convert(stages, count));
        const size_t rowBytes = size_t(w) * 16;
        if (cpuMs >= 0) {
            if (cancelled && cancelled(request)) return 0;
            // The worker retains a bounded copy, so qualify on a slice of rows:
            // the pass is per-pixel and admitted once for every size, so the
            // slice stands for the whole image, and its time scales with rows.
            // Qualification counts input and reference, 32 bytes a pixel,
            // against a 64 MiB cap: a larger slice was silently refused, so
            // the chain never qualified above about 3 MP.
            const int rows = std::max(1, std::min(h, int((64ULL << 20) / (rowBytes * 2))));
            const oma::gpu::ImageView slice{w, rows, input, rowBytes};
            gpu->warmup(slice, pipeline, {{w, rows, output, rowBytes}, cpuMs * rows / h});
            return 0;
        }
        chainCancelled.store(false);
        if (cancelled && cancelled(request)) return 1;
        const auto decision = gpu->decision(w, h, pipeline);
        const auto result = gpu->tryRunInto({w, h, input, rowBytes}, output, rowBytes, pipeline, &chainCancelled);
        if (qEnvironmentVariable("OMARAW_CHAIN_DEBUG") == "1")
            qInfo() << "Colour chain attempt" << w << "x" << h << "decision" << int(decision) << "usedVulkan" << result.usedVulkan
                    << "cancelled" << result.cancelled << "ms" << result.milliseconds << "reason" << QString::fromStdString(result.fallbackReason);
        if (result.cancelled || (cancelled && cancelled(request))) return 1;
        if (!result.usedVulkan) {
            if (!result.fallbackReason.empty()) qWarning() << "Colour chain GPU fallback:" << QString::fromStdString(result.fallbackReason);
            return 0;
        }
        ++served;
        if (primary) ++primaryServed;
        return 1;
    } catch (const std::exception &e) {
        qWarning() << "Colour chain GPU fallback:" << e.what();
        return 0;
    }
}

void ColourPipeline::setPrimaryGradeInteractive(const QObject *owner, bool active) {
    if (!owner) return;
    QMutexLocker lock(&m_mutex);
    if (active) m_primaryEditors.insert(owner); else m_primaryEditors.remove(owner);
    m_primaryInteractive.store(!m_primaryEditors.isEmpty());
}

QVariantMap ColourPipeline::chainBenchmark() const {
    std::shared_ptr<oma::gpu::Context> gpu;
    { QMutexLocker lock(&m_mutex); gpu = m_chainGpu; }
    const auto result = gpu ? gpu->lastBenchmark() : oma::gpu::Benchmark{};
    return {{"passed", result.passed}, {"preferVulkan", result.preferVulkan},
            {"cpuMs", result.cpuMs}, {"vulkanMs", result.vulkanMs},
            {"maxError", result.maxError}, {"meanError", result.meanError},
            {"reason", QString::fromStdString(result.reason)},
            {"attempts", attempts.load()}, {"offers", offers.load()}, {"served", served.load()}, {"primaryAttempts", primaryAttempts.load()},
            {"primaryOffers", primaryOffers.load()}, {"primaryServed", primaryServed.load()}};
}

extern "C" int oma_chain_stage(const oma_chain_stage_t *stages, int count,
                               const float *input, float *output, int width, int height,
                               double cpu_ms, int (*cancelled)(void *), void *request) {
    return ColourPipeline::instance().chain(stages, count, input, output, width, height, cpu_ms, cancelled, request);
}

extern "C" void oma_chain_stats(unsigned long long *a, unsigned long long *o, unsigned long long *s) {
    if (a) *a = attempts.load();
    if (o) *o = offers.load();
    if (s) *s = served.load();
}
