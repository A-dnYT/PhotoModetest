#include "Hooks.h"

#include <chrono>
#include <set>
#include <vector>

#include "Gallery/Manager.h"
#include "IGCSBridge/Bridge.h"  // IGCSDOF: direct IgcsConnector bridge
#include "Input.h"
#include "MenuIntegration.h"
#include "PhotoMode/CameraModes.h"
#include "PhotoMode/Manager.h"
#include "Screenshots/LoadScreen.h"
#include "Screenshots/Manager.h"

namespace PhotoMode
{
	struct FromEulerAnglesZXY
	{
		static void thunk(RE::NiMatrix3* a_matrix, float a_z, float a_x, float a_y)
		{
			return func(a_matrix, a_z, a_x, MANAGER(PhotoMode)->GetViewRoll(a_y));
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// IGCSDOF integration point: override the translation returned to the renderer
	// while preserving Photo Mode's native rotation and FOV.
	struct GetFreeCameraTranslation
	{
		static void thunk(RE::FreeCameraState* a_this, RE::NiPoint3& a_translation)
		{
			func(a_this, a_translation);
			auto* bridge = IGCSBridge::Bridge::GetSingleton();
			bridge->DiagnosticRenderHook(a_this, a_translation);
			bridge->OverrideRenderedTranslation(a_translation);
			bridge->DiagnosticAfterOverride(a_translation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static inline constexpr std::size_t            idx{ 0x05 };
	};

	// TESDataHandler idle array is not populated
	struct SetFormEditorID
	{
		static bool thunk(RE::TESIdleForm* a_this, const char* a_str)
		{
			if (!clib_util::string::is_empty(a_str)) {
				if (const std::string_view str(a_str); !str.starts_with("pa_")) {  // paired anims
					cachedIdles.emplace(a_str, a_this);
				}
			}
			return func(a_this, a_str);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static inline constexpr std::size_t            idx{ 0x33 };
	};

	struct ApplyFootIKErrorFeedback
	{
		static bool thunk(RE::Actor* a_actor)
		{
			if (a_actor && a_actor->IsPlayerRef() && MANAGER(PhotoMode)->IsActive()) {
				return false;
			}
			return func(a_actor);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// While Photo Mode is open over a conversation, the conversation's menu doesn't get any input (keys and clicks
	// are Photo Mode's; Escape closes Photo Mode, not the conversation).
	struct MenuControlsInput
	{
		static RE::BSEventNotifyControl thunk(RE::MenuControls* a_this, RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_source)
		{
			// (and briefly after Photo Mode closes, so the key release that closed it doesn't also act in the conversation)
			using Clock = std::chrono::steady_clock;
			static Clock::time_point lastBlocked{};
			const auto               now = Clock::now();
			if (const auto photoMode = MANAGER(PhotoMode); photoMode->IsActive() && photoMode->IsOverConversation()) {
				lastBlocked = now;
				return RE::BSEventNotifyControl::kContinue;
			}
			if (lastBlocked != Clock::time_point{} && now - lastBlocked < std::chrono::milliseconds(200)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			return func(a_this, a_event, a_source);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static inline constexpr std::size_t            idx{ 0x01 };  // BSTEventSink<InputEvent*>::ProcessEvent
	};

	void InstallHooks()
	{
		{
			// (this CommonLib's MenuControls has no VTABLE member, so use the vtable ID directly)
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_MenuControls[0] };
			MenuControlsInput::func = vtbl.write_vfunc(MenuControlsInput::idx, MenuControlsInput::thunk);
		}

		REL::Relocation<std::uintptr_t> getRot{ RELOCATION_ID(49814, 50744), 0x1B };  // FreeCamera::GetRotation
		stl::write_thunk_call<FromEulerAnglesZXY>(getRot.address());
		// IGCSDOF: intercept the rendered free-camera translation for aperture samples.
		stl::write_vfunc<RE::FreeCameraState, GetFreeCameraTranslation>();
		IGCSBridge::Bridge::GetSingleton()->LogHookInstallation(0, GetFreeCameraTranslation::idx);

		stl::write_vfunc<RE::TESIdleForm, SetFormEditorID>();

		// Freeze / Release / Follow camera modes
		CameraModes::InstallHooks();

		//REL::Relocation<std::uintptr_t> applyFootIKErrorFeedback{ RELOCATION_ID(42527, 43690) };  // Actor::ApplyFootIKErrorFeedback
		//stl::hook_function_prologue<ApplyFootIKErrorFeedback, 5>(applyFootIKErrorFeedback.address());
	}
}

namespace Screenshot
{
	struct TakeScreenshot
	{
		static void thunk(char const* a_path, RE::BSGraphics::TextureFileFormat a_format)
		{
			bool skipVanillaScreenshot = false;

			if (MANAGER(Input)->IsScreenshotQueued()) {
				skipVanillaScreenshot = MANAGER(Screenshot)->TakeScreenshot();
			}

			if (!skipVanillaScreenshot) {
				func(a_path, a_format);
			}

			MANAGER(Input)->OnScreenshotFinish();

			if (skipVanillaScreenshot) {
				RE::SendHUDMessage::ShowHUDMessage("$PM_ScreenshotNotif"_T);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(35556, 36555), OFFSET(0x48E, 0x454) };  // Main::Swap
		stl::write_thunk_call<TakeScreenshot>(target.address());
	}
}

namespace LoadScreen
{
	struct GetLoadScreenModel
	{
		static RE::TESModelTextureSwap* thunk([[maybe_unused]] RE::TESLoadScreen* a_loadScreen)
		{
			if (const auto screenshotModel = MANAGER(LoadScreen)->LoadScreenshotModel()) {
				return screenshotModel;
			}
			return func(a_loadScreen);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct InitLoadScreen3D
	{
		static void thunk(RE::MistMenu* a_this, float a_scale, const RE::NiPoint3& a_rotateOffset, const RE::NiPoint3& a_translateOffset, const char* a_cameraShotPath)
		{
			if (auto transform = MANAGER(LoadScreen)->GetModelTransform()) {
				func(a_this, transform->scale, transform->rotationalOffset, transform->translateOffset, MANAGER(LoadScreen)->GetCameraShotPath(a_cameraShotPath));
				if (const auto canvas = a_this->loadScreenModel ? a_this->loadScreenModel->GetObjectByName("Canvas:0") : nullptr) {
					MANAGER(LoadScreen)->ApplyScreenshotTexture(canvas->AsGeometry());
				}
			} else {
				func(a_this, a_scale, a_rotateOffset, a_translateOffset, a_cameraShotPath);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(51048, 51929), OFFSET(0x384, 0x27B) };
		stl::write_thunk_call<GetLoadScreenModel>(target.address());

		REL::Relocation<std::uintptr_t> target2{ RELOCATION_ID(51454, 52313), OFFSET(0x1D1, 0x1C3) };
		stl::write_thunk_call<InitLoadScreen3D>(target2.address());
	}
}

namespace Input
{
	// Freeze / Release / Follow Cam: the controller is Photo Mode's (camera, hotkeys) and the game doesn't get it at all,
	// so it can't also make the player sprint, switch the game to controller mode (e.g. Auto Input Switch, which changes
	// how keyboard movement behaves), etc. Photo Mode is handed the controller events itself.
	// The game still gets the release of anything it saw pressed before (and a stick going back to centre), so nothing
	// stays held when the mode is left.
	namespace ControllerToPhotoMode
	{
		std::set<std::uint32_t> gameHeldButtons;  // controller buttons the game last saw pressed
		bool                    gameStickOff[2]{ true, true };  // left / right stick last seen by the game was centred

		bool ShouldFilter()
		{
			const auto photoMode = MANAGER(PhotoMode);
			return photoMode->IsActive() && PhotoMode::CameraModes::IsCinematic() && !photoMode->ShouldBlockInput();
		}

		// whether the game should get this controller event, keeping track of what it has seen
		bool GameGets(RE::InputEvent* a_event, bool a_filtering)
		{
			if (const auto button = a_event->AsButtonEvent()) {
				const auto id = button->GetIDCode();
				const bool pressed = button->IsPressed();
				if (a_filtering && !gameHeldButtons.contains(id)) {
					return false;  // the game didn't see it pressed: a new press (or its release) while the controller is Photo Mode's
				}
				if (pressed) {
					gameHeldButtons.insert(id);
				} else {
					gameHeldButtons.erase(id);
				}
				return true;
			}
			if (a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
				const auto stick = static_cast<RE::ThumbstickEvent*>(a_event);
				const auto index = stick->IsRight() ? 1 : 0;
				const bool off = stick->xValue == 0.0f && stick->yValue == 0.0f;
				if (a_filtering && !(off && !gameStickOff[index])) {
					return false;  // only the return to centre, if the game still thinks it's pushed
				}
				gameStickOff[index] = off;
				return true;
			}
			return !a_filtering;
		}
	}

	struct ProcessInputQueue
	{
		static void thunk(RE::BSInputDeviceManager* a_deviceManager, RE::InputEvent* const* a_events)
		{
			if (a_events && *a_events && MANAGER(Gallery)->IsActive() && !MANAGER(MenuIntegration)->GetConsoleOpen()) {
				MANAGER(Input)->ProcessGalleryEvents(a_events);
				constexpr RE::InputEvent* const dummy[] = { nullptr };
				func(a_deviceManager, dummy);
				return;
			}
			if (!a_events || !*a_events) {
				func(a_deviceManager, a_events);
				return;
			}

			using namespace ControllerToPhotoMode;
			const bool filtering = ShouldFilter();

			// remember the list so it can be put back exactly
			std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> links;
			for (auto event = *a_events; event; event = event->next) {
				links.emplace_back(event, event->next);
			}

			const auto relink = [&](auto&& a_keep) {
				RE::InputEvent* head = nullptr;
				RE::InputEvent* tail = nullptr;
				for (const auto& [event, next] : links) {
					if (!a_keep(event)) {
						continue;
					}
					(tail ? tail->next : head) = event;
					tail = event;
				}
				if (tail) {
					tail->next = nullptr;
				}
				return head;
			};

			if (!filtering) {
				// keep track of what the game sees from the controller, then hand everything over as usual
				for (const auto& [event, next] : links) {
					if (event->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
						GameGets(event, false);
					}
				}
				func(a_deviceManager, a_events);
				return;
			}

			// 1. Photo Mode gets the controller events directly
			if (const auto controller = relink([](RE::InputEvent* a_event) { return a_event->GetDevice() == RE::INPUT_DEVICE::kGamepad; })) {
				MANAGER(Input)->ProcessPhotoModeEvents(&controller);
			}

			// 2. the game (and Photo Mode, as a listener) gets everything else, plus controller releases it needs
			std::set<RE::InputEvent*> forGame;
			for (const auto& [event, next] : links) {
				if (event->GetDevice() != RE::INPUT_DEVICE::kGamepad || GameGets(event, true)) {
					forGame.insert(event);
				}
			}
			const auto gameHead = relink([&](RE::InputEvent* a_event) { return forGame.contains(a_event); });
			if (gameHead) {
				func(a_deviceManager, &gameHead);
			} else {
				constexpr RE::InputEvent* const dummy[] = { nullptr };
				func(a_deviceManager, dummy);
			}

			for (const auto& [event, next] : links) {
				event->next = next;
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> inputUnk(RELOCATION_ID(67315, 68617), 0x7B);
		stl::write_thunk_call<ProcessInputQueue>(inputUnk.address());
	}
}

void Hooks::Install()
{
	PhotoMode::InstallHooks();
	Screenshot::InstallHooks();
	LoadScreen::InstallHooks();
	Input::InstallHooks();
}
