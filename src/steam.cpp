// Steamworks integration. Initializes Steam, pumps its callbacks and exposes a small part of the API to Evox scripts as `steam:steam`
// (see scripts/steam.evox): user info, achievements, stats and rich presence.

#include "core/allocator.h"
#include "core/log.h"
#include "core/stream.h"
#include "core/string.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/plugin.h"
#include "evox/evox_module.h"
struct ExEntity; // evox_wrapper.h needs it declared
#include "evox/evox_wrapper.h"
// the SDK headers use strncpy
#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable : 4996)
#endif
#include <steam/steam_api.h>
#ifdef _MSC_VER
	#pragma warning(pop)
#endif

namespace Lumix {

struct SteamSystem final : ISystem {
	explicit SteamSystem(Engine& engine) : m_engine(engine) {
		s_instance = this;
		// must be before gpu::init (renderer's initBegin), so the overlay can hook the device creation
		init();
	}

	~SteamSystem() {
		shutdown();
		s_instance = nullptr;
	}

	const char* getName() const override { return "steam"; }
	void serialize(OutputMemoryStream&) const override {}
	bool deserialize(i32, InputMemoryStream&) override { return true; }

	void initBegin() override {
		// scripts of the plugin, imported as "steam:<name>"
		m_engine.getFileSystem().mount(STEAM_SCRIPTS_DIR, "plugins/steam");
		auto* evox = (EvoxSystem*)m_engine.getSystemManager().getSystem("evox_system");
		if (!evox) {
			logWarning("steam: evox plugin not found, steam API is not available in scripts");
			return;
		}
		// the registry keeps views of the names, so they must be string literals
		evox->registerNativeFunction("steam:steam", "isInitialized", &Evox::wrap<steamIsInitialized>);
		evox->registerNativeFunction("steam:steam", "getUserName", &steamGetUserName);
		evox->registerNativeFunction("steam:steam", "getAppId", &Evox::wrap<steamGetAppId>);
		evox->registerNativeFunction("steam:steam", "isStatsReady", &Evox::wrap<steamIsStatsReady>);
		evox->registerNativeFunction("steam:steam", "setAchievement", &Evox::wrap<steamSetAchievement>);
		evox->registerNativeFunction("steam:steam", "clearAchievement", &Evox::wrap<steamClearAchievement>);
		evox->registerNativeFunction("steam:steam", "getAchievement", &Evox::wrap<steamGetAchievement>);
		evox->registerNativeFunction("steam:steam", "setStatInt", &Evox::wrap<steamSetStatInt>);
		evox->registerNativeFunction("steam:steam", "getStatInt", &Evox::wrap<steamGetStatInt>);
		evox->registerNativeFunction("steam:steam", "setStatFloat", &Evox::wrap<steamSetStatFloat>);
		evox->registerNativeFunction("steam:steam", "getStatFloat", &Evox::wrap<steamGetStatFloat>);
		evox->registerNativeFunction("steam:steam", "storeStats", &Evox::wrap<steamStoreStats>);
		evox->registerNativeFunction("steam:steam", "setRichPresence", &Evox::wrap<steamSetRichPresence>);
		evox->registerNativeFunction("steam:steam", "clearRichPresence", &Evox::wrap<steamClearRichPresence>);
	}

	void update(float) override {
		if (!m_initialized) return;
		SteamAPI_RunCallbacks();
		// Steam syncs stats before the process starts, so UserStatsReceived_t is not guaranteed to arrive; a loaded schema is enough
		if (!m_stats_ready && SteamUserStats()->GetNumAchievements() > 0) m_stats_ready = true;
	}

	void init() {
		SteamErrMsg err_msg = {};
		const ESteamAPIInitResult result = SteamAPI_InitEx(&err_msg);
		if (result != k_ESteamAPIInitResult_OK) {
			// running without Steam is normal during development
			logWarning("steam: initialization failed: ", (const char*)err_msg);
			return;
		}
		m_initialized = true;
		m_stats_callback.Register(this, &SteamSystem::onUserStatsReceived);
		m_stored_callback.Register(this, &SteamSystem::onUserStatsStored);
		logInfo("steam: initialized, user ", SteamFriends()->GetPersonaName());
	}

	void shutdown() {
		if (!m_initialized) return;
		m_stats_callback.Unregister();
		m_stored_callback.Unregister();
		SteamAPI_Shutdown();
		m_initialized = false;
		m_stats_ready = false;
	}

	void onUserStatsReceived(UserStatsReceived_t* data) {
		if (data->m_nGameID != SteamUtils()->GetAppID()) return;
		if (data->m_eResult != k_EResultOK) {
			logError("steam: failed to receive user stats, result ", (i32)data->m_eResult);
			return;
		}
		m_stats_ready = true;
	}

