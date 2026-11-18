// SC4 SIM: monthly workforce and R / C / I demand forecast, recorded in the city history so the Population R/C/I and
// Demand Forecast graphs can plot them. Driven by SimDllDirector. Notes: docs/population-graph.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace PopulationGraphHistory
{
	// Reads [PopulationGraph] from SC4SIM.ini.
	void OnStart();
	// SimNewMonth (0x66956816): records one point per series in the city's history warehouse.
	void DoMessage(cIGZMessage2* message);

	// Population R/C/I graph (0x6534284A / 0xCA4AD545 / 0x0000001F), next to the game's population totals.
	inline constexpr uint32_t kHistoryWorkforce = 0x5C4DE0E1;         // game.g_city_workforce_population
	// Demand Forecast graph (0x6534284A / 0xCA4AD545 / 0x00000020).
	inline constexpr uint32_t kHistoryResidentialForecast = 0x5C4DE0E2; // game.g_r_demand_extrap
	inline constexpr uint32_t kHistoryCommercialForecast = 0x5C4DE0E3;  // game.g_c_demand_extrap
	inline constexpr uint32_t kHistoryIndustrialForecast = 0x5C4DE0E4;  // game.g_i_demand_extrap
	inline constexpr uint32_t kHistoryWorkforceDemand = 0x5C4DE0E5;     // game.g_workforce_demand
}
