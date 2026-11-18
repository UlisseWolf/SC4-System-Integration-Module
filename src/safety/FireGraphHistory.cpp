////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: fire outbreaks and fire stations, for the Fire Protection graph.
//
// Same mechanism as the power graph (src/power/PowerGraphHistory.cpp): one point per series is recorded in the
// city's history warehouse on every SimNewMonth, and CAM's graph exemplar 0x1D plots them. The game records fire
// coverage (G_FIRE_PROTECTION_COVERAGE_P 0x2A11D203, percent, at 0x006AD342), average fire protection
// (GA_FIRE_PROTECTION 0x2A11D204, 0-256) and average flammability (GA_FLAMMABILITY 0x2A11CB48, 0-256, at
// 0x006AF899); the module adds:
//  - fire outbreaks: the game records a sample of FIRE_DISASTER (0x49D15B74, value 1) when a fire breaks out
//    (0x0057A133). The count of the last month comes from the advisors' own game.trend_count(FIRE_DISASTER, 1) (Lua,
//    0x00409900), so it follows the game's history rules (several outbreaks on the same day are one sample);
//  - fire stations: cISC4FireProtectionSimulator::GetFireStationCount (+0x5C, as the advisor data pump at 0x004160AD
//    reads g_fire_station_count).
//
////////////////////////////////////////////////////////////////////////////

#include "FireGraphHistory.h"
#include "SimLog.h"

#include <limits> // before cISC4FireProtectionSimulator.h: its ilist allocator uses std::numeric_limits

#include "cIGZMessage2Standard.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4FireProtectionSimulator.h"
#include "cISC4HistoryWarehouse.h"
#include "cISCLua.h"
#include "GZServPtrs.h"

#include <Windows.h>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessageSimNewMonth = 0x66956816;
	constexpr uint32_t kTrendFireDisaster = 0x49D15B74;

	bool enabled = true;

	// game.trend_count(FIRE_DISASTER, 1) in the advisor Lua; -1 when the call fails.
	int32_t FireOutbreaksLastMonth(cISC4City* city)
	{
		cISC4AdvisorSystem* const advisors = city->GetAdvisorSystem();
		cISCLua* const lua = advisors ? advisors->GetScriptingContext() : nullptr;
		if (!lua) return -1;
		const int32_t top = lua->GetTop();
		lua->GetGlobal("game");
		if (!lua->IsTable(-1))
		{
			lua->SetTop(top);
			return -1;
		}
		lua->PushString("trend_count");
		lua->GetTable(-2);
		if (!lua->IsFunction(-1))
		{
			lua->SetTop(top);
			return -1;
		}
		lua->PushNumber(static_cast<double>(kTrendFireDisaster));
		lua->PushNumber(1.0);
		int32_t result = -1;
		if (lua->CallProtected(2, 1) == 0)
		{
			if (lua->IsNumber(-1))
			{
				const double count = lua->ToNumber(-1);
				result = count > 0.0 ? static_cast<int32_t>(count + 0.5) : 0;
			}
		}
		else
		{
			const char* const error = lua->IsString(-1) ? lua->ToString(-1) : "?";
			SimLog::Write(SimLog::Level::Warning, "FireGraph: game.trend_count failed: %s", error);
		}
		lua->SetTop(top);
		return result;
	}

	void RecordMonth()
	{
		cISC4AppPtr app;
		cISC4City* const city = app ? app->GetCity() : nullptr;
		cISC4FireProtectionSimulator* const fire = city ? city->GetFireProtectionSimulator() : nullptr;
		cISC4HistoryWarehouse* const history = city ? city->GetHistoryWarehouse() : nullptr;
		if (!fire || !history) return;

		const uint32_t stations = fire->GetFireStationCount();
		const int32_t outbreaks = FireOutbreaksLastMonth(city);
		history->RecordHistoryData(FireGraphHistory::kHistoryFireStations, static_cast<int32_t>(stations));
		if (outbreaks >= 0) history->RecordHistoryData(FireGraphHistory::kHistoryFireOutbreaks, outbreaks);
		SimLog::Write(SimLog::Level::Debug, "FireGraph: %d fire outbreaks last month, %u fire stations.", outbreaks, stations);
	}
}

void FireGraphHistory::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[16]{};
	GetPrivateProfileStringW(L"FireGraph", L"Enabled", L"true", value, 16, ini.c_str());
	std::wstring text(value);
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	SimLog::Write(SimLog::Level::Info, "FireGraph: fire outbreaks and fire stations %s.", enabled ? "on" : "off");
}

void FireGraphHistory::DoMessage(cIGZMessage2* message)
{
	if (!enabled || message->GetType() != kMessageSimNewMonth) return;
	RecordMonth();
}