	// StoreStats only queues the upload, its result is delivered here
	void onUserStatsStored(UserStatsStored_t* data) {
		if (data->m_nGameID != SteamUtils()->GetAppID()) return;
		if (data->m_eResult != k_EResultOK) logError("steam: failed to store user stats, result ", (i32)data->m_eResult);
	}

	// Script strings are not null terminated.
	struct CString {
		explicit CString(ex_string_view str) {
			copyString(Span(data), StringView(str.begin, (u64)str.length));
			if ((u64)str.length >= sizeof(data)) logError("steam: string too long, truncated to ", data);
		}
		operator const char*() const { return data; }
		char data[256];
	};

	// Without Steam the API is a no-op, init reports that once, so it's not reported per call.
	static bool checkStatsReady(const char* function) {
		if (!steamIsInitialized()) return false;
		if (steamIsStatsReady()) return true;
		logWarning("steam: ", function, " called before stats are ready");
		return false;
	}

	static bool check(bool success, const char* function, const char* name) {
		if (!success) logError("steam: ", function, " failed for ", name);
		return success;
	}

	static bool steamIsInitialized() { return s_instance && s_instance->m_initialized; }
	static bool steamIsStatsReady() { return s_instance && s_instance->m_stats_ready; }
	static i32 steamGetAppId() { return steamIsInitialized() ? (i32)SteamUtils()->GetAppID() : 0; }

	static void steamGetUserName(ex_runtime* runtime, ex_call_frame frame) {
		const char* name = steamIsInitialized() ? SteamFriends()->GetPersonaName() : "";
		ex_result_string(runtime, &frame, ex_string_view{name, (i64)strlen(name)});
	}

	static bool steamSetAchievement(ex_string_view name) {
		if (!checkStatsReady("setAchievement")) return false;
		const CString c_name(name);
		return check(SteamUserStats()->SetAchievement(c_name), "setAchievement", c_name);
	}

	static bool steamClearAchievement(ex_string_view name) {
		if (!checkStatsReady("clearAchievement")) return false;
		const CString c_name(name);
		return check(SteamUserStats()->ClearAchievement(c_name), "clearAchievement", c_name);
	}

	// returns false if the achievement is not unlocked, failures are logged
	static bool steamGetAchievement(ex_string_view name) {
		if (!checkStatsReady("getAchievement")) return false;
		const CString c_name(name);
		bool unlocked = false;
		return check(SteamUserStats()->GetAchievement(c_name, &unlocked), "getAchievement", c_name) && unlocked;
	}

	static bool steamSetStatInt(ex_string_view name, i32 value) {
		if (!checkStatsReady("setStatInt")) return false;
		const CString c_name(name);
		return check(SteamUserStats()->SetStat(c_name, (int32)value), "setStatInt", c_name);
	}

	static i32 steamGetStatInt(ex_string_view name) {
		int32 value = 0;
		if (!checkStatsReady("getStatInt")) return value;
		const CString c_name(name);
		check(SteamUserStats()->GetStat(c_name, &value), "getStatInt", c_name);
		return value;
	}

	static bool steamSetStatFloat(ex_string_view name, float value) {
		if (!checkStatsReady("setStatFloat")) return false;
		const CString c_name(name);
		return check(SteamUserStats()->SetStat(c_name, value), "setStatFloat", c_name);
	}

	static float steamGetStatFloat(ex_string_view name) {
		float value = 0;
		if (!checkStatsReady("getStatFloat")) return value;
		const CString c_name(name);
		check(SteamUserStats()->GetStat(c_name, &value), "getStatFloat", c_name);
		return value;
	}

	// uploads changed stats and achievements, Steam shows the "achievement unlocked" popup
	// true means the upload is queued, failed upload is logged in onUserStatsStored
	static bool steamStoreStats() {
		if (!checkStatsReady("storeStats")) return false;
		return check(SteamUserStats()->StoreStats(), "storeStats", "current user");
	}

	static bool steamSetRichPresence(ex_string_view key, ex_string_view value) {
		if (!steamIsInitialized()) return false;
		const CString c_key(key);
		return check(SteamFriends()->SetRichPresence(c_key, CString(value)), "setRichPresence", c_key);
	}

	static void steamClearRichPresence() {
		if (steamIsInitialized()) SteamFriends()->ClearRichPresence();
	}

	Engine& m_engine;
	bool m_initialized = false;
	bool m_stats_ready = false;
	CCallbackManual<SteamSystem, UserStatsReceived_t> m_stats_callback;
	CCallbackManual<SteamSystem, UserStatsStored_t> m_stored_callback;
	static inline SteamSystem* s_instance = nullptr;
};

} // namespace Lumix

LUMIX_PLUGIN_ENTRY(steam) {
	return LUMIX_NEW(engine.getAllocator(), Lumix::SteamSystem)(engine);
}
