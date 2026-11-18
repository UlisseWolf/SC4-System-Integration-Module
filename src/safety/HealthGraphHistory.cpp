////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: health coverage, health, health grade, hospital occupancy and health buildings, for the Health graph.
//
// Same mechanism as the power graph (src/power/PowerGraphHistory.cpp): one point per series is recorded in the
// city's history warehouse on every SimNewMonth, and CAM's graph exemplar 0x1E plots them. The game keeps no history
// of health except life expectancy (0x2A5E7009, the Maxis Life Expectancy graph); G_POPULATION_HEALTH_COVERAGE
// (0xAA1A2C9F) is listed in the advisor constants but never recorded. The health simulator has no interface in
// gzcom-dll, so the values come from the advisor Lua table `game`, which the game's advisor data pump fills
// (0x004143C8-0x004157CE): g_health_coverage_p, ga_health, ga_health_grade, g_num_treated_patients,
// g_health_capacity, g_num_hospitals, g_num_clinics. CAM's panels read the same fields (Health Rating C-4 =
// ga_health / 2, medical buildings = g_num_hospitals). The same table gives the life expectancy of the best and worst
// neighbourhood (l_tract_life_exp_h at 0x004156FA, l_tract_life_exp_l at 0x004155EE), plotted in the Maxis Life
// Expectancy graph next to the city average.
//
////////////////////////////////////////////////////////////////////////////

#include "HealthGraphHistory.h"
#include "SimLog.h"

#include "cIGZMessage2Standard.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4HistoryWarehouse.h"
#include "cISCLua.h"
#include "GZServPtrs.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessageSimNewMonth = 0x66956816;

	bool enabled = true;

	struct HealthData
	{
		double coverage = 0.0;
		double health = 0.0;
		double grade = 0.0;
		double treated = 0.0;
		double capacity = 0.0;
		double hospitals = 0.0;
		double clinics = 0.0;
		double lifeHigh = 0.0;
		double lifeLow = 0.0;
	};

	double ReadField(cISCLua* lua, const char* name)
	{
		lua->PushString(name);
		lua->GetTable(-2);
		const double value = lua->IsNumber(-1) ? lua->ToNumber(-1) : 0.0;
		lua->Pop(1);
		return std::isfinite(value) ? value : 0.0;
	}

	bool ReadGameTable(cISC4City* city, HealthData& out)
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
		out.coverage = ReadField(lua, "g_health_coverage_p");
		out.health = ReadField(lua, "ga_health");
		out.grade = ReadField(lua, "ga_health_grade");
		out.treated = ReadField(lua, "g_num_treated_patients");
		out.capacity = ReadField(lua, "g_health_capacity");
		out.hospitals = ReadField(lua, "g_num_hospitals");
		out.clinics = ReadField(lua, "g_num_clinics");
		out.lifeHigh = ReadField(lua, "l_tract_life_exp_h");
		out.lifeLow = ReadField(lua, "l_tract_life_exp_l");
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
		HealthData data;
		if (!history || !ReadGameTable(city, data)) return;

		const double occupancy = data.capacity > 0.0 ? data.treated * 100.0 / data.capacity : 0.0;
		history->RecordHistoryData(HealthGraphHistory::kHistoryHealthCoverage, Round(data.coverage));
		history->RecordHistoryData(HealthGraphHistory::kHistoryAverageHealth, Round(data.health / 2.0));
		history->RecordHistoryData(HealthGraphHistory::kHistoryHealthGrade, Round(data.grade));
		history->RecordHistoryData(HealthGraphHistory::kHistoryHospitalOccupancy, Round(occupancy));
		history->RecordHistoryData(HealthGraphHistory::kHistoryHospitals, Round(data.hospitals));
		history->RecordHistoryData(HealthGraphHistory::kHistoryClinics, Round(data.clinics));
		history->RecordHistoryData(HealthGraphHistory::kHistoryLifeExpectancyHigh, Round(data.lifeHigh));
		history->RecordHistoryData(HealthGraphHistory::kHistoryLifeExpectancyLow, Round(data.lifeLow));
		SimLog::Write(SimLog::Level::Debug,
			"HealthGraph: coverage %.1f%%, health %.1f, grade %.1f%%, %.0f patients of %.0f places, %.0f hospitals, %.0f clinics, life expectancy %.0f-%.0f.",
			data.coverage, data.health, data.grade, data.treated, data.capacity, data.hospitals, data.clinics, data.lifeLow, data.lifeHigh);
	}
}

void HealthGraphHistory::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[16]{};
	GetPrivateProfileStringW(L"HealthGraph", L"Enabled", L"true", value, 16, ini.c_str());
	std::wstring text(value);
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	SimLog::Write(SimLog::Level::Info, "HealthGraph: health coverage, grade and hospitals %s.", enabled ? "on" : "off");
}

void HealthGraphHistory::DoMessage(cIGZMessage2* message)
{
	if (!enabled || message->GetType() != kMessageSimNewMonth) return;
	RecordMonth();
}
