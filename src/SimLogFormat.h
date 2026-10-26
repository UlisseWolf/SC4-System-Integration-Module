// SC4SIM DLL - std::format log macros over SimLog (used by the power-pole code).
#pragma once
#include "SimLog.h"

#include <format>
#include <string>

namespace SimLog
{
	void SetDebug(bool enabled);
	bool DebugEnabled();

	template <typename... Args>
	void WriteFormat(Level level, std::format_string<Args...> format, Args&&... args)
	{
		const std::string text = std::format(format, std::forward<Args>(args)...);
		Write(level, "%s", text.c_str());
	}
}

#define LOG_INFO(...) ::SimLog::WriteFormat(::SimLog::Level::Info, __VA_ARGS__)
#define LOG_WARN(...) ::SimLog::WriteFormat(::SimLog::Level::Warning, __VA_ARGS__)
#define LOG_ERROR(...) ::SimLog::WriteFormat(::SimLog::Level::Error, __VA_ARGS__)
#define LOG_DEBUG(...) do { if (::SimLog::DebugEnabled()) ::SimLog::WriteFormat(::SimLog::Level::Debug, __VA_ARGS__); } while (0)
