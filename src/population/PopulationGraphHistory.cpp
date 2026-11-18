////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: workforce and R / C / I demand forecast, for the Population R/C/I and Demand Forecast graphs.
//
// Same mechanism as the power graph (src/power/PowerGraphHistory.cpp): one point per series is recorded in the
// city's history warehouse on every SimNewMonth, and CAM's graph exemplars 0x1F and 0x20 plot them. The game records
// the total population (G_POPULATION 0xAA1A2CCA) and the R / C / I totals (G_R_POPULATION 0x2A4E2003, G_C_POPULATION
// 0x0A4E2056, G_I_POPULATION 0x4A4E206B) itself. The workforce and the demand forecasts (the demand simulator's
// extrapolated demand, the "Extrapolated" block of adv_game_data.lua) exist only in the advisor Lua table `game`,
// which the game's advisor data pump fills (g_city_workforce_population at 0x00412A3C, g_workforce_demand,
// g_r_demand_extrap at 0x0041337F, g_c_demand_extrap at 0x004134D3, g_i_demand_extrap at 0x004135E5): the DLL reads
// them there.
//
////////////////////////////////////////////////////////////////////////////

#include "PopulationGraphHistory.h"
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

	struct PopulationData
	{
		double workforce = 0.0;
		double workforceDemand = 0.0;
		double residentialForecast = 0.0;
		double commercialForecast = 0.0;
		double industrialForecast = 0.0;
	};

	double ReadField(cISCLua* lua, const char* name)
	{
		lua->PushString(name);
		lua->GetTable(-2);
		const double value = lua->IsNumber(-1) ? lua->ToNumber(-1) : 0.0;
		lua->Pop(1);
		return std::isfinite(value) ? value : 0.0;
	}

	bool ReadGameTable(cISC4City* city, PopulationData& out)
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
		out.workforce = ReadField(lua, "g_city_workforce_population");
		out.workforceDemand = ReadField(lua, "g_workforce_demand");
		out.residentialForecast = ReadField(lua, "g_r_demand_extrap");
		out.commercialForecast = ReadField(lua, "g_c_demand_extrap");
		out.industrialForecast = ReadField(lua, "g_i_demand_extrap");
		lua->SetTop(top);
		return true;
	}

	// Demand can be negative.
	int32_t Round(double value)
	{
		if (value >= 2147483647.0) return 0x7FFFFFFF;
		if (value <= -2147483647.0) return -0x7FFFFFFF;
		return static_cast<int32_t>(std::lround(value));
	}

	void RecordMonth()
	{
		cISC4AppPtr app;
		cISC4City* const city = app ? app->GetCity() : nullptr;
		cISC4HistoryWarehouse* const history = city ? city->GetHistoryWarehouse() : nullptr;
		PopulationData data;
		if (!history || !ReadGameTable(city, data)) return;

		history->RecordHistoryData(PopulationGraphHistory::kHistoryWorkforce, Round(data.workforce));
		history->RecordHistoryData(PopulationGraphHistory::kHistoryWorkforceDemand, Round(data.workforceDemand));
		history->RecordHistoryData(PopulationGraphHistory::kHistoryResidentialForecast, Round(data.residentialForecast));
		history->RecordHistoryData(PopulationGraphHistory::kHistoryCommercialForecast, Round(data.commercialForecast));
		history->RecordHistoryData(PopulationGraphHistory::kHistoryIndustrialForecast, Round(data.industrialForecast));
		SimLog::Write(SimLog::Level::Debug,
			"PopulationGraph: workforce %.0f (demand %.0f), forecast R %.0f, C %.0f, I %.0f.",
			data.workforce, data.workforceDemand, data.residentialForecast, data.commercialForecast, data.industrialForecast);
	}
}

void PopulationGraphHistory::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[16]{};
	GetPrivateProfileStringW(L"PopulationGraph", L"Enabled", L"true", value, 16, ini.c_str());
	std::wstring text(value);
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	SimLog::Write(SimLog::Level::Info, "PopulationGraph: workforce and demand forecast %s.", enabled ? "on" : "off");
}

void PopulationGraphHistory::DoMessage(cIGZMessage2* message)
{
	if (!enabled || message->GetType() != kMessageSimNewMonth) return;
	RecordMonth();
}
