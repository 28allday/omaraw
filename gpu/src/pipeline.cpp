#include <oma/gpu/pipeline.h>
#include "texture.h"
#include "chain.h"
#if OMA_GPU_WITH_VULKAN
#include <libplacebo/vulkan.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/custom.h>
#endif
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <optional>
#include <set>
#include <deque>
#include <thread>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace OCIO = OCIO_NAMESPACE;
namespace oma::gpu {
namespace {
// Resource failures must retire the device and its pools. Refusing a job for
// its dimensions or unsupported shader features is a separate, harmless case.
struct ResourceFailure : std::runtime_error { using std::runtime_error::runtime_error; };
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
bool cancelled(const std::atomic_bool *flag) { return flag && flag->load(); }
std::string number(float value) { std::ostringstream out; out.imbue(std::locale::classic()); out << std::scientific << std::setprecision(9) << value; return out.str(); }
struct Operation {
    enum Kind { Colour, Matrix, Blur, Over, Texture, Chain } kind;
    OCIO::ConstProcessorRcPtr colour;
    std::array<float, 16> matrix{};
    float sigma = 0, opacity = 1, textureAmount = 0, textureScale = 1;
    std::shared_ptr<const Image> foreground;
    std::vector<ChainStage> chain;
};
bool allChain(const std::vector<Operation> &ops) {
    if (ops.empty()) return false;
    for (const auto &op : ops) if (op.kind != Operation::Chain) return false;
    return true;
}
// Every operation reads only its own pixel (no blur, composite or texture),
// so a slice of rows qualifies it for any size and its admission need not
// depend on the image's size.
bool perPixel(const std::vector<Operation> &ops) {
    if (ops.empty()) return false;
    for (const auto &op : ops)
        if (op.kind != Operation::Chain && op.kind != Operation::Colour && op.kind != Operation::Matrix) return false;
    return true;
}
std::vector<float> weights(float sigma) {
    const int radius = std::max(1, int(std::ceil(3 * sigma)));
    std::vector<float> result(size_t(radius) * 2 + 1);
    double sum = 0;
    for (int x = -radius; x <= radius; ++x) { result[x + radius] = std::exp(-0.5 * x * x / (sigma * sigma)); sum += result[x + radius]; }
    for (float &v : result) v /= sum;
    return result;
}
void cpuBlur(Image &image, float sigma, const std::atomic_bool *cancel) {
    const auto kernel = weights(sigma); const int radius = int(kernel.size()) / 2;
    Image temporary(image.width, image.height);
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < image.height; ++y) {
            if (cancelled(cancel)) return;
            for (int x = 0; x < image.width; ++x) {
                float sum[4] = {};
                for (int k = -radius; k <= radius; ++k) {
                    const int sx = pass ? x : std::clamp(x + k, 0, image.width - 1);
                    const int sy = pass ? std::clamp(y + k, 0, image.height - 1) : y;
                    const float *p = &image.rgba[(size_t(sy) * image.width + sx) * 4];
                    for (int c = 0; c < 4; ++c) sum[c] += p[c] * (c < 3 ? p[3] : 1) * kernel[k + radius];
                }
                float *out = &temporary.rgba[(size_t(y) * image.width + x) * 4];
                for (int c = 0; c < 3; ++c) out[c] = sum[3] > 1e-12f ? sum[c] / sum[3] : 0;
                out[3] = sum[3];
            }
        }
        image.rgba.swap(temporary.rgba);
    }
}
void cpu(Image &image, const std::vector<Operation> &ops, const std::atomic_bool *cancel) {
    for (const auto &op : ops) {
        if (cancelled(cancel)) return;
        if (op.kind == Operation::Colour) {
            OCIO::PackedImageDesc desc(image.rgba.data(), image.width, image.height, 3, OCIO::BIT_DEPTH_F32, sizeof(float), 4 * sizeof(float), size_t(image.width) * 16);
            op.colour->getDefaultCPUProcessor()->apply(desc);
        } else if (op.kind == Operation::Blur) cpuBlur(image, op.sigma, cancel);
        else if (op.kind == Operation::Texture) detail::cpuTexture(image, op.textureAmount, op.textureScale, cancel);
        else if (op.kind == Operation::Chain) detail::cpuChain(image, op.chain, cancel);
        else for (size_t i = 0; i < image.rgba.size(); i += 4) {
            if ((i & 16383) == 0 && cancelled(cancel)) return;
            float *p = image.rgba.data() + i;
            if (op.kind == Operation::Matrix) {
                const float r = p[0], g = p[1], b = p[2];
                for (int c = 0; c < 3; ++c) p[c] = op.matrix[c*4] * r + op.matrix[c*4+1] * g + op.matrix[c*4+2] * b + op.matrix[c*4+3];
            } else {
                const float *f = op.foreground->rgba.data() + i;
                const float a = std::clamp(f[3] * op.opacity, 0.f, 1.f), back = p[3] * (1 - a), result = a + back;
                for (int c = 0; c < 3; ++c) p[c] = result > 1e-12f ? (f[c] * a + p[c] * back) / result : 0;
                p[3] = result;
            }
        }
    }
}
}
Image::Image(int w, int h) : width(w), height(h) {
    if (w <= 0 || h <= 0 || uint64_t(w) * h > (1ULL << 28)) throw std::invalid_argument("Invalid image dimensions");
    rgba.resize(size_t(w) * h * 4);
}
bool ImageView::valid() const {
    return width > 0 && height > 0 && uint64_t(width) * height <= (1ULL << 28)
        && pixels && rowBytes >= size_t(width) * 16 && rowBytes % sizeof(float) == 0
        && rowBytes <= SIZE_MAX / size_t(height);
}
bool Image::valid() const { return view().valid() && rgba.size() == size_t(width) * height * 4; }
ImageView Image::view() const { return {width, height, rgba.data(), size_t(std::max(0, width)) * 16}; }
struct Pipeline::State { std::vector<Operation> operations; };
Pipeline::Pipeline() : state_(std::make_shared<State>()) {}
Pipeline &Pipeline::colour(OCIO::ConstProcessorRcPtr processor) {
    if (!processor) throw std::invalid_argument("Missing colour processor");
    if (!state_.unique()) state_ = std::make_shared<State>(*state_);
    Operation op{}; op.kind = Operation::Colour; op.colour = std::move(processor); state_->operations.push_back(std::move(op)); return *this;
}
Pipeline &Pipeline::matrix(const std::array<float, 16> &matrix) {
    if (!std::all_of(matrix.begin(), matrix.end(), [](float x) { return std::isfinite(x); })) throw std::invalid_argument("Invalid matrix");
    if (matrix[12] != 0 || matrix[13] != 0 || matrix[14] != 0 || matrix[15] != 1) throw std::invalid_argument("Matrix must preserve coverage alpha");
    if (!state_.unique()) state_ = std::make_shared<State>(*state_);
    Operation op{}; op.kind = Operation::Matrix; op.matrix = matrix; state_->operations.push_back(std::move(op)); return *this;
}
Pipeline &Pipeline::exposure(float stops) {
    if (!std::isfinite(stops) || std::abs(stops) > 24) throw std::invalid_argument("Invalid exposure");
    const float gain = std::exp2(stops); return matrix({gain,0,0,0, 0,gain,0,0, 0,0,gain,0, 0,0,0,1});
}
Pipeline &Pipeline::gaussianBlur(float sigma) {
    if (!std::isfinite(sigma) || sigma < 0 || sigma > 32) throw std::invalid_argument("Blur sigma must be between 0 and 32");
    if (sigma < 0.01f) return *this;
    if (!state_.unique()) state_ = std::make_shared<State>(*state_);
    Operation op{}; op.kind = Operation::Blur; op.sigma = sigma; state_->operations.push_back(std::move(op)); return *this;
}
Pipeline &Pipeline::texture(float amount, float sourceScale) {
    if (!std::isfinite(amount) || amount < -100 || amount > 100 || !std::isfinite(sourceScale) || sourceScale < 1 || sourceScale > 65536)
        throw std::invalid_argument("Invalid Texture parameters");
    if (!state_.unique()) state_ = std::make_shared<State>(*state_);
    Operation op{}; op.kind = Operation::Texture; op.textureAmount = amount; op.textureScale = sourceScale;
    state_->operations.push_back(std::move(op)); return *this;
}
Pipeline &Pipeline::chain(std::vector<ChainStage> stages) {
    if (stages.empty()) throw std::invalid_argument("Empty colour chain");
    for (const auto &stage : stages) {
        if (stage.kind < ChainStage::Exposure || stage.kind > ChainStage::PrimaryGrade) throw std::invalid_argument("Unknown colour chain stage");
        if (stage.kind == ChainStage::PrimaryGrade) for (int c=0;c<3;++c) {
            if (!std::isfinite(stage.lift[c]) || !std::isfinite(stage.gamma[c]) || stage.gamma[c] <= 0
                || !std::isfinite(stage.gain[c]) || stage.gain[c] < 0 || !std::isfinite(stage.offset[c])
                || !std::isfinite(stage.luma[c]))
                throw std::invalid_argument("Invalid primary grade coefficients");
        }
        if (stage.kind == ChainStage::PrimaryGrade
            && (!std::isfinite(stage.lumMix) || stage.lumMix < 0 || stage.lumMix > 1
                || !std::isfinite(stage.master[0]) || !std::isfinite(stage.master[1]) || stage.master[1] <= 0
                || !std::isfinite(stage.master[2]) || stage.master[2] < 0 || !std::isfinite(stage.master[3])))
            throw std::invalid_argument("Invalid primary grade coefficients");
        // Only the matrices a stage uses must be finite: the engine marks an
        // unused one with NaN.
        std::vector<const float *> blocks;
        if (stage.kind == ChainStage::ColorIn || stage.kind == ChainStage::LabToRgb) blocks.push_back(stage.matrix.data());
        if (stage.kind == ChainStage::ColorIn && stage.clipping) blocks.push_back(stage.lmatrix.data());
        if (stage.kind == ChainStage::ChannelMixer) { blocks.push_back(stage.rgbToXyz.data()); blocks.push_back(stage.xyzToRgb.data()); blocks.push_back(stage.mix.data()); }
        if (stage.kind == ChainStage::Sigmoid && stage.method == 0) { blocks.push_back(stage.pipeToBase.data()); blocks.push_back(stage.baseToRendering.data()); blocks.push_back(stage.renderingToPipe.data()); }
        for (const float *m : blocks) for (int i = 0; i < 12; ++i) if (!std::isfinite(m[i])) throw std::invalid_argument("Colour chain matrix is not finite");
    }
    if (!state_.unique()) state_ = std::make_shared<State>(*state_);
    Operation op{}; op.kind = Operation::Chain; op.chain = std::move(stages); state_->operations.push_back(std::move(op)); return *this;
}
Pipeline &Pipeline::over(std::shared_ptr<const Image> foreground, float opacity) {
    if (!foreground || !foreground->valid() || !std::isfinite(opacity) || opacity < 0 || opacity > 1) throw std::invalid_argument("Invalid composite");
    if (!state_.unique()) state_ = std::make_shared<State>(*state_);
    Operation op{}; op.kind = Operation::Over; op.foreground = std::move(foreground); op.opacity = opacity; state_->operations.push_back(std::move(op)); return *this;
}

