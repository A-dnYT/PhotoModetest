#pragma once

// Camera modes while Photo Mode is open (only one is on at a time):
//
//   Photo Cam   - the normal Photo Mode free camera (default).
//   Freeze Cam  - the camera stays where it is; the player can move around.
//   Release Cam - the camera stays where it is but can be panned with the controller's right stick.
//   Follow Cam  - the camera stays where it is and turns to keep pointing at the player.
//
// In Freeze / Release / Follow the game is put back on its normal player camera so the player can be
// controlled with keyboard & mouse, and the rendered camera is overridden with the fixed camera. The
// controller does not control the player in these modes (it is left to the camera and the hotkeys).
// FOV, view roll, time and the other Photo Mode settings keep working in every mode.
//
// Settings ([Controls]): fReleasePanSpeed, fFollowYawSpeed, fFollowPitchSpeed, fFollowHeightOffset,
// and the mode hotkeys iPhotoCam* / iFreezeCam* / iReleaseCam* / iFollowCam* (handled in AdjustHotkeys).

namespace PhotoMode::CameraModes
{
	enum Mode : std::uint32_t
	{
		kPhoto = 0,
		kFreeze,
		kRelease,
		kFollow
	};

	inline constexpr std::array modeNames{
		"$PM_CameraMode_Photo",
		"$PM_CameraMode_Freeze",
		"$PM_CameraMode_Release",
		"$PM_CameraMode_Follow"
	};

	void InstallHooks();
	void LoadSettings(const CSimpleIniA& a_ini);

	[[nodiscard]] Mode GetMode();
	[[nodiscard]] bool IsCinematic();  // Freeze / Release / Follow

	// Switch to a mode (no toggling: switching to the current mode does nothing).
	void SetMode(Mode a_mode);

	// Right stick for Release Cam (called for every input event while Photo Mode is open).
	void OnInputEvent(const RE::InputEvent* a_event);

	// Once per rendered frame while Photo Mode is open.
	void OnFrameUpdate();

	// Photo Mode opened / about to close. OnDeactivate leaves the game on the player camera
	// (out of the free camera) so Photo Mode can restore the original camera.
	void OnActivate();
	void OnDeactivate();
}
