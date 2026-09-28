#include "Camera.h"

#include "ImGui/Widgets.h"

#include <algorithm>
#include <chrono>

namespace PhotoMode
{
	void Camera::OriginalState::Get()
	{
		fov = RE::PlayerCamera::GetSingleton()->worldFOV;
		translateSpeed = FreeCamera::translateSpeed;

		vanillaDOF.blurMultiplier = DOF::blurMultiplier;
		vanillaDOF.nearDist = DOF::nearDist;
		vanillaDOF.nearRange = DOF::nearRange;
		vanillaDOF.farDist = DOF::farDist;
		vanillaDOF.farRange = DOF::farRange;
	}

	void Camera::OriginalState::Revert(bool a_deactivate) const
	{
		RE::PlayerCamera::GetSingleton()->worldFOV = fov;

		DOF::blurMultiplier = vanillaDOF.blurMultiplier;
		DOF::nearDist = vanillaDOF.nearDist;
		DOF::nearRange = vanillaDOF.nearRange;
		DOF::farDist = vanillaDOF.farDist;
		DOF::farRange = vanillaDOF.farRange;

		if (a_deactivate) {
			FreeCamera::translateSpeed = translateSpeed;
		}
	}

	void Camera::GetOriginalState()
	{
		FOVControl::Reset();
		originalState.Get();
	}

	void Camera::RevertState(bool a_deactivate)
	{
		FOVControl::Reset();
		originalState.Revert(a_deactivate);

		// revert view roll
		currentViewRoll = 0.0f;

		// revert grid
		CameraGrid::gridType = CameraGrid::GridType::kDisabled;

		// revert DOF
		if (const auto& effect = RE::ImageSpaceManager::GetSingleton()->effects[RE::ImageSpaceManager::ImageSpaceEffectEnum::DepthOfField]) {
			static_cast<RE::ImageSpaceEffectDepthOfField*>(effect)->enabled = true;
		}
	}

	void Camera::Draw()
	{
		ImGui::EnumSlider("$PM_Grid"_T, &CameraGrid::gridType, CameraGrid::gridTypes);

		ImGui::Slider("$PM_FieldOfView"_T, &RE::PlayerCamera::GetSingleton()->worldFOV, 5.0f, 150.0f);

		currentViewRollDegrees = RE::rad_to_deg(currentViewRoll);
		if (ImGui::Slider("$PM_ViewRoll"_T, &currentViewRollDegrees, -90.0f, 90.0f)) {
			currentViewRoll = RE::deg_to_rad(currentViewRollDegrees);
		}

		ImGui::Slider("$PM_TranslateSpeed"_T,
			&FreeCamera::translateSpeed,  // fFreeCameraTranslationSpeed:Camera
			0.1f, 50.0f);

		/*if (ENB::IsEnabled()) {
			lastDOF = curDOF;
			curDOF.Get();

			ImGui::CheckBox("$PM_DepthOfField"_T, &curDOF.enabled);

		else 

		const auto& effect = RE::ImageSpaceManager::GetSingleton()->effects[RE::ImageSpaceManager::ImageSpaceEffectEnum::DepthOfField];

		if (effect && !ENB::IsEnabled()) {
			const auto dofEffect = static_cast<RE::ImageSpaceEffectDepthOfField*>(effect);

			ImGui::CheckBox("$PM_DepthOfField"_T, &dofEffect->enabled);

			ImGui::BeginDisabled(!dofEffect->enabled);
			{
				ImGui::Indent();
				{
					ImGui::Slider("$PM_DOF_Strength"_T, &DOF::blurMultiplier, 0.0f, 1.0f);
					ImGui::Slider("$PM_DOF_Distance"_T, &DOF::nearDist, 0.0f, 1000.0f);
					ImGui::Slider("$PM_DOF_Range"_T, &DOF::nearRange, 0.0f, 1000.0f);
				}
				ImGui::Unindent();
			}
			ImGui::EndDisabled();
		}*/

		// Camera position management
		cameraPositions.Draw();
	}

	namespace FOVControl
	{
		namespace detail
		{
			struct HeldKey
			{
				bool                                  held{ false };
				std::chrono::steady_clock::time_point pressedAt{};
			};

			inline HeldKey                                increase{};
			inline HeldKey                                decrease{};
			inline std::chrono::steady_clock::time_point lastUpdate{};

			void AddFOV(float a_delta)
			{
				if (const auto camera = RE::PlayerCamera::GetSingleton()) {
					camera->worldFOV = std::clamp(camera->worldFOV + a_delta, minFOV, maxFOV);
				}
			}
		}

		void OnButtonEvent(std::int32_t a_direction, const RE::ButtonEvent* a_event)
		{
			auto& key = a_direction > 0 ? detail::increase : detail::decrease;

			if (a_event->IsDown()) {
				// single tap: step immediately
				detail::AddFOV(static_cast<float>(a_direction) * stepSize);
				key.held = true;
				key.pressedAt = std::chrono::steady_clock::now();
			} else if (a_event->IsUp()) {
				key.held = false;
			}
		}

		void OnFrameUpdate()
		{
			const auto now = std::chrono::steady_clock::now();
			const float deltaTime = detail::lastUpdate.time_since_epoch().count() == 0 ?
			                            0.0f :
			                            std::min(std::chrono::duration<float>(now - detail::lastUpdate).count(), 0.1f);
			detail::lastUpdate = now;

			const auto heldLongEnough = [&](const detail::HeldKey& a_key) {
				return a_key.held && std::chrono::duration<float>(now - a_key.pressedAt).count() > holdDelay;
			};

			float direction = 0.0f;
			if (heldLongEnough(detail::increase)) {
				direction += 1.0f;
			}
			if (heldLongEnough(detail::decrease)) {
				direction -= 1.0f;
			}

			if (direction != 0.0f && deltaTime > 0.0f) {
				detail::AddFOV(direction * holdSpeed * deltaTime);
			}
		}

