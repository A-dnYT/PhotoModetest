#include "NearClip.h"

#include <algorithm>

namespace PhotoMode::NearClip
{
	namespace
	{
		float defaultValue{ 3.0f };  // fNearClipDistance
		float value{ 3.0f };

		RE::Setting* gameSetting{ nullptr };  // fNearDistance:Display
		float        gameValue{ 15.0f };      // its value before Photo Mode opened
		bool         active{ false };

		RE::Setting* FindGameSetting()
		{
			if (!gameSetting) {
				if (const auto prefs = RE::INIPrefSettingCollection::GetSingleton()) {
					gameSetting = prefs->GetSetting("fNearDistance:Display");
				}
				if (!gameSetting) {
					if (const auto ini = RE::INISettingCollection::GetSingleton()) {
						gameSetting = ini->GetSetting("fNearDistance:Display");
					}
				}
				if (!gameSetting) {
					REX::INFO("NearClip: fNearDistance:Display not found; using the camera's near plane only");
				}
			}
			return gameSetting;
		}
	}

	void LoadSettings(const CSimpleIniA& a_ini)
	{
		defaultValue = std::clamp(static_cast<float>(a_ini.GetDoubleValue("Settings", "fNearClipDistance", defaultValue)), kMin, kMax);
	}

	void OnActivate()
	{
		if (const auto setting = FindGameSetting()) {
			gameValue = setting->data.f;
		}
		value = defaultValue;
		active = true;
		REX::INFO("NearClip: {} while Photo Mode is open (game: {})", value, gameValue);
	}

	void OnDeactivate()
	{
		if (!active) {
			return;
		}
		active = false;
		if (const auto setting = FindGameSetting()) {
			setting->data.f = gameValue;
		}
		if (const auto camera = RE::Main::WorldRootCamera()) {
			camera->viewFrustum.fNear = gameValue;
		}
	}

	void ResetToDefault()
	{
		value = defaultValue;
	}

	float& Value()
	{
		return value;
	}

	void Apply(RE::NiCamera* a_camera)
	{
		if (!active) {
			return;
		}
		value = std::clamp(value, kMin, kMax);
		if (const auto setting = FindGameSetting()) {
			setting->data.f = value;
		}
		if (a_camera) {
			a_camera->viewFrustum.fNear = value;
		}
	}
}
