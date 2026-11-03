#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace java_lighting {

/// Per-frame inputs that feed the Java Edition lightmap.
/// Mirrors the relevant parts of Java's ``LightTexture`` state.
struct LightmapSettings {
    /// Minecraft "Brightness" option, 0.0 .. 1.0 (Java's default is 0.5).
    float gamma = 0.5f;
    /// Java's ``Level#getSkyDarken``: 0.0 at noon, 1.0 at midnight.
    float skyDarken = 0.0f;
    /// Night-vision / conduit-power style boost (Java: 1.0 normally).
    float nightVision = 0.0f;
    /// Java's ``getDarkenWorldAmount`` (boss darkness, e.g. the Wither fight).
    float darkenWorld = 0.0f;
};

/// Linear RGB triples in 0..1, the way Java stores lightmap pixels before
/// converting them to 8-bit sRGB for its 16x16 light texture.
struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    [[nodiscard]] constexpr Rgb operator*(float s) const { return {r * s, g * s, b * s}; }
    [[nodiscard]] constexpr Rgb operator+(Rgb const& o) const { return {r + o.r, g + o.g, b + o.b}; }
};

/// Reimplementation of Java Edition's lightmap generation.
///
/// Java builds a 16x16 texture indexed by ``[skyLight][blockLight]`` (0..15).
/// This class reproduces that function exactly so that:
///  * the same numbers can be emitted as Bedrock "Vibrant Visuals" presets,
///  * diagnostics can be diffed against Java screenshots,
///  * and a future native hook can upload the table without re-deriving it.
class JavaLightmap {
public:
    static constexpr int kSize = 16;

    /// Compute one lightmap cell (Java: ``calculateLightTexture`` inner loop).
    [[nodiscard]] static Rgb sample(int blockLight, int skyLight, LightmapSettings const& settings);

    /// Fill ``out`` with all 256 cells. Index is ``skyLight * 16 + blockLight``,
    /// matching the order Java writes pixels into its NativeImage.
    static void buildTable(LightmapSettings const& settings, std::array<Rgb, kSize * kSize>& out);

    /// Clamp/normalise the table to 8-bit sRGB the way a texture upload would.
    [[nodiscard]] static std::array<std::array<uint8_t, 3>, kSize * kSize>
    buildTableU8(LightmapSettings const& settings);

    /// Human-readable name of the Java version this implementation follows.
    [[nodiscard]] static char const* javaReferenceVersion();

    /// Deterministic checksum of the 8-bit table, handy for regression tests.
    [[nodiscard]] static uint64_t tableChecksum(std::array<std::array<uint8_t, 3>, kSize * kSize> const& table);
};

} // namespace java_lighting
