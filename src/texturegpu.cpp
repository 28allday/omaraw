#include "colourpipeline.h"
#include "texturebridge.h"
#include <oma/gpu/pipeline.h>
#include <QDebug>
#include <cstring>

namespace {
std::atomic_bool textureCancelled{false};
std::atomic<qulonglong> gpuCalls{0}, cpuCalls{0}, warmups{0};
}

int ColourPipeline::texture(const float *input, float *output, int w, int h,
                            float amount, float sourceScale, double cpuMs,
                            int (*cancelled)(void *), void *request) {
    if (!gpuEnabled() || qEnvironmentVariable("OMA_GPU") == "cpu") return 0;
    try {
        std::shared_ptr<oma::gpu::Context> gpu;
        {
            QMutexLocker lock(&m_mutex);
            if (!m_gpu) return 0; // resources have been released for shutdown
            if (!m_textureGpu) {
                oma::gpu::Options options;
                options.maxError = .0001; options.meanError = .00001;
                options.memoryBudgetBytes = 640ULL * 1024 * 1024;
                m_textureGpu = std::make_shared<oma::gpu::Context>(options);
            }
            gpu = m_textureGpu;
        }
        const oma::gpu::ImageView source{w, h, input, size_t(w) * 16};
        oma::gpu::Pipeline pipeline; pipeline.texture(amount, sourceScale);
        if (cpuMs >= 0) {
            if (cancelled && cancelled(request)) return 0;
            ++cpuCalls;
            if (gpu->warmup(source, pipeline, {{w, h, output, size_t(w) * 16}, cpuMs})) ++warmups;
            return 0;
        }
        // A cancellation racing this reset is either retained by the flag or
        // observed in the engine's generation predicate immediately afterwards.
        textureCancelled.store(false);
        if (cancelled && cancelled(request)) return 1;
        auto result = gpu->tryRun(source, pipeline, &textureCancelled);
        if (result.cancelled || (cancelled && cancelled(request))) return 1;
        if (!result.usedVulkan) return 0;
        std::memcpy(output, result.image.rgba.data(), result.image.rgba.size() * sizeof(float));
        ++gpuCalls;
        return 1;
    } catch (const std::exception &e) {
        qWarning() << "Texture GPU fallback:" << e.what();
        return 0;
    }
}

QVariantMap ColourPipeline::textureBenchmark() const {
    std::shared_ptr<oma::gpu::Context> gpu;
    { QMutexLocker lock(&m_mutex); gpu = m_textureGpu; }
    const auto result = gpu ? gpu->lastBenchmark() : oma::gpu::Benchmark{};
    return {{"passed", result.passed}, {"preferVulkan", result.preferVulkan},
            {"cpuMs", result.cpuMs}, {"vulkanMs", result.vulkanMs},
            {"maxError", result.maxError}, {"meanError", result.meanError},
            {"reason", QString::fromStdString(result.reason)},
            {"gpuCalls", gpuCalls.load()}, {"cpuCalls", cpuCalls.load()}, {"warmups", warmups.load()}};
}

extern "C" int oma_texture_stage(const float *input, float *output, int w, int h,
                                 float amount, float sourceScale, double cpuMs,
                                 int (*cancelled)(void *), void *request) {
    return ColourPipeline::instance().texture(input, output, w, h, amount, sourceScale, cpuMs, cancelled, request);
}
extern "C" void oma_texture_cancel(void) { textureCancelled.store(true); }