struct Context::State {
    std::mutex mutex;
    Capabilities caps;
    Options options;
    bool attempted = false, failed = false;
    std::map<std::string, bool> admitted;
    explicit State(Options o) : options(std::move(o)) {}
#if OMA_GPU_WITH_VULKAN
    pl_log log = nullptr;
    pl_vulkan vk = nullptr;
    pl_dispatch dispatch = nullptr;
    pl_tex pool[2] = {};
    std::vector<pl_tex> texturePool;
    pl_tex foregroundPool = nullptr;
    std::shared_ptr<const Image> foregroundSource;
    std::mutex logMutex;
    std::string logError;
    std::string lastError() { std::lock_guard lock(logMutex); return logError; }
    struct Lut {
        pl_gpu gpu; pl_tex texture; uint64_t bytes; std::string name; pl_tex_sample_mode sampling;
        ~Lut() { pl_tex_destroy(gpu, &texture); }
    };
    struct Shader { std::string text, function; std::vector<std::shared_ptr<Lut>> luts; };
    // Dynamic wheel values keep the shader reusable; qualification still
    // includes every coefficient and compares with the native pixels.
    struct ChainUniforms {
        std::vector<std::string> names;
        std::vector<pl_shader_var> variables;
        std::vector<std::array<float,4>> mixes;   // Y row and Lum Mix, packed
        explicit ChainUniforms(const std::vector<ChainStage> &stages) {
            names.reserve(stages.size()*6);
            variables.reserve(stages.size()*6);
            mixes.reserve(stages.size());
            for (size_t i=0;i<stages.size();++i) if (stages[i].kind == ChainStage::PrimaryGrade) {
                const auto &s = stages[i];
                const char *labels[] = {"lift", "gamma", "gain", "offset"};
                const float *values[] = {s.lift.data(),s.gamma.data(),s.gain.data(),s.offset.data()};
                for (int c=0;c<4;++c) {
                    names.push_back("oma_grade_" + std::string(labels[c]) + "_" + std::to_string(i));
                    pl_shader_var v{}; v.var = pl_var_vec3(names.back().c_str()); v.data = values[c]; v.dynamic = true;
                    variables.push_back(v);
                }
                mixes.push_back({s.luma[0],s.luma[1],s.luma[2],s.lumMix});
                const char *packed[] = {"master", "mix"};
                const float *packedValues[] = {s.master.data(), mixes.back().data()};
                for (int c=0;c<2;++c) {
                    names.push_back("oma_grade_" + std::string(packed[c]) + "_" + std::to_string(i));
                    pl_shader_var v{}; v.var = pl_var_vec4(names.back().c_str()); v.data = packedValues[c]; v.dynamic = true;
                    variables.push_back(v);
                }
            }
        }
    };
    std::map<std::string, Shader> colours;
    void releaseDevice() {
        colours.clear();
        foregroundSource.reset();
        if (vk) { for (auto &tex : texturePool) pl_tex_destroy(vk->gpu, &tex); pl_tex_destroy(vk->gpu, &pool[0]); pl_tex_destroy(vk->gpu, &pool[1]); pl_tex_destroy(vk->gpu, &foregroundPool); }
        texturePool.clear();
        pl_dispatch_destroy(&dispatch); pl_vulkan_destroy(&vk); pl_log_destroy(&log);
    }
    ~State() { releaseDevice(); }
    // One process, one verdict: a Vulkan loader that has no usable device
    // is not entered again by a later context (some loaders fault when
    // enumerating without a driver), and instance creation is serialised.
    static std::mutex &initialiseMutex() { static std::mutex m; return m; }
    static std::string &initialiseFailure() { static std::string reason; return reason; }
    void initialise() {
        if (attempted) return;
        attempted = true;
        std::lock_guard guard(initialiseMutex());
        if (!initialiseFailure().empty()) { caps.reason = initialiseFailure(); return; }
        pl_log_params lp{};
        lp.log_level = PL_LOG_ERR; lp.log_priv = this;
        lp.log_cb = [](void *p, pl_log_level, const char *message) { auto &s = *static_cast<State *>(p); std::lock_guard lock(s.logMutex); s.logError = message; };
        log = pl_log_create(PL_API_VER, &lp);
        auto params = pl_vulkan_default_params;
        params.allow_software = false;
        if (!options.deviceName.empty()) params.device_name = options.deviceName.c_str();
        vk = pl_vulkan_create(log, &params);
        if (!vk) { caps.reason = lastError().empty() ? "No suitable Vulkan device" : lastError(); initialiseFailure() = caps.reason; return; }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(vk->phys_device, &properties);
        caps.device = properties.deviceName; caps.vendorId = properties.vendorID; caps.deviceId = properties.deviceID; caps.apiVersion = properties.apiVersion;
        caps.maxTextureSize = vk->gpu->limits.max_tex_2d_dim;
        const auto fmt = pl_find_fmt(vk->gpu, PL_FMT_FLOAT, 4, 32, 32, static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_STORABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE));
        if (!fmt || !vk->gpu->glsl.compute) { caps.reason = "Vulkan device lacks float32 compute images"; initialiseFailure() = caps.reason; return; }
        dispatch = pl_dispatch_create(log, vk->gpu);
        caps.vulkan = dispatch != nullptr;
        if (!caps.vulkan) caps.reason = "Cannot create Vulkan shader dispatcher";
    }
    std::shared_ptr<Lut> texture(const char *name, const float *data, unsigned w, unsigned h, unsigned d, int channels, OCIO::Interpolation interpolation) {
        const auto fmt = pl_find_fmt(vk->gpu, PL_FMT_FLOAT, channels == 1 ? 1 : 4, 32, 32, static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR));
        if (!fmt || !data || uint64_t(w) * std::max(1u,h) * std::max(1u,d) * (channels == 1 ? 4 : 16) > options.memoryBudgetBytes / 4 || w > 65536 || h > 65536 || d > 256 || uint64_t(w) * std::max(1u,h) * std::max(1u,d) > (1ULL << 24)) throw std::runtime_error("Unsupported OCIO LUT resource");
        std::vector<float> rgba;
        if (channels == 3) {
            const size_t count = size_t(w) * std::max(1u,h) * std::max(1u,d);
            rgba.resize(count * 4);
            for (size_t i = 0; i < count; ++i) { std::memcpy(rgba.data() + 4*i, data + 3*i, 12); rgba[4*i+3] = 1; }
            data = rgba.data();
        }
        pl_tex_params tp{}; tp.w = w; tp.h = h; tp.d = d; tp.format = fmt; tp.sampleable = true; tp.initial_data = data;
        pl_tex tex = pl_tex_create(vk->gpu, &tp);
        if (!tex) throw ResourceFailure("Cannot upload OCIO LUT");
        return std::shared_ptr<Lut>(new Lut{vk->gpu, tex, uint64_t(w) * std::max(1u,h) * std::max(1u,d) * (channels == 1 ? 4 : 16), name, interpolation == OCIO::INTERP_NEAREST ? PL_TEX_SAMPLE_NEAREST : PL_TEX_SAMPLE_LINEAR});
    }
    const Shader &colourShader(const OCIO::ConstProcessorRcPtr &processor) {
        const std::string key = processor->getCacheID();
        if (const auto found = colours.find(key); found != colours.end()) return found->second;
        auto desc = OCIO::GpuShaderDesc::CreateShaderDesc();
        desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_4_0);
        desc->setAllowTexture1D(false); desc->setFunctionName("oma_colour"); desc->setResourcePrefix("oma_ocio_");
        processor->getDefaultGPUProcessor()->extractGpuShaderInfo(desc);
        if (desc->getNumUniforms()) throw std::runtime_error("Dynamic OCIO uniforms require the CPU path");
        Shader shader; shader.function = "oma_colour";
        // libplacebo assigns Vulkan descriptors and generates SPIR-V. OCIO
        // supplies the colour mathematics; declarations are supplied below.
        shader.text = std::regex_replace(std::string(desc->getShaderText()), std::regex("uniform\\s+sampler[123]D\\s+[A-Za-z0-9_]+\\s*;"), "");
        for (unsigned i = 0; i < desc->getNumTextures(); ++i) {
            const char *tex = nullptr, *sampler = nullptr; unsigned w = 0, h = 0;
            OCIO::GpuShaderCreator::TextureType channels; OCIO::GpuShaderCreator::TextureDimensions dimensions; OCIO::Interpolation interpolation;
            desc->getTexture(i, tex, sampler, w, h, channels, dimensions, interpolation);
            const float *values = nullptr; desc->getTextureValues(i, values);
            shader.luts.push_back(texture(sampler, values, w, h, 0, channels == OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL ? 1 : 3, interpolation));
        }
        for (unsigned i = 0; i < desc->getNum3DTextures(); ++i) {
            const char *tex = nullptr, *sampler = nullptr; unsigned edge = 0; OCIO::Interpolation interpolation;
            desc->get3DTexture(i, tex, sampler, edge, interpolation);
            const float *values = nullptr; desc->get3DTextureValues(i, values);
            shader.luts.push_back(texture(sampler, values, edge, edge, edge, 3, interpolation));
        }
        uint64_t bytes = 0;
        for (const auto &lut : shader.luts) bytes += lut->bytes;
        if (bytes > options.memoryBudgetBytes / 4) throw std::runtime_error("OCIO LUT exceeds GPU memory budget");
        for (const auto &entry : colours) for (const auto &lut : entry.second.luts) bytes += lut->bytes;
        if (colours.size() >= 16 || bytes > options.memoryBudgetBytes / 4) colours.clear();
        return colours.emplace(key, std::move(shader)).first->second;
    }
    static pl_shader_desc sampled(const char *name, pl_tex tex, pl_tex_sample_mode sampling = PL_TEX_SAMPLE_NEAREST) {
        pl_shader_desc desc{}; desc.desc.name = name; desc.desc.type = PL_DESC_SAMPLED_TEX;
        desc.binding.object = tex; desc.binding.sample_mode = sampling; desc.binding.address_mode = PL_TEX_ADDRESS_CLAMP;
        return desc;
    }
    void pass(pl_tex from, pl_tex to, const std::string &body, const Shader *shader = nullptr, pl_tex foreground = nullptr, const std::string *header = nullptr, const std::vector<pl_shader_var> *variables = nullptr) {
        std::vector<pl_shader_desc> descriptors{sampled("oma_input", from)};
        if (shader) for (const auto &lut : shader->luts) descriptors.push_back(sampled(lut->name.c_str(), lut->texture, lut->sampling));
        if (foreground) descriptors.push_back(sampled("oma_foreground", foreground));
        pl_shader sh = pl_dispatch_begin(dispatch);
        pl_custom_shader params{};
        params.header = shader ? shader->text.c_str() : header ? header->c_str() : nullptr; params.body = body.c_str();
        params.input = PL_SHADER_SIG_NONE; params.output = PL_SHADER_SIG_COLOR; params.compute = true;
        params.compute_group_size[0] = 16; params.compute_group_size[1] = 16;
        params.descriptors = descriptors.data(); params.num_descriptors = descriptors.size();
        if (variables) { params.variables = variables->data(); params.num_variables = variables->size(); }
        if (!pl_shader_custom(sh, &params)) { pl_dispatch_abort(dispatch, &sh); throw std::runtime_error("Vulkan shader construction failed"); }
        pl_dispatch_params dp{}; dp.shader = &sh; dp.target = to;
        if (!pl_dispatch_finish(dispatch, &dp)) throw ResourceFailure(lastError().empty() ? "Vulkan shader dispatch failed" : lastError());
    }
    // A chain-only pipeline is per-pixel: it runs in horizontal bands sized
    // to the budget, so a whole sensor's worth of float RGBA never has to
    // fit the GPU at once, and it can write straight into the caller's rows.
    Image gpuChainBands(ImageView input, const std::vector<Operation> &ops, Result &stats, const std::atomic_bool *cancel, float *into, size_t intoRowBytes) {
        if (input.width > caps.maxTextureSize) throw std::runtime_error("Image wider than the GPU texture limit");
        std::vector<ChainStage> stages;
        for (const auto &op : ops) stages.insert(stages.end(), op.chain.begin(), op.chain.end());
        const uint64_t rowBytes = uint64_t(input.width) * 16;
        const uint64_t bandBudget = options.memoryBudgetBytes / 4; // two pool textures and staging
        int bandRows = int(std::max<uint64_t>(1, std::min<uint64_t>(uint64_t(input.height), bandBudget / rowBytes)));
        bandRows = std::min(bandRows, caps.maxTextureSize);
        const auto fmt = pl_find_fmt(vk->gpu, PL_FMT_FLOAT, 4, 32, 32, static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_STORABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE));
        const std::string header = detail::chainHeader(), body = detail::chainBody(stages);
        const ChainUniforms uniforms(stages);
        Image output;
        if (!into) output = Image(input.width, input.height);
        for (int y0 = 0; y0 < input.height; y0 += bandRows) {
            if (cancelled(cancel)) return {};
            const int rows = std::min(bandRows, input.height - y0);
            pl_tex_params tp{}; tp.w = input.width; tp.h = rows; tp.format = fmt;
            tp.sampleable = tp.storable = tp.renderable = tp.host_readable = tp.host_writable = true;
            for (int i = 0; i < 2; ++i) {
                if (!pool[i] || pool[i]->params.w != tp.w || pool[i]->params.h != tp.h) ++stats.imageAllocations;
                if (!pl_tex_recreate(vk->gpu, &pool[i], &tp)) throw ResourceFailure("Vulkan image allocation failed");
            }
            pl_dispatch_reset_frame(dispatch);
            pl_tex_transfer_params upload{}; upload.tex = pool[0];
            upload.ptr = const_cast<char *>(reinterpret_cast<const char *>(input.pixels) + size_t(y0) * input.rowBytes);
            upload.row_pitch = input.rowBytes;
            if (!pl_tex_upload(vk->gpu, &upload)) throw ResourceFailure("Vulkan image upload failed");
            stats.uploadedBytes += rowBytes * rows;
            pass(pool[0], pool[1], body, nullptr, nullptr, &header, &uniforms.variables);
            pl_tex_transfer_params download{}; download.tex = pool[1];
            download.ptr = into ? reinterpret_cast<char *>(into) + size_t(y0) * intoRowBytes
                                : reinterpret_cast<char *>(output.rgba.data()) + size_t(y0) * rowBytes;
            download.row_pitch = into ? intoRowBytes : rowBytes;
            if (!pl_tex_download(vk->gpu, &download)) throw ResourceFailure("Vulkan image download failed");
            stats.downloadedBytes += rowBytes * rows;
        }
        stats.usedVulkan = true;
        return output;
    }
    Image gpu(ImageView input, const std::vector<Operation> &ops, Result &stats, const std::atomic_bool *cancel, float *into = nullptr, size_t intoRowBytes = 0) {
        if (allChain(ops)) return gpuChainBands(input, ops, stats, cancel, into, intoRowBytes);
        if (into) throw std::runtime_error("Direct output needs a per-pixel pipeline");
        const uint64_t imageBytes = uint64_t(input.width) * input.height * 16;
        size_t textureSlots = 0;
        for (const auto &op : ops) if (op.kind == Operation::Texture)
            textureSlots = std::max(textureSlots, detail::textureRadii(op.textureScale).size() + 4);
        // Resident intermediates, staging and composite; leave a quarter for LUTs.
        const uint64_t slots = textureSlots ? textureSlots + 5 : 8;
        const uint64_t available = textureSlots ? options.memoryBudgetBytes - options.memoryBudgetBytes / 4 : options.memoryBudgetBytes;
        if (imageBytes > available / slots
            || input.width > caps.maxTextureSize || input.height > caps.maxTextureSize)
            throw std::runtime_error("Image exceeds GPU memory or texture budget");
        const auto fmt = pl_find_fmt(vk->gpu, PL_FMT_FLOAT, 4, 32, 32, static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_STORABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE));
        pl_tex_params tp{}; tp.w = input.width; tp.h = input.height; tp.format = fmt;
        tp.sampleable = tp.storable = tp.renderable = tp.host_readable = tp.host_writable = true;
        for (auto &tex : pool) {
            if (!tex || tex->params.w != tp.w || tex->params.h != tp.h) ++stats.imageAllocations;
            if (!pl_tex_recreate(vk->gpu, &tex, &tp)) throw ResourceFailure("Vulkan image allocation failed");
        }
        while (texturePool.size() > textureSlots) { pl_tex_destroy(vk->gpu, &texturePool.back()); texturePool.pop_back(); }
        texturePool.resize(textureSlots, nullptr);
        for (auto &tex : texturePool) {
            if (!tex || tex->params.w != tp.w || tex->params.h != tp.h) ++stats.imageAllocations;
            if (!pl_tex_recreate(vk->gpu, &tex, &tp)) throw ResourceFailure("Vulkan Texture allocation failed");
        }
        pl_dispatch_reset_frame(dispatch);
        pl_tex_transfer_params upload{}; upload.tex = pool[0]; upload.ptr = const_cast<float *>(input.pixels); upload.row_pitch = input.rowBytes;
        if (!pl_tex_upload(vk->gpu, &upload)) throw ResourceFailure("Vulkan image upload failed");
        stats.uploadedBytes += imageBytes;
        int current = 0;
        const std::string sample = "ivec2 pos=ivec2(gl_GlobalInvocationID.xy); vec4 source=texelFetch(oma_input,pos,0);\n";
        for (const auto &op : ops) {
            if (cancelled(cancel)) return {};
            if (op.kind == Operation::Blur) {
                const auto kernel = weights(op.sigma); const int radius = int(kernel.size()) / 2;
                for (int axis = 0; axis < 2; ++axis) {
                    std::string body = "ivec2 pos=ivec2(gl_GlobalInvocationID.xy); ivec2 size=textureSize(oma_input,0); vec4 sum=vec4(0.0); vec4 p;\n";
                    for (int k = -radius; k <= radius; ++k) body += "p=texelFetch(oma_input,clamp(pos+ivec2(" + std::to_string(axis ? 0 : k) + "," + std::to_string(axis ? k : 0) + "),ivec2(0),size-1),0); sum+=vec4(p.rgb*p.a,p.a)*" + number(kernel[k+radius]) + ";\n";
                    body += "color=vec4(sum.a>1e-12 ? sum.rgb/sum.a : vec3(0.0),sum.a);";
                    pass(pool[current], pool[1-current], body); current = 1-current;
                    if (cancelled(cancel)) return {};
                }
            } else if (op.kind == Operation::Texture) {
                const auto radii = detail::textureRadii(op.textureScale);
                const size_t n = radii.size();
                pl_tex working = pool[current];
                const pl_tex vertical = texturePool[n], a = texturePool[n+1], b = texturePool[n+2], result = texturePool[n+3];
                for (int iteration = 0; iteration < 5; ++iteration) {
                    pl_tex low = working;
                    for (size_t s = 0; s < n; ++s) {
                        if (cancelled(cancel)) return {};
                        const pl_tex next = s % 2 ? b : a;
                        pass(low, vertical, detail::textureBlur(1 << s, false));
                        if (cancelled(cancel)) return {};
                        pass(vertical, next, detail::textureBlur(1 << s, true));
                        if (cancelled(cancel)) return {};
                        pass(low, texturePool[s], sample + "color=vec4(source.rgb-texelFetch(oma_foreground,pos,0).rgb,1);", nullptr, next);
                        low = next;
                    }
                    for (int s = int(n)-1; s >= 0; --s) {
                        if (cancelled(cancel)) return {};
                        const pl_tex next = s == 0 ? result : low == a ? b : a;
                        // Slider values are data, not shader source. Baking
                        // them in compiled another diffusion kernel at every
                        // scale for each amount and preview resolution.
                        const auto speeds = detail::textureSpeeds(op.textureAmount, op.textureScale, radii[s]);
                        const float regularization = 99.f * (radii[s]*radii[s]) / 9.f;
                        std::vector<pl_shader_var> variables(2);
                        variables[0].var = pl_var_vec4("oma_texture_speeds");
                        variables[0].data = speeds.data(); variables[0].dynamic = true;
                        variables[1].var = pl_var_float("oma_texture_regularization");
                        variables[1].data = &regularization; variables[1].dynamic = true;
                        pass(low, next, detail::textureSolve(1 << s), nullptr, texturePool[s], nullptr, &variables);
                        low = next;
                    }
                    working = result;
                }
                if (cancelled(cancel)) return {};
                pass(working, pool[1-current], sample + "color=vec4(source.rgb,texelFetch(oma_foreground,pos,0).a);", nullptr, pool[current]);
                current = 1-current;
            } else if (op.kind == Operation::Chain) {
                const std::string header = detail::chainHeader(), body = detail::chainBody(op.chain);
                const ChainUniforms uniforms(op.chain);
                pass(pool[current], pool[1-current], body, nullptr, nullptr, &header, &uniforms.variables);
                current = 1-current;
            } else if (op.kind == Operation::Colour) {
                const auto &shader = colourShader(op.colour);
                // Packed RGB OCIO CPU transforms supply zero for the unused
                // alpha input, including matrices that mix alpha into RGB.
                pass(pool[current], pool[1-current], sample + "color=oma_colour(vec4(source.rgb,0.0)); color.a=source.a;", &shader); current = 1-current;
            } else if (op.kind == Operation::Matrix) {
                std::string body = sample;
                for (int c = 0; c < 3; ++c) body += "color[" + std::to_string(c) + "]=dot(vec4(source.rgb,1.0),vec4(" + number(op.matrix[c*4]) + "," + number(op.matrix[c*4+1]) + "," + number(op.matrix[c*4+2]) + "," + number(op.matrix[c*4+3]) + "));\n";
                pass(pool[current], pool[1-current], body + "color.a=source.a;"); current = 1-current;
            } else {
                if (foregroundSource != op.foreground) {
                    auto fp = tp; fp.storable = fp.renderable = fp.host_readable = false;
                    if (!foregroundPool || foregroundPool->params.w != fp.w || foregroundPool->params.h != fp.h) ++stats.imageAllocations;
                    if (!pl_tex_recreate(vk->gpu, &foregroundPool, &fp)) throw ResourceFailure("Vulkan composite allocation failed");
                    pl_tex_transfer_params transfer{}; transfer.tex = foregroundPool; transfer.ptr = const_cast<float *>(op.foreground->rgba.data());
                    if (!pl_tex_upload(vk->gpu, &transfer)) throw ResourceFailure("Vulkan composite upload failed");
                    stats.uploadedBytes += op.foreground->rgba.size() * sizeof(float);
                    foregroundSource = op.foreground;
                }
                pass(pool[current], pool[1-current], sample + "vec4 f=texelFetch(oma_foreground,pos,0); float a=clamp(f.a*" + number(op.opacity) + ",0.0,1.0); float back=source.a*(1.0-a); float alpha=a+back; color=vec4(alpha>1e-12 ? (f.rgb*a+source.rgb*back)/alpha : vec3(0.0),alpha);", nullptr, foregroundPool);
                current = 1-current;
            }
        }
        Image output(input.width, input.height);
        pl_tex_transfer_params download{}; download.tex = pool[current]; download.ptr = output.rgba.data();
        if (!pl_tex_download(vk->gpu, &download)) throw ResourceFailure("Vulkan image download failed");
        stats.downloadedBytes += output.rgba.size() * sizeof(float);
        return output;
    }
