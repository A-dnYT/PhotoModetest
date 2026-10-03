#pragma once

// A small on-screen message drawn by Photo Mode itself (so it shows even while the menus are hidden).
// It appears straight away, stays briefly and fades out quickly. A new message replaces the current one.
// It is never drawn on a frame that is being saved as a photo.
namespace PhotoMode::Toast
{
	void Show(std::string a_text, float a_seconds = 1.0f);

	// Whether a message is currently on screen (so a frame is rendered for it while the Photo Mode UI is hidden).
	[[nodiscard]] bool IsVisible();

	// Draw it (inside an ImGui frame). Does nothing when there is no message.
	void Draw();

	void Clear();
}
