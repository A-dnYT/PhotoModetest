#include "PanCorrection.h"

#include "Manager.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace PhotoMode::PanCorrection
{
	namespace
	{
		constexpr float kPi = std::numbers::pi_v<float>;
		constexpr float kTwoPi = 2.0f * kPi;
		constexpr float kPitchLimit = 1.5533f;  // ~89 degrees either side of level
		constexpr float kNearVertical = 1.3f;   // ~75 degrees
		constexpr float kStickDeadzone = 0.05f;
		constexpr float kMaxFrameTurn = 0.5f;   // bigger jumps come from something else setting the rotation

		// Running totals used to learn how much the game turns per unit of input on each axis.
		// Old samples decay away so the ratio follows frame-rate changes.
		constexpr float kDecay = 0.97f;
		constexpr float kMinSamples = 5.0f;  // input units needed on an axis before trusting it
		constexpr float kMinRatio = 0.2f;
		constexpr float kMaxRatio = 10.0f;

		struct Response
		{
			float yawTurn{ 0.0f };     // sum |yaw change|
			float yawInput{ 0.0f };    // sum |horizontal input|
			float pitchTurn{ 0.0f };   // sum |pitch change|
			float pitchInput{ 0.0f };  // sum |vertical input|

			void Reset() { *this = {}; }

			// how many times faster the game turns horizontally than vertically for the same input
			[[nodiscard]] float Ratio() const
			{
				if (yawInput < kMinSamples || pitchInput < kMinSamples || yawTurn <= 0.0f || pitchTurn <= 0.0f) {
					return 0.0f;
				}
				const float ratio = (yawTurn / yawInput) / (pitchTurn / pitchInput);
				return std::isfinite(ratio) ? std::clamp(ratio, kMinRatio, kMaxRatio) : 0.0f;
			}
		};

		bool enabled{ true };

		RE::NiPoint2 mouseDelta{};  // accumulated since the last camera update
		RE::NiPoint2 rightStick{};  // latest right stick position

		Response mouseResponse;
		Response stickResponse;

		// whether the game stores pitch as 0..2pi (looking up = just under 2pi) rather than -pi..pi
		bool unsignedPitch{ true };

		// Always compare angles in -pi..pi, whatever range the game stores them in.
		float WrapSigned(float a_angle)
		{
			a_angle = std::fmod(a_angle + kPi, kTwoPi);
			if (a_angle < 0.0f) {
				a_angle += kTwoPi;
			}
			return a_angle - kPi;
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
			{
				const auto mouse = static_cast<const RE::MouseMoveEvent*>(a_event);
				mouseDelta.x += static_cast<float>(mouse->mouseInputX);
				mouseDelta.y += static_cast<float>(mouse->mouseInputY);
			}
			break;
		case RE::INPUT_EVENT_TYPE::kThumbstick:
			{
				const auto stick = static_cast<const RE::ThumbstickEvent*>(a_event);
				if (stick->IsRight()) {
					rightStick.x = stick->xValue;
					rightStick.y = stick->yValue;
				}
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

		if (!enabled || !a_state || !MANAGER(PhotoMode)->IsActive()) {
			return;
		}

		const float pitchAfter = a_state->rotation.x;
		if (pitchAfter > kPi || a_pitchBefore > kPi) {
			unsignedPitch = true;
		} else if (pitchAfter < 0.0f || a_pitchBefore < 0.0f) {
			unsignedPitch = false;
		}

		// the camera is only look-controlled while the cursor is hidden (panning, or on gamepad)
		if (RE::UI::GetSingleton()->IsMenuOpen(RE::CursorMenu::MENU_NAME)) {
			return;
		}

		const float gamePitchDelta = WrapSigned(pitchAfter - a_pitchBefore);
		const float gameYawDelta = WrapSigned(a_state->rotation.y - a_yawBefore);

		if (gamePitchDelta == 0.0f && gameYawDelta == 0.0f) {
			return;  // the game didn't turn the camera this frame
		}
		if (std::abs(gamePitchDelta) > kMaxFrameTurn || std::abs(gameYawDelta) > kMaxFrameTurn) {
			return;
		}

		const bool mouseMoved = mouse.x != 0.0f || mouse.y != 0.0f;
		const bool stickMoved = std::abs(rightStick.x) > kStickDeadzone || std::abs(rightStick.y) > kStickDeadzone;
		if (mouseMoved == stickMoved) {
			return;  // no look input, or both devices at once: leave the game's result alone
		}

		auto&       response = mouseMoved ? mouseResponse : stickResponse;
		const float inputX = mouseMoved ? std::abs(mouse.x) : (std::abs(rightStick.x) > kStickDeadzone ? std::abs(rightStick.x) : 0.0f);
		const float inputY = mouseMoved ? std::abs(mouse.y) : (std::abs(rightStick.y) > kStickDeadzone ? std::abs(rightStick.y) : 0.0f);

		// learn the game's own (uncorrected) response on each axis
		if (inputX > 0.0f) {
			response.yawTurn = response.yawTurn * kDecay + std::abs(gameYawDelta);
			response.yawInput = response.yawInput * kDecay + inputX;
		}
		if (inputY > 0.0f) {
			response.pitchTurn = response.pitchTurn * kDecay + std::abs(gamePitchDelta);
			response.pitchInput = response.pitchInput * kDecay + inputY;
		}

		const float ratio = response.Ratio();
		if (ratio == 0.0f || gamePitchDelta == 0.0f) {
			return;
		}

		// scale the game's own pitch change (keeps its direction, smoothing and invert-Y) so vertical matches horizontal
		const float corrected = std::clamp(WrapSigned(a_pitchBefore) + gamePitchDelta * ratio, -kPitchLimit, kPitchLimit);

		// near straight up/down, let the game's own pitch limit win so the view doesn't jitter against it
		const float gamePitch = WrapSigned(pitchAfter);
		if (std::abs(gamePitch) > kNearVertical && std::abs(corrected) > std::abs(gamePitch)) {
			return;
		}

		// write back in the same range the game uses
		a_state->rotation.x = (unsignedPitch && corrected < 0.0f) ? corrected + kTwoPi : corrected;
	}

	void Reset()
	{
		mouseDelta = {};
		rightStick = {};
		mouseResponse.Reset();
		stickResponse.Reset();
	}
}