#else
    void initialise() { attempted = true; caps.reason = "Built with CPU processing only"; }
#endif
    static std::string key(ImageView image, const std::vector<Operation> &ops) {
        std::ostringstream out;
        // A chain is per-pixel: its parity and its speed against the CPU do
        // not depend on the image, so one qualification covers every size.
        if (allChain(ops)) out << "chain:"; else if (perPixel(ops)) out << "pixel:"; else out << image.width << 'x' << image.height << ':';
        for (const auto &op : ops) {
            out << op.kind << ':' << number(op.sigma) << ':' << number(op.opacity);
            if (op.kind == Operation::Chain) for (const auto &st : op.chain) {
                out << '[' << st.kind << ',' << number(st.black) << ',' << number(st.scale) << ',' << st.blueMapping << st.clipping << st.adaptation << st.version << st.clip << st.applyGrey << st.method
                    << ',' << number(st.p) << ',' << number(st.gamut) << ',' << number(st.whiteTarget) << ',' << number(st.blackTarget) << ',' << number(st.paperExposure) << ',' << number(st.filmFog) << ',' << number(st.filmPower) << ',' << number(st.paperPower) << ',' << number(st.huePreservation);
                for (float v : st.coeffs) out << ',' << number(v);
                for (const auto *m : {&st.matrix, &st.lmatrix, &st.rgbToXyz, &st.xyzToRgb, &st.mix, &st.pipeToBase, &st.baseToRendering, &st.renderingToPipe}) for (float v : *m) out << ',' << number(v);
                for (const auto *v4 : {&st.illuminant, &st.saturation, &st.lightness, &st.grey}) for (float v : *v4) out << ',' << number(v);
                if (st.kind == ChainStage::PrimaryGrade) {
                    for (const auto *v : {&st.lift,&st.gamma,&st.gain,&st.offset}) for (float f : *v) out << ',' << number(f);
                    for (float f : st.master) out << ',' << number(f);
                    for (float f : st.luma) out << ',' << number(f);
                    out << ',' << number(st.lumMix);
                }
                out << ']';
            }
            if (op.colour) out << op.colour->getCacheID();
            if (op.kind == Operation::Matrix) for (float v : op.matrix) out << number(v);
            if (op.kind == Operation::Texture) out << number(op.textureAmount) << ":" << number(op.textureScale);
        }
        return out.str();
    }
    Result run(ImageView input, const std::vector<Operation> &ops, Preference preference, const std::atomic_bool *cancel, bool cpuRecovery = true, float *into = nullptr, size_t intoRowBytes = 0) {
        const auto start = Clock::now(); Result result;
        if (!input.valid()) throw std::invalid_argument("Invalid input image");
        for (const auto &op : ops) if (op.foreground && (!op.foreground->valid() || op.foreground->width != input.width || op.foreground->height != input.height)) throw std::invalid_argument("Composite dimensions differ");
        if (cancelled(cancel)) { result.cancelled = true; return result; }
        const auto admission = admitted.find(key(input, ops));
        const bool requested = preference == Preference::Vulkan || (preference == Preference::Automatic && admission != admitted.end() && admission->second);
        if (requested) {
            initialise();
#if OMA_GPU_WITH_VULKAN
            if (caps.vulkan && !failed) {
                try { result.image = gpu(input, ops, result, cancel, into, intoRowBytes); result.usedVulkan = into ? result.usedVulkan && !cancelled(cancel) : result.image.valid(); }
                catch (const std::exception &e) {
                    failed = dynamic_cast<const ResourceFailure *>(&e) || pl_gpu_is_failed(vk->gpu);
                    result.fallbackReason = e.what();
                    if (failed) {
                        caps.vulkan = false; caps.reason = result.fallbackReason;
                        admitted.clear();
                        // An allocation failure need not mark libplacebo's
                        // device lost. Keeping its pools and qualifying each
                        // new slider value would keep starving the interface.
                        releaseDevice();
                    } else if (admission != admitted.end()) {
                        // An unsupported job does not disable unrelated work.
                        admitted.erase(admission);
                    }
                }
            } else
#endif
                result.fallbackReason = caps.reason.empty() ? "Vulkan disabled after a device failure" : caps.reason;
        }
        if (cancelled(cancel)) { result.image = {}; result.cancelled = true; result.usedVulkan = false; result.milliseconds = elapsed(start); return result; }
        if (into) { result.milliseconds = elapsed(start); return result; }
        if (!result.image.valid() && !cpuRecovery) { result.milliseconds = elapsed(start); return result; }
        if (!result.image.valid()) {
            result.image.width = input.width; result.image.height = input.height;
            if (input.rowBytes == size_t(input.width) * 16)
                result.image.rgba.assign(input.pixels, input.pixels + size_t(input.width) * input.height * 4);
            else {
                result.image.rgba.resize(size_t(input.width) * input.height * 4);
                for (int y = 0; y < input.height; ++y)
                    std::memcpy(result.image.rgba.data() + size_t(y) * input.width * 4,
                                reinterpret_cast<const char *>(input.pixels) + size_t(y) * input.rowBytes, size_t(input.width) * 16);
            }
            cpu(result.image, ops, cancel);
        }
        result.cancelled = cancelled(cancel); if (result.cancelled) result.image = {};
        result.milliseconds = elapsed(start); return result;
    }
};
struct Context::Worker {
    struct Job { Image image; Pipeline pipeline; Image reference; double cpuMs = 0; };
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::optional<Job> job;
    std::set<std::string> tried;
    std::deque<std::string> triedOrder;
    Benchmark last;
    bool stop = false;
    std::thread thread;
    ~Worker() {
        { std::lock_guard lock(mutex); stop = true; job.reset(); }
        ready.notify_one();
        if (thread.joinable()) thread.join();
    }
};
Context::Context(Options options) : state_(std::make_unique<State>(options)), worker_(std::make_unique<Worker>()) {
    if (!std::isfinite(options.maxError) || !std::isfinite(options.meanError) || options.maxError < 0 || options.meanError < 0)
        throw std::invalid_argument("Invalid GPU parity tolerance");
}
Context::~Context() { worker_.reset(); }
Capabilities Context::capabilities() { std::lock_guard lock(state_->mutex); state_->initialise(); return state_->caps; }
Result Context::run(const Image &image, const Pipeline &pipeline, Preference preference, const std::atomic_bool *cancel) {
    return run(image.view(), pipeline, preference, cancel);
}
Result Context::run(ImageView image, const Pipeline &pipeline, Preference preference, const std::atomic_bool *cancel) {
    if (preference == Preference::Cpu) return State({}).run(image, pipeline.state_->operations, Preference::Cpu, cancel);
    std::unique_lock lock(state_->mutex, std::defer_lock);
    if (preference == Preference::Automatic && !lock.try_lock())
        return State({}).run(image, pipeline.state_->operations, Preference::Cpu, cancel);
    if (!lock.owns_lock()) lock.lock();
    return state_->run(image, pipeline.state_->operations, preference, cancel);
}
Result Context::tryRun(ImageView image, const Pipeline &pipeline, const std::atomic_bool *cancel) {
    std::unique_lock lock(state_->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {};
    return state_->run(image, pipeline.state_->operations, Preference::Automatic, cancel, false);
}
Result Context::tryRunInto(ImageView image, float *output, size_t rowBytes, const Pipeline &pipeline, const std::atomic_bool *cancel) {
    if (!output || rowBytes < size_t(image.width) * 16 || !allChain(pipeline.state_->operations)) return {};
    std::unique_lock lock(state_->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {};
    return state_->run(image, pipeline.state_->operations, Preference::Automatic, cancel, false, output, rowBytes);
}
Benchmark Context::benchmark(const Image &image, const Pipeline &pipeline, int repeats) {
    return benchmark(image.view(), pipeline, repeats);
}
Benchmark Context::benchmark(ImageView image, const Pipeline &pipeline, int repeats) {
    return benchmarkReference(image, pipeline, nullptr, repeats);
}
Benchmark Context::benchmark(ImageView image, const Pipeline &pipeline, CpuReference reference, int repeats) {
    return benchmarkReference(image, pipeline, &reference, repeats);
}
Benchmark Context::benchmarkReference(ImageView image, const Pipeline &pipeline, const CpuReference *native, int repeats) {
    Benchmark result;
    if (!image.valid()) throw std::invalid_argument("Invalid benchmark image");
    if (native && (!native->image.valid() || native->image.width != image.width || native->image.height != image.height
        || !std::isfinite(native->milliseconds) || native->milliseconds <= 0)) throw std::invalid_argument("Invalid CPU reference");
    // Engines already own their output buffer. Include the final copy from
    // the downloaded result into that buffer in the GPU admission timing.
    std::vector<float> delivered;
    if (native) delivered.resize(size_t(image.width) * image.height * 4);
    const auto key = state_->key(image, pipeline.state_->operations);
    {
        std::lock_guard lock(state_->mutex);
        state_->admitted.erase(key);
        state_->initialise();
        if (!state_->caps.vulkan || state_->failed) { result.reason = state_->caps.reason; return result; }
    }
    repeats = std::clamp(repeats, 1, 9);
    std::vector<double> cpuTimes, gpuTimes;
    for (int i = -1; i < repeats; ++i) {
        // CPU references can be slow. Only hold the device lock for GPU work,
        // so another view can keep using its already measured pipeline.
        Result computed;
        if (!native) computed = run(image, pipeline, Preference::Cpu);
        const CpuReference reference = native ? *native : CpuReference{computed.image.view(), computed.milliseconds};
        Result candidate;
        {
            std::lock_guard lock(state_->mutex);
            // A refused GPU candidate needs no CPU replay: the benchmark
            // already has its reference, potentially from a faster engine.
            candidate = state_->run(image, pipeline.state_->operations, Preference::Vulkan, nullptr, false);
        }
        if (!candidate.usedVulkan) { result.reason = candidate.fallbackReason; return result; }
        double gpuMs = candidate.milliseconds;
        if (native) {
            const auto start = Clock::now();
            std::memcpy(delivered.data(), candidate.image.rgba.data(), delivered.size() * sizeof(float));
            gpuMs += elapsed(start);
        }
        double sum = 0, maximum = 0;
        for (int y = 0; y < image.height; ++y) {
            const auto *row = reinterpret_cast<const float *>(reinterpret_cast<const char *>(reference.image.pixels) + size_t(y) * reference.image.rowBytes);
            for (int x = 0; x < image.width * 4; ++x) {
                const double difference = std::abs(double(row[x]) - candidate.image.rgba[size_t(y) * image.width * 4 + x]);
                if (!std::isfinite(difference)) { result.reason = "Non-finite Vulkan parity result"; return result; }
                maximum = std::max(maximum, difference); sum += difference;
            }
        }
        result.maxError = std::max(result.maxError, maximum); result.meanError = std::max(result.meanError, sum / candidate.image.rgba.size());
        if (maximum > state_->options.maxError || result.meanError > state_->options.meanError) { result.reason = "Vulkan result exceeded float parity tolerance"; return result; }
        if (i >= 0) { cpuTimes.push_back(reference.milliseconds); gpuTimes.push_back(gpuMs); }
    }
    std::sort(cpuTimes.begin(), cpuTimes.end()); std::sort(gpuTimes.begin(), gpuTimes.end());
    result.cpuMs = cpuTimes[cpuTimes.size()/2]; result.vulkanMs = gpuTimes[gpuTimes.size()/2];
    result.passed = true; result.preferVulkan = result.vulkanMs < result.cpuMs * 0.85;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->admitted.size() >= 128) state_->admitted.clear();
        state_->admitted[key] = result.preferVulkan;
    }
    result.reason = result.preferVulkan ? "Vulkan passed parity and end-to-end timing" : "CPU is faster including transfers";
    return result;
}
Result Context::runAdaptive(ImageView image, const Pipeline &pipeline, const std::atomic_bool *cancel) {
    auto result = run(image, pipeline, Preference::Automatic, cancel);
    if (!result.cancelled) warmup(image, pipeline);
    return result;
}
Decision Context::decision(int width, int height, const Pipeline &pipeline) const {
    const auto key = State::key(ImageView{width,height,nullptr,0}, pipeline.state_->operations);
    {
        std::unique_lock lock(state_->mutex, std::try_to_lock);
        if (!lock.owns_lock()) return Decision::Cpu;
        if (state_->attempted && (!state_->caps.vulkan || state_->failed)) return Decision::Cpu;
        const auto found = state_->admitted.find(key);
        if (found != state_->admitted.end()) return found->second ? Decision::Vulkan : Decision::Cpu;
    }
    std::lock_guard lock(worker_->mutex);
    return worker_->job || worker_->tried.count(key) ? Decision::Cpu : Decision::Untried;
}
bool Context::warmup(ImageView image, const Pipeline &pipeline) {
    return warmupReference(image, pipeline, nullptr);
}
bool Context::warmup(ImageView image, const Pipeline &pipeline, CpuReference reference) {
    return warmupReference(image, pipeline, &reference);
}
bool Context::warmupReference(ImageView image, const Pipeline &pipeline, const CpuReference *native) {
    if (native && (!native->image.valid() || native->image.width != image.width || native->image.height != image.height
        || !std::isfinite(native->milliseconds) || native->milliseconds <= 0)) return false;
    // A per-pixel pipeline too large to copy whole qualifies on its top rows
    // (the display transform of a 4K preview, for example): the slice stands
    // for the image, and its time scales with its rows.
    CpuReference sliced{};
    if (image.valid() && perPixel(pipeline.state_->operations)) {
        const uint64_t perRow = uint64_t(image.width) * (native ? 32 : 16);
        const int rows = int(std::min<uint64_t>(uint64_t(image.height), std::max<uint64_t>(1, (64ULL * 1024 * 1024) / perRow)));
        if (rows < image.height) {
            if (native) {
                sliced = CpuReference{ImageView{image.width, rows, native->image.pixels, native->image.rowBytes},
                                      native->milliseconds * rows / image.height};
                native = &sliced;
            }
            image = ImageView{image.width, rows, image.pixels, image.rowBytes};
        }
    }
    if (!image.valid() || uint64_t(image.width) * image.height * (native ? 32 : 16) > 64ULL * 1024 * 1024 ||
        decision(image.width,image.height,pipeline) != Decision::Untried) return false;
    const auto key = State::key(image, pipeline.state_->operations);
    auto &worker = *worker_;
    std::lock_guard lock(worker.mutex);
    if (worker.job || worker.tried.count(key)) return false;
    Worker::Job job{Image(image.width, image.height), pipeline, {}, 0};
    for (int y = 0; y < image.height; ++y)
        std::memcpy(job.image.rgba.data() + size_t(y) * image.width * 4,
                    reinterpret_cast<const char *>(image.pixels) + size_t(y) * image.rowBytes, size_t(image.width) * 16);
    if (native) {
        job.reference = Image(image.width, image.height); job.cpuMs = native->milliseconds;
        for (int y = 0; y < image.height; ++y)
            std::memcpy(job.reference.rgba.data() + size_t(y) * image.width * 4,
                        reinterpret_cast<const char *>(native->image.pixels) + size_t(y) * native->image.rowBytes, size_t(image.width) * 16);
    }
    // Bound recent attempts without permanently disabling new parameter sets
    // after a long slider session. Evicted values must qualify again.
    if (worker.triedOrder.size() >= 128) {
        worker.tried.erase(worker.triedOrder.front());
        worker.triedOrder.pop_front();
    }
    worker.tried.insert(key);
    worker.triedOrder.push_back(key);
    worker.job = std::move(job);
    if (!worker.thread.joinable()) worker.thread = std::thread([this, workerPtr = &worker] {
        auto &w = *workerPtr;
        for (;;) {
            std::optional<Worker::Job> job;
            { std::unique_lock lock(w.mutex); w.ready.wait(lock, [&] { return w.stop || w.job.has_value(); });
              if (w.stop) return;
              job.swap(w.job); }
            Benchmark report;
            try {
                report = job->reference.valid()
                    ? benchmark(job->image.view(), job->pipeline, CpuReference{job->reference.view(), job->cpuMs}, 3)
                    : benchmark(job->image, job->pipeline, 3);
            }
            catch (const std::exception &e) { report.reason = e.what(); }
            { std::lock_guard lock(w.mutex); w.last = std::move(report); }
        }
    });
    worker.ready.notify_one();
    return true;
}
Benchmark Context::lastBenchmark() const { std::lock_guard lock(worker_->mutex); return worker_->last; }

}
