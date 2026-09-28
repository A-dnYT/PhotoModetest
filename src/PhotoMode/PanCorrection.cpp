#include "PanCorrection.h"

#include "Manager.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace PhotoMode::PanCorrection
{
	namespace
	{
		constexpr float kPitchLimit = 1.5533f;       // ~89 degrees
		constexpr float kStickDeadzone = 0.05f;
		constexpr float kMinYawForLearning = 1.0e-6f;

		// Per input device: how much yaw (radians) the game gives per unit of horizontal input,
		// and which way the game turns pitch for positive vertical input (handles inverted Y).
		struct Response
		{
			float yawPerUnit{ 0.0f };
			float pitchSign{ 0.0f };

			void Reset() { *this = {}; }
		};

		bool enabled{ true };

		RE::NiPoint2 mouseDelta{};  // accumulated since the last camera update
		RE::NiPoint2 rightStick{};  // latest right stick position

		Response mouseResponse;
		Response stickResponse;

		// rotation at the end of the previous update (after our correction), used as the baseline so
		// look input applied by the game outside of Update is also covered
		const RE::FreeCameraState* lastState{ nullptr };
		RE::NiPoint2               lastRotation{};

		float WrapAngle(float a_angle)
		{
			constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
			a_angle = std::fmod(a_angle + std::numbers::pi_v<float>, twoPi);
			if (a_angle < 0.0f) {
				a_angle += twoPi;
			}
			return a_angle - std::numbers::pi_v<float>;
		}

		float Sign(float a_value)
		{
			return a_value > 0.0f ? 1.0f : (a_value < 0.0f ? -1.0f : 0.0f);
		}
	}

	void LoadSettings(const CSimpleIniA& a_ini)
	{
		enabled = a_ini.GetBoolValue("Controls", "bEqualizePanSpeed", enabled);
	}

	void OnInputEvent(const RE::InputEvent* a_event)
	{
		switch (a_event->GetEventType()) {
		case RE::INPUT_EVENT_TYPE::kMouseMove:
			if (const auto mouse = static_cast<const RE::MouseMoveEvent*>(a_event)) {
				mouseDelta.x += static_cast<float>(mouse->mouseInputX);
				mouseDelta.y += static_cast<float>(mouse->mouseInputY);
			}
			break;
		case RE::INPUT_EVENT_TYPE::kThumbstick:
			if (const auto stick = static_cast<const RE::ThumbstickEvent*>(a_event); stick && stick->IsRight()) {
				rightStick.x = stick->xValue;
				rightStick.y = stick->yValue;
			}
			break;
		default:
			break;
		}
	}

	void OnFreeCameraUpdate(RE::FreeCameraState* a_state, float a_pitchBefore, float a_yawBefore)
	{
		// consume the mouse movement that fed this update
		const RE::NiPoint2 mouse = mouseDelta;
		mouseDelta = {};

		const RE::FreeCameraState* previousState = std::exchange(lastState, nullptr);

		if (!enabled || !a_state || !MANAGER(PhotoMode)->IsActive()) {
			return;
		}

		// the camera is only look-controlled while the cursor is hidden (panning, or on gamepad);
		// while the cursor is visible, rotation changes come from the menu (e.g. loading a camera position)
		const bool cursorVisible = RE::UI::GetSingleton()->IsMenuOpen(RE::CursorMenu::MENU_NAME);

		const RE::NiPoint2 before = previousState == a_state ? lastRotation : RE::NiPoint2{ a_pitchBefore, a_yawBefore };

		struct Remember
		{
			RE::FreeCameraState* state;
			~Remember()
			{
				lastState = state;
				lastRotation = { state->rotation.x, state->rotation.y };
			}
		} remember{ a_state };

		if (cursorVisible) {
			return;
		}

		const float gamePitchDelta = a_state->rotation.x - before.x;
		const float gameYawDelta = WrapAngle(a_state->rotation.y - before.y);

		// only touch frames where the game itself turned the camera from look input
		// (so moving the cursor around the menu never rotates anything)
		if (gamePitchDelta == 0.0f && gameYawDelta == 0.0f) {
			return;
		}

		const bool mouseMoved = mouse.x != 0.0f || mouse.y != 0.0f;
		const bool stickMoved = std::abs(rightStick.x) > kStickDeadzone || std::abs(rightStick.y) > kStickDeadzone;
		if (mouseMoved == stickMoved) {
			return;  // no look input, or both devices at once: leave the game's result alone
		}

		auto&              response = mouseMoved ? mouseResponse : stickResponse;
		const RE::NiPoint2 input = mouseMoved ? mouse : RE::NiPoint2{
			std::abs(rightStick.x) > kStickDeadzone ? rightStick.x : 0.0f,
			std::abs(rightStick.y) > kStickDeadzone ? rightStick.y : 0.0f
		};

		// learn the game's horizontal rate from this frame
		if (input.x != 0.0f && std::abs(gameYawDelta) > kMinYawForLearning) {
			response.yawPerUnit = std::abs(gameYawDelta) / std::abs(input.x);
		}
		// learn which way the game turns pitch for this device
		if (input.y != 0.0f && gamePitchDelta != 0.0f) {
			response.pitchSign = Sign(gamePitchDelta) * Sign(input.y);
		}

		if (input.y == 0.0f || response.yawPerUnit <= 0.0f || response.pitchSign == 0.0f) {
			return;
		}

		const float pitchDelta = response.pitchSign * response.yawPerUnit * input.y;
		a_state->rotation.x = std::clamp(before.x + pitchDelta, -kPitchLimit, kPitchLimit);
	}

	void Reset()
	{
		mouseDelta = {};
		rightStick = {};
		mouseResponse.Reset();
		stickResponse.Reset();
		lastState = nullptr;
	}
}
