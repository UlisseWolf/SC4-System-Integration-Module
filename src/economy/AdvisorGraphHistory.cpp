////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: budget, tax, mass transit and congestion values for the column-1 graphs of the Graphs window.
//
// Same mechanism as the power graph (src/power/PowerGraphHistory.cpp): one point per series is recorded in the
// city's history warehouse on every SimNewMonth, and CAM's graph exemplars plot them. The values exist only in the
// advisor Lua table `game`, which the game's advisor data pump fills:
//  - budget (0x00413A46-0x00413CEB): g_borrowed, g_borrowing_limit, g_bond_payments_monthly, g_tax_income,
//    g_nd_income, g_nd_expense and the 12 tax rates g_tax_rate_*;
//  - transport (0x0041BA1B-0x0041C89E): ga_*_station_utilization_p (ratio x 100), ga_*_congestion (ratio x 100,
//    below 100 = no congestion) and g_pothole_count. The pump writes the monorail and elevated station utilizations
//    and the rail congestion twice (0x0041C281, 0x0041C29F, 0x0041C263); the last write, read here, is the right one.
// The table below says what is read, where it goes and how it is scaled; the graphs are in
// tools/power-graph/make_utility_graphs.py.
//
////////////////////////////////////////////////////////////////////////////

#include "AdvisorGraphHistory.h"
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

	enum class Group { Economy, Transport };

	struct Series
	{
		Group group;
		const char* field;   // in the Lua table `game`
		uint32_t history;    // history type
		double factor;       // value recorded = field * factor, rounded
	};

	constexpr Series kSeries[] = {
		{ Group::Economy, "g_borrowed", AdvisorGraphHistory::kHistoryLoans, 0.001 },
		{ Group::Economy, "g_borrowing_limit", AdvisorGraphHistory::kHistoryBorrowingLimit, 0.001 },
		{ Group::Economy, "g_tax_income", AdvisorGraphHistory::kHistoryTaxIncome, 1.0 },
		{ Group::Economy, "g_nd_income", AdvisorGraphHistory::kHistoryDealsIncome, 1.0 },
		{ Group::Economy, "g_nd_expense", AdvisorGraphHistory::kHistoryDealsExpense, 1.0 },
		{ Group::Economy, "g_bond_payments_monthly", AdvisorGraphHistory::kHistoryLoanPayments, 1.0 },
		// Tax rates x 10 (the game uses 0.1 steps; the graph scales them back by 0.1), in the order of the Maxis
		// Jobs & Pop. groups: R$, R$$, R$$$, CS$, CS$$, CS$$$, CO$$, CO$$$, I-Ag, I-D, I-M, I-HT.
		{ Group::Economy, "g_tax_rate_r_low", AdvisorGraphHistory::kHistoryTaxRateFirst + 0, 10.0 },
		{ Group::Economy, "g_tax_rate_r_med", AdvisorGraphHistory::kHistoryTaxRateFirst + 1, 10.0 },
		{ Group::Economy, "g_tax_rate_r_high", AdvisorGraphHistory::kHistoryTaxRateFirst + 2, 10.0 },
		{ Group::Economy, "g_tax_rate_cs_low", AdvisorGraphHistory::kHistoryTaxRateFirst + 3, 10.0 },
		{ Group::Economy, "g_tax_rate_cs_med", AdvisorGraphHistory::kHistoryTaxRateFirst + 4, 10.0 },
		{ Group::Economy, "g_tax_rate_cs_high", AdvisorGraphHistory::kHistoryTaxRateFirst + 5, 10.0 },
		{ Group::Economy, "g_tax_rate_co_med", AdvisorGraphHistory::kHistoryTaxRateFirst + 6, 10.0 },
		{ Group::Economy, "g_tax_rate_co_high", AdvisorGraphHistory::kHistoryTaxRateFirst + 7, 10.0 },
		{ Group::Economy, "g_tax_rate_i_resource", AdvisorGraphHistory::kHistoryTaxRateFirst + 8, 10.0 },
		{ Group::Economy, "g_tax_rate_i_dirty", AdvisorGraphHistory::kHistoryTaxRateFirst + 9, 10.0 },
		{ Group::Economy, "g_tax_rate_i_manufacturing", AdvisorGraphHistory::kHistoryTaxRateFirst + 10, 10.0 },
		{ Group::Economy, "g_tax_rate_i_hightech", AdvisorGraphHistory::kHistoryTaxRateFirst + 11, 10.0 },
		{ Group::Transport, "ga_bus_station_utilization_p", AdvisorGraphHistory::kHistoryStationUseFirst + 0, 1.0 },
		{ Group::Transport, "ga_train_station_utilization_p", AdvisorGraphHistory::kHistoryStationUseFirst + 1, 1.0 },
		{ Group::Transport, "ga_subway_station_utilization_p", AdvisorGraphHistory::kHistoryStationUseFirst + 2, 1.0 },
		{ Group::Transport, "ga_elevated_station_utilization_p", AdvisorGraphHistory::kHistoryStationUseFirst + 3, 1.0 },
		{ Group::Transport, "ga_monorail_station_utilization_p", AdvisorGraphHistory::kHistoryStationUseFirst + 4, 1.0 },
		{ Group::Transport, "ga_road_congestion", AdvisorGraphHistory::kHistoryCongestionFirst + 0, 1.0 },
		{ Group::Transport, "ga_street_congestion", AdvisorGraphHistory::kHistoryCongestionFirst + 1, 1.0 },
		{ Group::Transport, "ga_avenue_congestion", AdvisorGraphHistory::kHistoryCongestionFirst + 2, 1.0 },
		{ Group::Transport, "ga_bridge_congestion", AdvisorGraphHistory::kHistoryCongestionFirst + 3, 1.0 },
		{ Group::Transport, "ga_rail_congestion", AdvisorGraphHistory::kHistoryCongestionFirst + 4, 1.0 },
		{ Group::Transport, "ga_subway_congestion", AdvisorGraphHistory::kHistoryCongestionFirst + 5, 1.0 },
		{ Group::Transport, "ga_elevated_congestion", AdvisorGraphHistory::kHistoryCongestionFirst + 6, 1.0 },
		{ Group::Transport, "ga_monorail_congestion", AdvisorGraphHistory::kHistoryCongestionFirst + 7, 1.0 },
		{ Group::Transport, "g_pothole_count", AdvisorGraphHistory::kHistoryPotholes, 1.0 },
	};

	bool economyEnabled = true;
	bool transportEnabled = true;

	int32_t Round(double value)
	{
		if (!std::isfinite(value)) return 0;
		if (value >= 2147483647.0) return 0x7FFFFFFF;
		if (value <= -2147483647.0) return -0x7FFFFFFF;
		return static_cast<int32_t>(std::lround(value));
	}

	void RecordMonth()
	{
		cISC4AppPtr app;
		cISC4City* const city = app ? app->GetCity() : nullptr;
		cISC4HistoryWarehouse* const history = city ? city->GetHistoryWarehouse() : nullptr;
		cISC4AdvisorSystem* const advisors = city ? city->GetAdvisorSystem() : nullptr;
		cISCLua* const lua = advisors ? advisors->GetScriptingContext() : nullptr;
		if (!history || !lua) return;

		const int32_t top = lua->GetTop();
		lua->GetGlobal("game");
		if (!lua->IsTable(-1))
		{
			lua->SetTop(top);
			return;
		}
		uint32_t recorded = 0;
		for (const Series& series : kSeries)
		{
			if (!(series.group == Group::Economy ? economyEnabled : transportEnabled)) continue;
			lua->PushString(series.field);
			lua->GetTable(-2);
			const double value = lua->IsNumber(-1) ? lua->ToNumber(-1) : 0.0;
			lua->Pop(1);
			history->RecordHistoryData(series.history, Round(value * series.factor));
			++recorded;
		}
		lua->SetTop(top);
		SimLog::Write(SimLog::Level::Debug, "AdvisorGraph: %u series recorded.", recorded);
	}

	bool ReadEnabled(const std::filesystem::path& ini, const wchar_t* section)
	{
		wchar_t value[16]{};
		GetPrivateProfileStringW(section, L"Enabled", L"true", value, 16, ini.c_str());
		std::wstring text(value);
		for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
		return !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	}
}

void AdvisorGraphHistory::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");
	economyEnabled = ReadEnabled(ini, L"EconomyGraph");
	transportEnabled = ReadEnabled(ini, L"TransportGraph");
	SimLog::Write(SimLog::Level::Info, "AdvisorGraph: economy series %s, transport series %s.",
		economyEnabled ? "on" : "off", transportEnabled ? "on" : "off");
}

void AdvisorGraphHistory::DoMessage(cIGZMessage2* message)
{
	if (!(economyEnabled || transportEnabled) || message->GetType() != kMessageSimNewMonth) return;
	RecordMonth();
}
