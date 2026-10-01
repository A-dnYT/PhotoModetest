#include "CameraModes.h"

#include "AdjustHotkeys.h"
#include "Manager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

namespace PhotoMode::CameraModes
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr float kPi = std::numbers::pi_v<float>;
		constexpr float kTwoPi = 2.0f * kPi;
		constexpr float kPitchLimit = 1.5533f;  // ~89 degrees up / down
		constexpr float kStickDeadzone = 0.15f;
		constexpr float kMaxFrameDelta = 0.1f;

		// settings
		float releasePanSpeed{ 90.0f };      // degrees per second at full right stick
		float followYawSpeed{ 4.0f };        // how quickly Follow Cam turns sideways (higher = snappier)
		float followPitchSpeed{ 4.0f };      // how quickly Follow Cam turns up / down
		float followHeightOffset{ 100.0f };  // aim point above the player's feet (game units)
		bool  moveCameraOnFreeze{ false };   // left stick / up-down move the camera in each cinematic mode
		bool  moveCameraOnRelease{ false };
		bool  moveCameraOnFollow{ false };
		bool  carryWithPlayerOnFreeze{ false };   // the camera is carried along by the player's movement in each cinematic mode
		bool  carryWithPlayerOnRelease{ false };
		bool  carryWithPlayerOnFollow{ false };

		constexpr float kUnitsPerCameraSpeed = 100.0f;  // camera movement: game units per second per point of Camera Speed

		// state
		Mode         mode{ kPhoto };
		RE::NiPoint3 position{};  // fixed camera position in the cinematic modes
		float        pitch{ 0.0f };  // -pi..pi, positive looks down (free camera convention)
		float        yaw{ 0.0f };
		RE::NiPoint2 rightStick{};
		RE::NiPoint2 leftStick{};
		bool         gameUpHeld{ false };    // the game's own controller camera up / down buttons
		bool         gameDownHeld{ false };
		RE::NiPoint3 lastPlayerPosition{};
		bool         lastPlayerPositionValid{ false };
		bool         pendingFreeCameraPose{ false };  // copy the cinematic pose into the free camera once it is active
		bool         forcedThirdPerson{ false };      // we left first person to show the player
		Clock::time_point lastUpdate{};

		// POV switch (1 << 5) / wheel zoom (1 << 9) would flip the player camera into first person; off while in a cinematic mode
		std::uint32_t disabledViewControls{ 0 };

		float WrapSigned(float a_angle)
		{
			a_angle = std::fmod(a_angle + kPi, kTwoPi);
			if (a_angle < 0.0f) {
				a_angle += kTwoPi;
			}
			return a_angle - kPi;
		}

		float WrapUnsigned(float a_angle)
		{
			a_angle = std::fmod(a_angle, kTwoPi);
			return a_angle < 0.0f ? a_angle + kTwoPi : a_angle;
		}

		RE::FreeCameraState* GetFreeCameraState()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			return camera ? static_cast<RE::FreeCameraState*>(camera->cameraStates[RE::CameraState::kFree].get()) : nullptr;
		}

		bool OverrideActive()
		{
			return mode != kPhoto && MANAGER(PhotoMode)->IsActive();
		}

		void DisableViewControls()
		{
			const auto controlMap = RE::ControlMap::GetSingleton();
			if (!controlMap || disabledViewControls != 0) {
				return;
			}
			std::uint32_t flags = 0;
			if (controlMap->IsPOVSwitchControlsEnabled()) {
				flags |= 1 << 5;
			}
			if (controlMap->IsWheelZoomControlsEnabled()) {
				flags |= 1 << 9;
			}
			if (flags) {
				controlMap->ToggleControls(static_cast<RE::ControlMap::UEFlag>(flags), false, true);
			}
			disabledViewControls = flags;
		}

		void RestoreViewControls()
		{
			const auto controlMap = RE::ControlMap::GetSingleton();
			if (controlMap && disabledViewControls != 0) {
				controlMap->ToggleControls(static_cast<RE::ControlMap::UEFlag>(disabledViewControls), true, true);
			}
			disabledViewControls = 0;
		}

		// Remember where the free camera currently is (when leaving Photo Cam).
		void CaptureFreeCameraPose()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (camera->IsInFreeCameraMode()) {
				if (const auto freeCamera = static_cast<RE::FreeCameraState*>(camera->currentState.get())) {
					position = freeCamera->translation;
					pitch = WrapSigned(freeCamera->rotation.x);
					yaw = WrapSigned(freeCamera->rotation.y);
					return;
				}
			}
			// fallback: wherever the rendered camera is
			if (const auto root = camera->cameraRoot.get()) {
				position = root->world.translate;
				const auto forward = root->world.rotate.GetVectorY();
				yaw = std::atan2(forward.x, forward.y);
				pitch = std::asin(std::clamp(-forward.z, -1.0f, 1.0f));
			}
		}

		// Put the free camera where the cinematic camera was (when returning to Photo Cam), once it is active.
		void ApplyPendingFreeCameraPose()
		{
			if (!pendingFreeCameraPose) {
				return;
			}
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera->IsInFreeCameraMode()) {
				return;
			}
			if (const auto freeCamera = static_cast<RE::FreeCameraState*>(camera->currentState.get())) {
				freeCamera->translation = position;
				freeCamera->rotation.x = WrapUnsigned(pitch);  // the free camera stores angles as 0..2pi
				freeCamera->rotation.y = WrapUnsigned(yaw);
			}
			pendingFreeCameraPose = false;
		}

		void LeaveFirstPerson()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (camera->IsInFirstPerson()) {
				camera->ForceThirdPerson();
				forcedThirdPerson = true;
			}
		}

		// ---- rendered camera override (player camera states) ----

		bool OverrideRotation(RE::NiQuaternion& a_rotation)
		{
			if (!OverrideActive()) {
				return false;
			}
			const auto freeCamera = GetFreeCameraState();
			if (!freeCamera) {
				return false;
			}
			// Build the rotation exactly like the free camera does (this also applies Photo Mode's view roll).
			const auto saved = freeCamera->rotation;
			freeCamera->rotation.x = WrapUnsigned(pitch);
			freeCamera->rotation.y = WrapUnsigned(yaw);
			freeCamera->GetRotation(a_rotation);
			freeCamera->rotation = saved;
			return true;
		}

		bool OverrideTranslation(RE::NiPoint3& a_translation)
		{
			if (!OverrideActive()) {
				return false;
			}
			a_translation = position;
			return true;
		}

		template <class State>
		struct GetRotation
		{
			static void thunk(State* a_this, RE::NiQuaternion& a_rotation)
			{
				if (!OverrideRotation(a_rotation)) {
					func(a_this, a_rotation);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline constexpr std::size_t            idx{ 0x04 };  // TESCameraState::GetRotation
		};

		template <class State>
		struct GetTranslation
		{
			static void thunk(State* a_this, RE::NiPoint3& a_translation)
			{
				if (!OverrideTranslation(a_translation)) {
					func(a_this, a_translation);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline constexpr std::size_t            idx{ 0x05 };  // TESCameraState::GetTranslation
		};

		// After the game's whole camera update: put the camera node where the cinematic camera is. The player camera
		// states don't take their rendered position from GetTranslation, so this is what actually pins the position.
		struct PlayerCameraUpdate
		{
			static void thunk(RE::PlayerCamera* a_this)
			{
				func(a_this);

				if (!OverrideActive()) {
					return;
				}
				const auto root = a_this->cameraRoot.get();
				if (!root) {
					return;
				}
				RE::NiQuaternion rotation;
				if (OverrideRotation(rotation)) {
					root->local.rotate = rotation.ToRotation();
				}
				root->local.translate = position;

				RE::NiUpdateData updateData{};
				root->Update(updateData);
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline constexpr std::size_t            idx{ 0x02 };  // TESCamera::Update
		};

		// Backup for the PlayerCameraUpdate hook: after a third-person state's own update, keep its position on the
		// cinematic camera (ThirdPersonState::translation is where its camera position is kept).
		template <class State>
		struct ThirdPersonUpdate
		{
			static void thunk(State* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
			{
				func(a_this, a_nextState);
				if (OverrideActive()) {
					a_this->translation = position;
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline constexpr std::size_t            idx{ 0x03 };  // TESCameraState::Update
		};

		template <class State>
		void InstallStateHooks()
		{
			stl::write_vfunc<State, GetRotation<State>>();
			stl::write_vfunc<State, GetTranslation<State>>();
			if constexpr (std::is_base_of_v<RE::ThirdPersonState, State>) {
				stl::write_vfunc<State, ThirdPersonUpdate<State>>();
			}
		}

		// ---- controller does not control the player in the cinematic modes ----

		struct PlayerControlsInput
		{
			static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::InputEvent*>* a_this, RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_source)
			{
				if (!a_event || !*a_event || !OverrideActive()) {
					return func(a_this, a_event, a_source);
				}

				// Pass the player only the non-controller events: unlink controller events for this call, then restore the list.
				std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> links;
				for (auto event = *a_event; event; event = event->next) {
					links.emplace_back(event, event->next);
				}

				RE::InputEvent* head = nullptr;
				RE::InputEvent* tail = nullptr;
				for (const auto& [event, next] : links) {
					if (event->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
						continue;
					}
					if (tail) {
						tail->next = event;
					} else {
						head = event;
					}
					tail = event;
				}
				if (tail) {
					tail->next = nullptr;
				}

				const auto result = head ? func(a_this, &head, a_source) : RE::BSEventNotifyControl::kContinue;

				for (const auto& [event, next] : links) {
					event->next = next;
				}
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline constexpr std::size_t            idx{ 0x01 };  // BSTEventSink<InputEvent*>::ProcessEvent
		};

		// ---- per-mode camera behaviour ----

		void UpdateRelease(float a_deltaTime)
		{
			const float x = std::abs(rightStick.x) > kStickDeadzone ? rightStick.x : 0.0f;
			const float y = std::abs(rightStick.y) > kStickDeadzone ? rightStick.y : 0.0f;
			const float speed = RE::deg_to_rad(releasePanSpeed) * a_deltaTime;

			yaw = WrapSigned(yaw + x * speed);
			pitch = std::clamp(pitch - y * speed, -kPitchLimit, kPitchLimit);  // stick up looks up
		}

		bool CarriedWithPlayer()
		{
			switch (mode) {
			case kFreeze:
				return carryWithPlayerOnFreeze;
			case kRelease:
				return carryWithPlayerOnRelease;
			case kFollow:
				return carryWithPlayerOnFollow;
			default:
				return false;
			}
		}

		// Move the camera by however much the player moved since the last frame (camera keeps its offset to the player).
		void CarryWithPlayer()
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			const auto playerPosition = player->GetPosition();
			if (lastPlayerPositionValid && CarriedWithPlayer()) {
				position.x += playerPosition.x - lastPlayerPosition.x;
				position.y += playerPosition.y - lastPlayerPosition.y;
				position.z += playerPosition.z - lastPlayerPosition.z;
			}
			lastPlayerPosition = playerPosition;
			lastPlayerPositionValid = true;
		}

		bool CanMoveCamera()
		{
			switch (mode) {
			case kFreeze:
				return moveCameraOnFreeze;
			case kRelease:
				return moveCameraOnRelease;
			case kFollow:
				return moveCameraOnFollow;
			default:
				return false;
			}
		}

		// Fly the cinematic camera like the free camera: left stick = forward / back / sideways where it is facing,
		// Move Up / Move Down hotkeys and the game's controller up/down buttons = straight up / down.
		void MoveCamera(float a_deltaTime)
		{
			const float forwardInput = std::abs(leftStick.y) > kStickDeadzone ? leftStick.y : 0.0f;
			const float sideInput = std::abs(leftStick.x) > kStickDeadzone ? leftStick.x : 0.0f;
			int         vertical = AdjustHotkeys::GetCameraMoveDirection();
			vertical += (gameUpHeld ? 1 : 0) - (gameDownHeld ? 1 : 0);
			const float verticalInput = static_cast<float>(std::clamp(vertical, -1, 1));

			if (forwardInput == 0.0f && sideInput == 0.0f && verticalInput == 0.0f) {
				return;
			}

			const float distance = std::max(FreeCamera::translateSpeed, 0.0f) * kUnitsPerCameraSpeed * a_deltaTime;

			// same axes as the free camera: forward = (sin yaw cos pitch, cos yaw cos pitch, -sin pitch), right = (cos yaw, -sin yaw, 0)
			const RE::NiPoint3 forward{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
			const RE::NiPoint3 right{ std::cos(yaw), -std::sin(yaw), 0.0f };

			position.x += (forward.x * forwardInput + right.x * sideInput) * distance;
			position.y += (forward.y * forwardInput + right.y * sideInput) * distance;
			position.z += (forward.z * forwardInput + verticalInput) * distance;
		}

		void UpdateFollow(float a_deltaTime)
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			auto target = player->GetPosition();
			target.z += followHeightOffset;

			const RE::NiPoint3 toTarget = target - position;
			const float        distance = toTarget.Length();
			if (distance < 1.0f) {
				return;
			}

			const float targetYaw = std::atan2(toTarget.x, toTarget.y);
			const float targetPitch = std::clamp(std::asin(std::clamp(-toTarget.z / distance, -1.0f, 1.0f)), -kPitchLimit, kPitchLimit);

			// smooth: move a fraction of the remaining angle each frame (frame-rate independent)
			const float yawBlend = 1.0f - std::exp(-std::max(followYawSpeed, 0.0f) * a_deltaTime);
			const float pitchBlend = 1.0f - std::exp(-std::max(followPitchSpeed, 0.0f) * a_deltaTime);

			yaw = WrapSigned(yaw + WrapSigned(targetYaw - yaw) * yawBlend);
			pitch = std::clamp(pitch + (targetPitch - pitch) * pitchBlend, -kPitchLimit, kPitchLimit);
		}
	}

	void InstallHooks()
	{
		InstallStateHooks<RE::ThirdPersonState>();
		InstallStateHooks<RE::HorseCameraState>();
		InstallStateHooks<RE::BleedoutCameraState>();
		InstallStateHooks<RE::DragonCameraState>();
		InstallStateHooks<RE::FirstPersonState>();
		InstallStateHooks<RE::FurnitureCameraState>();
		InstallStateHooks<RE::AutoVanityState>();
		InstallStateHooks<RE::PlayerCameraTransitionState>();

		stl::write_vfunc<RE::PlayerCamera, PlayerCameraUpdate>();
		stl::write_vfunc<RE::PlayerControls, PlayerControlsInput>();

		REX::INFO("Installed camera mode hooks");
	}

	void LoadSettings(const CSimpleIniA& a_ini)
	{
		releasePanSpeed = static_cast<float>(a_ini.GetDoubleValue("Controls", "fReleasePanSpeed", releasePanSpeed));
		followYawSpeed = static_cast<float>(a_ini.GetDoubleValue("Controls", "fFollowYawSpeed", followYawSpeed));
		followPitchSpeed = static_cast<float>(a_ini.GetDoubleValue("Controls", "fFollowPitchSpeed", followPitchSpeed));
		followHeightOffset = static_cast<float>(a_ini.GetDoubleValue("Controls", "fFollowHeightOffset", followHeightOffset));
		moveCameraOnFreeze = a_ini.GetBoolValue("Controls", "bMoveCameraOnFreeze", moveCameraOnFreeze);
		moveCameraOnRelease = a_ini.GetBoolValue("Controls", "bMoveCameraOnRelease", moveCameraOnRelease);
		moveCameraOnFollow = a_ini.GetBoolValue("Controls", "bMoveCameraOnFollow", moveCameraOnFollow);
		carryWithPlayerOnFreeze = a_ini.GetBoolValue("Controls", "bCameraFollowsPlayerOnFreeze", carryWithPlayerOnFreeze);
		carryWithPlayerOnRelease = a_ini.GetBoolValue("Controls", "bCameraFollowsPlayerOnRelease", carryWithPlayerOnRelease);
		carryWithPlayerOnFollow = a_ini.GetBoolValue("Controls", "bCameraFollowsPlayerOnFollow", carryWithPlayerOnFollow);
	}

	Mode GetMode()
	{
		return mode;
	}

	bool IsCinematic()
	{
		return mode != kPhoto;
	}

	void SetMode(Mode a_mode)
	{
		if (a_mode == mode || !MANAGER(PhotoMode)->IsActive()) {
			return;
		}
		const auto camera = RE::PlayerCamera::GetSingleton();
		if (!camera) {
			return;
		}

		if (mode == kPhoto) {
			// Photo Cam -> cinematic: keep the camera where it is, hand the player camera back to the player
			CaptureFreeCameraPose();
			if (camera->IsInFreeCameraMode()) {
				camera->ToggleFreeCameraMode(false);
			}
			LeaveFirstPerson();
			DisableViewControls();
		} else if (a_mode == kPhoto) {
			// cinematic -> Photo Cam: back to the free camera, starting where the cinematic camera is
			RestoreViewControls();
			if (!camera->IsInFreeCameraMode()) {
				camera->ToggleFreeCameraMode(false);
			}
			pendingFreeCameraPose = true;
			ApplyPendingFreeCameraPose();
		}
		// cinematic -> cinematic: same camera position, just different behaviour

		mode = a_mode;
		rightStick = {};
		leftStick = {};
		lastPlayerPositionValid = false;
		gameUpHeld = false;
		gameDownHeld = false;
		RE::SendHUDMessage::ShowHUDMessage(TRANSLATE(modeNames[a_mode]));
	}

	void OnInputEvent(const RE::InputEvent* a_event)
	{
		if (a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
			const auto stick = static_cast<const RE::ThumbstickEvent*>(a_event);
			if (stick->IsRight()) {
				rightStick = { stick->xValue, stick->yValue };
			} else {
				leftStick = { stick->xValue, stick->yValue };
			}
		}
	}

	void SetGameVerticalInput(bool a_up, bool a_pressed)
	{
		(a_up ? gameUpHeld : gameDownHeld) = a_pressed;
	}

	void OnFrameUpdate()
	{
		const auto  now = Clock::now();
		const float deltaTime = lastUpdate == Clock::time_point{} ? 0.0f : std::min(std::chrono::duration<float>(now - lastUpdate).count(), kMaxFrameDelta);
		lastUpdate = now;

		const auto camera = RE::PlayerCamera::GetSingleton();
		if (!camera) {
			return;
		}

		if (mode == kPhoto) {
			ApplyPendingFreeCameraPose();
			return;
		}

		// Something else switched to the free camera (e.g. loading a saved camera position): that is Photo Cam
		if (camera->IsInFreeCameraMode()) {
			RestoreViewControls();
			mode = kPhoto;
			return;
		}

		LeaveFirstPerson();  // e.g. the game switched to first person on its own

		CarryWithPlayer();

		switch (mode) {
		case kRelease:
			UpdateRelease(deltaTime);
			break;
		case kFollow:
			UpdateFollow(deltaTime);
			break;
		default:
			break;  // Freeze: the camera doesn't turn
		}

		// move first, then Follow re-aims at the player next frame from the new position
		if (CanMoveCamera()) {
			MoveCamera(deltaTime);
		}
	}

	void OnActivate()
	{
		mode = kPhoto;
		lastPlayerPositionValid = false;
		rightStick = {};
		leftStick = {};
		gameUpHeld = false;
		gameDownHeld = false;
		pendingFreeCameraPose = false;
		forcedThirdPerson = false;
		lastUpdate = {};
	}

	void OnDeactivate()
	{
		if (mode != kPhoto) {
			RestoreViewControls();
			if (forcedThirdPerson) {
				RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
			}
		}
		mode = kPhoto;
		pendingFreeCameraPose = false;
		forcedThirdPerson = false;
	}
}
