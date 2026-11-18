// SC4 SIM: monthly health coverage, health, health grade, hospital occupancy and health buildings, recorded in the
// city history so the Health graph can plot them, and the highest / lowest neighbourhood life expectancy for the
// Life Expectancy graph. Driven by SimDllDirector. Notes: docs/health-graph.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace HealthGraphHistory
{
	// Reads [HealthGraph] from SC4SIM.ini.
	void OnStart();
	// SimNewMonth (0x66956816): records one point per series in the city's history warehouse.
	void DoMessage(cIGZMessage2* message);

	// Health graph (0x6534284A / 0xCA4AD545 / 0x0000001E).
	inline constexpr uint32_t kHistoryHealthCoverage = 0x5C4DE0C1;    // game.g_health_coverage_p (percent)
	inline constexpr uint32_t kHistoryAverageHealth = 0x5C4DE0C2;     // game.ga_health / 2 (percent, CAM's Health Rating)
	inline constexpr uint32_t kHistoryHealthGrade = 0x5C4DE0C3;       // game.ga_health_grade (percent)
	inline constexpr uint32_t kHistoryHospitalOccupancy = 0x5C4DE0C4; // treated patients * 100 / health capacity
	inline constexpr uint32_t kHistoryHospitals = 0x5C4DE0C5;         // game.g_num_hospitals
	inline constexpr uint32_t kHistoryClinics = 0x5C4DE0C6;           // game.g_num_clinics

	// Life Expectancy graph (0x6534284A / 0xCA4AD545 / 0x00000005), next to the game's average life expectancy.
	inline constexpr uint32_t kHistoryLifeExpectancyHigh = 0x5C4DE0C7; // game.l_tract_life_exp_h (years)
	inline constexpr uint32_t kHistoryLifeExpectancyLow = 0x5C4DE0C8;  // game.l_tract_life_exp_l (years)
}
