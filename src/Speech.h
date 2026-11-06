#pragma once

#include "Planner.h"

// The player's "voice": builds a plan of clips from a line of text and its
// mood, plays it, animates the mouth and face, and tells the caller when it's
// done so the NPC can answer. Everything that touches the engine runs in
// Tick(), on the main thread.

namespace Speech
{
	using Planner::Plan;
	using Planner::PlannedClip;

	// a_cancelled = true when the line was cut off by the menu closing or a load
	using DoneCallback = std::function<void(bool a_cancelled)>;

	// Thread-safe. Queues a line; it starts on the next Tick. a_onDone runs on
	// the main thread exactly once.
	void Say(std::string a_text, Emotion::Mood a_mood, DoneCallback a_onDone);

	// Thread-safe, plays immediately (even while the game is paused in a menu)
	// through processed playback; no mouth. For the settings page.
	bool PreviewNow(const std::string& a_text, Emotion::Mood a_mood);

	void Skip();    // thread-safe: cut the hum short, the line counts as finished
	void Cancel();  // thread-safe: stop everything, a_onDone(true)

	[[nodiscard]] bool Busy();
	[[nodiscard]] std::string LastSummary();

	// which playback is actually in use right now, for the menu
	[[nodiscard]] const char* ActivePlaybackName();

	void Tick();  // main thread, every frame
}
