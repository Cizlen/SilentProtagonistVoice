#include "PCH.h"

#include "VoicePacks.h"

#include "Settings.h"

#include <SimpleIni.h>

namespace VoicePacks
{
	namespace
	{
		std::mutex                                                   g_lock;
		std::vector<std::shared_ptr<const Pack>>                     g_packs;
		std::unordered_map<std::string, std::vector<std::size_t>>    g_bags;  // shuffle bags, key = pack|cat
		std::unordered_map<std::string, std::size_t>                 g_last;

		std::string Lower(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		std::string PathToUtf8(const std::filesystem::path& a_p)
		{
			const auto u8 = a_p.u8string();
			return std::string(u8.begin(), u8.end());
		}

		void ReadPackIni(Pack& a_pack, const std::filesystem::path& a_dir)
		{
			const auto iniPath = a_dir / "voice.ini";
			std::error_code ec;
			if (!std::filesystem::exists(iniPath, ec)) {
				return;
			}
			CSimpleIniA ini;
			ini.SetUnicode();
			if (ini.LoadFile(iniPath.string().c_str()) < 0) {
				logger::warn("Could not read {}", iniPath.string());
				return;
			}
			a_pack.description = ini.GetValue("Pack", "Description", "");
			a_pack.sex = Lower(ini.GetValue("Pack", "Sex", "any"));
			const std::string mode = Lower(ini.GetValue("Pack", "Mode", ""));
			if (mode == "auto") {
				a_pack.modeOverride = 0;
			} else if (mode == "babble") {
				a_pack.modeOverride = 1;
			} else if (mode == "phrase") {
				a_pack.modeOverride = 2;
			}
			a_pack.basePitch = std::clamp(static_cast<float>(ini.GetDoubleValue("Pack", "BasePitch", 1.0)), 0.5f, 2.0f);
			a_pack.pitchVariation = static_cast<float>(ini.GetDoubleValue("Pack", "PitchVariation", -1.0));
			a_pack.speed = std::clamp(static_cast<float>(ini.GetDoubleValue("Pack", "Speed", 1.0)), 0.4f, 2.5f);
			a_pack.gain = std::clamp(static_cast<float>(ini.GetDoubleValue("Pack", "Gain", 1.0)), 0.0f, 2.0f);
			a_pack.syllablesPerBlip = static_cast<float>(ini.GetDoubleValue("Pack", "SyllablesPerBlip", -1.0));
			a_pack.throat = std::clamp(static_cast<float>(ini.GetDoubleValue("Pack", "Throat", 0.0)), -1.0f, 1.0f);
			a_pack.breath = std::clamp(static_cast<float>(ini.GetDoubleValue("Pack", "Breath", 0.0)), -1.0f, 1.0f);
			a_pack.brightness = std::clamp(static_cast<float>(ini.GetDoubleValue("Pack", "Brightness", 0.0)), -1.0f, 1.0f);
			a_pack.expression = std::clamp(static_cast<float>(ini.GetDoubleValue("Pack", "Expression", 0.0)), -1.0f, 1.0f);
		}
	}

	Category ForEmotion(int a_emotionType)
	{
		// Emotion::Type: 0 neutral, 1 anger ... 7 puzzled
		if (a_emotionType < 1 || a_emotionType > 7) {
			return Category::kTotal;
		}
		return static_cast<Category>(static_cast<int>(Category::kAnger) + a_emotionType - 1);
	}

	std::filesystem::path RootOnDisk()
	{
		return std::filesystem::path("Data") / "Sound" / "FX" / "SilentProtagonistVoice" / "voices";
	}

