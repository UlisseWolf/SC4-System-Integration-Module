// SC4 SIM: monthly power production of each energy class, recorded in the city history so the Power graph can plot
// it. Driven by SimDllDirector. Notes: docs/power-graph.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace PowerGraphHistory
{
	// Reads [PowerGraph] from SC4SIM.ini.
	void OnStart();
	// SimNewMonth (0x66956816): records one point per energy class in the city's history warehouse.
	void DoMessage(cIGZMessage2* message);

	// History types (data sources of the Power graph exemplar 0x6534284A / 0xCA4AD545 / 0x00000006, plots 5-18).
	inline constexpr uint32_t kHistoryCoal = 0x5C4DE001;
	inline constexpr uint32_t kHistoryOil = 0x5C4DE002;
	inline constexpr uint32_t kHistoryNaturalGas = 0x5C4DE003;
	inline constexpr uint32_t kHistoryNuclearFission = 0x5C4DE004;
	inline constexpr uint32_t kHistoryNuclearFusion = 0x5C4DE005;
	inline constexpr uint32_t kHistorySolar = 0x5C4DE006;
	inline constexpr uint32_t kHistoryWind = 0x5C4DE007;
	inline constexpr uint32_t kHistoryGeothermal = 0x5C4DE008;
	inline constexpr uint32_t kHistoryHydroelectric = 0x5C4DE009;
	inline constexpr uint32_t kHistoryTidal = 0x5C4DE00A;
	inline constexpr uint32_t kHistoryMicrowave = 0x5C4DE00B;
	inline constexpr uint32_t kHistoryBiomass = 0x5C4DE00C;
	inline constexpr uint32_t kHistoryElectricGenerator = 0x5C4DE00D;
	inline constexpr uint32_t kHistoryWasteToEnergy = 0x5C4DE00E;
}
