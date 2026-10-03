#include "Toast.h"

#include "ImGui/Util.h"
#include "Input.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <mutex>

namespace PhotoMode::Toast
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr float kFadeOutSeconds = 0.15f;

		std::mutex        lock;
		std::string       text;
		Clock::time_point shownAt{};
		float             duration{ 0.0f };

		// seconds left on screen, including the fade (0 when nothing is showing); needs the lock
		float TimeLeft()
		{
			if (text.empty()) {
				return 0.0f;
			}
			const float elapsed = std::chrono::duration<float>(Clock::now() - shownAt).count();
			return std::max(duration + kFadeOutSeconds - elapsed, 0.0f);
		}
	}

	void Show(std::string a_text, float a_seconds)
	{
		std::scoped_lock guard{ lock };
		text = std::move(a_text);
		shownAt = Clock::now();
		duration = std::max(a_seconds, 0.0f);
	}

	bool IsVisible()
	{
		std::scoped_lock guard{ lock };
		return TimeLeft() > 0.0f;
	}

	void Clear()
	{
		std::scoped_lock guard{ lock };
		text.clear();
	}

	void Draw()
	{
		std::string message;
		float       alpha = 1.0f;
		{
			std::scoped_lock guard{ lock };
			const float      left = TimeLeft();
			if (left <= 0.0f) {
				text.clear();
				return;
			}
			message = text;
			alpha = std::min(left / kFadeOutSeconds, 1.0f);  // full until the last moment, then a quick fade
		}

		// keep it out of photos
		if (MANAGER(Input)->IsScreenshotQueued()) {
			return;
		}

		const auto  font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize() * 1.15f;
		const auto  textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, message.c_str());

		const auto  viewportPos = ImGui::GetNativeViewportPos();
		const auto  viewportSize = ImGui::GetNativeViewportSize();
		const float padX = fontSize * 0.9f;
		const float padY = fontSize * 0.45f;

		// top centre, a little below the edge
		const ImVec2 boxMin{ viewportPos.x + (viewportSize.x - textSize.x) * 0.5f - padX, viewportPos.y + viewportSize.y * 0.08f };
		const ImVec2 boxMax{ boxMin.x + textSize.x + padX * 2.0f, boxMin.y + textSize.y + padY * 2.0f };

		const auto drawList = ImGui::GetForegroundDrawList();
		drawList->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f * alpha)), fontSize * 0.35f);
		drawList->AddText(font, fontSize, ImVec2(boxMin.x + padX, boxMin.y + padY), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)), message.c_str());
	}
}
