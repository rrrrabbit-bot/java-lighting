#include "lighting/LightmapHook.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "ll/api/io/Logger.h"
#include "ll/api/memory/Hook.h"

#include "lighting/JavaLightmap.h"

// Bedrock client headers (provided by the LeviLamina client package).
#include "mc/client/renderer/ptexture/BaseLightData.h"
#include "mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h"
#include "mc/deps/core/image/Image.h"

namespace java_lighting {
namespace {

std::atomic<config::Config const*> gConfig{nullptr};
ll::io::Logger*                    gLogger = nullptr;
std::filesystem::path              gDataDir;
std::atomic_bool                   gInstalled{false};
std::atomic_bool                   gDumpedOnce{false};
std::mutex                         gDumpMutex;

// ---------------------------------------------------------------------------
// BaseLightData access.
//
// The SDK exposes members through ll::TypedStorage, whose accessors are an
// implementation detail of the header generator. Every field's byte offset is
// well-defined though, so read through offsetof + memcpy: no aliasing tricks,
// no dependency on TypedStorage's API, and the layout is enforced by the
// generated header itself.
// ---------------------------------------------------------------------------
template <class T>
T readField(void const* base, size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<char const*>(base) + offset, sizeof(T));
    return value;
}

struct LiveLightInputs {
    float gamma         = 0.5f;
    float skyDarken     = 1.0f;
    float nightVision   = 0.0f;
    float darkenWorld   = 0.0f;
    int   skyFlashTime  = 0;
    int   dimensionType = 0;
    float dimensionAmbient = 0.0f;
    bool  nightVisionActive = false;
};

[[nodiscard]] LiveLightInputs readInputs(BaseLightData const& data) {
    LiveLightInputs in{};
    in.gamma        = readField<float>(&data, offsetof(BaseLightData, mGamma));
    in.skyDarken    = readField<float>(&data, offsetof(BaseLightData, mSkyDarken));
    in.darkenWorld  = readField<float>(&data, offsetof(BaseLightData, mDarkenWorldAmount));
    in.nightVision  = readField<float>(&data, offsetof(BaseLightData, mNightvisionScale));
    in.nightVisionActive = readField<bool>(&data, offsetof(BaseLightData, mNightvisionActive));
    in.skyFlashTime = readField<int>(&data, offsetof(BaseLightData, mSkyFlashTime));
    in.dimensionType = readField<int>(&data, offsetof(BaseLightData, mDimensionType));
    return in;
}

// ---------------------------------------------------------------------------
// mce::Image helpers
// ---------------------------------------------------------------------------
struct ImageView {
    uint8_t*  pixels = nullptr;
    uint32_t  width  = 0;
    uint32_t  height = 0;
    uint32_t  depth  = 1;
    size_t    bpp    = 0;
    char const* formatName = "unknown";
};

[[nodiscard]] bool makeView(::mce::Image& image, ImageView& view) {
    size_t bpp = 0;
    char const* name = "unknown";
    switch (image.imageFormat) {
    case ::mce::ImageFormat::RGBA8Unorm:
        bpp = 4;
        name = "RGBA8Unorm";
        break;
    case ::mce::ImageFormat::RGB8Unorm:
        bpp = 3;
        name = "RGB8Unorm";
        break;
    case ::mce::ImageFormat::RG8Unorm:
        bpp = 2;
        name = "RG8Unorm";
        break;
    case ::mce::ImageFormat::R8Unorm:
        bpp = 1;
        name = "R8Unorm";
        break;
    default:
        return false;
    }

    uint32_t depth = image.mDepth ? image.mDepth : 1;
    if (image.mWidth == 0 || image.mHeight == 0) { return false; }

    size_t needed = static_cast<size_t>(image.mWidth) * image.mHeight * depth * bpp;
    if (image.mImageBytes.size() < needed) { return false; }

    view.pixels     = image.mImageBytes.data();
    view.width      = image.mWidth;
    view.height     = image.mHeight;
    view.depth      = depth;
    view.bpp        = bpp;
    view.formatName = name;
    return true;
}

[[nodiscard]] int texelLevel(uint32_t texel, uint32_t size) {
    if (size <= 1) { return 0; }
    double scaled = static_cast<double>(texel) * 15.0 / static_cast<double>(size - 1);
    return static_cast<int>(scaled + 0.5);
}

void writePixel(ImageView& view, uint32_t x, uint32_t y, Rgb const& rgb, uint8_t keepAlpha) {
    size_t index = (static_cast<size_t>(y) * view.width + x) * view.bpp;
    uint8_t* p   = view.pixels + index;
    switch (view.bpp) {
    case 4:
        p[0] = static_cast<uint8_t>(rgb.r * 255.0f + 0.5f);
        p[1] = static_cast<uint8_t>(rgb.g * 255.0f + 0.5f);
        p[2] = static_cast<uint8_t>(rgb.b * 255.0f + 0.5f);
        p[3] = keepAlpha;
        break;
    case 3:
        p[0] = static_cast<uint8_t>(rgb.r * 255.0f + 0.5f);
        p[1] = static_cast<uint8_t>(rgb.g * 255.0f + 0.5f);
        p[2] = static_cast<uint8_t>(rgb.b * 255.0f + 0.5f);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
void dumpImage(ImageView const& view, LiveLightInputs const& in, std::string const& tag) {
    if (gDataDir.empty()) { return; }

    std::error_code ec;
    auto dir = gDataDir / "lightmap_dump";
    std::filesystem::create_directories(dir, ec);

    std::ofstream csv(dir / (tag + ".csv"));
    if (csv) {
        csv << "dimension_type,gamma,sky_darken,darken_world,night_vision,sky_flash_time,"
               "width,height,format\n";
        csv << in.dimensionType << ',' << in.gamma << ',' << in.skyDarken << ',' << in.darkenWorld << ','
            << in.nightVision << ',' << in.skyFlashTime << ',' << view.width << ',' << view.height << ','
            << view.formatName << "\n";
        csv << "y,x,r,g,b\n";
        for (uint32_t y = 0; y < view.height; ++y) {
            for (uint32_t x = 0; x < view.width; ++x) {
                size_t index = (static_cast<size_t>(y) * view.width + x) * view.bpp;
                uint8_t const* p = view.pixels + index;
                csv << y << ',' << x << ',' << int(p[0]) << ',' << int(p[1]) << ',' << int(p[2]) << '\n';
            }
        }
    }

    // PPM is trivially viewable and needs no image library.
    std::ofstream ppm(dir / (tag + ".ppm"), std::ios::binary);
    if (ppm) {
        ppm << "P6\n" << view.width << ' ' << view.height << "\n255\n";
        for (size_t i = 0; i < static_cast<size_t>(view.width) * view.height; ++i) {
            uint8_t const* p = view.pixels + i * view.bpp;
            ppm.write(reinterpret_cast<char const*>(p), 3);
        }
    }
}

// ---------------------------------------------------------------------------
// Hook implementation
// ---------------------------------------------------------------------------
LightmapSettings makeSettings(config::Config const& cfg, LiveLightInputs const& in) {
    LightmapSettings s;
    if (cfg.lightmap.useLiveLightData) {
        if (in.gamma >= 0.0f && in.gamma <= 1.0f) { s.gamma = in.gamma; }
        float sky = in.skyDarken;
        if (sky >= 0.0f && sky <= 1.0f) {
            s.skyDarken = cfg.lightmap.invertSkyDarken ? (1.0f - sky) : sky;
        }
    } else {
        s.gamma     = cfg.lightmap.gamma;
        s.skyDarken = cfg.lightmap.skyDarken;
    }
    s.nightVision = in.nightVisionActive ? in.nightVision : 0.0f;
    s.darkenWorld = (in.darkenWorld > 0.0f && in.darkenWorld <= 1.0f) ? in.darkenWorld : 0.0f;
    return s;
}

void rewriteImage(ImageView& view, LiveLightInputs const& in, config::Config const& cfg) {
    // Only 2D RGB(A) images are safe to rewrite here.
    if (view.bpp < 3 || view.depth != 1) { return; }

    auto table = JavaLightmap::buildTableU8(makeSettings(cfg, in));
    bool blockIsY = (cfg.lightmap.blockAxis == "y");

    std::vector<uint8_t> alphas;
    if (cfg.lightmap.preserveAlpha && view.bpp == 4) {
        alphas.resize(static_cast<size_t>(view.width) * view.height);
        for (size_t i = 0; i < alphas.size(); ++i) { alphas[i] = view.pixels[i * 4 + 3]; }
    }

    for (uint32_t y = 0; y < view.height; ++y) {
        uint32_t ty     = cfg.lightmap.flipY ? (view.height - 1 - y) : y;
        int      levelY = std::clamp(texelLevel(ty, view.height), 0, 15);
        for (uint32_t x = 0; x < view.width; ++x) {
            uint32_t tx     = cfg.lightmap.flipX ? (view.width - 1 - x) : x;
            int      levelX = std::clamp(texelLevel(tx, view.width), 0, 15);

            int block = blockIsY ? levelY : levelX;
            int sky   = blockIsY ? levelX : levelY;
            auto const& rgb = table[static_cast<size_t>(sky * 16 + block)];

            uint8_t alpha = 255;
            if (cfg.lightmap.preserveAlpha && view.bpp == 4) {
                alpha = alphas[static_cast<size_t>(y) * view.width + x];
            }
            writePixel(view, x, y, {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f}, alpha);
        }
    }
}

void onImageBuilt(BaseLightData const& lightData, ::mce::Image* targetImage, uint imageLength) {
    auto const* cfgPtr = gConfig.load(std::memory_order_acquire);
    if (!cfgPtr || !targetImage) { return; }
    config::Config const& cfg = *cfgPtr;

    ImageView view;
    if (!makeView(*targetImage, view)) { return; }

    LiveLightInputs in = readInputs(lightData);

    static std::atomic_bool loggedLayout{false};
    if (gLogger && !loggedLayout.exchange(true)) {
        gLogger->info(
            "Lightmap built: {}x{} depth={} format={} imageLength={} gamma={:.3f} skyDarken={:.3f} "
            "darkenWorld={:.3f} nightVision={:.2f} skyFlash={} dimensionType={}",
            view.width,
            view.height,
            view.depth,
            view.formatName,
            imageLength,
            in.gamma,
            in.skyDarken,
            in.darkenWorld,
            in.nightVision,
            in.skyFlashTime,
            in.dimensionType
        );
        if (gDataDir.empty()) {
            gLogger->warn("No data directory configured; lightmap dumps are disabled.");
        }
    }

    auto dumpOnce = [&](char const* tag) {
        if (!cfg.diagnostics.dumpLightmap) { return; }
        if (!cfg.diagnostics.dumpEveryFrame) {
            bool expected = false;
            if (!gDumpedOnce.compare_exchange_strong(expected, true)) { return; }
        }
        std::lock_guard lock(gDumpMutex);
        dumpImage(view, in, tag);
    };

    if (cfg.lightmap.mode == "observe") {
        dumpOnce("bedrock_lightmap");
        return;
    }

    if (cfg.lightmap.mode == "java") {
        rewriteImage(view, in, cfg);
        dumpOnce("java_lightmap");
    }
}

} // namespace
} // namespace java_lighting

// Hooks the generated non-virtual thunk for the virtual `buildImage` function
// (LeviLamina's "$" workaround for virtual members) so every dimension's
// lightmap rebuild goes through us.
LL_TYPE_INSTANCE_HOOK(
    JavaLightmapBuildImageHook,
    ll::memory::HookPriority::Normal,
    BaseLightTextureImageBuilder,
    &BaseLightTextureImageBuilder::$buildImage,
    bool,
    ::BaseLightData const& lightData,
    ::mce::Image*          targetImage,
    uint                   imageLength,
    float                  a,
    float                  ambientBoost,
    bool                   clampToMinimum
) {
    bool ok = origin(lightData, targetImage, imageLength, a, ambientBoost, clampToMinimum);
    if (ok) { ::java_lighting::onImageBuilt(lightData, targetImage, imageLength); }
    return ok;
}

namespace java_lighting {

void LightmapHook::configure(config::Config const& config) { gConfig.store(&config, std::memory_order_release); }

void LightmapHook::setDataDir(std::filesystem::path dir) { gDataDir = std::move(dir); }

void LightmapHook::setLogger(ll::io::Logger& logger) { gLogger = &logger; }

bool LightmapHook::install() {
    if (gInstalled.load(std::memory_order_acquire)) { return true; }

    int result = JavaLightmapBuildImageHook::hook();
    // ll::memory::hookEx returns 0 on success.
    if (result != 0) { return false; }

    gInstalled.store(true, std::memory_order_release);
    return true;
}

bool LightmapHook::uninstall() {
    if (!gInstalled.load(std::memory_order_acquire)) { return true; }
    bool ok = JavaLightmapBuildImageHook::unhook();
    gInstalled.store(false, std::memory_order_release);
    return ok;
}

bool LightmapHook::installed() { return gInstalled.load(std::memory_order_acquire); }

} // namespace java_lighting
