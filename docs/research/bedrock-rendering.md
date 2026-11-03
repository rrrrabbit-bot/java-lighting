# Bedrock 1.26 Rendering Research — Lightmap, Entity Shadows, Ambient Occlusion

**Target:** Minecraft Bedrock **1.26.51.01** (Windows x64, RenderDragon, legacy + Vibrant Visuals/Deferred modes), **LeviLamina 26.51.9**.
LeviLamina 26.51.x ships both a server and a client build; the official version table lists client **1.26.51.1** for loader 26.51.x ([versions.md](https://raw.githubusercontent.com/liteldev/levilamina/main/docs/main/contents/versions.md)).
**Date of research:** 2026-11-03. **Method:** public web sources only (no game binary inspection in this session — everything below is sourced from official docs, Mojang sample packs, public reverse-engineering repos and LeviLamina API docs).

**Confidence legend**

| Tag | Meaning |
|---|---|
| `[confirmed]` | Directly read in a fetched primary source (URL given) |
| `[inferred]` | Follows logically from confirmed material, not stated verbatim |
| `[unverified]` | Plausible but not sourced; treat as a hypothesis to test in-game |

---

## 0. Executive summary

| Question | Short answer | Confidence |
|---|---|---|
| Is Bedrock's lightmap CPU-generated? | Yes. Runtime **procedural texture** built as an `mce::Image` by `BaseLightTextureImageBuilder` (path `mc/client/renderer/ptexture/...`), per dimension | `[confirmed]` (LeviLamina client headers) |
| Is it texture-driven? | No file in any resource pack; no `textures/lightmap` in Mojang's sample RP. Block geometry stores a **per-vertex lightmap UV** (`a_texcoord1`) that samples `s_LightMapTexture` | `[confirmed]` |
| Can a resource pack override it (legacy mode)? | No documented path. `lighting/global.json` + `local_lighting/local_lighting.json` only affect **Vibrant Visuals / deferred**, not legacy lighting | `[confirmed]` (MS Learn) |
| Can an RP change block-light vs sky-light tint separately in legacy? | No. Only global post-process via `color_grading/color_grading.json` (whole image) or shader replacement | `[inferred]` |
| Entity shadow | Legacy Bedrock drew a stencil-overlay pass with material `shadow_overlay` (`shaders/shadow_stencil_overlay.fragment`); that material mechanism is dead under RenderDragon. No shadow texture exists in the Bedrock sample RP (Java's does) | `[confirmed]` / `[inferred]` |
| AO | Per-vertex smooth lighting + AO baked CPU-side into chunk vertex colours; no legacy SSAO symbol found. VV adds normal/depth-based deferred shading | `[confirmed]` / `[unverified]` for SSAO absence |
| Safest layer | RP-only (VV JSON, color grading) = safe but limited; shader `.material.bin` replacement = medium risk, works in legacy; native LeviLamina hook = max control, version-fragile, several symbols gated behind MCAPI access requests | `[confirmed]` |

---

## 1. Pipeline map (what is data-driven and where)

RenderDragon is the only Bedrock renderer on Windows since 1.16.200; it supports a deferred path (Vibrant Visuals) on D3D12 FL12_1+, otherwise the classic forward path runs ([Minecraft Wiki: RenderDragon](https://minecraft.wiki/w/RenderDragon)).

| Asset family | Location | Moddable by resource pack? |
|---|---|---|
| RenderDragon compiled shaders | `<install>/data/renderer/materials/*.material.bin` | **No** — game reads from install dir. Replaced by file patching (Matject) or in-memory redirect (BetterRenderDragon) |
| Shader source (bgfx `.sc`) | n/a in install | Only via recompiled `.material.bin` ([MaterialBinTool](https://raw.githubusercontent.com/ddf8196/MaterialBinTool/main/README.md)) |
| Lightmap | runtime `mce::Image`, sampled as `s_LightMapTexture` | No file-based override found |
| Vibrant Visuals lighting | `lighting/global.json`, `local_lighting/local_lighting.json`, `pbr/global.json` in RP | **Yes**, but only when Vibrant Visuals/deferred is active ([MS Learn: Light Sources](https://learn.microsoft.com/en-us/minecraft/creator/documents/vibrantvisuals/lightingcustomization?view=minecraft-bedrock-stable)) |
| Vibrant Visuals shadow style | `shadows/global.json` in RP | **Yes**, VV only ([MS Learn: Shadow Customization](https://learn.microsoft.com/en-us/minecraft/creator/documents/vibrantvisuals/shadowscustomization?view=minecraft-bedrock-stable)) |
| Post-process colour grading | `color_grading/color_grading.json` (+ per-biome variants) | **Yes** (present in vanilla sample RP; exact activation conditions `[unverified]`) |
| Entity shadow texture | **absent** from Bedrock sample RP (`textures/misc` contains only glint/missing_texture/nausea/pumpkinblur) | No |

Key quote showing that shader binaries are loaded from the install tree, not the pack stack — BetterRenderDragon hooks `readFile` and redirects only `/data/renderer/materials/*.material.bin` to `renderer/materials/...` inside a resource pack:

```cpp
if (p.find("/data/renderer/materials/") != std::string::npos &&
    strncmp(p.c_str() + p.size() - 13, ".material.bin", 13) == 0) {
    std::string binPath = "renderer/materials/" + p.substr(p.find_last_of('/') + 1);
    ...
    bool success = ResourcePackManager_load(resourcePackManager, location, out);
```

Source: [BetterRenderDragon/MCHooks.cpp](https://raw.githubusercontent.com/ddf8196/BetterRenderDragon/main/BetterRenderDragon/MCHooks.cpp). Matject confirms the on-disk location it patches: `%ProgramFiles%\WindowsApps\Microsoft.Minecraft*...\data\renderer\materials` ([matject README](https://raw.githubusercontent.com/faizul726/matject/main/README.md)).

---

## 2. Lightmap

### 2.1 It is a CPU-side procedural texture (`ptexture` subsystem)

LeviLamina's generated client headers expose the builder class at `src-client/mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h`:

```cpp
class BaseLightTextureImageBuilder {
public:
    ::ll::TypedStorage<8, 8, ::Dimension const*> mDimension;
    virtual void init(::Dimension* d);
    virtual bool buildImage(::BaseLightData const& lightData, ::mce::Image* targetImage,
                            uint imageLength, float a, float ambientBoost, bool clampToMinimum);
    virtual bool buildImageSplit(::BaseLightData const&, ::mce::Image*, uint, float, bool);
    virtual ::std::unique_ptr<::BaseLightData> createBaseLightTextureData(
        ::IClientInstance* client, ::BaseLightData const& currentData);
};
```

Sources: [BaseLightTextureImageBuilder class](https://docs.levimc.org/LeviLamina/api/dc/d8a/classBaseLightTextureImageBuilder.html) and [header source page](https://docs.levimc.org/LeviLamina/api/d8/d69/src-client_2mc_2client_2renderer_2ptexture_2BaseLightTextureImageBuilder_8h_source.html).

There is at least one per-dimension subclass, file `src-client/mc/client/world/level/dimension/NetherLightTextureImageBuilder.h`, which overrides `createBaseLightTextureData` and inherits `buildImage`/`buildImageSplit` ([NetherLightTextureImageBuilder](https://docs.levimc.org/LeviLamina/api/d1/dd9/classNetherLightTextureImageBuilder.html)). The `ptexture` folder name and the `mce::Image*` output make this **procedural generation of the lightmap image on the CPU**, not a file lookup. `[inferred]` Overworld/End have analogous builders in the same folder (only the Nether one surfaced in the public docs).

### 2.2 How the lightmap is consumed (per-vertex UV, then texture sample)

Block geometry carries a **lightmap UV as `a_texcoord1`**:

```glsl
v_texcoord0 = a_texcoord0;
v_lightmapUV = a_texcoord1;
```

Source: [RenderChunk.vertex.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/RenderChunk/src/RenderChunk.vertex.sc).

The chunk fragment shader samples the lightmap and multiplies it into albedo (slot 2):

```glsl
SAMPLER2D(s_LightMapTexture, 2);
...
diffuse.rgb *= texture2D(s_LightMapTexture, v_lightmapUV).rgb;
```

Source: [RenderChunk.fragment.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/RenderChunk/src/RenderChunk.fragment.sc). The same shader also multiplies `v_color0` (per-vertex tint / smooth-lighting-AO term) into the diffuse — i.e. **lightmap lookup and AO/tint are two independent vertex-borne factors**. `[confirmed]`

Because the UV is 2D, the lightmap texture is indexed along **two axes** (block light and sky light). `[inferred]` from a 2-component UV plus the two independent `BaseLightData` inputs.

Community shader packs confirm the semantics: Newb X computes its own lighting from `uv1` where `uv1.x` is treated as block-light fraction and `uv1.y` as sky-light fraction:

```glsl
float torchAttenuation = (NL_TORCH_INTENSITY*uv1.x)/(0.5-0.45*lit.x);
...
light = vec3_splat((1.35+NL_CAVE_BRIGHTNESS)*(1.0-uv1.x)*(1.0-uv1.y));
light += mix(horizonCol,zenithCol,...)*(lit.y*(3.0-2.0*uv1.y)*...);
```

Source: [newb-x-morgi `functions/lighting.h`](https://raw.githubusercontent.com/faizul726/newb-x-morgi/main/src/newb/functions/lighting.h).

### 2.3 Can a resource pack change lightmap colours?

| Route | Works in legacy mode? | Effect | Source |
|---|---|---|---|
| `textures/lightmap/*` | **No such path** — the vanilla Bedrock sample RP has no `lightmap` folder under `textures/` (listed dirs: blocks, colormap, entity, environment, gui, items, map, misc, models, painting, particle, particles, persona_thumbnails, trims, ui) | — | [resource_pack/textures listing](https://api.github.com/repos/Mojang/bedrock-samples/contents/resource_pack/textures) |
| `textures/misc/shadow.png`-style hidden texture | Not present; `textures/misc` has 5 files only | — | [textures/misc listing](https://api.github.com/repos/Mojang/bedrock-samples/contents/resource_pack/textures/misc) |
| `lighting/global.json`, `local_lighting/local_lighting.json` | **No** — Vibrant Visuals only | Sun/moon colour + illuminance (lux), emissive desaturation, ambient colour/lux, sky intensity; per-biome files; block `light_color` and `light_type` (`static_light` / `point_light`) | [MS Learn: Light Sources](https://learn.microsoft.com/en-us/minecraft/creator/documents/vibrantvisuals/lightingcustomization?view=minecraft-bedrock-stable) |
| Vanilla VV values | — | Sun 100 lux day / 0.4 lux moon, moon colour `[163,182,255]`, ambient `#FFFFFF` 0.02 lux, End flash `[228,93,255]` | [bedrock-samples `lighting/global.json`](https://raw.githubusercontent.com/Mojang/bedrock-samples/main/resource_pack/lighting/global.json) |
| `color_grading/color_grading.json` + per-biome variants | `[unverified]` | Whole-screen post-process colour curve — can cool shadows / warm highlights globally but cannot separate block vs sky light | [color_grading listing](https://api.github.com/repos/Mojang/bedrock-samples/contents/resource_pack/color_grading) |
| `.material.bin` shader replacement | **Yes** (that is what shader packs do) | Full control: replace lightmap sampling with arbitrary colour curves | [MaterialBinTool](https://raw.githubusercontent.com/ddf8196/MaterialBinTool/main/README.md), [BetterRenderDragon](https://github.com/ddf8196/BetterRenderDragon/) |
| Native hook of the lightmap builder | Yes | Rewrite the generated image / light data directly | [BaseLightTextureImageBuilder](https://docs.levimc.org/LeviLamina/api/dc/d8a/classBaseLightTextureImageBuilder.html) |

So: **Bedrock's legacy lightmap is engine-generated and not resource-pack configurable**; the only file-level levers are (a) VV JSON (deferred only) and (b) shader replacement (not an RP mechanism on its own).

### 2.4 Bedrock vs Java: how block light and sky light are tinted

| Aspect | Java Edition | Bedrock 1.26 legacy | Bedrock Vibrant Visuals |
|---|---|---|---|
| Generation | Runtime `LightTexture`, 16×16 image indexed by (blockLight, skyLight); since 1.21.2 rendered by a shader instead of hardcoded code ([Minecraft Wiki: Light, history](https://minecraft.wiki/w/Light?action=raw)) | Runtime procedural `mce::Image` via `BaseLightTextureImageBuilder`; per-dimension builders ([LeviLamina docs](https://docs.levimc.org/LeviLamina/api/dc/d8a/classBaseLightTextureImageBuilder.html)) | Deferred HDR lighting; no lightmap in the classic sense — block light becomes lux-valued indirect diffuse (1 block-light unit = 90.190359 lumen), sky light a separate white indirect term, sun/moon/ambient coloured ([Minecraft Wiki: Light §Vibrant Visuals](https://minecraft.wiki/w/Light?action=raw)) |
| Separate block/sky colour control | Yes — a Java mod exposing it names the knobs `sky_light_color` and `block_light_color` multipliers and states the vanilla lightmap is "one of the more stubborn hardcoded features" ([ElysiumAPI Lightmap Settings](https://github-wiki-see.page/m/ThatJadenXgamer/ElysiumAPI/wiki/Lightmap-Settings)) | Not exposed; single combined texture, colours baked by the builder. `[inferred]` two-axis image still allows different hues per light type if you control generation | Yes — `local_lighting/local_lighting.json` per-block `light_color`, `directional_lights` sun/moon colours |
| Typical look | Warm block light (torch orange) + cool sky light (biome sky tint), separate curves | Neutral/blue-ish darkening; block light reads as a white-ish warm-white, sky light as neutral day tint — **no independently authorable warm/cool split** | Sun colour is keyframed warm→white; moon fixed `[163,182,255]` cool blue; ambient white |

**Conclusion:** the Java "warm block light + cool sky light" split is *architecturally possible* in Bedrock (the lightmap is generated from two light channels on the CPU) but is **not exposed to resource packs**; it must be authored either in a replacement shader or by intercepting the CPU-side lightmap image generation. `[inferred, supported by sources above]`

---

## 3. Entity shadows

### 3.1 Legacy (pre-RenderDragon) mechanism — stencil overlay pass

Bedrock Wiki documents the old material used for entity shadows, quoted verbatim:

```json
"shadow_overlay":{
    "+states":["DisableDepthTest","DisableCulling","Blending","EnableStencilTest"],
    "vertexShader":"shaders/color.vertex",
    "vrGeometryShader":"shaders/color.geometry",
    "fragmentShader":"shaders/shadow_stencil_overlay.fragment",
    "blendSrc":"DestColor","blendDst":"Zero",
    "frontFace":{"stencilFunc":"Equal","stencilPass":"Replace"}
}
```

Source: [Bedrock Wiki: Remove Entity Shadows §Using Materials](https://wiki.bedrock.dev/visuals/remove-shadows).

That is a **separate stencil-tested, multiply-blended overlay pass** (`blendSrc: DestColor, blendDst: Zero` is a multiply), not part of the entity's own lit material. The same page states the material folder is no longer loaded: *"With the advent of render-dragon, materials like this no longer function."* `[confirmed]`

Shadow **extent is derived from the entity's collision box**, not from a texture:

* shrinking `minecraft:collision_box` makes the shadow disappear (with a `minecraft:custom_hit_test` workaround for interaction),
* teleporting the entity `~ ~-0.01 ~` into the ground stops the shadow from showing,
* certain runtime identifiers have no/very small shadows.

Source: same page ([Bedrock Wiki](https://wiki.bedrock.dev/visuals/remove-shadows)). `[confirmed]`

### 3.2 RenderDragon era (1.26 legacy mode)

| Question | Finding | Confidence |
|---|---|---|
| Is there a `textures/misc/shadow.png` in Bedrock? | No. The vanilla sample RP's `textures/misc` has only `enchanted_actor_glint.png`, `enchanted_item_glint.png`, `missing_texture.png`, `nausea.png`, `pumpkinblur.png` | `[confirmed]` ([listing](https://api.github.com/repos/Mojang/bedrock-samples/contents/resource_pack/textures/misc)) |
| Can an RP change shadow opacity/size? | No documented RP field. Entity JSON has no `"shadow"` component in current docs; `render_controllers` control geometry/materials, and under RD the old material override is rejected | `[confirmed]` for RP rejection; `[unverified]` that no entity JSON field exists anywhere |
| Is it a separate pass? | In legacy RD it is still a separate draw (historically stencil overlay). The compiled material set is engine-internal, so the pass name is not authorable | `[inferred]` |
| Can a native hook change it? | Yes in principle — intercept the actor render path and suppress/modify the shadow draw, or add your own blob quad. `BaseActorRenderContext` exposes `mEntityRenderDispatcher` (`std::shared_ptr<ActorRenderDispatcher>`) and per-frame flags `mIgnoresLighting`, `mForceLightingOn` | `[confirmed]` ([BaseActorRenderContext](https://docs.levimc.org/LeviLamina/api/da/d5a/classBaseActorRenderContext.html)) |
| Can VV change it? | Yes, cosmetically: `shadows/global.json` → `"shadow_style": "blocky_shadows" | "soft_shadows"` plus `"texel_size"` (default soft; texel size defaults to 16) | `[confirmed]` ([MS Learn: Shadow Customization](https://learn.microsoft.com/en-us/minecraft/creator/documents/vibrantvisuals/shadowscustomization?view=minecraft-bedrock-stable)) |

VV shadows are shadow-mapped with cascades — the client header `LevelRendererCamera::LevelRendererDebugInformation` contains `cascadeCameras` / `lastCascadeCameras` (`std::vector<mce::Camera>`) alongside frustum edges, which is the signature of a cascaded shadow-map setup: [LevelRendererDebugInformation](https://docs.levimc.org/LeviLamina/api/d3/d11/structLevelRendererCamera_1_1LevelRendererDebugInformation.html). `[inferred]`

### 3.3 Java comparison

Java draws a **soft, radially faded blob** per entity: a textured quad on the ground whose size scales with entity width/shadow radius, drawn by the entity render dispatcher as its own render type. The texture is a normal vanilla asset that resource packs can replace — Mojira issue **MC-182455 "Entity shadow texture glitching with resource pack"** describes exactly that interaction ([bugs-legacy.mojang.com/browse/MC-182455](https://bugs-legacy.mojang.com/browse/MC-182455)). *Note: that issue page could not be opened from this session (fetch failed); URL is cited from the search listing, and the rest of the Java description is `[unverified]` detail-level.*

Visible differences to close:

| Property | Java | Bedrock 1.26 legacy |
|---|---|---|
| Shape | Circular/elliptical, alpha falloff | Hard-edged quad/geometry from the collision box (blocky, snaps to blocks in VV by default only if `blocky_shadows` chosen; VV default is soft) |
| Texture | `misc/shadow.png`, RP-replaceable | No RP texture |
| Opacity control | via texture alpha | Not exposed |
| Multi-entity blending | additive-ish multiply, soft overlap | stencil multiply overlay; overlap behaviour differs `[unverified]` |

---

## 4. Ambient occlusion / smooth lighting

* Bedrock has a user-facing **Smooth Lighting** toggle: added in v0.7.0, removed in v0.8.0, re-added in 1.1.0 ([Minecraft Wiki: Light, Bedrock history](https://minecraft.wiki/w/Light?action=raw)).
* AO in Minecraft (both editions in the wiki's general description) is **per-corner computation on block faces, then blurred across the face**: "For each corner of a side, the game checks diagonally for full blocks ... The brightnesses of the four corners are then blurred across the whole side and are applied to the block's texture." ([Minecraft Wiki: Light §Ambient occlusion](https://minecraft.wiki/w/Light?action=raw)).
* In Bedrock this lands in the chunk mesh as **vertex data**: the RenderChunk shader multiplies `v_color0` (per-vertex colour = tint/AO) and samples the lightmap with a per-vertex UV. There is no normal in the RenderChunk vertex format (`a_color0, a_position, a_texcoord0, a_texcoord1`), so no per-pixel directional/AO term exists in the legacy path ([RenderChunk.varying.def.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/RenderChunk/src/RenderChunk.varying.def.sc), [RenderChunk.vertex.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/RenderChunk/src/RenderChunk.vertex.sc)). `[confirmed]`
* **Actors do not use the lightmap at all in legacy RD.** `Actor.vertex.sc` computes a per-vertex pseudo-lighting term from the vertex normal:

```glsl
float lightIntensity = calculateLightIntensity(World, vec4(a_normal.xyz, 0.0), TileLightColor);
lightIntensity += OverlayColor.a * 0.35;
vec4 light = vec4(lightIntensity * TileLightColor.rgb, 1.0);
```

with ([DynamicUtil.dragonh](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/include/MinecraftRenderer.Materials/DynamicUtil.dragonh)):

```glsl
const float AMBIENT = 0.45; const float XFAC = -0.1; const float ZFAC = 0.1;
float yLight = (1.0 + N.y) * 0.5;
return yLight * (1.0 - AMBIENT) + N.x*N.x*XFAC + N.z*N.z*ZFAC + AMBIENT;
```

Sources: [Actor.vertex.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/Actor/src/Actor.vertex.sc), [Actor.fragment.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/Actor/src/Actor.fragment.sc). Consequences: mob lighting is a **fake directional shade** driven by `TileLightColor` (per-actor CPU light colour/intensity) and a constant 0.45 ambient floor — visibly flatter and more "plasticky" than Java, and with no smooth-light AO at all. `[confirmed for shader; inferred for visual consequence]`

* **Vibrant Visuals** replaces this with deferred shading: "The game calculates surface normals and depth to shade the albedo color image in a smooth way similar to smooth lighting, which is applied to any object in the game except transparencies like water." ([Minecraft Wiki: Light §Deferred shading](https://minecraft.wiki/w/Light?action=raw)). `[confirmed]`
* **No `ScreenSpaceAmbientOcclusion`-named class was found** in the public LeviLamina client API pages searched; absence is not proof, so a legacy SSAO implementation inside RenderDragon is `[unverified]`. The named classes found are `BaseLightTextureImageBuilder` (lights) and `LevelRendererCamera` (cascades); `ActorRenderCandidates` exists but its header is an empty forward-declared stub in the docs ([ActorRenderCandidates.h](https://docs.levimc.org/LeviLamina/api/d8/d7a/ActorRenderCandidates_8h_source.html)).

**Net difference to close:** Java = per-vertex smooth light + AO from a warm/cool lightmap, with entity lighting also sampled from the lightmap; Bedrock legacy = per-vertex lightmap UV + vertex-colour AO for blocks, but a separate flat normal-based fake light for entities.

---

## 5. Ground truth: concrete symbols seen in public sources

### 5.1 RenderDragon engine internals (BetterRenderDragon hooks)

All from [MCHooks.cpp](https://raw.githubusercontent.com/ddf8196/BetterRenderDragon/main/BetterRenderDragon/MCHooks.cpp) and [MCPatches.cpp](https://raw.githubusercontent.com/ddf8196/BetterRenderDragon/main/BetterRenderDragon/MCPatches.cpp):

| Symbol | Used for |
|---|---|
| `RayTracingOptions::isRayTracingAvailable` / `isDeferredShadingAvailable` / `getLightingModel` / `setLightingModel` (vtable slots 8/9/10) | Force deferred rendering on/off |
| `dragon::rendering::LightingModels` (`Vanilla`, `Deferred`) | Lighting-model enum |
| `isLightingModelSupported` | Feature gate |
| `dragon::materials::MaterialUniformMap::setUniform<glm::vec4>` | Uniform access |
| `dragon::materials::MaterialResourceManager` (`forceTrim`), `CompiledMaterialManager::freeShaderBlobs` | Shader cache control / hot reload |
| `ResourcePackManager::ResourcePackManager` + `ResourcePackManager::load` + `readFile` | Load `.material.bin` from a resource pack |
| `mce::framebuilder::BgfxFrameBuilder::endFrame` / `discardFrame` | Safe reload point |
| `dragon::bgfximpl::toSamplerFlags` | GPU workarounds |
| `MinecraftGame::_updateLightingModel` | Force lighting model at game level |

Also relevant: `include/RenderDragon/Materials/MaterialResourceManager.h`, `MaterialUniformName.h`, `Rendering/LightingModels.h` in the same repo ([tree](https://api.github.com/repos/ddf8196/BetterRenderDragon/git/trees/main?recursive=1)). `[confirmed]`

### 5.2 RenderDragon shader-level names (decompiled/reconstructed sources)

| Symbol | Meaning | Source |
|---|---|---|
| `s_LightMapTexture` (slot 2), `v_lightmapUV`, `a_texcoord1` | block lightmap sampling | [RenderChunk.fragment.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/RenderChunk/src/RenderChunk.fragment.sc) |
| `TileLightColor`, `v_light`, `calculateLightIntensity`, `applyLighting`, `applyActorDiffuse` | actor lighting | [Actor.vertex.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/Actor/src/Actor.vertex.sc), [ActorUtil.dragonh](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/include/MinecraftRenderer.Materials/ActorUtil.dragonh) |
| `LightDiffuseColorAndIlluminance`, `LightWorldSpaceDirection` uniforms | directional light in actor shaders | [Actor.fragment.sc](https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/Actor/src/Actor.fragment.sc) |
| `DeferredShading`, `LightClustering`, `RenderChunkPrepass`, `ToneMapping`, `LuminanceHistogram`, `Upscaling` materials | the deferred/VV pass set | [RenderDragonSourceCodeInv tree](https://api.github.com/repos/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/git/trees/main?recursive=1) |

Shader pack `Actor` config also exposes `ColorBased`, `OverlayColor`, `MultiplicativeTintColor`, `MatColor` uniforms ([newb Actor config.json](https://github.com/faizul726/newb-x-morgi)) — the same uniforms a native hook could animate.

### 5.3 LeviLamina client API (26.51.x) — what a native mod can address

| Class / path | Notes | Source |
|---|---|---|
| `mc/client/renderer/ptexture/BaseLightTextureImageBuilder` | lightmap image builder, virtuals | [docs](https://docs.levimc.org/LeviLamina/api/dc/d8a/classBaseLightTextureImageBuilder.html) |
| `mc/client/world/level/dimension/NetherLightTextureImageBuilder` | per-dimension subclass | [docs](https://docs.levimc.org/LeviLamina/api/d1/dd9/classNetherLightTextureImageBuilder.html) |
| `mc/client/renderer/game/LevelRendererCamera` (+ `LevelRendererDebugInformation::cascadeCameras`) | camera/cascade management | [docs](https://docs.levimc.org/LeviLamina/api/d3/d11/structLevelRendererCamera_1_1LevelRendererDebugInformation.html) |
| `mc/client/renderer/BaseActorRenderContext` (`mEntityRenderDispatcher`, `mBlockEntityRenderDispatcher`, `mIgnoresLighting`, `mForceLightingOn`, `mItemInHandRenderer`, `mScreenContext`) | per-frame actor render context | [docs](https://docs.levimc.org/LeviLamina/api/da/d5a/classBaseActorRenderContext.html) |
| `ActorRenderDispatcher`, `ActorBlockRenderer`, `DataDrivenRenderer`, `ScreenContext`, `IClientInstance`, `LightPropagation::LightVolumeManager` | entity rendering / light propagation | same page |
| `mce::ShaderGroup`, `mce::Image` | shader and image plumbing | [ShaderGroup](https://docs.levimc.org/LeviLamina/api/d4/d00/classmce_1_1ShaderGroup.html), [Image](https://docs.levimc.org/LeviLamina/api/d4/daa/structmce_1_1Image.html) |
| Hook macros `LL_AUTO_TYPE_INSTANCE_HOOK`, `LL_TYPE_STATIC_HOOK`, … keyed by decorated name, byte pattern, or `&Class::$func` | native hook mechanism | [Hook Guide](https://raw.githubusercontent.com/LiteLDev/LeviLamina/main/docs/main/contents/developer_guides/how_to_guides/hook_guide.md) |

**Important caveat:** many of these client SDK functions are declared `MCNAPI`/`MCAPI` and the docs carry the banner *"This function is not yet publicly available. To request access: reference this function in your code and attempt to link ... Submit a access request at github.com/LiteLDev/mcapi-requests"* (e.g. `$buildImage`, `$createBaseLightTextureData`, `$vftable`). So linking against them may require an MCAPI request, or you hook by signature/vtable instead, BRD-style. `[confirmed]`

Naming not found in public LeviLamina docs: `LightTexture` (only `*LightTextureImageBuilder`), `LevelRenderer` (only `LevelRendererCamera`), `Shadow`/`ShadowRenderer`, `ScreenSpaceAmbientOcclusion`. Do **not** assume those class names exist in this build; treat them as `[unverified]`.

---

## 6. Feasibility on RenderDragon and safest modding layer

| Layer | What it can change | Risk | Version fragility | Notes |
|---|---|---|---|---|
| **Resource pack (VV JSON only)** | VV shadow style (blocky/soft + texel size), sun/moon/ambient colour + lux, per-block light colour, PBR/emissive, post-process colour grading | Very low | Stable, documented schema keyed by `format_version` | Requires Vibrant Visuals active (D3D12 FL12_1) for lighting/shadow keys; does nothing for legacy-mode lightmap/shadow |
| **`color_grading` RP post-process** | Whole-screen tint/contrast/tonemap | Low | Low | Cannot separate block vs sky light; `[unverified]` whether it is applied outside VV |
| **`.material.bin` replacement (Matject-style file swap / BRD in-memory redirect)** | Arbitrary shader math: lightmap curves, warm/cool split, AO/actor lighting, shadow blending | Medium (bad shader = red sky/crash; AV/WindowsApps unlock) | High — needs per-version material compatibility checks, recompile on shader ABI changes | This is how all "Java lighting" Bedrock shader packs work ([MaterialBinTool](https://raw.githubusercontent.com/ddf8196/MaterialBinTool/main/README.md), [matject](https://raw.githubusercontent.com/faizul726/matject/main/README.md)) |
| **Native hook in LeviLamina client 26.51.x** | Replace lightmap image/data generation, hijack actor light colour, suppress/replace shadow draw, draw custom shadow quads | High (ABI/vtable assumptions, crash = save risk; hook guide explicitly warns) | Very high — signatures/offsets change each build | Best control; combine with Sig scanning ([BRD approach](https://raw.githubusercontent.com/ddf8196/BetterRenderDragon/main/BetterRenderDragon/MCHooks.cpp), [Hook Guide](https://raw.githubusercontent.com/LiteLDev/LeviLamina/main/docs/main/contents/developer_guides/how_to_guides/hook_guide.md)) |

**Feasibility verdict:** CPU-side lightmap control **is** feasible on RenderDragon because the lightmap is generated on the CPU into an `mce::Image` and consumed as a texture; but no data-only RP hook exists in legacy mode, so achieving it *without* shader replacement requires a native hook. `[inferred from confirmed sources]`

---

## 7. Top actionable hook points (ranked)

### #1 — Intercept lightmap generation (`BaseLightTextureImageBuilder::buildImage` / `createBaseLightTextureData`)
- **What to do:** post-process (or fully rewrite) the `mce::Image` lightmap after the vanilla builder writes it, or hook `createBaseLightTextureData` to return a `BaseLightData` tuned for Java-style curves. Optionally specialise per dimension by also targeting the Nether/End builders.
- **Risk:** Medium-High. Virtuals live behind MCAPI gates (`$buildImage`, `$vftable` "not yet publicly available"); plan on a vtable hook / signature scan. Image format/stride must be validated per build.
- **Effort:** Medium (one hook + pixel math), plus in-game calibration against Java screenshots.
- **Visual change:** Warm orange block light, cool blue sky light, Java-like low-light gamma — applies to **all block geometry at once**, since every chunk samples the same `s_LightMapTexture`. Highest parity-per-line-of-code.

### #2 — Replace `RenderChunk` (+ `Actor`) shader materials, BRD/Matject style
- **What to do:** recompile `RenderChunk.material.bin` to compute light colour from `v_lightmapUV` (warm block axis / cool sky axis, gamma curve), and `Actor` materials to replace `calculateLightIntensity`'s hardcoded `AMBIENT=0.45` fake shade with a lightmap-like lookup driven by `TileLightColor`.
- **Risk:** Medium. Shader errors are visible but recoverable; distribution needs either BRD, a file patcher, or an unlocked install.
- **Effort:** Medium. All tooling exists (MaterialBinTool, RenderDragonSourceCodeInv headers); the hard part is maintaining shader ABI across patches.
- **Visual change:** Per-pixel Java-like lightmap tint **plus** entity lighting that matches blocks (removes the flat/plasticky mob look). Also the cheapest place to add a stronger AO/crease term.

### #3 — Entity shadow: soft blob via VV JSON first, native replacement second
- **What to do (low risk):** ship `shadows/global.json` with `"shadow_style": "soft_shadows"` (default) or `"blocky_shadows"` + `texel_size` to at least stop blocky shadows — zero code, VV-only.
- **What to do (real parity):** hook the actor render path around `BaseActorRenderContext`/`ActorRenderDispatcher` to suppress the engine shadow draw and render your own alpha-gradient quad (Java's soft disc), driven by entity width. `mEntityRenderDispatcher` and `mIgnoresLighting`/`mForceLightingOn` are exposed.
- **Risk:** Low for the JSON route (deferred only); High for the native quad route (draw-call/state management, depth test ordering).
- **Effort:** Low / High.
- **Visual change:** JSON route = softer shadow edges and pixel-snapping control; native route = Java-style circular faded shadow whose size/opacity you author.

---

## 8. Open questions / failed accesses

1. **Exact lightmap format and curves** (dimensions, how `imageLength`, `ambientBoost`, `clampToMinimum` map to pixels) — not documented publicly. Must be measured in-game or read from the binary.
2. **Whether the legacy stencil shadow pass still exists in the 1.26 RenderDragon build** and whether it can still be disabled via material state — the bedrock.dev material method is explicitly dead, but the internal pass name is unknown.
3. **`color_grading` activation conditions** — file exists in the vanilla sample RP; whether it applies in legacy (non-VV) graphics mode is `[unverified]`.
4. **Unreachable pages:** minecraftforum.net's "Lightmaps on bedrock?" thread returned HTTP 403 (Cloudflare); `bugs-legacy.mojang.com/browse/MC-182455` and a DeepWiki page failed to fetch from this session. No claims above depend solely on those.
5. **No binary inspection** was performed: symbol presence/absence in 1.26.51.01 binaries (e.g. `LightTexture`, `ShadowRenderer`, `ScreenSpaceAmbientOcclusion`) is unconfirmed either way; only the LeviLamina public header surface was checked.

---

## 9. Sources

1. Minecraft Wiki — RenderDragon: https://minecraft.wiki/w/RenderDragon
2. Minecraft Wiki — Light (raw, includes AO, smooth lighting, VV illumination, 1.21.2 lightmap-shader history): https://minecraft.wiki/w/Light?action=raw
3. MS Learn — Vibrant Visuals: Shadow Customization: https://learn.microsoft.com/en-us/minecraft/creator/documents/vibrantvisuals/shadowscustomization?view=minecraft-bedrock-stable
4. MS Learn — Vibrant Visuals: Light Sources: https://learn.microsoft.com/en-us/minecraft/creator/documents/vibrantvisuals/lightingcustomization?view=minecraft-bedrock-stable
5. Mojang bedrock-samples — `lighting/global.json`: https://raw.githubusercontent.com/Mojang/bedrock-samples/main/resource_pack/lighting/global.json
6. Mojang bedrock-samples — `textures/misc` listing: https://api.github.com/repos/Mojang/bedrock-samples/contents/resource_pack/textures/misc
7. Mojang bedrock-samples — `textures` listing: https://api.github.com/repos/Mojang/bedrock-samples/contents/resource_pack/textures
8. Mojang bedrock-samples — `color_grading` listing: https://api.github.com/repos/Mojang/bedrock-samples/contents/resource_pack/color_grading
9. BetterRenderDragon — README: https://github.com/ddf8196/BetterRenderDragon/
10. BetterRenderDragon — `MCHooks.cpp`: https://raw.githubusercontent.com/ddf8196/BetterRenderDragon/main/BetterRenderDragon/MCHooks.cpp
11. BetterRenderDragon — `MCPatches.cpp`: https://raw.githubusercontent.com/ddf8196/BetterRenderDragon/main/BetterRenderDragon/MCPatches.cpp
12. MaterialBinTool — README: https://raw.githubusercontent.com/ddf8196/MaterialBinTool/main/README.md
13. Matject — README: https://raw.githubusercontent.com/faizul726/matject/main/README.md
14. RenderDragonSourceCodeInv — `RenderChunk.fragment.sc`: https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/RenderChunk/src/RenderChunk.fragment.sc
15. RenderDragonSourceCodeInv — `RenderChunk.vertex.sc`: https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/RenderChunk/src/RenderChunk.vertex.sc
16. RenderDragonSourceCodeInv — `RenderChunk.varying.def.sc`: https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/RenderChunk/src/RenderChunk.varying.def.sc
17. RenderDragonSourceCodeInv — `Actor.vertex.sc`: https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/Actor/src/Actor.vertex.sc
18. RenderDragonSourceCodeInv — `Actor.fragment.sc`: https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/materials/Actor/src/Actor.fragment.sc
19. RenderDragonSourceCodeInv — `DynamicUtil.dragonh`: https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/include/MinecraftRenderer.Materials/DynamicUtil.dragonh
20. RenderDragonSourceCodeInv — `ActorUtil.dragonh`: https://raw.githubusercontent.com/SurvivalApparatusCommunication/RenderDragonSourceCodeInv/main/include/MinecraftRenderer.Materials/ActorUtil.dragonh
21. Bedrock Wiki — Remove Entity Shadows: https://wiki.bedrock.dev/visuals/remove-shadows
22. newb-x-morgi — shader pack `functions/lighting.h`: https://raw.githubusercontent.com/faizul726/newb-x-morgi/main/src/newb/functions/lighting.h
23. LeviLamina API — `BaseLightTextureImageBuilder`: https://docs.levimc.org/LeviLamina/api/dc/d8a/classBaseLightTextureImageBuilder.html
24. LeviLamina API — `BaseLightTextureImageBuilder.h` source: https://docs.levimc.org/LeviLamina/api/d8/d69/src-client_2mc_2client_2renderer_2ptexture_2BaseLightTextureImageBuilder_8h_source.html
25. LeviLamina API — `NetherLightTextureImageBuilder`: https://docs.levimc.org/LeviLamina/api/d1/dd9/classNetherLightTextureImageBuilder.html
26. LeviLamina API — `BaseActorRenderContext`: https://docs.levimc.org/LeviLamina/api/da/d5a/classBaseActorRenderContext.html
27. LeviLamina API — `LevelRendererCamera::LevelRendererDebugInformation`: https://docs.levimc.org/LeviLamina/api/d3/d11/structLevelRendererCamera_1_1LevelRendererDebugInformation.html
28. LeviLamina API — `ActorRenderCandidates.h`: https://docs.levimc.org/LeviLamina/api/d8/d7a/ActorRenderCandidates_8h_source.html
29. LeviLamina API — `mce::ShaderGroup`: https://docs.levimc.org/LeviLamina/api/d4/d00/classmce_1_1ShaderGroup.html
30. LeviLamina — Hook Guide: https://raw.githubusercontent.com/LiteLDev/LeviLamina/main/docs/main/contents/developer_guides/how_to_guides/hook_guide.md
31. LeviLamina — Supported Versions: https://raw.githubusercontent.com/liteldev/levilamina/main/docs/main/contents/versions.md
32. LeviLamina — Install client on Windows: https://raw.githubusercontent.com/liteldev/levilamina/5414a8e47d576818df2da663c556ba480711a3a9/docs/main/contents/user_guides/install_on_client.md
33. ElysiumAPI — Lightmap Settings (Java-side reference for LightTexture / sky_light_color / block_light_color): https://github-wiki-see.page/m/ThatJadenXgamer/ElysiumAPI/wiki/Lightmap-Settings
34. Mojira MC-182455 — "Entity shadow texture glitching with resource pack" (page fetch failed; cited from search listing): https://bugs-legacy.mojang.com/browse/MC-182455
