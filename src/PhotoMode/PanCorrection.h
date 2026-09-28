#pragma once

// Makes vertical free-camera panning (pitch) as fast as horizontal panning (yaw).
//
// Skyrim scales vertical look input differently from horizontal (it also depends on
// frame rate), so in Photo Mode looking up/down feels much slower than looking left/right.
// Horizontal panning is left exactly as the game does it. Every frame we measure how
// much yaw the game produced per unit of horizontal input, and apply that same rate to
// the vertical input, replacing the game's pitch change for that frame.
//
// Setting: bEqualizePanSpeed in [Controls] (default on).

namespace PhotoMode::PanCorrection
{
	void LoadSettings(const CSimpleIniA& a_ini);

	// Record raw mouse movement / right stick input (called for every input event while Photo Mode is active).
	void OnInputEvent(const RE::InputEvent* a_event);

	// Called right after FreeCameraState::Update, with the rotation (pitch, yaw) from before the update.
	void OnFreeCameraUpdate(RE::FreeCameraState* a_state, float a_pitchBefore, float a_yawBefore);

	void Reset();
}
