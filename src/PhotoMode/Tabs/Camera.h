#pragma once

#include "CameraPositions.h"

namespace PhotoMode
{
	class Camera
	{
	public:
		void GetOriginalState();
		void RevertState(bool a_deactivate);

		[[nodiscard]] float GetViewRoll() const { return currentViewRoll; }
		void                SetViewRoll(float a_roll) { currentViewRoll = a_roll; }

		void Draw();

	private:
		struct OriginalState
		{
			void Get();
			void Revert(bool a_deactivate) const;

			// members
			float fov{};
			float translateSpeed{};

			struct
			{
				float blurMultiplier;
				float nearDist;
				float nearRange;
				float farDist;
				float farRange;
			} vanillaDOF;
		};

		// members
		OriginalState originalState{};

		bool revertENB{ false };

		float currentViewRoll{};
		float currentViewRollDegrees{};

		// Camera position management
		CameraPositions cameraPositions{};
	};

	// Hotkey-driven field of view adjustment (iFOVIncreaseKey / iFOVDecreaseKey)
	namespace FOVControl
	{
		inline constexpr float minFOV{ 5.0f };    // same range as the FOV slider
		inline constexpr float maxFOV{ 150.0f };
		inline constexpr float holdDelay{ 0.25f };  // seconds before holding a key starts continuous change

		inline float stepSize{ 1.0f };    // fFOVStep:Controls, degrees per key tap
		inline float holdSpeed{ 30.0f };  // fFOVHoldSpeed:Controls, degrees per second while held

		// a_direction: +1 = increase, -1 = decrease
		void OnButtonEvent(std::int32_t a_direction, const RE::ButtonEvent* a_event);
		void OnFrameUpdate();
		void Reset();
	}

	namespace CameraGrid
	{
		enum GridType : std::uint8_t
		{
			kDisabled,
			kRuleOfThirds,
			kDiagonal,
			kTriangle,
			kGoldenRatio,
			//kGoldenSpiral,
			kGrid
		};

		static constexpr std::array gridTypes{
			"$PM_NONE",
			"$PM_Grid_Thirds",
			"$PM_Grid_Diagonal",
			"$PM_Grid_Triangle",
			"$PM_Grid_GoldenRatio",
			/*"$PM_Grid_GoldenSpiral"*/
			"$PM_Grid_Grid"
		};

		void Draw();

		// members
		inline GridType gridType{ kDisabled };
	}
}
