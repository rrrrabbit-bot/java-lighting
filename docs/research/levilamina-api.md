# LeviLamina 26.51.9 — Client Mod API Research

Target: **Windows x64 native client mod**, LeviLamina **26.51.9**, MCBE **1.26.51.01**, goal =
Java-like lighting/shadows. Sources checked 2026-11-03. Signatures are quoted with URLs;
**[inferred]** = deduction, **[unverified]** = not confirmed by a primary source. Version pairing:
`26.51.x | 1.26.51.1 | 1.26.51.1` (server | client) — [versions](https://docs.levimc.org/LeviLamina/versions/).

## 1. Client mod packaging and loading

| Item | Server | Client |
| --- | --- | --- |
| Install dir | `plugins/<id>/` (`plugins` links to `mods`) | `mods/<id>/` |
| Installer | `lip install` | LeviLauncher (drives lip) |
| Build flag | `--target_type=server` | `--target_type=client` |
| Package define | `LL_PLAT_S` | `LL_PLAT_C` |

> "A mod is a package with a `mod` key. The whole archive is written to `mods/<id>/`, on a server
> (by lip) as on a client (by launchers such as LeviLauncher)."
> — [lip, Creating Packages](https://docs.levimc.org/lip/guides/creating_packages.html)

The package adds `LL_PLAT_S`/`LL_PLAT_C` per `target_type` and installs the client build with
`{"--target_type=client"}` — [xmake-repo `levilamina/xmake.lua`](https://github.com/LiteLDev/xmake-repo/blob/main/packages/l/levilamina/xmake.lua).

Runtime nuance: since 26.10 the client enables mods when *game startup* finishes, not on world load —
"please do not perform any game-related operations such as event subscriptions or command
registration inside the loading method" — [Create Your First Mod](https://docs.levimc.org/LeviLamina/developer_guides/tutorials/create_your_first_mod/).

## 2. `xmake.lua` skeleton (ready to copy)

Template structure ([`xmake.lua`](https://github.com/LiteLDev/levilamina-mod-template/blob/main/xmake.lua))
plus the version pin the tutorial requires ("we add `26.51.*` after `levilamina` as the version
requirement") and the client-only narrowing a real client mod uses.

```lua
add_rules("mode.debug", "mode.release")
add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("client")     -- server template defaults to "server"
    set_showmenu(true)
    set_values("client")      -- template allows {"server","client"}
option_end()

-- 26.51.x == MCBE 1.26.51.1 (client + server)
add_requires("levilamina 26.51.*", {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("MyClientMod") -- Change this to your mod name.
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags("/EHs", "-Wno-microsoft-cast", "-Wno-invalid-offsetof", "-Wno-c++2b-extensions",
                    "-Wno-microsoft-include", "-Wno-overloaded-virtual", "-Wno-ignored-qualifiers",
                    "-Wno-missing-field-initializers", "-Wno-potentially-evaluated-expression",
                    "-Wno-pragma-system-header-outside-header", {tools = {"clang_cl"}})
        set_toolchains("clang-cl")
    end
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    -- template also offers src-server/ / src-client/ include dirs per target_type
```

Toolchain: XMake 3.0.0+, `clang-cl`, MSVC 2022 SDK, C++20 + `_HAS_CXX23=1`; "client builds are
Windows-only". Build: `xmake f -a x64 -m release -p windows -v --target_type=client -y && xmake -v -y`
— [Build Guide](https://docs.levimc.org/LeviLamina/developer_guides/how_to_guides/build_guide/). CI
(template `build.yml`) runs a `server`/`client` matrix on `windows-latest` with LLVM 22.

## 3. Minimal client mod class

The template does **not** derive from `ll::mod::Mod`; it uses composition over `ll::mod::NativeMod`
and registers via `LL_REGISTER_MOD`.

`src/mod/MyMod.h` (verbatim):

```cpp
#pragma once
#include "ll/api/mod/NativeMod.h"

namespace my_mod {
class MyMod {
public:
    static MyMod& getInstance();
    MyMod() : mSelf(*ll::mod::NativeMod::current()) {}
    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }
    bool load();
    bool enable();
    bool disable();
    // bool unload();  // implement if you need unloading
private:
    ll::mod::NativeMod& mSelf;
};
} // namespace my_mod
```

`src/mod/MyMod.cpp` (verbatim):

```cpp
#include "mod/MyMod.h"
#include "ll/api/mod/RegisterHelper.h"

namespace my_mod {
MyMod& MyMod::getInstance() { static MyMod instance; return instance; }
bool MyMod::load()    { getSelf().getLogger().debug("Loading...");   return true; }
bool MyMod::enable()  { getSelf().getLogger().debug("Enabling...");  return true; }
bool MyMod::disable() { getSelf().getLogger().debug("Disabling..."); return true; }
} // namespace my_mod

LL_REGISTER_MOD(my_mod::MyMod, my_mod::MyMod::getInstance());
```

— [MyMod.h](https://github.com/LiteLDev/levilamina-mod-template/blob/main/src/mod/MyMod.h),
[MyMod.cpp](https://github.com/LiteLDev/levilamina-mod-template/blob/main/src/mod/MyMod.cpp)

Registration macro (exact; there is no `MOD_REGISTER` macro in this header):

```cpp
#define LL_REGISTER_MOD(CLAZZ, BINDER)                                                                                 \
    extern "C" {                                                                                                       \
    LL_SHARED_EXPORT bool ll_mod_load(ll::mod::NativeMod& self) {                                                      \
        static_assert(::ll::mod::Loadable<CLAZZ>, #CLAZZ " must be Loadable");                                         \
        ::ll::mod::bindToMod((BINDER), self);                                                                          \
        return (BINDER).load();                                                                                        \
    }                                                                                                                  \
    }
```

`bindToMod` wires `enable()`/`disable()`/`unload()` only when `bool name()` exists
(`Enableable`/`Disableable`/`Unloadable` concepts); `load()` is mandatory. Logger:
`LLNDAPI io::Logger& getLogger() const;` — [RegisterHelper.h](https://github.com/LiteLDev/LeviLamina/blob/main/src/ll/api/mod/RegisterHelper.h),
[Mod.h](https://github.com/LiteLDev/LeviLamina/blob/main/src/ll/api/mod/Mod.h). A shipping client mod
uses this exact shape — [LeviSchematic.cpp](https://github.com/LiteLDev/LeviSchematic/blob/main/src/levischematic/LeviSchematic.cpp).

## 4. Package metadata

**`tooth.json` (format 3, legacy but still the template's format).** Client variant = `"label": "client"`,
a `mods/<name>/` placement, and a `#client` dependency:

```json
{ "label": "client", "platform": "win-x64",
  "dependencies": { "github.com/LiteLDev/LeviLamina#client": "26.51.*" },
  "assets": [{ "type": "zip", "urls": ["https://{{tooth}}/releases/download/v{{version}}/MyMod-client-windows-x64.zip"],
    "placements": [{ "type": "dir", "src": "my-mod/", "dest": "mods/my-mod/" }] }] }
```

"lip's current package format is `manifest.json` (format 4)"; `lip init` emits
`{ "format": "bf45b1", "id": "MyMod", "version": "0.1.0", "dependencies": {}, "mod": { "loader": "native", "entry": "MyMod.dll" } }`.
A client package adds `"platform": "client"`, `"runtime": "win-x64"`, a `LeviLamina` semver dependency
and `targets` pointing at `.mcmod` assets. The template `manifest.json` is a build-time placeholder
(`${modName}`, `${modFile}`, `"type": "native"`, `${modPlatform}`) filled by the modpacker rule
— [template tooth.json](https://github.com/LiteLDev/levilamina-mod-template/blob/main/tooth.json),
[lip Creating Packages](https://docs.levimc.org/lip/guides/creating_packages.html).

## 5. Hooking — `ll/api/memory/Hook.h`

| Macro | Target | `TYPE` base | Auto register |
| --- | --- | --- | --- |
| `LL_STATIC_HOOK` | free/static fn | `ll::memory::Hook` | no |
| `LL_TYPE_STATIC_HOOK` | free/static fn | your `TYPE` | no |
| `LL_INSTANCE_HOOK` | member fn | `ll::memory::Hook` | no |
| `LL_TYPE_INSTANCE_HOOK` | member fn | your `TYPE` | no |
| `LL_AUTO_(TYPE_)STATIC_HOOK` | free/static fn | as above | yes |
| `LL_AUTO_(TYPE_)INSTANCE_HOOK` | member fn | as above | yes |

```cpp
#define LL_TYPE_STATIC_HOOK(DEF_TYPE, PRIORITY, TYPE, IDENTIFIER, RET_TYPE, ...)
#define LL_STATIC_HOOK(DEF_TYPE, PRIORITY, IDENTIFIER, RET_TYPE, ...)
#define LL_TYPE_INSTANCE_HOOK(DEF_TYPE, PRIORITY, TYPE, IDENTIFIER, RET_TYPE, ...)
#define LL_INSTANCE_HOOK(DEF_TYPE, PRIORITY, IDENTIFIER, RET_TYPE, ...)
#define LL_AUTO_TYPE_STATIC_HOOK(DEF_TYPE, PRIORITY, TYPE, IDENTIFIER, RET_TYPE, ...)
#define LL_AUTO_STATIC_HOOK(DEF_TYPE, PRIORITY, IDENTIFIER, RET_TYPE, ...)
#define LL_AUTO_TYPE_INSTANCE_HOOK(DEF_TYPE, PRIORITY, TYPE, IDENTIFIER, RET_TYPE, ...)
#define LL_AUTO_INSTANCE_HOOK(DEF_TYPE, PRIORITY, IDENTIFIER, RET_TYPE, ...)
```

— [Hook.h](https://github.com/LiteLDev/LeviLamina/blob/main/src/ll/api/memory/Hook.h), same list in the
[Hook Guide](https://docs.levimc.org/LeviLamina/developer_guides/how_to_guides/hook_guide/).

Each expands to a `struct DEF_TYPE : public TYPE` with:

* your `detour(...)` body — params only, **no `this`** for instance hooks;
* `origin(args...)` → calls the original;
* `thisFor<T>()` → re-casts `this` with the offset fix-up for multiple inheritance
  (docs use `thisFor<NetEventCallback>()->_getServerPlayer(...)`);
* `hook(bool suspendThreads = true)` / `unhook(bool suspendThreads = true)`;
* `AUTO` variants add an inline `ll::memory::HookRegistrar<DEF_TYPE>` global that hooks on
  construction and unhooks on destruction, ref-counted via `_AutoHookCount`.

Key semantics:

* **`IDENTIFIER`** may be "a function pointer, symbol, address or a signature" — `resolveIdentifier`
  is overloaded for function pointer, `SignatureView`, `SymbolView`, `uintptr_t`.
* **Priority**: `Highest=0, High=100, Normal=200, Low=300, Lowest=400` ("The higher priority, the
  hook will be executed earlier"); docs recommend `Normal`.* **Virtuals need a `$` prefix**: the macro `static_assert`s
  `#IDENTIFIER " is a virtual function, you need use prefix $ workaround to hook it."`
  (real ids: `&ClientInstance::_startLeaveGame` vs `&ClientInstance::$onLevelExit`).
* "All Hooks will automatically unload when Minecraft unloads."

Manual group registration — real client code:

```cpp
using TickHookRegistrar = ll::memory::HookRegistrar<ClientStartLeaveGameHook, ClientLevelExitHook,
    LocalPlayerFireDimensionChangedHook, BeginResourceReloadHook, QueueResourceReloadHook, CleanReloadMaterialsHook>;
void registerTickHooks()   { if (gTickHooksRegistered) return; TickHookRegistrar::hook();   gTickHooksRegistered = true; }
void unregisterTickHooks() { if (!gTickHooksRegistered) return; TickHookRegistrar::unhook(); gTickHooksRegistered = false; }
```

— [TickHook.cpp](https://github.com/LiteLDev/LeviSchematic/blob/main/src/levischematic/hook/TickHook.cpp)

## 6. Signature / symbol resolution

There is **no `ll::memory::resolveSymbol` free function** in 26.51.9 — resolution lives on the value
types (verified by reading `Memory.h`, `Symbol.h`, `Signature.h`).

```cpp
// Symbol.h
class SymbolView {
    [[nodiscard]] constexpr explicit SymbolView(std::string_view symbol) : sym(symbol) {}
    LLNDAPI void* resolve(bool disableErrorOutput = false) const;
};
template <memory::FixedSymbol symbol> constexpr void* operator""_symp() noexcept;

// Signature.h — SignatureView::resolve(bool) and resolve(std::span<std::byte>, bool)
[[nodiscard]] static constexpr Signature parse(std::string_view str); // "48 8B ? 89"
template <FixedString signature> constexpr void* operator""_sigp() noexcept;
```

`"?func@Class@@QEAAXXZ"_sym` and `"48 89 5C 24 ? 57 48 83 EC 20"_sig` are accepted directly as
`IDENTIFIER` (they are `SymbolView`/`SignatureView`); `_sigp`/`_symp` yield a raw `void*` for
`ll::memory::addressCall<R>(addr, args...)` when you only want to call. `Memory.h` also provides
`virtualCall`, `dAccess<T>(ptr, off)` (offset access), `getVtableIndex`, and `modify` (temporarily
RWX for patching).
## 7. Bedrock headers and singletons

LeviLamina ships its own Bedrock declaration tree (`src/mc`), called the "Fake Header":

> "You can refer to the [`Fake Header`](https://github.com/LiteLDev/LeviLamina/tree/main/src/mc)
> provided by LeviLamina to obtain the definitions of the functions you want to Hook." — [Hook Guide](https://docs.levimc.org/LeviLamina/developer_guides/how_to_guides/hook_guide/)

They come through the `levilamina` xmake package (no extra include path). Game binaries/data come
from `bedrockdata`, "version-locked to the Minecraft version LeviLamina currently targets, and its
`-server`/`-client` variant is selected by `--target_type`" — [Build Guide](https://docs.levimc.org/LeviLamina/developer_guides/how_to_guides/build_guide/).

Client renderer headers exist (`src/mc/client/renderer/game/`, plus data types such as
`DeferredLighting.h`, `ShadowConfiguration.h`, `BlockLightConfiguration.h`, `SkyboxConfig.h`,
`LightingGroup.h`, `TonemapperConfig.h`, `ShadowParametersConstraint.h`,
`VolumetricFogConfiguration.h`). **Most are empty stubs** — names only:

```cpp
// src/mc/client/renderer/ShadowConfiguration.h
struct ShadowConfiguration {};
// LightingGroup.h: struct Emissive {}; struct LightingDescription {}; struct OrbitalLights {};
```

So: names/signatures yes, layouts no — offsets must be reverse-engineered (§10). Singletons,
`namespace ll::service::inline bedrock` in `Bedrock.h`:

```cpp
LLNDAPI optional_ref<Minecraft> getMinecraft(bool isClientSide = false);
LLNDAPI optional_ref<Level>     getLevel();
LLNDAPI optional_ref<NetworkSystem> getNetworkSystem(bool isClientSide = false);
LLNDAPI optional_ref<CommandRegistry> getCommandRegistry(bool isClientSide = false);
```

Inside a hook prefer the receiver (`this`, plus `thisFor<T>()`). Real client code additionally uses
`ll::service::getClientInstance()` and `client->getLocalPlayer()` — **[unverified]** declaration: it
is absent from the `Bedrock.h` revision read (possibly in `TargetedBedrock.h`, which 404'd).

## 8. Client events: there are **no** render events

Verified from `src/ll/api/event/` at `main`: only `command/`, `entity/`, `player/`, `service/`,
`world/` subdirectories. **No** `ll/api/event/client/`, **no** `ClientEnterLevelEvent`, **no**
before/after-render event. Existing headers include `Event.h`, `EventBus.h`, `Listener.h`,
`Cancellable.h`, `Emitter.h`, `EventId.h`, `world/LevelTickEvent.h`, `world/BlockChangedEvent.h`,
`command/ExecuteCommandEvent.h`, `player/PlayerDieEvent.h`, `entity/ActorHurtEvent.h`.

A lighting mod must therefore hook the renderer. Verified injectable point, as a shipping client mod
uses it:

```cpp
LL_TYPE_INSTANCE_HOOK(
    PreviewScreenRenderHook,
    ll::memory::HookPriority::Normal,
    LevelRendererCamera,
    &LevelRendererCamera::$renderBlockEntities,
    void,
    BaseActorRenderContext& renderContext,
    bool                    renderAlphaLayer
) {
    if (auto session = app::tryAccessSession(); session && !renderAlphaLayer) {
        session->schematicRenderer().renderFrame(renderContext.mScreenContext, session->placement());
    }
    origin(renderContext, renderAlphaLayer);
}
```

The same file also hooks `InGamePlayScreen::$_renderLevel`,
`void(ScreenContext& screenContext, FrameRenderObject const& renderObj)` (the per-frame level pass),
`LevelRendererPlayer::renderHitSelect` and `LevelRendererPlayer::_renderOutlineSelection`
(`void(BaseActorRenderContext&, BlockSource&, BlockPos const&, ...)`). Includes used:
`mc/client/renderer/game/{LevelRendererCamera,LevelRendererPlayer}.h`, `mc/client/gui/screens/InGamePlayScreen.h`,
`mc/client/renderer/BaseActorRenderContext.h`, `mc/deps/minecraft_renderer/objects/{FrameRenderObject,ViewRenderObject}.h`
— [PreviewRenderHook.cpp](https://github.com/LiteLDev/LeviSchematic/blob/main/src/levischematic/hook/PreviewRenderHook.cpp)

## 9. What is NOT available (as of 2026-11-03)

* **No stable public Bedrock render API**: `ll/api/` has no graphics/render module and no docs page
  exposes a frame/pass callback. Everything is hook-based ⇒ version-fragile.
* **No render events** (§8). **No usable renderer struct layouts** (empty stubs, §7) ⇒ offsets, not fields.
* **No official shader / RenderDragon API** — **[unverified absence]**; treat shader-level work as
  out of scope for a LeviLamina mod.
* **No `MOD_REGISTER`** (it is `LL_REGISTER_MOD`); **no `resolveSymbol`** (use `SymbolView::resolve`).
* **No RTTI**: "Because MCBE lacks RTTI information, you cannot use `dynamic_cast<T>()`."
* **Fragility**: signatures/offsets must be re-derived per MCBE version; `bedrockdata` is
  version-locked, so a game bump invalidates them.

## 10. Recommended mechanism for an unknown-signature client render function

Use a **manual `LL_INSTANCE_HOOK` with a `_sym`/`_sig` identifier** — the only form needing neither a
header for the owner nor a declared member pointer:

```cpp
#include "ll/api/memory/Hook.h"

// 1) Find the function in IDA (docs: Find Function Guide); note its decorated name / byte pattern.
LL_INSTANCE_HOOK(                        // manual: no TYPE base, no auto-register
    MyUnknownRenderFnHook,
    ll::memory::HookPriority::Normal,    // Highest only if you must win; Normal otherwise
    "?renderSomething@SomeRenderer@@QEAAXAEAVBlockSource@@_N@Z"_sym, // or "48 89 5C 24 ? ..."_sig
    void,                                // RET_TYPE you reconstructed
    BlockSource& region,                 // params you reconstructed (NOT this)
    bool         someFlag
) {
    auto* self = reinterpret_cast<MyForwardDeclaredOwner*>(this); // `this` == receiver object
    // ... optionally return early WITHOUT origin() to suppress the vanilla pass ...
    origin(region, someFlag);            // 2) forward to vanilla unless deliberately skipping
}

// enable():  MyUnknownRenderFnHook::hook();   disable(): MyUnknownRenderFnHook::unhook();
```

Why: `resolveIdentifier(SignatureView)` / `resolveIdentifier(SymbolView)` make `IDENTIFIER` work with
no SDK declaration; `LL_INSTANCE_HOOK` supplies `::ll::memory::Hook` as base so the macro compiles
without a class definition, while `this` is still the real receiver and `origin()` thunks correctly.

Variants: `LL_AUTO_INSTANCE_HOOK` for process-lifetime registration (ref-counted, auto-unhooked); if
the class header exists prefer `LL_TYPE_INSTANCE_HOOK(..., LevelRendererCamera,
&LevelRendererCamera::$renderBlockEntities, ...)` (type-checked, `thisFor<T>()` available); for free
functions use `LL_STATIC_HOOK`/`LL_TYPE_STATIC_HOOK`; if the owner is unknown, use `LL_STATIC_HOOK`
and take `void* self` as the first parameter, casting inside the detour.

Rules: hook in `enable()`, unhook in `disable()`, nothing game-related in `load()`; guard
`client`/`player` nulls; record the exact MCBE build (1.26.51.01) next to every saved signature.
For lighting, start from `LevelRendererCamera` / `LevelRendererPlayer` /
`InGamePlayScreen::$_renderLevel` (they bracket the geometry passes where lighting resolves), locate
lighting state in IDA using the header names as anchors (`DeferredLightingConfiguration`,
`BlockLightConfiguration`, `ShadowConfiguration`, `ShadowParametersConstraint`, `LightingGroup`), then
read state via `ll::memory::dAccess<T>(ptr, off)` with offsets confirmed for 1.26.51.01 — never by
header field. Custom drawing works from inside these hooks through the engine's own path
(`BaseActorRenderContext&` / `ScreenContext&`, `FrameRenderObject`→`mViews`→`mViewData`→`mCameraTargetPos`).
## 11. Verification status

| Claim | Status |
| --- | --- |
| 26.51.9 ↔ MCBE 1.26.51.1 client; client mods in `mods/<id>/`; `LL_PLAT_C` | verified (versions page, lip docs, xmake-repo) |
| Template files: xmake / MyMod.h /.cpp / manifest / tooth / workflow | verified (template repo) |
| `LL_REGISTER_MOD`, `bindToMod` concepts; logger/`Mod` API | verified (`RegisterHelper.h`, `Mod.h`) |
| Hook macro set, signatures, `$`-virtual rule, `origin`, `thisFor` | verified (`Hook.h`) |
| `Signature::parse`, `SymbolView::resolve`, `_sig`/`_sigp`/`_sym`/`_symp`; no `resolveSymbol` | verified (`Signature.h`, `Symbol.h`, `Memory.h`) |
| No `ll/api/event/client/`, no render events | verified (dir listing @main) |
| Renderer headers exist but are empty stubs | verified (`ShadowConfiguration.h`, `LightingGroup.h`) |
| Real render hooks on `LevelRendererCamera` / `InGamePlayScreen` / `LevelRendererPlayer` | verified (LeviSchematic source) |
| `ll::service::getClientInstance()` | **unverified** declaration (used in real source) |
| `mc/client/renderer/game/LevelRendererCamera.h` filename | **unverified** (used by a working mod; raw fetch 404 ⇒ branch drift) |
| No RenderDragon/shader API | **unverified absence** |

## 12. Key sources

Docs: [Hook Guide](https://docs.levimc.org/LeviLamina/developer_guides/how_to_guides/hook_guide/),
[Find Function Guide](https://docs.levimc.org/LeviLamina/developer_guides/how_to_guides/find_function_guide/),
[Build Guide](https://docs.levimc.org/LeviLamina/developer_guides/how_to_guides/build_guide/),
[Versions](https://docs.levimc.org/LeviLamina/versions/),
[Client install](https://docs.levimc.org/LeviLamina/user_guides/install_on_client/),
[lip Creating Packages](https://docs.levimc.org/lip/guides/creating_packages.html).
Code: [levilamina-mod-template](https://github.com/LiteLDev/levilamina-mod-template),
[LeviSchematic](https://github.com/LiteLDev/LeviSchematic),
[Lamium](https://github.com/amatouhake/Lamium),
[DetailArmorBarBedrock-LeviLamina](https://github.com/rahmanlied/DetailArmorBarBedrock-LeviLamina).
Headers: `src/ll/api/memory/{Hook,Memory,Signature,Symbol}.h`, `src/ll/api/mod/{Mod,NativeMod,RegisterHelper}.h`, `src/ll/api/service/Bedrock.h`, [`src/mc`](https://github.com/LiteLDev/LeviLamina/tree/main/src/mc).
