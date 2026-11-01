#include "core/allocator.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/string.h"
#include "editor/studio_app.h"
#include "engine/component_uid.h"

using namespace Lumix;

namespace {

// IPlugin must be the first base, studio deletes the plugin through IPlugin*
struct SteamEditorPlugin final : StudioApp::IPlugin, StudioApp::GUIPlugin {
	void init() override {}
	void onGUI() override {}
	bool showGizmo(WorldView&, ComponentUID) override { return false; }
	const char* getName() const override { return "steam"; }

	// the exported executable does not start without the dll
	bool exportData(const char* dest_dir) override {
		const char* dll = "steam_api64.dll";
		char exe_path[MAX_PATH];
		os::getExecutablePath(Span(exe_path));
		const StaticString<MAX_PATH> src(Path::getDir(exe_path), dll);
		const StaticString<MAX_PATH> dest(dest_dir, dll);
		if (!os::copyFile(src, dest)) {
			logError("Failed to copy ", src, " to ", dest);
			return false;
		}
		return true;
	}
};

} // namespace

LUMIX_STUDIO_ENTRY(steam) {
	auto* plugin = LUMIX_NEW(app.getAllocator(), SteamEditorPlugin);
	app.addPlugin((StudioApp::GUIPlugin&)*plugin);
	return plugin;
}
