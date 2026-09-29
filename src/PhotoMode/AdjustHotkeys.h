#pragma once

// Hotkeys that nudge camera values while Photo Mode is open:
//   Field of View, Free Camera Translate Speed, View Roll, Global Time Multiplier,
//   plus actions: Camera Up/Down (hold) and Freeze Time (toggle, iFreezeTimeKey[Modifier] / iFreezeTimeGamePad[Modifier]).
//
// Each value has an Increase and a Decrease binding. Every binding has a keyboard
// combo and a gamepad combo, each made of a primary key plus an optional modifier
// (-1 = no modifier). Settings live in [Controls] of the MCM ini:
//
//   i<Name><Dir>Key, i<Name><Dir>KeyModifier, i<Name><Dir>GamePad, i<Name><Dir>GamePadModifier
//   f<Name>Step, f<Name>HoldSpeed
//   bSmoothHotkeyAdjust
//
// where <Name> is FOV / TranslateSpeed / ViewRoll / GlobalTime and <Dir> is Increase / Decrease.
//
// Step mode (bSmoothHotkeyAdjust = 0): a tap changes the value by f<Name>Step, holding
// for longer than a short delay changes it continuously at f<Name>HoldSpeed per second.
// Smooth mode (bSmoothHotkeyAdjust = 1): no step, the value changes continuously at
// f<Name>HoldSpeed per second from the instant the combo is pressed.

namespace PhotoMode::AdjustHotkeys
{
	void LoadSettings(const CSimpleIniA& a_ini);

	// a_key is the SKSE keycode (keyboard scancode, mouse + 256, gamepad 266+).
	// Returns true when the key is the primary key of an active adjust hotkey, in which case
	// the caller should not pass it on to other hotkeys or the UI.
	bool OnButtonEvent(std::uint32_t a_key, const RE::ButtonEvent* a_event);

	void OnFrameUpdate();

	// Whether the mouse should currently pan the camera (cursor hidden), based on iPanCameraMode
	// (0 = hold the pan key, 1 = toggle with the pan key, 2 = always, hold the pan key for the cursor).
	bool ShouldMousePan(bool a_panning, bool a_cursorOverWindow);

	// Forget all pressed keys (called when Photo Mode opens / resets / closes).
	void Reset();
}
