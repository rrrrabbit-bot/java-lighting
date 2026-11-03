#include "lighting/JavaLightmap.h"

#include <algorithm>
#include <cmath>

namespace java_lighting {
namespace {

constexpr float kAmbientMixColor = 0.75f;
constexpr float kAmbientMixWeight = 0.04f;
constexpr float kSkyFactorBias = 0.05f;   // h = skyDarken * 0.95 + 0.05
constexpr float kSkyFactorScale = 0.95f;
constexpr float kBlockFactor = 1.5f;      // blockLightRedFlicker + 1.5

[[nodiscard]] constexpr float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

[[nodiscard]] float mix(float a, float b, float t) { return a + (b - a) * t; }

/// Java: LightTexture.getBrightness(DimensionType, int)
///   f = level / 15; g = f / (4 - 3f); return Mth.lerp(ambientLight, g, 1.0)
/// Mth.lerp(delta, start, end) == start + delta * (end - start), so the
/// dimension's ambient light lift is applied as g + ambient * (1 - g).
[[nodiscard]] float brightness(int level, float dimensionAmbient) {
    float f      = static_cast<float>(level) / 15.0f;
    float curved = f / (4.0f - 3.0f * f);
    return curved + dimensionAmbient * (1.0f - curved);
}

/// Java: LightTexture.notGamma(float) — 1 - (1 - x)^4
[[nodiscard]] float notGamma(float x) {
    float g = 1.0f - clamp01(x);
    return 1.0f - g * g * g * g;
}

} // namespace

Rgb JavaLightmap::sample(int blockLight, int skyLight, LightmapSettings const& s) {
    blockLight = std::clamp(blockLight, 0, 15);
    skyLight   = std::clamp(skyLight, 0, 15);

    float const skyDarken   = clamp01(s.skyDarken);
    float const skyFactor   = skyDarken * kSkyFactorScale + kSkyFactorBias;
    float const blockFactor = kBlockFactor;
    float const nightVision = clamp01(s.nightVision);

    float const q = brightness(skyLight, 0.0f) * skyFactor;   // Overworld ambient = 0
    float const r = brightness(blockLight, 0.0f) * blockFactor;

    // Warm block-light ramp. R rises fastest, B slowest.
    float c[3] = {
        r,
        r * ((r * 0.6f + 0.4f) * 0.6f + 0.4f),
        r * (r * r * 0.6f + 0.4f),
    };

    // Sky-light colour: neutral at noon (skyDarken 1), blue at night.
    float const skyColorR = skyDarken * 0.65f + 0.35f;
    float const skyColorG = skyColorR;
    float const skyColorB = 1.0f;

    c[0] += skyColorR * q;
    c[1] += skyColorG * q;
    c[2] += skyColorB * q;

    for (float& v : c) { v = mix(v, kAmbientMixColor, kAmbientMixWeight); }

    // Boss darkness: lerp towards a 0.7/0.6/0.6 - multiplied copy.
    float const darken = clamp01(s.darkenWorld);
    if (darken > 0.0f) {
        c[0] = mix(c[0], c[0] * 0.7f, darken);
        c[1] = mix(c[1], c[1] * 0.6f, darken);
        c[2] = mix(c[2], c[2] * 0.6f, darken);
    }

    if (nightVision > 0.0f) {
        float mx = std::max(c[0], std::max(c[1], c[2]));
        if (mx < 1.0f && mx > 0.0f) {
            for (float& v : c) { v = mix(v, v / mx, nightVision); }
        }
    }

    for (float& v : c) { v = clamp01(v); }

    // Java's Brightness option is a notGamma() mix weight (default 0.5).
    float const g = clamp01(s.gamma);
    for (float& v : c) { v = mix(v, notGamma(v), g); }

    for (float& v : c) { v = mix(v, kAmbientMixColor, kAmbientMixWeight); }

    return {clamp01(c[0]), clamp01(c[1]), clamp01(c[2])};
}

void JavaLightmap::buildTable(LightmapSettings const& settings, std::array<Rgb, kSize * kSize>& out) {
    for (int sky = 0; sky < kSize; ++sky) {
        for (int block = 0; block < kSize; ++block) {
            out[static_cast<size_t>(sky * kSize + block)] = sample(block, sky, settings);
        }
    }
}

std::array<std::array<uint8_t, 3>, JavaLightmap::kSize * JavaLightmap::kSize>
JavaLightmap::buildTableU8(LightmapSettings const& settings) {
    std::array<Rgb, kSize * kSize> table{};
    buildTable(settings, table);

    std::array<std::array<uint8_t, 3>, kSize * kSize> out{};
    for (size_t i = 0; i < table.size(); ++i) {
        // Java converts with (int)(component * 255.0F): truncation, not rounding.
        out[i][0] = static_cast<uint8_t>(clamp01(table[i].r) * 255.0f);
        out[i][1] = static_cast<uint8_t>(clamp01(table[i].g) * 255.0f);
        out[i][2] = static_cast<uint8_t>(clamp01(table[i].b) * 255.0f);
    }
    return out;
}

char const* JavaLightmap::javaReferenceVersion() { return "Java Edition 1.21.4 (lightmap.fsh)"; }

uint64_t JavaLightmap::tableChecksum(
    std::array<std::array<uint8_t, 3>, JavaLightmap::kSize * JavaLightmap::kSize> const& table
) {
    uint64_t hash = 0xCBF29CE484222325ull; // FNV-1a 64
    for (auto const& rgb : table) {
        for (uint8_t v : rgb) {
            hash ^= v;
            hash *= 0x100000001B3ull;
        }
    }
    return hash;
}

} // namespace java_lighting
