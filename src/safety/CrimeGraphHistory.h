// SC4 SIM: monthly criminals, inmates and police / jail ratios, recorded in the city history so the Crime and
// Police & Jails graphs can plot them. Driven by SimDllDirector. Notes: docs/crime-graph.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace CrimeGraphHistory
{
	// Reads [CrimeGraph] from SC4SIM.ini.
	void OnStart();
	// SimNewMonth (0x66956816): records one point per series in the city's history warehouse.
	void DoMessage(cIGZMessage2* message);

	// Crime graph (0x6534284A / 0xCA4AD545 / 0x0000000C), next to the game's crimes and arrests.
	inline constexpr uint32_t kHistoryCriminals = 0x5C4DE081;       // criminals in the city
	inline constexpr uint32_t kHistoryInmates = 0x5C4DE082;         // jail population
	inline constexpr uint32_t kHistoryUnsolvedCrimes = 0x5C4DE083;  // crimes - arrests of the month, never below 0

	// Police & Jails graph (0x6534284A / 0xCA4AD545 / 0x0000001C), next to the game's police coverage and average crime.
	inline constexpr uint32_t kHistoryArrestRate = 0x5C4DE084;      // arrests * 100 / crimes of the month
	inline constexpr uint32_t kHistoryJailOccupancy = 0x5C4DE085;   // inmates * 100 / jail capacity
}
