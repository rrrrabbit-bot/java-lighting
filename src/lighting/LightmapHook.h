#pragma once

#include <filesystem>

#include "config/Config.h"

namespace ll::io {
class Logger;
} // namespace ll::io

namespace java_lighting {

/// Hooks Bedrock's `BaseLightTextureImageBuilder::buildImage` (the CPU-side
/// lightmap generator) and optionally replaces the generated 16x16 lightmap
/// with the Java Edition equivalent.
///
/// The hook is only installed when `lightmap.mode` is "observe" or "java".
/// "observe" never modifies pixels: it only logs and dumps the Bedrock lightmap
/// so the layout can be verified before trusting the rewrite.
class LightmapHook {
public:
    static void configure(config::Config const& config);
    static void setDataDir(std::filesystem::path dir);
    static void setLogger(ll::io::Logger& logger);

    /// @return true when the hook was installed (or already was).
    static bool install();
    static bool uninstall();

    [[nodiscard]] static bool installed();
};

} // namespace java_lighting
