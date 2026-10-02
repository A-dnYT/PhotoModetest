#pragma once

namespace Shared
{
	// Why Photo Mode / the gallery can't be shown right now (empty when it can).
	inline std::string GetMenuBlockReason()
	{
		static constexpr std::array badMenus{
			RE::MainMenu::MENU_NAME,
			RE::MistMenu::MENU_NAME,
			RE::LoadingMenu::MENU_NAME,
			RE::FaderMenu::MENU_NAME,
			"LootMenu"sv,
			"CustomMenu"sv
		};

		const auto UI = RE::UI::GetSingleton();
		if (!UI) {
			return "game UI not ready";
		}
		for (const auto& menuName : badMenus) {
			if (UI->IsMenuOpen(menuName)) {
				return std::format("{} is open", menuName);
			}
		}

		const auto* controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap) {
			return "controls not ready";
		}

		switch (const auto context = controlMap->contextPriorityStack.back()) {
		case RE::UserEvents::INPUT_CONTEXT_ID::kGameplay:
		case RE::UserEvents::INPUT_CONTEXT_ID::kTFCMode:
		case RE::UserEvents::INPUT_CONTEXT_ID::kConsole:
		case RE::UserEvents::INPUT_CONTEXT_ID::kCursor:
			return {};
		default:
			return std::format("the game is in a menu or other non-gameplay controls (input context {})", static_cast<int>(context));
		}
	}

	inline bool CanShowMenu()
	{
		return GetMenuBlockReason().empty();
	}

	inline ScreenshotIndex GetScreenshotIndex(const std::string& a_path)
	{
		boost::smatch       matches;
		static boost::regex screenshotPattern{ R"(Screenshot_?(\d+))", boost::regex::icase };
		if (boost::regex_search(a_path, matches, screenshotPattern)) {
			if (matches.size() > 1) {
				return stl::to_num_safe<ScreenshotIndex>(matches[1].str()).value_or(-1);  // STR::TO_NUM throws if idx is INT_MAX
			}
		}
		return -1;
	}

	inline std::filesystem::path GetThumbnailPath(const std::filesystem::path& a_thumbnailDir, const std::filesystem::path& a_srcPNG)
	{
		std::error_code ec;
		auto            absPath = std::filesystem::weakly_canonical(a_srcPNG, ec);
		if (ec || absPath.empty()) {
			absPath = std::filesystem::absolute(a_srcPNG, ec);
		}

		return a_thumbnailDir / std::format("{:X}.png", REX::FNV1A_64(REX::STR::TO_LOWER(absPath.string())));
	}

	inline std::expected<void, std::error_code> GetOrCreateDirectory(const std::filesystem::path& a_dir)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_dir, ec);
		if (ec) {
			return std::unexpected(ec);
		}
		return {};
	}

	inline const std::filesystem::path& GetDocumentsFolder()
	{
		static std::filesystem::path docDir = []() -> std::filesystem::path {
			if (auto directory = SKSE::log::log_directory()) {
				directory->remove_filename();
				return *directory;
			}
			return {};
		}();

		return docDir;
	}

	inline std::filesystem::path GetDocumentsFolder(std::string_view a_subPath)
	{
		return GetDocumentsFolder() / a_subPath;
	}

	inline const std::filesystem::path& GetVanillaPhotosFolder()
	{
		static std::filesystem::path vanillaDir = []() {
			const auto            baseName = "sScreenShotBaseName:Display"_ini;
			std::filesystem::path base{ *baseName };
			const auto            dir = base.parent_path();

			return dir.is_absolute() ? dir : std::filesystem::current_path() / dir;
		}();

		return vanillaDir;
	}

	template <class F>
	void ForEachFile(const std::filesystem::path& a_dir, std::string_view a_extension, F&& a_func)
	{
		std::error_code ec;
		if (!std::filesystem::exists(a_dir, ec) || ec) {
			REX::INFO("{} does not exist", a_dir.string());
			return;
		}

		for (const auto& entry : std::filesystem::directory_iterator(a_dir, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == a_extension) {
				a_func(entry.path());
			}
		}
	}

	// https://stackoverflow.com/questions/70257751/move-a-file-or-folder-to-the-recyclebin-trash-c17
	inline bool RecycleFile(const std::wstring& path)
	{
		const std::wstring widestr = path + L'\0';

		SHFILEOPSTRUCT fileOp;
		fileOp.hwnd = nullptr;
		fileOp.wFunc = FO_DELETE;
		fileOp.pFrom = widestr.c_str();
		fileOp.pTo = nullptr;
		fileOp.fFlags = FOF_ALLOWUNDO | FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;

		return SHFileOperation(&fileOp) == 0;
	}

	// std::filesystem::remove doesn't remove files managed by Root Builder
	// update: nope, doesn't work either
	inline bool RemoveFile(const std::filesystem::path& a_path)
	{
		std::error_code ec;
		std::filesystem::permissions(a_path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, ec);

		const HANDLE handle = ::CreateFileW(a_path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			std::error_code ec;
			return std::filesystem::remove(a_path, ec);
		}
		::CloseHandle(handle);
		return true;
	}
}
