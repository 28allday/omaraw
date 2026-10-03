#pragma once
#include <OpenColorIO/OpenColorIO.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "chain.h"

namespace oma::gpu {
struct ImageView {
    int width = 0, height = 0;
    const float *pixels = nullptr;
    size_t rowBytes = 0;
    bool valid() const;
};
// Straight float32 RGBA in the caller's declared linear working space.
// Colour transforms alone may change that declaration; alpha is coverage.
struct Image {
    int width = 0, height = 0;
    std::vector<float> rgba;
    Image() = default;
    Image(int w, int h);
    bool valid() const;
    ImageView view() const;
};
enum class Preference { Automatic, Cpu, Vulkan };
enum class Decision { Untried, Cpu, Vulkan };
struct Capabilities {
    bool vulkan = false;
    std::string device, reason;
    uint32_t vendorId = 0, deviceId = 0, apiVersion = 0;
    int maxTextureSize = 0;
};
struct Result {
    Image image;
    bool usedVulkan = false, cancelled = false;
    std::string fallbackReason;
    double milliseconds = 0;
    uint64_t uploadedBytes = 0, downloadedBytes = 0;
    unsigned imageAllocations = 0;
};
struct Benchmark {
    bool passed = false, preferVulkan = false;
    double cpuMs = 0, vulkanMs = 0, maxError = 0, meanError = 0;
    std::string reason;
};
// The consuming engine's completed CPU result and measured processing time.
// Borrowed only for the call; warmup retains its own bounded copies.
struct CpuReference { ImageView image; double milliseconds = 0; };
struct Options {
    // Exact runtime device name, for an explicit user selection or diagnostics.
    // Empty lets Vulkan choose a suitable device without vendor assumptions.
    std::string deviceName;
    uint64_t memoryBudgetBytes = 512ULL * 1024 * 1024;
    double maxError = 0.002, meanError = 0.0001;
};

// Immutable operation list after submission. Every factory installs both a
// CPU implementation and its GPU counterpart; raw shaders are not an API.
class Pipeline {
public:
    Pipeline();
    Pipeline &colour(OCIO_NAMESPACE::ConstProcessorRcPtr processor);
    Pipeline &matrix(const std::array<float, 16> &rowMajor);
    Pipeline &exposure(float stops);
    Pipeline &gaussianBlur(float sigma);
    // Five-pass anisotropic Texture preset in linear RGB. Amount is -100..100;
    // sourceScale is source pixels per output pixel (>=1). Preserves alpha.
    Pipeline &texture(float amount, float sourceScale = 1);
    Pipeline &over(std::shared_ptr<const Image> foreground, float opacity = 1);
    // The engine's colour chain (exposure to sigmoid) as one fused per-pixel
    // pass. A pipeline made only of chains is admitted once for every image
    // size, and processes images beyond the memory budget in bands.
    Pipeline &chain(std::vector<ChainStage> stages);
private:
    struct State;
    std::shared_ptr<State> state_;
    friend class Context;
};

// One context owns a Vulkan device, shader cache and reusable image pool.
// Calls are serialized. No manufacturer is used to select feature support.
// Automatic stays on CPU until this pipeline/size passes an end-to-end
// benchmark. A failed GPU job is replayed from the original input on CPU.
class Context {
public:
    explicit Context(Options = {});
    ~Context();
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    Capabilities capabilities();
    Result run(const Image &, const Pipeline &, Preference = Preference::Automatic,
               const std::atomic_bool *cancel = nullptr);
    Result run(ImageView, const Pipeline &, Preference = Preference::Automatic,
               const std::atomic_bool *cancel = nullptr);
    // Nonblocking, admitted Vulkan work only. An empty image means the caller
    // must run its own CPU implementation from the unchanged original input.
    Result tryRun(ImageView, const Pipeline &, const std::atomic_bool *cancel = nullptr);
    // As tryRun, writing straight into the caller's buffer (rowBytes apart)
    // instead of a returned image, so a large result is not copied twice.
    Result tryRunInto(ImageView, float *output, size_t rowBytes, const Pipeline &, const std::atomic_bool *cancel = nullptr);
    Benchmark benchmark(const Image &, const Pipeline &, int repeats = 3);
    Benchmark benchmark(ImageView, const Pipeline &, int repeats = 3);
    Benchmark benchmark(ImageView, const Pipeline &, CpuReference, int repeats = 3);
    // First use returns the CPU result immediately and benchmarks a retained
    // input on one background worker. Later calls use a validated winner.
    // The queue holds at most one image; a busy GPU never blocks a CPU job.
    Result runAdaptive(ImageView, const Pipeline &, const std::atomic_bool *cancel = nullptr);
    // Nonblocking admission query for applications retaining their own CPU
    // path. Untried permits warmup; Cpu includes a pending or busy benchmark.
    Decision decision(int width, int height, const Pipeline &) const;
    // Queue measurement without running or copying a CPU result for the caller.
    // Retains at most one pending input, capped at 64 MiB. Returns if queued.
    bool warmup(ImageView, const Pipeline &);
    bool warmup(ImageView, const Pipeline &, CpuReference);
    Benchmark lastBenchmark() const;
private:
    Benchmark benchmarkReference(ImageView, const Pipeline &, const CpuReference *, int);
    bool warmupReference(ImageView, const Pipeline &, const CpuReference *);
    struct State;
    struct Worker;
    std::unique_ptr<State> state_;
    std::unique_ptr<Worker> worker_;
};
}
