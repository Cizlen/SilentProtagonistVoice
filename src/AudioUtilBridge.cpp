#include "PCH.h"

#include "AudioUtilBridge.h"

namespace AudioUtilBridge
{
	namespace
	{
		API g_api;

		template <class T>
		T Resolve(REX::W32::HMODULE a_module, const char* a_name)
		{
			return reinterpret_cast<T>(REX::W32::GetProcAddress(a_module, a_name));
		}
	}

	void Load()
	{
		g_api = {};
		auto* module = REX::W32::GetModuleHandleA("AudioUtil.dll");
		if (!module) {
			logger::info("AudioUtil not installed; using native playback only");
			return;
		}
		g_api.getVersion = Resolve<GetUIntFn>(module, "AudioUtil_GetVersion");
		g_api.getInterfaceVersion = Resolve<GetUIntFn>(module, "AudioUtil_GetInterfaceVersion");
		g_api.playFile = Resolve<PlayFileFn>(module, "AudioUtil_PlayFile");
		g_api.playFileWithLipSync = Resolve<PlayFileFn>(module, "AudioUtil_PlayFileWithLipSync");
		g_api.stopHandle = Resolve<StopHandleFn>(module, "AudioUtil_StopHandle");
		g_api.isHandlePlaying = Resolve<IsHandlePlayingFn>(module, "AudioUtil_IsHandlePlaying");
		g_api.claimMouth = Resolve<ClaimMouthFn>(module, "AudioUtil_ClaimMouth");
		g_api.releaseMouth = Resolve<ReleaseMouthFn>(module, "AudioUtil_ReleaseMouth");

		logger::info("AudioUtil found: {} (playback {}, mouth claims {}, stop {})", VersionString(),
			g_api.CanPlay() ? "yes" : "no", g_api.CanClaim() ? "yes" : "no", g_api.stopHandle ? "yes" : "no");
	}

	const API& Get() { return g_api; }

	bool Installed() { return g_api.getVersion != nullptr; }

	std::string VersionString()
	{
		if (!g_api.getVersion) {
			return "not installed";
		}
		const auto v = g_api.getVersion();
		const auto iface = g_api.getInterfaceVersion ? g_api.getInterfaceVersion() : 0u;
		return std::format("{}.{}.{} (C API {})", v / 10000000u, (v / 100000u) % 100u, v % 100000u, iface);
	}
}
