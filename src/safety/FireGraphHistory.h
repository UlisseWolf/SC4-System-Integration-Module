// SC4 SIM: monthly fire outbreaks and fire stations, recorded in the city history so the Fire Protection graph can
// plot them. Driven by SimDllDirector. Notes: docs/fire-graph.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace FireGraphHistory
{
	// Reads [FireGraph] from SC4SIM.ini.
	void OnStart();
	// SimNewMonth (0x66956816): records one point per series in the city's history warehouse.
	void DoMessage(cIGZMessage2* message);

	// Fire Protection graph (0x6534284A / 0xCA4AD545 / 0x0000001D), next to the game's fire coverage, average fire
	// protection and average flammability.
	inline constexpr uint32_t kHistoryFireOutbreaks = 0x5C4DE0A1; // FIRE_DISASTER samples of the last month
	inline constexpr uint32_t kHistoryFireStations = 0x5C4DE0A2;  // fire stations in the city
}
