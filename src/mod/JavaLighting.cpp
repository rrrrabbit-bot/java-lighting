#include "mod/JavaLighting.h"

#include <array>
#include <filesystem>
#include <fstream>

#include "ll/api/Config.h"
#include "ll/api/mod/RegisterHelper.h"

#include "lighting/JavaLightmap.h"
#include "lighting/LightmapHook.h"

namespace java_lighting {
namespace {

/// Dump the Java reference lightmap table next to the config file so users can
/// compare it with the in-game "observe" dump without needing any tooling.
void dumpJavaReference(std::filesystem::path const& dataDir) {
    JavaLightmap::LightmapSettings settings; // gamma 0.5, skyDarken 0 (noon)
    auto                       table = JavaLightmap::buildTableU8(settings);

    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);

    std::ofstream csv(dataDir / "java_lightmap_reference.csv");
    if (!csv) { return; }
    csv << "# Java Edition " << JavaLightmap::javaReferenceVersion() << "\n";
    csv << "# gamma=0.5 skyDarken=0.0 (noon); rows skyLight, columns blockLight; Java truncates to 8 bit\n";
    for (int sky = 0; sky < JavaLightmap::kSize; ++sky) {
        for (int block = 0; block < JavaLightmap::kSize; ++block) {
            auto const& rgb = table[static_cast<size_t>(sky * JavaLightmap::kSize + block)];
            csv << int(rgb[0]) << ',' << int(rgb[1]) << ',' << int(rgb[2]) << (block == 15 ? '\n' : ',');
        }
    }
}

} // namespace

JavaLighting& JavaLighting::getInstance() {
    static JavaLighting instance;
    return instance;
}

bool JavaLighting::load() {
    auto& logger = getSelf().getLogger();
    logger.debug("Loading JavaLighting...");

    auto dataDir    = getSelf().getDataDir();
    auto configPath = dataDir / "config.json";

    if (!ll::config::loadConfig(mConfig, configPath)) {
        logger.info("Config written/updated: {}", configPath.string());
    }
    logger.info("Config file: {}", configPath.string());

    if (mConfig.diagnostics.dumpLightmap) {
        dumpJavaReference(dataDir);
        logger.info("Java lightmap reference written to {}", (dataDir / "java_lightmap_reference.csv").string());
    }

    logger.info(
        "JavaLighting loaded: mode={}, liveLightData={}, blockAxis={}",
        mConfig.lightmap.mode,
        mConfig.lightmap.useLiveLightData,
        mConfig.lightmap.blockAxis
    );
    return true;
}

bool JavaLighting::enable() {
    auto& logger = getSelf().getLogger();
    logger.debug("Enabling JavaLighting...");

    if (mConfig.lightmap.mode == "off") {
        logger.info("lightmap.mode=off: native lightmap hook disabled (resource pack still works).");
        return true;
    }

    if (mConfig.lightmap.mode != "observe" && mConfig.lightmap.mode != "java") {
        logger.warn(
            "Invalid lightmap.mode '{}' (expected off|observe|java); falling back to 'observe'.",
            mConfig.lightmap.mode
        );
        mConfig.lightmap.mode = "observe";
    }

    LightmapHook::setDataDir(getSelf().getDataDir());
    LightmapHook::setLogger(logger);
    LightmapHook::configure(mConfig);

    if (!LightmapHook::install()) {
        logger.error(
            "Failed to hook BaseLightTextureImageBuilder::buildImage. "
            "The Bedrock build may not match LeviLamina 26.51.*; lightmap stays vanilla. "
            "Report this with your full client version."
        );
        return true; // not fatal: the resource pack half of the mod still applies
    }

    if (mConfig.lightmap.mode == "observe") {
        logger.info(
            "Lightmap hook installed in OBSERVE mode (no visual change). Open a world, then inspect "
            "the lightmap_dump/ folder next to config.json to verify the layout. "
            "Switch lightmap.mode to \"java\" to apply the Java lightmap."
        );
    } else {
        logger.info(
            "Lightmap hook installed in JAVA mode: Bedrock lightmaps are replaced with the Java Edition LUT "
            "(gamma={}, blockAxis={}). If lighting looks wrong, set lightmap.mode=observe and inspect the dump.",
            mConfig.lightmap.useLiveLightData ? "live" : std::to_string(mConfig.lightmap.gamma),
            mConfig.lightmap.blockAxis
        );
    }
    return true;
}

bool JavaLighting::disable() {
    getSelf().getLogger().debug("Disabling JavaLighting...");
    LightmapHook::uninstall();
    return true;
}

} // namespace java_lighting

LL_REGISTER_MOD(java_lighting::JavaLighting, java_lighting::JavaLighting::getInstance());
