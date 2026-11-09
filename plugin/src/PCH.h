#pragma once

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>
#ifdef SOFTBODY_RD
#	include <spdlog/sinks/basic_file_sink.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std::literals;

// A rotation matrix's element (row, column): libxse's CommonLibF4 stores rows as float arrays, CommonLibF4RD (the
// one-plugin-for-every-runtime build, see rd/) as NiPoint4s
#ifdef SOFTBODY_RD
inline float&       At(RE::NiMatrix3& a_m, int a_r, int a_c) { return a_m.entry[a_r].pt[a_c]; }
inline float        At(const RE::NiMatrix3& a_m, int a_r, int a_c) { return a_m.entry[a_r].pt[a_c]; }
#else
inline float&       At(RE::NiMatrix3& a_m, int a_r, int a_c) { return a_m.entry[a_r][a_c]; }
inline float        At(const RE::NiMatrix3& a_m, int a_r, int a_c) { return a_m.entry[a_r][a_c]; }
#endif

#ifdef SOFTBODY_RD
// libxse's REX logging and Win32 wrappers, as the CommonLibF4RD build needs them (spdlog underneath)
extern "C" __declspec(dllimport) unsigned int __stdcall GetPrivateProfileIntA(const char*, const char*, int, const char*);
extern "C" __declspec(dllimport) unsigned long __stdcall GetPrivateProfileStringA(const char*, const char*, const char*,
	char*, unsigned long, const char*);
namespace REX
{
	template <class... A>
	void INFO(std::format_string<A...> a_fmt, A&&... a_args) { spdlog::info("{}", std::format(a_fmt, std::forward<A>(a_args)...)); }
	template <class... A>
	void WARN(std::format_string<A...> a_fmt, A&&... a_args) { spdlog::warn("{}", std::format(a_fmt, std::forward<A>(a_args)...)); }
	template <class... A>
	void ERROR(std::format_string<A...> a_fmt, A&&... a_args) { spdlog::error("{}", std::format(a_fmt, std::forward<A>(a_args)...)); }
	namespace W32
	{
		using ::GetPrivateProfileIntA;
		using ::GetPrivateProfileStringA;
	}
}
#endif
