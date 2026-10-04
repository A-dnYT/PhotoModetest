#pragma once

// How close the camera can get to things before they are cut off (the camera's near clip plane).
// The game uses fNearDistance:Display (15 by default). While Photo Mode is open it uses its own value
// (fNearClipDistance in [Settings], adjustable on the Camera tab); the game's value is restored when it closes.
// Lower values let the camera get closer, at some cost in depth precision for very distant objects.
namespace PhotoMode::NearClip
{
	inline constexpr float kMin = 0.5f;
	inline constexpr float kMax = 15.0f;

	void LoadSettings(const CSimpleIniA& a_ini);

	// Photo Mode opened / closed
	void OnActivate();
	void OnDeactivate();

	// back to the fNearClipDistance setting (Camera tab reset)
	void ResetToDefault();

	// current value while Photo Mode is open (the Camera tab slider edits it)
	float& Value();

	// keep the game's camera on the current value (called whenever the world camera updates while Photo Mode is open)
	void Apply(RE::NiCamera* a_camera);
}
