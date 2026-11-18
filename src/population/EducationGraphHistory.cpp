////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: education coverage, school / library / museum grades and the highest / lowest neighbourhood EQ, for the
// Maxis Education graph.
//
// Same mechanism as the power graph (src/power/PowerGraphHistory.cpp): one point per series is recorded in the
// city's history warehouse on every SimNewMonth, and CAM's Education graph exemplar (0x0D, the game's average EQ
// GA_EQ 0xAA1A2864 on a 0-200 scale) plots them next to the EQ. The values exist only in the advisor Lua table `game`,
// which the game's advisor data pump fills: g_education_coverage_p (0x0041529F, ratio x 100), ga_school_grade
// (0x00414C21), ga_library_grade (0x00415244), ga_museum_grade (0x00414F36, each ratio x 100), l_tract_eq_h
// (0x00415924) and l_tract_eq_l (0x004158A2), the EQ of the best and worst neighbourhood.
//
////////////////////////////////////////////////////////////////////////////

#include "EducationGraphHistory.h"
#include "SimLog.h"

#include "cIGZMessage2Standard.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4HistoryWarehouse.h"
#include "cISCLua.h"
#include "GZServPtrs.h"

#include <Windows.h>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessageSimNewMonth = 0x66956816;

	bool enabled = true;

	struct EducationData
	{
		double coverage = 0.0;
		double school = 0.0;
		double library = 0.0;
		double museum = 0.0;
		double eqHigh = 0.0;
		double eqLow = 0.0;
	};

	double ReadField(cISCLua* lua, const char* name)
	{
		lua->PushString(name);
		lua->GetTable(-2);
		const double value = lua->IsNumber(-1) ? lua->ToNumber(-1) : 0.0;
		lua->Pop(1);
		return std::isfinite(value) ? value : 0.0;
	}

	bool ReadGameTable(cISC4City* city, EducationData& out)
	{
		cISC4AdvisorSystem* const advisors = city->GetAdvisorSystem();
		cISCLua* const lua = advisors ? advisors->GetScriptingContext() : nullptr;
		if (!lua) return false;
		const int32_t top = lua->GetTop();
		lua->GetGlobal("game");
		if (!lua->IsTable(-1))
		{
			lua->SetTop(top);
			return false;
		}
		out.coverage = ReadField(lua, "g_education_coverage_p");
		out.school = ReadField(lua, "ga_school_grade");
		out.library = ReadField(lua, "ga_library_grade");
		out.museum = ReadField(lua, "ga_museum_grade");
		out.eqHigh = ReadField(lua, "l_tract_eq_h");
		out.eqLow = ReadField(lua, "l_tract_eq_l");
		lua->SetTop(top);
		return true;
	}

	int32_t Round(double value)
	{
		if (!(value > 0.0)) return 0;
		return value >= 2147483647.0 ? 0x7FFFFFFF : static_cast<int32_t>(value + 0.5);
	}

	void RecordMonth()
	{
		cISC4AppPtr app;
		cISC4City* const city = app ? app->GetCity() : nullptr;
		cISC4HistoryWarehouse* const history = city ? city->GetHistoryWarehouse() : nullptr;
		EducationData data;
		if (!history || !ReadGameTable(city, data)) return;

		history->RecordHistoryData(EducationGraphHistory::kHistoryEducationCoverage, Round(data.coverage));
		history->RecordHistoryData(EducationGraphHistory::kHistorySchoolGrade, Round(data.school));
		history->RecordHistoryData(EducationGraphHistory::kHistoryLibraryGrade, Round(data.library));
		history->RecordHistoryData(EducationGraphHistory::kHistoryMuseumGrade, Round(data.museum));
		history->RecordHistoryData(EducationGraphHistory::kHistoryEqHigh, Round(data.eqHigh));
		history->RecordHistoryData(EducationGraphHistory::kHistoryEqLow, Round(data.eqLow));
		SimLog::Write(SimLog::Level::Debug,
			"EducationGraph: coverage %.1f%%, grades school %.1f%% library %.1f%% museum %.1f%%, EQ %.0f-%.0f.",
			data.coverage, data.school, data.library, data.museum, data.eqLow, data.eqHigh);
	}
}

void EducationGraphHistory::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[16]{};
	GetPrivateProfileStringW(L"EducationGraph", L"Enabled", L"true", value, 16, ini.c_str());
	std::wstring text(value);
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	SimLog::Write(SimLog::Level::Info, "EducationGraph: education coverage, grades and neighbourhood EQ %s.", enabled ? "on" : "off");
}

void EducationGraphHistory::DoMessage(cIGZMessage2* message)
{
	if (!enabled || message->GetType() != kMessageSimNewMonth) return;
	RecordMonth();
}
