-- include subprojects
includes("lib/commonlibsse-ng")

-- set project constants
set_project("HotkeyAtlas")
set_version("1.0.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- define targets
target("HotkeyAtlas")
    add_rules("commonlibsse-ng.plugin", {
        name = "HotkeyAtlas",
        author = "Neutral9",
        description = "Every Skyrim and mod hotkey in one SKSE Menu Framework window: view, rebind, combine, reset."
    })

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src", "extern")
    set_pcxxheader("src/pch.h")

    -- mod data (Notes.json, translations) next to the dll: `xmake install` puts both into
    -- XSE_TES5_MODS_PATH/HotkeyAtlas (a mod manager's mods folder) or XSE_TES5_GAME_PATH/Data
    add_installfiles("dist/(SKSE/**)")

    -- with XSE_TES5_MODS_PATH set, every build also refreshes the mod folder, so the game
    -- picks the new build up without an install step
    after_build(function (target)
        local mods = os.getenv("XSE_TES5_MODS_PATH")
        if not mods then
            return
        end
        local dest = path.join(mods, target:name())
        try
        {
            function ()
                -- file by file: copying the folder would replace the one already there
                local dist = path.join(os.projectdir(), "dist")
                for _, file in ipairs(os.files(path.join(dist, "SKSE/**"))) do
                    local to = path.join(dest, path.relative(file, dist))
                    os.mkdir(path.directory(to))
                    os.cp(file, to)
                end
                os.mkdir(path.join(dest, "SKSE/Plugins"))
                os.cp(target:targetfile(), path.join(dest, "SKSE/Plugins"))
                cprint("${green}copied %s and dist/SKSE -> %s", path.filename(target:targetfile()), dest)
            end,
            catch
            {
                function (e)
                    cprint("${yellow}could not copy to %s (is Skyrim running?): %s", dest, e)
                end
            }
        }
    end)
