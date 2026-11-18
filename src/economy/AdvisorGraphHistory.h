// SC4 SIM: monthly budget, tax, mass transit and congestion values from the advisors' game data, recorded in the
// city history so the Funds, City Income/Expenses, Tax Rates, Mass Transit and Traffic Congestion graphs can plot
// them. Driven by SimDllDirector. Notes: docs/economy-transport-graphs.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace AdvisorGraphHistory
{
	// Reads [EconomyGraph] and [TransportGraph] from SC4SIM.ini.
	void OnStart();
	// SimNewMonth (0x66956816): records one point per series in the city's history warehouse.
	void DoMessage(cIGZMessage2* message);

	// Economy: Funds (0x02) and City Income/Expenses (0x03) graphs, Tax Rates graph (0x21).
	inline constexpr uint32_t kHistoryLoans = 0x5C4DE101;          // game.g_borrowed / 1000
	inline constexpr uint32_t kHistoryBorrowingLimit = 0x5C4DE102; // game.g_borrowing_limit / 1000
	inline constexpr uint32_t kHistoryTaxIncome = 0x5C4DE103;      // game.g_tax_income
	inline constexpr uint32_t kHistoryDealsIncome = 0x5C4DE104;    // game.g_nd_income
	inline constexpr uint32_t kHistoryDealsExpense = 0x5C4DE105;   // game.g_nd_expense
	inline constexpr uint32_t kHistoryLoanPayments = 0x5C4DE106;   // game.g_bond_payments_monthly
	inline constexpr uint32_t kHistoryTaxRateFirst = 0x5C4DE111;   // 12 tax rates x 10, 0x5C4DE111-0x5C4DE11C

	// Transport: Mass Transit graph (0x22) and Traffic Congestion graph (0x23).
	inline constexpr uint32_t kHistoryStationUseFirst = 0x5C4DE121; // 5 station utilizations %, 0x5C4DE121-0x5C4DE125
	inline constexpr uint32_t kHistoryCongestionFirst = 0x5C4DE131; // 8 congestions %, 0x5C4DE131-0x5C4DE138
	inline constexpr uint32_t kHistoryPotholes = 0x5C4DE139;        // game.g_pothole_count
}
