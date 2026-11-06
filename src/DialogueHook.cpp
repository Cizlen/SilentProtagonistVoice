#include "PCH.h"

#include "DialogueHook.h"

#include "Emotion.h"

#include "Settings.h"
#include "Speech.h"

namespace DialogueHook
{
	namespace
	{
		using CallbackFn = RE::FxDelegateHandler::CallbackFn;
		using CallbackProcessor = RE::FxDelegateHandler::CallbackProcessor;

		CallbackFn* g_origTopicClicked = nullptr;
		CallbackFn* g_origSkipText = nullptr;
		CallbackFn* g_origCloseMenu = nullptr;

		// A click we are holding back until the player has finished "speaking".
		struct Deferred
		{
			std::uint64_t                   id = 0;
			CallbackFn*                     fn = nullptr;
			RE::GPtr<RE::FxDelegateHandler> handler;
			RE::GPtr<RE::GFxMovieView>      movie;
			RE::GFxValue                    responseID;
			std::vector<RE::GFxValue>       args;
		};

		std::mutex                g_lock;
		std::shared_ptr<Deferred> g_pending;
		std::atomic<std::uint64_t> g_nextId{ 1 };

		// FxDelegateArgs keeps the response id protected; read it through a derived type
		struct ArgsAccess : RE::FxDelegateArgs
		{
			static RE::GFxValue ResponseID(const RE::FxDelegateArgs& a_args) { return a_args.*(&ArgsAccess::_responseID); }
		};

		Settings::Config Snapshot() { return Settings::Snapshot(); }

		std::shared_ptr<Deferred> Take(std::uint64_t a_id)
		{
			std::scoped_lock lock{ g_lock };
			if (g_pending && (a_id == 0 || g_pending->id == a_id)) {
				return std::exchange(g_pending, nullptr);
			}
			return nullptr;
		}

		// The held click owns a reference to the menu and its movie: let go of it
		// on the UI thread, where the menu would normally be destroyed.
		void Release(std::shared_ptr<Deferred> a_deferred)
		{
			if (a_deferred) {
				SKSE::GetTaskInterface()->AddUITask([d = std::move(a_deferred)]() {});
			}
		}

		// Replay the held click on the UI thread, if the same dialogue menu is still up.
		void Fire(std::shared_ptr<Deferred> a_deferred)
		{
			if (!a_deferred || !a_deferred->fn) {
				return;
			}
			SKSE::GetTaskInterface()->AddUITask([d = std::move(a_deferred)]() {
				auto* ui = RE::UI::GetSingleton();
				if (!ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
					return;
				}
				const auto menu = ui->GetMenu(RE::DialogueMenu::MENU_NAME);
				if (!menu || static_cast<RE::FxDelegateHandler*>(menu.get()) != d->handler.get()) {
					return;  // a different conversation by now
				}
				const RE::FxDelegateArgs args(d->responseID, d->handler.get(), d->movie.get(),
					d->args.empty() ? nullptr : d->args.data(), static_cast<std::uint32_t>(d->args.size()));
				d->fn(args);
			});
		}

		std::string FromMovie(RE::GFxMovieView* a_movie, const char* a_path)
		{
			RE::GFxValue v;
			if (a_movie && a_movie->GetVariable(&v, a_path) && v.IsString() && v.GetString()) {
				return v.GetString();
			}
			return {};
		}

		std::string FromTopicManager(const RE::FxDelegateArgs& a_args)
		{
			if (a_args.GetArgCount() < 1 || !a_args[0].IsNumber()) {
				return {};
			}
			const auto index = static_cast<std::int64_t>(a_args[0].GetNumber());
			auto*      mtm = RE::MenuTopicManager::GetSingleton();
			if (!mtm || !mtm->dialogueList || index < 0) {
				return {};
			}
			std::int64_t i = 0;
			for (auto* dlg : *mtm->dialogueList) {
				if (!dlg) {
					continue;
				}
				if (i++ == index) {
					return dlg->topicText.c_str() ? dlg->topicText.c_str() : "";
				}
			}
			return {};
		}

