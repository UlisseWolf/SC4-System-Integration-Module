#include "SimLog.h"
#include "SimLogFormat.h"
#include <Windows.h>
#include <cstdio>
#include <share.h>
#include <filesystem>
#include <atomic>
#include <mutex>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	std::FILE* file = nullptr;
	std::mutex lock;
	std::atomic<bool> debug = false;
	bool debugCheats = false;
}

void SimLog::Open()
{
	std::scoped_lock guard(lock);
	if (file) return;
	wchar_t path[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH) == 0) return;
	const std::filesystem::path log = std::filesystem::path(path).replace_filename(L"SC4SIM.log");
	const std::filesystem::path ini = std::filesystem::path(path).replace_filename(L"SC4SIM.ini");
	debugCheats = GetPrivateProfileIntW(L"SC4SIM", L"DebugCheats", 0, ini.c_str()) != 0;
	// _wfopen_s opens the file without sharing: other programs (a text editor, SC4MCP read_log) could not read the
	// log while the game runs. Deny writes only.
	file = _wfsopen(log.c_str(), L"w", _SH_DENYWR);
}

void SimLog::Write(Level level, const char* format, ...)
{
	if (level == Level::Debug && !debug) return; // [DEBUG] lines only with DebugLog=true
	std::scoped_lock guard(lock);
	if (!file) return;
	static const char* const prefix[] = { "INFO", "WARNING", "ERROR", "DEBUG" };
	std::fprintf(file, "[%s] ", prefix[static_cast<int>(level)]);
	va_list args;
	va_start(args, format);
	std::vfprintf(file, format, args);
	va_end(args);
	std::fputc('\n', file);
	std::fflush(file);
}

void SimLog::SetDebug(bool enabled) { debug = enabled; }
bool SimLog::DebugEnabled() { return debug; }
bool SimLog::DebugCheatsEnabled() { return debugCheats; }
