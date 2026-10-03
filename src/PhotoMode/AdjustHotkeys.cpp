#include "AdjustHotkeys.h"

#include "CameraModes.h"
#include "Manager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <set>

namespace PhotoMode::AdjustHotkeys
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr float holdDelay = 0.25f;        // step mode: seconds a key must be held before continuous change starts
		constexpr float staleKeyTimeout = 1.0f;   // a key with no events for this long is treated as released (lost key-up safety)
		constexpr float maxFrameDelta = 0.1f;     // clamp long frames so a hitch doesn't cause a big jump
		constexpr std::int32_t kNone = -1;

		struct Combo
		{
			std::int32_t primary{ kNone };
			std::int32_t modifier{ kNone };

			[[nodiscard]] bool IsValid() const { return primary >= 0; }
			[[nodiscard]] bool HasModifier() const { return modifier >= 0; }

			void Load(const CSimpleIniA& a_ini, const std::string& a_setting)
			{
				primary = static_cast<std::int32_t>(a_ini.GetLongValue("Controls", a_setting.c_str(), primary));
				modifier = static_cast<std::int32_t>(a_ini.GetLongValue("Controls", (a_setting + "Modifier").c_str(), modifier));
				if (primary < 0) {
					primary = kNone;
				}
				if (modifier < 0 || modifier == primary) {
					modifier = kNone;
				}
			}
		};

		struct Binding
		{
			Combo keyboard;
			Combo gamePad;

			// runtime
			const Combo*      matched{ nullptr };  // combo that is currently held (before specificity filtering)
			bool              active{ false };
			Clock::time_point activatedAt{};
			bool              navArrowDown{ false };  // menu nav: we pressed an arrow key in the menu for this binding
		};

		struct Control
		{
			const char* name;  // settings name, e.g. "FOV"
			float       step;
			float       holdSpeed;
			float       min;
			float       max;
			float (*get)();
			void (*set)(float);

			Binding increase;
			Binding decrease;

			// hold actions (camera up/down) have no value: they act only while held
			bool        holdAction{ false };
			const char* increaseName{ "Increase" };
			const char* decreaseName{ "Decrease" };

			// toggle actions (freeze time) run once per press of the "increase" binding; there is no decrease binding
			using Action = void (*)();
			Action onPress{ nullptr };

			// the mouse pan key: tracked here for combos, but never swallowed (the UI still sees e.g. Shift)
			bool panAction{ false };

			// menu navigation: "increase" = up, "decrease" = down; held like the arrow keys / d-pad
			bool menuNav{ false };

			// PhotoMode's own hotkeys (Next Tab, Take Photo, ...): their action is run by Input.cpp, but they take part
			// here so a Modifier+Key combo of theirs beats a plain Key binding, and so the menu doesn't also react to them
			bool passthrough{ false };

			// a single binding that only matters while held (Level Movement); read by other code via IsLevelMoveHeld
			bool holdOnly{ false };

			[[nodiscard]] bool IsAction() const { return holdAction || onPress || panAction || menuNav || passthrough || holdOnly; }
		};

		float GetFOV() { return RE::PlayerCamera::GetSingleton()->worldFOV; }
		void  SetFOV(float a_value) { RE::PlayerCamera::GetSingleton()->worldFOV = a_value; }

		float GetTranslateSpeed() { return FreeCamera::translateSpeed; }
		void  SetTranslateSpeed(float a_value) { FreeCamera::translateSpeed = a_value; }

		float GetViewRoll() { return RE::rad_to_deg(MANAGER(PhotoMode)->GetViewRoll()); }
		void  SetViewRoll(float a_value) { MANAGER(PhotoMode)->SetViewRoll(RE::deg_to_rad(a_value)); }

		void ToggleFreezeTime()
		{
			auto& freezeTime = RE::Main::GetSingleton()->freezeTime;
			freezeTime = !freezeTime;
		}

		void SwitchToPhotoCam() { CameraModes::SetMode(CameraModes::kPhoto); }
		void SwitchToFreezeCam() { CameraModes::SetMode(CameraModes::kFreeze); }
		void SwitchToReleaseCam() { CameraModes::SetMode(CameraModes::kRelease); }
		void SwitchToFollowCam() { CameraModes::SetMode(CameraModes::kFollow); }

		float GetGlobalTime() { return RE::BSTimer::QGlobalTimeMultiplier(); }
		void  SetGlobalTime(float a_value) { RE::BSTimer::GetSingleton()->SetGlobalTimeMultiplier(a_value, true); }

		// ranges match the sliders on the Camera and Time tabs
		std::array<Control, 18> controls{ {
			{ "FOV", 1.0f, 30.0f, 5.0f, 150.0f, GetFOV, SetFOV,
				{ { 78, kNone }, { kNone, kNone } },     // Numpad +
				{ { 74, kNone }, { kNone, kNone } } },   // Numpad -
			{ "TranslateSpeed", 0.5f, 5.0f, 0.1f, 50.0f, GetTranslateSpeed, SetTranslateSpeed,
				{ { 55, kNone }, { kNone, kNone } },     // Numpad *
				{ { 181, kNone }, { kNone, kNone } } },  // Numpad /
			{ "ViewRoll", 1.0f, 20.0f, -90.0f, 90.0f, GetViewRoll, SetViewRoll,
				{ { 73, kNone }, { kNone, kNone } },     // Numpad 9
				{ { 71, kNone }, { kNone, kNone } } },   // Numpad 7
			{ "GlobalTime", 0.05f, 0.5f, 0.01f, 2.0f, GetGlobalTime, SetGlobalTime,
				{ { 81, kNone }, { kNone, kNone } },     // Numpad 3
				{ { 79, kNone }, { kNone, kNone } } },   // Numpad 1
			// free camera move up / down (in addition to the game's own buttons); unbound by default
			{ "Camera", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr,
				{ { kNone, kNone }, { kNone, kNone } },
				{ { kNone, kNone }, { kNone, kNone } },
				true, "Up", "Down" },
			// freeze time toggle: keeps PhotoMode's existing iFreezeTimeKey / iFreezeTimeGamePad settings
			{ "FreezeTime", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr,
				{ { 33, kNone }, { kNone, kNone } },  // F
				{ { kNone, kNone }, { kNone, kNone } },
				false, "", "", ToggleFreezeTime },
			// mouse pan key: keeps PhotoMode's existing iPanCameraKey setting, adds iPanCameraKeyModifier
			{ "PanCamera", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr,
				{ { 42, kNone }, { kNone, kNone } },  // Left Shift
				{ { kNone, kNone }, { kNone, kNone } },
				false, "", "", nullptr, true },
			// move up / down in the Photo Mode menu; unbound by default
			{ "MenuNav", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr,
				{ { kNone, kNone }, { kNone, kNone } },
				{ { kNone, kNone }, { kNone, kNone } },
				false, "Up", "Down", nullptr, false, true },
			// PhotoMode's misc hotkeys (bindings come from i<Name>Key / i<Name>GamePad and their Modifier settings)
			{ "NextTab", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", nullptr, false, false, true },
			{ "PreviousTab", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", nullptr, false, false, true },
			{ "TakePhoto", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", nullptr, false, false, true },
			{ "ToggleMenus", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", nullptr, false, false, true },
			{ "Reset", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", nullptr, false, false, true },
			// camera mode switches (i<Name>Key / i<Name>GamePad + Modifier); unbound by default
			{ "PhotoCam", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", SwitchToPhotoCam },
			{ "FreezeCam", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", SwitchToFreezeCam },
			{ "ReleaseCam", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", SwitchToReleaseCam },
			{ "FollowCam", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", SwitchToFollowCam },
			// level movement (iLevelMoveKey / iLevelMoveGamePad + Modifier): while held, moving forward / back stays horizontal; unbound by default
			{ "LevelMove", 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr, {}, {}, false, "", "", nullptr, false, false, false, true },
		} };

		// primaries of Modifier+Key combos that are currently held (a plain Key binding on these is overridden)
		std::set<std::int32_t> primariesWithModifierHeld;

		// Press / release the up or down arrow key in the Photo Mode menu (keyboard Menu Up/Down bindings).
		// Gamepad Menu Up/Down bindings press the d-pad instead, in FilterGamepadForMenu.
		void SendMenuArrow(float a_direction, bool a_down)
		{
			if (!ImGui::GetCurrentContext()) {
				return;
			}
			auto& io = ImGui::GetIO();
			if (a_down) {
				io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // needed when navigating with the mouse
			}
			io.AddKeyEvent(a_direction > 0.0f ? ImGuiKey_UpArrow : ImGuiKey_DownArrow, a_down);
		}

		enum PanMode : std::int32_t
		{
			kPanHold = 0,    // hold the pan key to pan
			kPanToggle = 1,  // press the pan key to switch panning on / off
			kPanAlways = 2   // mouse always pans; hold the pan key to show the cursor
		};

		std::int32_t panMode{ kPanHold };
		bool         panToggledOn{ false };

		Control& PanControl()
		{
			return *std::ranges::find_if(controls, [](const Control& a_control) { return a_control.panAction; });
		}

		std::int16_t lastMoveDirection{ 0 };  // vertical direction we last set on the free camera

		bool smoothMode{ false };

		// bShowGlobalTimeOnRelease: show the new Global Time Multiplier once its Increase / Decrease hotkeys are let go
		bool showGlobalTimeOnRelease{ true };
		bool globalTimeHeld{ false };

		Control& GlobalTimeControl()
		{
			return *std::ranges::find_if(controls, [](const Control& a_control) { return std::string_view{ a_control.name } == "GlobalTime"; });
		}

		void ShowGlobalTime()
		{
			static std::string message;  // kept alive in case the HUD keeps the pointer
			message = std::format("{}: {:.2f}x", TRANSLATE("$PM_GlobalTimeMult"), GetGlobalTime());
			RE::SendHUDMessage::ShowHUDMessage(message.c_str());
		}

		std::map<std::uint32_t, Clock::time_point> pressedKeys;  // key -> last time an event said it was down
		Clock::time_point                          lastUpdate{};

		template <class F>
		void ForEachBinding(F&& a_func)
		{
			for (auto& control : controls) {
				a_func(control, control.increase, 1.0f);
				a_func(control, control.decrease, -1.0f);
			}
		}

		bool IsPressed(std::int32_t a_key)
		{
			return a_key >= 0 && pressedKeys.contains(static_cast<std::uint32_t>(a_key));
		}

		bool IsComboDown(const Combo& a_combo)
		{
			return a_combo.IsValid() && IsPressed(a_combo.primary) && (!a_combo.HasModifier() || IsPressed(a_combo.modifier));
		}

		void AddToValue(Control& a_control, float a_delta)
		{
			if (a_delta == 0.0f || a_control.IsAction()) {
				return;
			}
			a_control.set(std::clamp(a_control.get() + a_delta, a_control.min, a_control.max));
		}

		// Recompute which bindings are active from the currently pressed keys.
		void Evaluate(Clock::time_point a_now)
		{
			// 1. find the held combo for every binding (prefer a combo that uses a modifier)
			primariesWithModifierHeld.clear();
			ForEachBinding([&](Control&, Binding& a_binding, float) {
				a_binding.matched = nullptr;
				for (const auto* combo : { &a_binding.keyboard, &a_binding.gamePad }) {
					if (IsComboDown(*combo) && (!a_binding.matched || combo->HasModifier())) {
						a_binding.matched = combo;
					}
				}
				if (a_binding.matched && a_binding.matched->HasModifier()) {
					primariesWithModifierHeld.insert(a_binding.matched->primary);
				}
			});

			// 2. most specific combo wins: while Modifier+Key is held, a plain Key binding stays inactive
			ForEachBinding([&](Control& a_control, Binding& a_binding, float a_direction) {
				const auto* matched = a_binding.matched;
				const bool  nowActive = matched && (matched->HasModifier() || !primariesWithModifierHeld.contains(matched->primary));

				if (a_control.menuNav) {
					const bool wantArrow = nowActive && matched == &a_binding.keyboard;
					if (wantArrow != a_binding.navArrowDown) {
						SendMenuArrow(a_direction, wantArrow);
						a_binding.navArrowDown = wantArrow;
					}
				}

				if (nowActive && !a_binding.active) {
					a_binding.activatedAt = a_now;
					if (a_control.panAction) {
						panToggledOn = !panToggledOn;
					} else if (a_control.onPress) {
						a_control.onPress();
					} else if (!smoothMode) {
						AddToValue(a_control, a_direction * a_control.step);  // tap = one step
					}
				}
				a_binding.active = nowActive;
			});

			// Global Time: report the value once both of its hotkeys have been let go
			const auto& globalTime = GlobalTimeControl();
			const bool  globalTimeNowHeld = globalTime.increase.active || globalTime.decrease.active;
			if (globalTimeHeld && !globalTimeNowHeld && showGlobalTimeOnRelease) {
				ShowGlobalTime();
			}
			globalTimeHeld = globalTimeNowHeld;
		}

		// Move the free camera up/down while the Camera Up/Down hotkeys are held.
		void UpdateCameraMove()
		{
			const auto& move = *std::ranges::find_if(controls, [](const Control& a_control) { return a_control.holdAction; });
			const auto  direction = static_cast<std::int16_t>((move.increase.active ? 1 : 0) - (move.decrease.active ? 1 : 0));
			if (direction == 0 && lastMoveDirection == 0) {
				return;  // not ours: leave the game's own up/down buttons alone
			}

			const auto camera = RE::PlayerCamera::GetSingleton();
			if (camera && camera->IsInFreeCameraMode()) {
				if (const auto freeCamera = static_cast<RE::FreeCameraState*>(camera->currentState.get())) {
					freeCamera->verticalDirection = direction;
				}
			}
			lastMoveDirection = direction;
		}

		bool IsPrimaryOfActiveBinding(std::uint32_t a_key)
		{
			bool result = false;
			ForEachBinding([&](Control& a_control, Binding& a_binding, float) {
				if (!a_control.panAction && !a_control.passthrough && a_binding.active && a_binding.matched && static_cast<std::uint32_t>(a_binding.matched->primary) == a_key) {
					result = true;
				}
			});
			return result;
		}
	}

	void LoadSettings(const CSimpleIniA& a_ini)
	{
		smoothMode = a_ini.GetBoolValue("Controls", "bSmoothHotkeyAdjust", smoothMode);
		showGlobalTimeOnRelease = a_ini.GetBoolValue("Controls", "bShowGlobalTimeOnRelease", showGlobalTimeOnRelease);
		panMode = std::clamp(static_cast<std::int32_t>(a_ini.GetLongValue("Controls", "iPanCameraMode", panMode)), 0, 2);

		for (auto& control : controls) {
			const std::string name{ control.name };

			const std::string inc{ control.increaseName };
			const std::string dec{ control.decreaseName };

			control.increase.keyboard.Load(a_ini, "i" + name + inc + "Key");
			control.increase.gamePad.Load(a_ini, "i" + name + inc + "GamePad");
			if (control.onPress || control.panAction || control.passthrough || control.holdOnly) {
				continue;  // single binding, no step/speed settings
			}
			control.decrease.keyboard.Load(a_ini, "i" + name + dec + "Key");
			control.decrease.gamePad.Load(a_ini, "i" + name + dec + "GamePad");

			if (control.IsAction()) {
				continue;  // no step / speed settings
			}

			control.step = std::max(0.0f, static_cast<float>(a_ini.GetDoubleValue("Controls", ("f" + name + "Step").c_str(), control.step)));
			control.holdSpeed = std::max(0.0f, static_cast<float>(a_ini.GetDoubleValue("Controls", ("f" + name + "HoldSpeed").c_str(), control.holdSpeed)));
		}

		Reset();
	}

	bool OnButtonEvent(std::uint32_t a_key, const RE::ButtonEvent* a_event)
	{
		const auto now = Clock::now();
		const bool wasConsumed = IsPrimaryOfActiveBinding(a_key);

		if (a_event->IsPressed()) {
			pressedKeys[a_key] = now;
		} else {
			pressedKeys.erase(a_key);
		}

		Evaluate(now);

		return wasConsumed || IsPrimaryOfActiveBinding(a_key);
	}

	void OnFrameUpdate()
	{
		const auto now = Clock::now();

		const float deltaTime = lastUpdate == Clock::time_point{} ?
		                            0.0f :
		                            std::min(std::chrono::duration<float>(now - lastUpdate).count(), maxFrameDelta);
		lastUpdate = now;

		// held keys send an event every frame; drop any key we haven't heard from in a while
		std::erase_if(pressedKeys, [&](const auto& a_entry) {
			return std::chrono::duration<float>(now - a_entry.second).count() > staleKeyTimeout;
		});
		Evaluate(now);
		UpdateCameraMove();

		if (deltaTime <= 0.0f) {
			return;
		}

		for (auto& control : controls) {
			if (control.IsAction()) {
				continue;
			}
			float direction = 0.0f;
			for (const auto& [binding, sign] : { std::pair{ &control.increase, 1.0f }, std::pair{ &control.decrease, -1.0f } }) {
				if (binding->active && (smoothMode || std::chrono::duration<float>(now - binding->activatedAt).count() > holdDelay)) {
					direction += sign;
				}
			}
			AddToValue(control, direction * control.holdSpeed * deltaTime);
		}
	}

	int GetCameraMoveDirection()
	{
		const auto& move = *std::ranges::find_if(controls, [](const Control& a_control) { return a_control.holdAction; });
		return (move.increase.active ? 1 : 0) - (move.decrease.active ? 1 : 0);
	}

	bool IsLevelMoveHeld()
	{
		const auto& level = *std::ranges::find_if(controls, [](const Control& a_control) { return a_control.holdOnly; });
		return level.increase.active;
	}

	bool IsKeyHeld(std::int32_t a_key)
	{
		return IsPressed(a_key);
	}

	bool IsKeyClaimed(std::uint32_t a_key)
	{
		bool result = false;
		ForEachBinding([&](Control& a_control, Binding& a_binding, float) {
			if (!a_control.panAction && a_binding.active && a_binding.matched && static_cast<std::uint32_t>(a_binding.matched->primary) == a_key) {
				result = true;
			}
		});
		return result;
	}

	bool IsOverriddenByModifierCombo(std::uint32_t a_key)
	{
		return primariesWithModifierHeld.contains(static_cast<std::int32_t>(a_key));
	}

	void FilterGamepadForMenu(std::uint16_t& a_buttons, std::uint8_t& a_leftTrigger, std::uint8_t& a_rightTrigger)
	{
		const auto photoMode = MANAGER(PhotoMode);
		if (!photoMode->IsActive() || photoMode->ShouldBlockInput()) {
			return;  // e.g. the gallery uses the d-pad for its own navigation
		}

		// XInput button bits -> SKSE gamepad keycodes (266 = d-pad up ... 279 = Y, 280 = LT, 281 = RT)
		constexpr std::array<std::pair<std::uint16_t, std::int32_t>, 14> kButtons{ {
			{ 0x0001, 266 }, { 0x0002, 267 }, { 0x0004, 268 }, { 0x0008, 269 },  // d-pad up, down, left, right
			{ 0x0010, 270 }, { 0x0020, 271 }, { 0x0040, 272 }, { 0x0080, 273 },  // start, back, left stick, right stick
			{ 0x0100, 274 }, { 0x0200, 275 },                                    // LB, RB
			{ 0x1000, 276 }, { 0x2000, 277 }, { 0x4000, 278 }, { 0x8000, 279 }   // A, B, X, Y
		} };
		constexpr std::int32_t kLT = 280;
		constexpr std::int32_t kRT = 281;
		constexpr std::uint8_t kTriggerThreshold = 30;  // XINPUT_GAMEPAD_TRIGGER_THRESHOLD

		std::set<std::int32_t> down;
		for (const auto& [mask, key] : kButtons) {
			if (a_buttons & mask) {
				down.insert(key);
			}
		}
		if (a_leftTrigger > kTriggerThreshold) {
			down.insert(kLT);
		}
		if (a_rightTrigger > kTriggerThreshold) {
			down.insert(kRT);
		}

		const auto comboDown = [&](const Combo& a_combo) {
			return a_combo.IsValid() && down.contains(a_combo.primary) && (!a_combo.HasModifier() || down.contains(a_combo.modifier));
		};

		// Hide from the menu: the main button of any hotkey combo being pressed, and any button used as a modifier
		// (modifiers are reserved, so pressing one first doesn't also do something in the menu).
		std::set<std::int32_t> hidden;
		std::set<std::int32_t> withModifier;
		ForEachBinding([&](Control& a_control, Binding& a_binding, float) {
			const auto& combo = a_binding.gamePad;
			if (a_control.panAction || !combo.IsValid()) {
				return;
			}
			if (combo.HasModifier()) {
				hidden.insert(combo.modifier);
			}
			if (comboDown(combo)) {
				hidden.insert(combo.primary);
				if (combo.HasModifier()) {
					withModifier.insert(combo.primary);
				}
			}
		});

		// Gamepad Menu Up / Down: hold the d-pad for the menu while the combo is held (so it repeats like the d-pad)
		std::uint16_t forced = 0;
		const auto&   nav = *std::ranges::find_if(controls, [](const Control& a_control) { return a_control.menuNav; });
		for (const auto& [binding, mask] : { std::pair{ &nav.increase, std::uint16_t{ 0x0001 } }, std::pair{ &nav.decrease, std::uint16_t{ 0x0002 } } }) {
			const auto& combo = binding->gamePad;
			if (comboDown(combo) && (combo.HasModifier() || !withModifier.contains(combo.primary))) {
				forced = static_cast<std::uint16_t>(forced | mask);
			}
		}

		for (const auto& [mask, key] : kButtons) {
			if (hidden.contains(key)) {
				a_buttons = static_cast<std::uint16_t>(a_buttons & ~mask);
			}
		}
		if (hidden.contains(kLT)) {
			a_leftTrigger = 0;
		}
		if (hidden.contains(kRT)) {
			a_rightTrigger = 0;
		}
		a_buttons = static_cast<std::uint16_t>(a_buttons | forced);
	}

	bool ShouldMousePan(bool a_panning, bool a_cursorOverWindow)
	{
		const bool keyHeld = PanControl().increase.active;
		switch (panMode) {
		case kPanToggle:
			return panToggledOn;
		case kPanAlways:
			return !keyHeld;
		default:
			// start only when the cursor isn't over the menu; once panning, keep going until the key is released
			return keyHeld && (a_panning || !a_cursorOverWindow);
		}
	}

	void Reset()
	{
		panToggledOn = false;
		ForEachBinding([](Control&, Binding& a_binding, float a_direction) {
			if (a_binding.navArrowDown) {
				SendMenuArrow(a_direction, false);
				a_binding.navArrowDown = false;
			}
		});
		primariesWithModifierHeld.clear();
		pressedKeys.clear();
		globalTimeHeld = false;  // closing / resetting Photo Mode doesn't count as letting go
		ForEachBinding([](Control&, Binding& a_binding, float) {
			a_binding.matched = nullptr;
			a_binding.active = false;
		});
		UpdateCameraMove();  // stop any up/down movement we started
		lastUpdate = {};
	}
}
