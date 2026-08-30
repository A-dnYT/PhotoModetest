#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#define IMGUI_DEFINE_MATH_OPERATORS
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#define MANAGER(T) T::Manager::GetSingleton()

#include "RE/Skyrim.h"
#include "REX/REX.h"
#include "SKSE/SKSE.h"

#include <codecvt>
#include <wrl/client.h>

#include <shellapi.h>

#include <DirectXMath.h>
#include <DirectXTex.h>

#include <boost/regex.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/unordered/unordered_node_map.hpp>
#include <freetype/freetype.h>
#include <glaze/glaze.hpp>
#include <rapidfuzz/rapidfuzz_all.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <xbyak/xbyak.h>

#include "ImGui/Backend/imgui_impl_win32.h"
#include "imgui_internal.h"
#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <ClibUtil/editorID.hpp>
#include <ClibUtil/simpleini.hpp>

#define DLLEXPORT __declspec(dllexport)

using namespace std::literals;
using namespace RE::literals;

namespace ini = clib_util::ini;
namespace editorID = clib_util::editorID;

using EventResult = RE::BSEventNotifyControl;

using KEY = RE::BSWin32KeyboardDevice::Key;
using GAMEPAD_DIRECTX = RE::BSWin32GamepadDevice::Key;
using GAMEPAD_ORBIS = RE::BSPCOrbisGamepadDevice::Key;
using MOUSE = RE::BSWin32MouseDevice::Key;

using ScreenshotIndex = std::int64_t;

template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

template <class K, class D, class H = boost::hash<K>, class KEqual = std::equal_to<K>>
using NodeMap = boost::unordered_node_map<K, D, H, KEqual>;

template <class K, class D, class H = boost::hash<K>, class KEqual = std::equal_to<K>>
using FlatMap = boost::unordered_flat_map<K, D, H, KEqual>;

template <class K, class H = boost::hash<K>, class KEqual = std::equal_to<K>>
using FlatSet = boost::unordered_flat_set<K, H, KEqual>;

struct string_hash
{
	using is_transparent = void;

	std::size_t operator()(const char* str) const
	{
		return boost::hash<std::string_view>{}(str);
	}

	std::size_t operator()(std::string_view str) const
	{
		return boost::hash<std::string_view>{}(str);
	}

	std::size_t operator()(const std::string& str) const
	{
		return boost::hash<std::string>{}(str);
	}
};

template <class D>
using StringMap = FlatMap<std::string, D, string_hash, std::equal_to<>>;

using StringSet = FlatSet<std::string, string_hash, std::equal_to<>>;

namespace stl
{
	template <class T>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = REL::GetTrampoline();
		T::func = trampoline.write_call<5>(a_src, T::thunk);
	}

	template <class F, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[0] };
		T::func = vtbl.write_vfunc(T::idx, T::thunk);
	}

	template <class T, std::size_t BYTES>
	void hook_function_prologue(std::uintptr_t a_src)
	{
		struct Patch : Xbyak::CodeGenerator
		{
			Patch(std::uintptr_t a_originalFuncAddr, std::size_t a_originalByteLength)
			{
				// Hook returns here. Execute the restored bytes and jump back to the original function.
				for (size_t i = 0; i < a_originalByteLength; ++i) {
					db(*reinterpret_cast<std::uint8_t*>(a_originalFuncAddr + i));
				}

				jmp(ptr[rip]);
				dq(a_originalFuncAddr + a_originalByteLength);
			}
		};

		Patch p(a_src, BYTES);
		p.ready();

		auto& trampoline = REL::GetTrampoline();
		trampoline.write_jmp<5>(a_src, T::thunk);

		auto alloc = trampoline.allocate(p.getSize());
		std::memcpy(alloc, p.getCode(), p.getSize());

		T::func = reinterpret_cast<std::uintptr_t>(alloc);
	}

	inline std::string uft16_to_uft8(std::wstring_view a_value)
	{
		std::string value8;
		REX::UTF16_TO_UTF8(a_value, value8);
		return value8;
	}

	inline std::wstring uft8_to_uft16(std::string_view a_value)
	{
		std::wstring value16;
		REX::UTF8_TO_UTF16(a_value, value16);
		return value16;
	}

	template <class T>
	std::optional<T> to_num_safe(std::string_view a_str)
	{
		T value{};
		const auto [ptr, ec] = std::from_chars(a_str.data(), a_str.data() + a_str.size(), value);
		if (ec == std::errc() && ptr == a_str.data() + a_str.size()) {
			return value;
		}
		return std::nullopt;
	}
}

namespace Runtime
{
	inline constexpr REL::Version SSE_1_7_99(1, 7, 99, 0);
	inline constexpr REL::Version MIN_ADDRESS_LIBRARY_V5 = SSE_1_7_99;

	[[nodiscard]] inline bool IsAtLeast1_7_99() noexcept
	{
		static bool result = REX::FModule::GetExecutingModule().GetFileVersion() >= Runtime::SSE_1_7_99;
		return result;
	}
}

#ifdef SKYRIM_AE
#	define OFFSET(se, ae) ae
#else
#	define OFFSET(se, ae) se
#endif

#include "Cache.h"
#include "Shared.h"
#include "Translation.h"
#include "Version.h"