		// The menu's entry for the clicked topic: by its text first (exact), else by index.
		const RE::MenuTopicManager::Dialogue* FindDialogue(const RE::FxDelegateArgs& a_args, const std::string& a_text)
		{
			auto* mtm = RE::MenuTopicManager::GetSingleton();
			if (!mtm || !mtm->dialogueList) {
				return nullptr;
			}
			for (auto* dlg : *mtm->dialogueList) {
				if (dlg && dlg->topicText.c_str() && a_text == dlg->topicText.c_str()) {
					return dlg;
				}
			}
			if (a_args.GetArgCount() >= 1 && a_args[0].IsNumber()) {
				const auto   index = static_cast<std::int64_t>(a_args[0].GetNumber());
				std::int64_t i = 0;
				for (auto* dlg : *mtm->dialogueList) {
					if (dlg && i++ == index) {
						return dlg;
					}
				}
			}
			return nullptr;
		}

		// How the line should sound. A speech-check tag in the option text wins;
		// otherwise the emotion the game's dialogue data gives the NPC's reply
		// (every INFO response has one, 0-100) sets the mood of the exchange.
		Emotion::Mood MoodFor(const RE::FxDelegateArgs& a_args, const std::string& a_text, const Settings::Config& a_cfg)
		{
			using Source = Settings::EmotionSource;
			if (a_cfg.emotionSource == Source::kOff) {
				return {};
			}
			if (auto tagged = Emotion::FromTags(a_text)) {
				return *tagged;
			}
			if (a_cfg.emotionSource != Source::kTagsAndReply) {
				return {};
			}
			const auto* dlg = FindDialogue(a_args, a_text);
			if (!dlg) {
				return {};
			}
			// BSSimpleList's const begin() doesn't compile on MSVC; iterate a mutable view.
			auto& responses = const_cast<RE::MenuTopicManager::Dialogue*>(dlg)->responses;
			for (auto* resp : responses) {
				if (!resp) {
					continue;
				}
				const auto type = static_cast<int>(resp->animFaceArchType.get());
				const int  value = std::clamp(static_cast<int>(resp->percent), 0, 100);
				if (a_cfg.debugLog) {
					logger::info("Reply emotion: {} {}", Emotion::Name(static_cast<Emotion::Type>(std::clamp(type, 0, 7))), value);
				}
				if (type <= 0 || type >= static_cast<int>(Emotion::Type::kTotal) || value < a_cfg.emotionThreshold) {
					return {};
				}
				return Emotion::Mood{ static_cast<Emotion::Type>(type), value / 100.0f, "reply" };
			}
			return {};
		}

		// What the player just clicked. The list entry the menu shows is the
		// truth; the topic manager by index is the fallback for menu replacers
		// that restructure the movie.
		std::string SelectedText(const RE::FxDelegateArgs& a_args, bool a_log)
		{
			auto* movie = a_args.GetMovie();
			auto  fromList = FromMovie(movie, "_root.DialogueMenu_mc.TopicList.selectedEntry.text");
			auto  fromManager = FromTopicManager(a_args);
			auto  fromCopy = fromList.empty() && fromManager.empty() ?
			                     FromMovie(movie, "_root.DialogueMenu_mc.TopicListHolder.TextCopy_mc.textField.text") :
			                     std::string{};
			if (a_log) {
				logger::info("TopicClicked: list='{}' manager='{}' copy='{}'", fromList, fromManager, fromCopy);
			}
			if (!fromList.empty()) {
				return fromList;
			}
			if (!fromManager.empty()) {
				return fromManager;
			}
			return fromCopy;
		}

		// ---------------------------------------------------------------- callbacks