	void Scan()
	{
		std::vector<std::shared_ptr<const Pack>> found;
		const auto                               root = RootOnDisk();
		std::error_code                          ec;

		if (!std::filesystem::is_directory(root, ec)) {
			logger::warn("Voice folder {} not found; no voice packs loaded", root.string());
		} else {
			for (const auto& packDir : std::filesystem::directory_iterator(root, ec)) {
				if (!packDir.is_directory()) {
					continue;
				}
				auto pack = std::make_shared<Pack>();
				pack->name = PathToUtf8(packDir.path().filename());
				ReadPackIni(*pack, packDir.path());

				for (int c = 0; c < static_cast<int>(Category::kTotal); ++c) {
					const auto catDir = packDir.path() / kFolderNames[c];
					if (!std::filesystem::is_directory(catDir, ec)) {
						continue;
					}
					std::vector<std::filesystem::path> files;
					for (const auto& f : std::filesystem::directory_iterator(catDir, ec)) {
						if (f.is_regular_file() && Lower(f.path().extension().string()) == ".wav") {
							files.push_back(f.path());
						}
					}
					std::ranges::sort(files);
					for (const auto& f : files) {
						const auto gamePath = std::format("{}\\{}\\{}\\{}", kGameRoot, pack->name, kFolderNames[c], PathToUtf8(f.filename()));
						if (auto info = Wav::Load(f, gamePath); info && info->duration > 0.02f) {
							pack->clips[c].push_back(std::move(info));
						}
					}
				}

				if (pack->Usable()) {
					logger::info("Voice pack '{}': {} syllables, {} short, {} medium, {} long, {} question, {} exclaim, {} emotion",
						pack->name, pack->Count(Category::kSyllables), pack->Count(Category::kShort), pack->Count(Category::kMedium),
						pack->Count(Category::kLong), pack->Count(Category::kQuestion), pack->Count(Category::kExclaim), pack->EmotionClipCount());
					found.push_back(std::move(pack));
				} else {
					logger::warn("Folder '{}' has no usable .wav files, skipped", pack->name);
				}
			}
		}

		std::ranges::sort(found, [](const auto& a, const auto& b) { return Lower(a->name) < Lower(b->name); });

		std::scoped_lock lock{ g_lock };
		g_packs = std::move(found);
		g_bags.clear();
		g_last.clear();
	}

	std::vector<std::string> Names()
	{
		std::scoped_lock         lock{ g_lock };
		std::vector<std::string> out;
		for (const auto& p : g_packs) {
			out.push_back(p->name);
		}
		return out;
	}

	std::shared_ptr<const Pack> Find(const std::string& a_name)
	{
		std::scoped_lock lock{ g_lock };
		const auto       want = Lower(a_name);
		for (const auto& p : g_packs) {
			if (Lower(p->name) == want) {
				return p;
			}
		}
		return nullptr;
	}

	std::shared_ptr<const Pack> ForPlayer()
	{
		bool female = false;
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			if (auto* base = player->GetActorBase()) {
				female = base->GetSex() == RE::SEXES::kFemale;
			}
		}

		const auto        cfg = Settings::Snapshot();
		const std::string wanted = female ? cfg.packFemale : cfg.packMale;
		if (auto p = Find(wanted)) {
			return p;
		}

		// fall back: a pack tagged for this sex, then anything
		std::scoped_lock lock{ g_lock };
		const auto       sex = female ? "female"s : "male"s;
		for (const auto& p : g_packs) {
			if (p->sex == sex) {
				return p;
			}
		}
		return g_packs.empty() ? nullptr : g_packs.front();
	}

	std::shared_ptr<Wav::Info> Pick(const Pack& a_pack, Category a_cat, std::mt19937& a_rng)
	{
		const auto& list = a_pack.clips[static_cast<int>(a_cat)];
		if (list.empty()) {
			return nullptr;
		}
		if (list.size() == 1) {
			return list.front();
		}

		std::scoped_lock lock{ g_lock };
		const auto       key = std::format("{}|{}", a_pack.name, static_cast<int>(a_cat));
		auto&            bag = g_bags[key];
		if (bag.empty()) {
			bag.resize(list.size());
			for (std::size_t i = 0; i < bag.size(); ++i) {
				bag[i] = i;
			}
			std::ranges::shuffle(bag, a_rng);
			// never start a new round with the clip that ended the last one
			if (auto it = g_last.find(key); it != g_last.end() && bag.back() == it->second) {
				std::swap(bag.back(), bag.front());
			}
		}
		const auto idx = bag.back();
		bag.pop_back();
		g_last[key] = idx;
		return idx < list.size() ? list[idx] : list.front();
	}
}
