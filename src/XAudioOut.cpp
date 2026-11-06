#include "PCH.h"

#include "XAudioOut.h"

// Win32 only in this file
#include <windows.h>
#include <xaudio2.h>

namespace XAudioOut
{
	namespace
	{
		using CreateFn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);

		struct Voice
		{
			IXAudio2SourceVoice* voice = nullptr;
			Render::Buffer       samples;  // must outlive the voice
			float                volume = 1.0f;
			float                fadeRate = 0.0f;  // volume per second while fading out
		};

		std::mutex                         g_lock;
		bool                               g_tried = false;
		IXAudio2*                          g_xaudio = nullptr;
		IXAudio2MasteringVoice*            g_master = nullptr;
		std::unordered_map<Id, Voice>      g_voices;
		Id                                 g_nextId = 1;
		constexpr std::size_t              kMaxVoices = 48;

		bool InitLocked()
		{
			if (g_tried) {
				return g_xaudio != nullptr;
			}
			g_tried = true;

			HMODULE dll = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!dll) {
				logger::warn("xaudio2_9.dll not found; processed playback unavailable");
				return false;
			}
			auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(dll, "XAudio2Create")));
			if (!create) {
				logger::warn("XAudio2Create not exported; processed playback unavailable");
				return false;
			}
			if (FAILED(create(&g_xaudio, 0, static_cast<XAUDIO2_PROCESSOR>(0)))) {
				logger::warn("XAudio2Create failed; processed playback unavailable");
				g_xaudio = nullptr;
				return false;
			}
			if (FAILED(g_xaudio->CreateMasteringVoice(&g_master))) {
				logger::warn("No audio output device for XAudio2; processed playback unavailable");
				g_xaudio->Release();
				g_xaudio = nullptr;
				return false;
			}
			logger::info("XAudio2 output ready");
			return true;
		}

		void DestroyLocked(Voice& a_v)
		{
			if (a_v.voice) {
				a_v.voice->Stop(0);
				a_v.voice->FlushSourceBuffers();
				a_v.voice->DestroyVoice();
				a_v.voice = nullptr;
			}
		}

		bool QueuedLocked(const Voice& a_v)
		{
			if (!a_v.voice) {
				return false;
			}
			XAUDIO2_VOICE_STATE state{};
			a_v.voice->GetState(&state);
			return state.BuffersQueued > 0;
		}
	}

	bool Available()
	{
		std::scoped_lock lock{ g_lock };
		return InitLocked();
	}

	Id Play(Render::Buffer a_samples, std::uint32_t a_sampleRate, float a_volume)
	{
		if (!a_samples || a_samples->empty() || a_sampleRate == 0) {
			return 0;
		}
		std::scoped_lock lock{ g_lock };
		if (!InitLocked() || g_voices.size() >= kMaxVoices) {
			return 0;
		}

		WAVEFORMATEX fmt{};
		fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		fmt.nChannels = 1;
		fmt.nSamplesPerSec = a_sampleRate;
		fmt.wBitsPerSample = 32;
		fmt.nBlockAlign = 4;
		fmt.nAvgBytesPerSec = a_sampleRate * 4;
		fmt.cbSize = 0;

		Voice v;
		v.samples = std::move(a_samples);
		v.volume = std::clamp(a_volume, 0.0f, 1.0f);
		if (FAILED(g_xaudio->CreateSourceVoice(&v.voice, &fmt))) {
			return 0;
		}

		XAUDIO2_BUFFER buf{};
		buf.Flags = XAUDIO2_END_OF_STREAM;
		buf.AudioBytes = static_cast<UINT32>(v.samples->size() * sizeof(float));
		buf.pAudioData = reinterpret_cast<const BYTE*>(v.samples->data());
		if (FAILED(v.voice->SubmitSourceBuffer(&buf))) {
			v.voice->DestroyVoice();
			return 0;
		}
		v.voice->SetVolume(v.volume);
		v.voice->Start(0);

		const Id id = g_nextId++;
		if (g_nextId == 0) {
			g_nextId = 1;
		}
		g_voices.emplace(id, std::move(v));
		return id;
	}

	void Stop(Id a_id, float a_fadeSeconds)
	{
		std::scoped_lock lock{ g_lock };
		auto             it = g_voices.find(a_id);
		if (it == g_voices.end()) {
			return;
		}
		if (a_fadeSeconds <= 0.0f) {
			DestroyLocked(it->second);
			g_voices.erase(it);
		} else {
			it->second.fadeRate = std::max(it->second.fadeRate, it->second.volume / a_fadeSeconds);
		}
	}

	void StopAll()
	{
		std::scoped_lock lock{ g_lock };
		for (auto& [id, v] : g_voices) {
			DestroyLocked(v);
		}
		g_voices.clear();
	}

	bool IsPlaying(Id a_id)
	{
		std::scoped_lock lock{ g_lock };
		auto             it = g_voices.find(a_id);
		return it != g_voices.end() && QueuedLocked(it->second);
	}

	void Update(float a_dt)
	{
		std::scoped_lock lock{ g_lock };
		for (auto it = g_voices.begin(); it != g_voices.end();) {
			auto& v = it->second;
			if (v.fadeRate > 0.0f) {
				v.volume -= v.fadeRate * a_dt;
				if (v.voice) {
					v.voice->SetVolume(std::max(0.0f, v.volume));
				}
			}
			if (v.volume <= 0.0f || !QueuedLocked(v)) {
				DestroyLocked(v);
				it = g_voices.erase(it);
			} else {
				++it;
			}
		}
	}
}