		void Reset()
		{
			detail::increase = {};
			detail::decrease = {};
			detail::lastUpdate = {};
		}
	}

	void CameraGrid::Draw()
	{
		if (gridType == kDisabled) {
			return;
		}

		const auto drawList = ImGui::GetBackgroundDrawList();

		const auto pos = ImGui::GetWindowPos();
		const auto size = ImGui::GetWindowSize();

		constexpr auto topLeft = ImVec2(0.0f, 0.0f);
		const auto static topRight = ImVec2(size.x, 0.0f);
		const auto static bottomLeft = ImVec2(0.0f, size.y);
		const auto static bottomRight = ImVec2(size.x, size.y);

		const auto colour = ImGui::GetUserStyleColorU32(ImGui::USER_STYLE::kGridLines);
		const auto thickness = ImGui::GetUserStyleVar(ImGui::USER_STYLE::kGridLines);

		switch (gridType) {
		case kRuleOfThirds:
			{
				const static auto third_width = size.x / 3;
				const static auto third_height = size.y / 3;

				// Draw the horizontal lines
				drawList->AddLine(ImVec2(0, third_height), ImVec2(size.x, third_height), colour, thickness);
				drawList->AddLine(ImVec2(0, third_height * 2), ImVec2(size.x, third_height * 2), colour, thickness);

				// Draw the vertical lines
				drawList->AddLine(ImVec2(third_width, 0), ImVec2(third_width, size.y), colour, thickness);
				drawList->AddLine(ImVec2(third_width * 2, 0), ImVec2(third_width * 2, size.y), colour, thickness);
			}
			break;
		case kDiagonal:
			{
				drawList->AddLine(topLeft, ImVec2(size.x * 0.6666f, size.y), colour, thickness);
				drawList->AddLine(topRight, ImVec2(size.x * 0.3333f, size.y), colour, thickness);

				drawList->AddLine(bottomLeft, ImVec2(size.x * 0.6666f, 0.0f), colour, thickness);
				drawList->AddLine(bottomRight, ImVec2(size.x * 0.3333f, 0.0f), colour, thickness);
			}
			break;
		case kTriangle:
			{
				static bool mirrored = false;
				if (ImGui::IsKeyPressed(ImGuiKey_LeftShift) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft)) {
					mirrored = !mirrored;
				}

				// Draw main diagonal line
				drawList->AddLine(
					!mirrored ? topLeft : bottomLeft,
					!mirrored ? bottomRight : topRight,
					colour, thickness);

				drawList->AddLine(
					!mirrored ? bottomLeft : topLeft,
					!mirrored ? ImVec2(size.x * 0.30f, size.y * 0.30f) : ImVec2(size.x * 0.30f, size.y * 0.70f),
					colour, thickness);

				drawList->AddLine(
					!mirrored ? topRight : bottomRight,
					!mirrored ? ImVec2(size.x * 0.70f, size.y * 0.70f) : ImVec2(size.x * 0.70f, size.y * 0.30f),
					colour, thickness);
			}
			break;
		case kGoldenRatio:
			{
				const static auto width40 = size.x * 0.4f;
				const static auto width60 = size.x * 0.6f;

				const static auto height40 = size.y * 0.4f;
				const static auto height60 = size.y * 0.6f;

				// Draw the horizontal lines
				drawList->AddLine(ImVec2(0, height40), ImVec2(size.x, height40), colour, thickness);
				drawList->AddLine(ImVec2(0, height60), ImVec2(size.x, height60), colour, thickness);

				// Draw the vertical lines
				drawList->AddLine(ImVec2(width40, 0), ImVec2(width40, size.y), colour, thickness);
				drawList->AddLine(ImVec2(width60, 0), ImVec2(width60, size.y), colour, thickness);
			}
			break;
		/*case kGoldenSpiral:
			{
			}
			break;*/
		case kGrid:
			{
				constexpr std::int32_t GRID_WIDTH = 8;
				constexpr std::int32_t GRID_HEIGHT = 6;
				constexpr float        GRID_PADDING = 20.0f;

				const float cellWidth = (size.x - 2 * GRID_PADDING) / GRID_WIDTH;
				const float cellHeight = (size.y - 2 * GRID_PADDING) / GRID_HEIGHT;

				const float startX = pos.x + GRID_PADDING - cellWidth;
				const float endX = pos.x + size.x - GRID_PADDING + cellWidth;

				const float startY = pos.y + GRID_PADDING - cellHeight;
				const float endY = pos.y + size.y - GRID_PADDING + cellHeight;

				// Draw vertical lines
				for (std::int32_t i = 0; i <= GRID_WIDTH + 1; ++i) {
					const float x = startX + i * cellWidth;
					drawList->AddLine(ImVec2(x, startY), ImVec2(x, endY), colour, thickness);
				}

				// Draw horizontal lines
				for (std::int32_t j = 0; j <= GRID_HEIGHT + 1; ++j) {
					const float y = startY + j * cellHeight;
					drawList->AddLine(ImVec2(startX, y), ImVec2(endX, y), colour, thickness);
				}
			}
			break;
		default:
			break;
		}
	}
}
