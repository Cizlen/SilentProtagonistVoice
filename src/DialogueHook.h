#pragma once

// Intercepts the dialogue menu's Scaleform callbacks:
//   TopicClicked - the player picked a line. We hum it first and only then
//                  pass the click on, so the NPC answers after the player.
//   SkipText     - clicking again while humming skips straight to the NPC.
//   CloseMenu    - leaving the conversation stops the hum.
//
// Done by hooking DialogueMenu::Accept (the vtable function that registers
// those callbacks with the menu's FxDelegate) and handing the game our
// functions in place of its own. No .swf edits, so any dialogue menu replacer
// that still calls GameDelegate "TopicClicked" keeps working.

namespace DialogueHook
{
	void Install();        // at plugin load
	void RegisterSinks();  // at kDataLoaded

	// Menu close / game load: drop a deferred click without firing it.
	void DropPending();

	[[nodiscard]] bool HasPending();
}
