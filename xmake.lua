set_xmakever("3.0.0")

-- CommonLibSSE-NG: this project's own submodule if present, otherwise the
-- copy already checked out next door in ../SpellBlockHit. To give this
-- project its own copy:
--   git init
--   git submodule add -b ng https://github.com/alandtse/CommonLibVR.git lib/commonlibsse-ng
local clib = "lib/commonlibsse-ng"
if not os.isfile(path.join(os.scriptdir(), clib, "xmake.lua")) then
    clib = "../SpellBlockHit/lib/commonlibsse-ng"
end

-- SE + AE only: the hooks use flat-runtime vtable indices
set_config("skyrim_vr", false)
includes(clib)

-- set project constants
set_project("SilentProtagonistVoice")
set_version("1.0.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_requires("simpleini v4.25")

-- define targets
target("SilentProtagonistVoice")
    add_rules("commonlibsse-ng.plugin", {
        name = "SilentProtagonistVoice",
        author = "Cizlen",
        description = "Silent-protagonist hums and mouth animation for the player's dialogue lines"
    })

    add_packages("simpleini")

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h", "include/**.h")
    add_includedirs("src", "include")
    set_pcxxheader("include/PCH.h")

    add_defines("NOMINMAX")  -- SKSEMenuFramework.h includes <windows.h>
    add_cxxflags("cl::/utf-8", "cl::/Zc:preprocessor", "cl::/wd4100", "cl::/wd4324")

    -- `xmake install` / `xmake package` also ship the INI and the voice packs
    add_installfiles("dist/(**)")
