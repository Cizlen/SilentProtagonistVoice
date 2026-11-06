#pragma once

// Optional integration with AudioUtil (crajjjj, github.com/crajjjj/AudioUtil).
// Resolved with GetProcAddress, so there is no link-time dependency: if
// AudioUtil.dll is not loaded every pointer stays null and we use the native
// backend.
//
// Two uses:
//  1. Backend = AudioUtil: clips are played through AudioUtil_PlayFileWithLipSync,
//     which also drives the mouth from the clip's envelope / .lip.
//  2. Backend = Native while AudioUtil is installed: we claim the player's mouth
//     for the length of the line so AudioUtil-driven content doesn't fight our
//     lip sync, and release it afterwards.

namespace AudioUtilBridge
{
	using PlayFileFn = std::int32_t (*)(const char* a_dataRelPath, RE::Actor* a_follow, float a_volume, const char* a_group, const char* a_channel);
	using StopHandleFn = bool (*)(std::int32_t a_handle);
	using IsHandlePlayingFn = bool (*)(std::int32_t a_handle);
	using ClaimMouthFn = void (*)(RE::Actor* a_actor, float a_seconds, const char* a_owner);
	using ReleaseMouthFn = void (*)(RE::Actor* a_actor, const char* a_owner);
	using GetUIntFn = std::uint32_t (*)();

	struct API
	{
		GetUIntFn         getVersion = nullptr;
		GetUIntFn         getInterfaceVersion = nullptr;
		PlayFileFn        playFile = nullptr;
		PlayFileFn        playFileWithLipSync = nullptr;
		StopHandleFn      stopHandle = nullptr;
		IsHandlePlayingFn isHandlePlaying = nullptr;
		ClaimMouthFn      claimMouth = nullptr;
		ReleaseMouthFn    releaseMouth = nullptr;

		[[nodiscard]] bool CanPlay() const { return playFileWithLipSync != nullptr; }
		[[nodiscard]] bool CanClaim() const { return claimMouth != nullptr && releaseMouth != nullptr; }
	};

	void Load();  // call on kPostLoad or later
	[[nodiscard]] const API& Get();
	[[nodiscard]] bool Installed();
	[[nodiscard]] std::string VersionString();

	inline constexpr const char* kOwner = "SilentProtagonistVoice";
	inline constexpr const char* kGroup = "SilentProtagonistVoice";
}
