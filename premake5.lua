-- Runs inside the plugin project. Paths are relative to this file.
-- Needs the Steamworks SDK (https://partner.steamgames.com/downloads/list), copy these folders from it to external/steamworks:
--   public/steam/                      (headers)
--   redistributable_bin/win64/         (steam_api64.lib, steam_api64.dll)
--   redistributable_bin/linux64/       (libsteam_api.so)
local SDK = "external/steamworks"
if not os.isdir(SDK .. "/public/steam") then
	error("steam plugin: Steamworks SDK not found, see plugins/steam/README.md")
end

files { "src/**.cpp", "src/**.h" }
includedirs { SDK .. "/public" }
defines { "BUILDING_STEAM", 'STEAM_SCRIPTS_DIR="' .. path.join(_SCRIPT_DIR, "scripts") .. '"' }

local sdk_abs = path.join(_SCRIPT_DIR, SDK)

local function linkSteam()
	filter "system:windows"
		libdirs { sdk_abs .. "/redistributable_bin/win64" }
		links { "steam_api64" }
	filter "system:linux"
		libdirs { sdk_abs .. "/redistributable_bin/linux64" }
		links { "steam_api" }
	filter {}
end

-- steam_api64.dll must be next to the executable
local function copySteamDll()
	filter "system:windows"
		postbuildcommands { '{COPYFILE} "' .. sdk_abs .. '/redistributable_bin/win64/steam_api64.dll" "%{cfg.targetdir}"' }
	filter {}
end

if _OPTIONS["dynamic-plugins"] then
	-- plugins link each other only in dynamic builds
	links { "core", "engine" }
	linkSteam()
	copySteamDll()
else
	-- the plugin is a part of a static library, the executables link Steam
	local exes = {}
	if not _OPTIONS["no-studio"] then table.insert(exes, "studio") end
	if not _OPTIONS["no-app"] then table.insert(exes, "app") end
	if _OPTIONS["with-tests"] then table.insert(exes, "tests") end

	for _, exe in ipairs(exes) do
		project(exe)
		linkSteam()
		copySteamDll()
	end

	-- the main script continues in the plugin's project
	project "engine_merged"
end
