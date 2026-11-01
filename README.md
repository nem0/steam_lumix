# Steam plugin

Steamworks integration for Lumix Engine. It initializes Steam when the plugin is loaded (before the renderer creates
its device, so the overlay works), runs Steam callbacks every frame and shuts Steam down with the engine. It exposes
a part of the Steam API to Evox scripts as `steam:steam` (`scripts/steam.evox`, the plugin mounts `scripts/` as `plugins/steam`).
If Steam is not available, the plugin logs a warning and the script API is a no-op.

## Setup

1. Download the Steamworks SDK from https://partner.steamgames.com/downloads/list (needs a Steamworks account).
2. Unpack the SDK to `external/steamworks/`, so that `external/steamworks/public/steam/` exists (the SDK is git-ignored).
3. Regenerate the project (`scripts/create_vs22_sln.bat`) and build.
4. For runs that are not started by the Steam client, put a `steam_appid.txt` with your app id (480 is Spacewar, the test app)
   in the working directory.

The build copies `steam_api64.dll` next to the executables.

Exporting the game from the editor copies `steam_api64.dll` next to the exported executable. `steam_appid.txt` is not copied,
it should not be shipped.

## Usage

```
import "steam:steam" as steam

fn unlockWinAchievement() : void {
	// stats and achievements work after isStatsReady() becomes true (Steam sends them shortly after startup)
	if not steam.isStatsReady() { return; }
	steam.setAchievement("ACH_WIN_ONE_GAME");
	steam.storeStats();
}
```
