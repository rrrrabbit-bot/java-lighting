#pragma once

#include "config/Config.h"
#include "ll/api/mod/NativeMod.h"

namespace java_lighting {

/// Entry point of the JavaLighting client mod.
///
/// The mod has three jobs:
///  1. own the canonical, exactly-ported Java Edition lightmap maths
///     (``lighting/JavaLightmap.h``);
///  2. drive the native lightmap hook for the classic renderer and emit
///     diagnostics derived from the same maths;
///  3. ship the Vibrant Visuals preset that covers the deferred renderer.
class JavaLighting {
public:
    static JavaLighting& getInstance();

    JavaLighting() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// @return True if the mod was loaded successfully.
    bool load();

    /// @return True if the mod was enabled successfully.
    bool enable();

    /// @return True if the mod was disabled successfully.
    bool disable();

private:
    ll::mod::NativeMod& mSelf;
    config::Config      mConfig;
};

} // namespace java_lighting
