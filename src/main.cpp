#include "PCH.h"

#include "AudioUtilBridge.h"
#include "DialogueHook.h"
#include "Settings.h"
#include "Speech.h"
#include "UI.h"
#include "VoicePacks.h"

namespace
{
	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		const auto path = *dir / std::format("{}.log", SKSE::PluginDeclaration::GetSingleton()->GetName());
		auto       sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto       log = std::make_shared<spdlog::logger>("log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		log->set_pattern("[%H:%M:%S.%e] [%l] %v");
		spdlog::set_default_logger(std::move(log));
	}

	// Drives Speech every frame on the main thread. The dialogue menu does
	// not pause the game, so this keeps running while the player talks.
	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			func(a_this, a_delta);
			Speech::Tick();
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			func = vtbl.write_vfunc(0xAD, thunk);
			logger::info("Hooked PlayerCharacter::Update");
		}
	};

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			AudioUtilBridge::Load();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			VoicePacks::Scan();
			DialogueHook::RegisterSinks();
			UI::Register();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			DialogueHook::DropPending();
			Speech::Cancel();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();
	logger::info("{} {} loading", MOD_NAME, SKSE::PluginDeclaration::GetSingleton()->GetVersion().string());

	Settings::Load();
	DialogueHook::Install();
	PlayerUpdate::Install();

	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
