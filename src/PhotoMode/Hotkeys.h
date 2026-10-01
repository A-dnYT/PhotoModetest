#pragma once

namespace IconFont
{
	struct IconTexture;
}

namespace PhotoMode::Hotkeys
{
	class Manager : public REX::TSingleton<Manager>
	{
	public:
		void LoadHotKeys(const CSimpleIniA& a_ini);

		void TogglePhotoMode(RE::InputEvent* const* a_event);
		void ToggleGallery(RE::InputEvent* const* a_event);

		// true when a_key is the hotkey and its modifier (if any) is held
		bool IsReset(std::uint32_t a_key) const { return reset.Matches(a_key); }
		bool IsTakePhoto(std::uint32_t a_key) const { return takePhoto.Matches(a_key); }
		bool IsToggleMenus(std::uint32_t a_key) const { return toggleMenus.Matches(a_key); }
		bool IsNextTab(std::uint32_t a_key) const { return nextTab.Matches(a_key); }
		bool IsPreviousTab(std::uint32_t a_key) const { return previousTab.Matches(a_key); }

		std::uint32_t        ResetKey() const;
		std::uint32_t        TakePhotoKey() const;
		std::uint32_t        ToggleMenusKey() const;
		std::uint32_t        NextTabKey() const;
		std::uint32_t        PreviousTabKey() const;
		std::uint32_t        FreezeTimeKey() const;
		std::uint32_t        PanCameraKey() const;
		static std::uint32_t EscapeKey();
		std::uint32_t        GalleryEnlargeKey() const;
		std::uint32_t        GalleryDeleteKey() const;
		std::uint32_t        GalleryLoadScreenKey() const;

		const IconFont::IconTexture*        ResetIcon() const;
		const IconFont::IconTexture*        TakePhotoIcon() const;
		const IconFont::IconTexture*        ToggleMenusIcon() const;
		const IconFont::IconTexture*        NextTabIcon() const;
		const IconFont::IconTexture*        PreviousTabIcon() const;
		const IconFont::IconTexture*        FreezeTimeIcon() const;
		const IconFont::IconTexture*        PanCameraIcon() const;
		static const IconFont::IconTexture* EscapeIcon();
		const IconFont::IconTexture*        GalleryEnlargeIcon() const;
		const IconFont::IconTexture*        GalleryDeleteIcon() const;
		const IconFont::IconTexture*        GalleryLoadScreenIcon() const;

		std::set<const IconFont::IconTexture*> ToggleGalleryIcons() const;
		std::set<const IconFont::IconTexture*> TogglePhotoModeIcons() const;

	private:
		struct Key
		{
			void          LoadKeys(const CSimpleIniA& a_ini, std::string_view a_setting);
			std::uint32_t GetKey() const;
			bool          Matches(std::uint32_t a_key) const;

			std::uint32_t Keyboard() const;
			std::uint32_t GamePad() const;

		private:
			std::uint32_t keyboard{ 0 };
			std::uint32_t gamePad{ 0 };
			std::int32_t  keyboardModifier{ -1 };  // <setting>KeyModifier, -1 = none
			std::int32_t  gamePadModifier{ -1 };   // <setting>GamePadModifier, -1 = none
		};

		struct KeyCombo
		{
			void LoadKeys(const CSimpleIniA& a_ini, std::string_view a_settingPrefix);

			bool                    IsInvalid() const;
			std::set<std::uint32_t> GetKeys() const;

			bool ProcessKeyPress(RE::InputEvent* const* a_event, std::function<void()> a_callback);

		private:
			struct KeyComboImpl
			{
				void LoadKeys(const CSimpleIniA& a_ini, std::string_view a_setting);

				std::int32_t primary{ -1 };
				std::int32_t modifier{ -1 };

				std::set<std::uint32_t> keys{};
			};

			KeyComboImpl keyboard;
			KeyComboImpl gamePad;

			bool triggered{ false };
		} togglePhotoMode;

		KeyCombo toggleGallery;

		Key nextTab;
		Key previousTab;
		Key takePhoto;
		Key toggleMenus;
		Key reset;
		Key freezeTime;
		Key panCamera;
		Key galleryEnlarge;
		Key galleryDelete;
		Key galleryLoadScreen;
	};
}
namespace Hotkeys = PhotoMode::Hotkeys;
