// SC4 SIM: monthly water production by water source and the city's wastewater, recorded in the city history so the
// Water graph can plot them. Driven by SimDllDirector. Notes: docs/water-graph.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace WaterGraphHistory
{
	// Reads [WaterGraph] from SC4SIM.ini.
	void OnStart();
	// SimNewMonth (0x66956816): records one point per series in the city's history warehouse.
	void DoMessage(cIGZMessage2* message);

	// History types (data sources of the Water graph exemplar 0x6534284A / 0xCA4AD545 / 0x00000007, plots 5-10).
	inline constexpr uint32_t kHistoryAquifer = 0x5C4DE021;            // water towers, aquifer pumps
	inline constexpr uint32_t kHistoryFreshWater = 0x5C4DE022;         // fresh water pumps
	inline constexpr uint32_t kHistorySaltWater = 0x5C4DE023;          // desalination plants
	inline constexpr uint32_t kHistoryWastewaterProduced = 0x5C4DE024; // water consumed by the city
	inline constexpr uint32_t kHistoryWastewaterTreated = 0x5C4DE025;  // by treatment plants
	inline constexpr uint32_t kHistoryWastewaterRecycled = 0x5C4DE026; // turned back into water by sewage plants
}
