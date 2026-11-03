-- JavaLighting: make Minecraft Bedrock Edition lighting & shadows closer to Java Edition.
-- Build:   xmake f -a x64 -m release -p windows -v --target_type=client -y
--          xmake -v -y
-- Output:  bin/  (packed by @levibuildscript/modpacker, copy the produced folder to
--          <game>/mods/java-lighting/)

add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("server", "client")
option_end()

-- Pin the LeviLamina version this mod is built against.
-- Minecraft Bedrock 1.26.51.01  <->  LeviLamina 26.51.*
add_requires("levilamina 26.51.*", {configs = {target_type = get_config("target_type")}})

add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("java-lighting")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")

-- Host-side regression test for the Java lightmap port. It needs no game
-- binary, so it can run in CI (xmake build java-lighting-tests && xmake run ...).
target("java-lighting-tests")
    set_kind("binary")
    set_languages("c++20")
    add_includedirs("src")
    add_files("tests/test_lightmap.cpp", "src/lighting/JavaLightmap.cpp")
    if is_plat("windows") then
        set_toolchains("clang-cl")
    end
