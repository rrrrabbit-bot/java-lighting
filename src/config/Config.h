#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace java_lighting::config {

/// How the lightmap hook behaves.
///  - "off"     : never install the hook (the mod only reports diagnostics).
///  - "observe" : install the hook, log the Bedrock lightmap layout and dump it
///                to <data>/lightmap_dump/, but do not alter rendering.
///  - "java"    : install the hook and replace the Bedrock lightmap with the
///                Java Edition LUT computed from the live light data.
struct LightmapConfig {
    std::string mode = "observe";

    /// Recompute the Java LUT every time Bedrock rebuilds its lightmap, using
    /// the live BaseLightData (gamma, sky darken, night vision, ...).
    bool useLiveLightData = true;

    /// Fallback values used when useLiveLightData is false, or when a live
    /// value is out of range.
    float gamma      = 0.5f;
    float skyDarken  = 1.0f;

    /// Which image axis carries the block-light level. Bedrock lightmap UVs use
    /// (u, v); u is expected to be block light (like Java's texture X). Set to
    /// "y" if the dump shows the axes swapped.
    std::string blockAxis = "x"; // "x" or "y"
    bool        flipX     = false;
    bool        flipY     = false;

    /// Some Bedrock builds hand the builder a sky-darken value where 0 is noon
    /// and 1 is midnight, others the inverse. Flip if the "java" mode looks
    /// inverted (day looks like night).
    bool invertSkyDarken = false;

    /// Keep the alpha channel of the original Bedrock pixels. Bedrock may use
    /// alpha for its own purposes ("clamp to minimum" tricks), so preserving it
    /// is safer than writing 255.
    bool preserveAlpha = true;
};

struct DiagnosticsConfig {
    /// Write lightmap dumps (CSV + PPM) and the generated Java LUT to the mod
    /// data directory. Only the first rebuild is dumped, unless dumpEveryFrame.
    bool dumpLightmap = true;
    bool dumpEveryFrame = false;
    bool verbose = false;
};

struct Config {
    /// Schema version, required by ll::config::loadConfig.
    int version = 1;

    LightmapConfig    lightmap;
    DiagnosticsConfig diagnostics;
};

} // namespace java_lighting::config
