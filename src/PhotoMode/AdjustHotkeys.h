#pragma once

// Hotkeys that nudge camera values while Photo Mode is open:
//   Field of View, Free Camera Translate Speed, View Roll, Global Time Multiplier,
//   plus actions: Camera Up/Down (hold), Menu Up/Down (iMenuNavUp*/iMenuNavDown*) and Freeze Time (toggle, iFreezeTimeKey[Modifier] / iFreezeTimeGamePad[Modifier]).
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

	// Once per rendered frame while Photo Mode is open, including while its UI is hidden.
	void OnFrameUpdate();

	// Whether the mouse should currently pan the camera (cursor hidden), based on iPanCameraMode
	// (0 = hold the pan key, 1 = toggle with the pan key, 2 = always, hold the pan key for the cursor).
	bool ShouldMousePan(bool a_panning, bool a_cursorOverWindow);

	// Move Up / Move Down hotkeys: 1 = up held, -1 = down held, 0 = neither (or both).
	int GetCameraMoveDirection();

	// Whether a key (SKSE keycode) is currently held. Used for the modifiers of PhotoMode's other hotkeys.
	bool IsKeyHeld(std::int32_t a_key);

	// Whether a key is the main key of any hotkey combo that is currently active (including PhotoMode's own
	// Next Tab / Take Photo / ... keys). Such keys are not passed on to the menu.
	bool IsKeyClaimed(std::uint32_t a_key);

	// Whether a Modifier+Key combo using this key is currently held, so a plain binding on the key should not fire.
	bool IsOverriddenByModifierCombo(std::uint32_t a_key);

	// The menu reads the controller directly (XInput) every frame. Called with that raw state before the menu sees it:
	// removes buttons that belong to hotkeys being pressed and buttons used as modifiers, and presses the d-pad for
	// gamepad Menu Up / Down bindings. Only while Photo Mode is open.
	void FilterGamepadForMenu(std::uint16_t& a_buttons, std::uint8_t& a_leftTrigger, std::uint8_t& a_rightTrigger);

	// Forget all pressed keys (called when Photo Mode opens / resets / closes).
	void Reset();
}