		void OnTopicClicked(const RE::FxDelegateArgs& a_args)
		{
			if (!g_origTopicClicked) {
				return;
			}
			const auto cfg = Snapshot();
			if (!cfg.enabled) {
				g_origTopicClicked(a_args);
				return;
			}

			auto text = SelectedText(a_args, cfg.debugLog);
			if (text.empty()) {
				g_origTopicClicked(a_args);
				return;
			}

			const auto mood = MoodFor(a_args, text, cfg);

			if (!cfg.waitForPlayer) {
				g_origTopicClicked(a_args);
				Speech::Say(std::move(text), mood, nullptr);
				return;
			}

			auto d = std::make_shared<Deferred>();
			d->id = g_nextId++;
			d->fn = g_origTopicClicked;
			d->handler = RE::GPtr<RE::FxDelegateHandler>(a_args.GetHandler());
			d->movie = RE::GPtr<RE::GFxMovieView>(a_args.GetMovie());
			d->responseID = ArgsAccess::ResponseID(a_args);
			for (std::uint32_t i = 0; i < a_args.GetArgCount(); ++i) {
				d->args.push_back(a_args[i]);
			}

			std::shared_ptr<Deferred> stale;
			{
				std::scoped_lock lock{ g_lock };
				stale = std::exchange(g_pending, d);
			}
			if (stale) {
				Fire(std::move(stale));  // should never happen, but never eat a click
			}

			const auto id = d->id;
			Speech::Say(std::move(text), mood, [id](bool a_cancelled) {
				if (auto held = Take(id)) {
					a_cancelled ? Release(std::move(held)) : Fire(std::move(held));
				}
			});

			// watchdog: whatever happens to the speech, the NPC must get its turn
			const auto wait = std::chrono::duration<float>(cfg.maxLineSeconds + cfg.responseDelay + 2.0f);
			std::thread([id, wait]() {
				std::this_thread::sleep_for(wait);
				if (auto held = Take(id)) {
					logger::warn("Speech did not finish in time, releasing the NPC");
					Fire(std::move(held));
				}
			}).detach();
		}

		void OnSkipText(const RE::FxDelegateArgs& a_args)
		{
			if (HasPending()) {
				// the NPC hasn't been asked yet: a click here belongs to us
				if (Snapshot().allowSkip) {
					Speech::Skip();
				}
				return;
			}
			if (g_origSkipText) {
				g_origSkipText(a_args);
			}
		}

		void OnCloseMenu(const RE::FxDelegateArgs& a_args)
		{
			if (Take(0)) {
				Speech::Cancel();
			}
			if (g_origCloseMenu) {
				g_origCloseMenu(a_args);
			}
		}

		// ---------------------------------------------------------------- Accept hook

		class ProcessorWrapper final : public CallbackProcessor
		{
		public:
			explicit ProcessorWrapper(CallbackProcessor* a_inner) :
				_inner(a_inner)
			{}

			void Process(const RE::GString& a_name, CallbackFn* a_method) override
			{
				if (a_name == "TopicClicked") {
					g_origTopicClicked = a_method;
					_inner->Process(a_name, &OnTopicClicked);
				} else if (a_name == "SkipText") {
					g_origSkipText = a_method;
					_inner->Process(a_name, &OnSkipText);
				} else if (a_name == "CloseMenu") {
					g_origCloseMenu = a_method;
					_inner->Process(a_name, &OnCloseMenu);
				} else {
					_inner->Process(a_name, a_method);
				}
			}

		private:
			CallbackProcessor* _inner;
		};

		struct AcceptHook
		{
			static void thunk(RE::DialogueMenu* a_this, CallbackProcessor* a_processor)
			{
				if (!a_processor) {
					func(a_this, a_processor);
					return;
				}
				ProcessorWrapper wrapper(a_processor);
				func(a_this, &wrapper);
				logger::debug("DialogueMenu callbacks wrapped (TopicClicked {}, SkipText {}, CloseMenu {})",
					g_origTopicClicked != nullptr, g_origSkipText != nullptr, g_origCloseMenu != nullptr);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// covers every way the menu can close (combat, scripts, the NPC walking off)
		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* GetSingleton()
			{
				static MenuSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && !a_event->opening && a_event->menuName == RE::DialogueMenu::MENU_NAME) {
					if (auto held = Take(0)) {
						Release(std::move(held));
						Speech::Cancel();
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_DialogueMenu[0] };
		AcceptHook::func = vtbl.write_vfunc(0x1, AcceptHook::thunk);
		logger::info("Hooked DialogueMenu::Accept");
	}

	void RegisterSinks()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
		}
	}

	void DropPending()
	{
		Release(Take(0));
	}

	bool HasPending()
	{
		std::scoped_lock lock{ g_lock };
		return g_pending != nullptr;
	}
}
