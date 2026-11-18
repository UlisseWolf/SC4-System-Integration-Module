// SC4 SIM: monthly education coverage, school / library / museum grades and the highest / lowest neighbourhood EQ,
// recorded in the city history so the Education graph can plot them. Driven by SimDllDirector. Notes:
// docs/population-graph.md.
#pragma once
#include <cstdint>

class cIGZMessage2;

namespace EducationGraphHistory
{
	// Reads [EducationGraph] from SC4SIM.ini.
	void OnStart();
	// SimNewMonth (0x66956816): records one point per series in the city's history warehouse.
	void DoMessage(cIGZMessage2* message);

	// Education graph (0x6534284A / 0xCA4AD545 / 0x0000000D), next to the game's average EQ (GA_EQ 0xAA1A2864).
	inline constexpr uint32_t kHistoryEducationCoverage = 0x5C4DE0F1; // game.g_education_coverage_p (percent)
	inline constexpr uint32_t kHistorySchoolGrade = 0x5C4DE0F2;       // game.ga_school_grade (percent)
	inline constexpr uint32_t kHistoryLibraryGrade = 0x5C4DE0F3;      // game.ga_library_grade (percent)
	inline constexpr uint32_t kHistoryMuseumGrade = 0x5C4DE0F4;       // game.ga_museum_grade (percent)
	inline constexpr uint32_t kHistoryEqHigh = 0x5C4DE0F5;            // game.l_tract_eq_h
	inline constexpr uint32_t kHistoryEqLow = 0x5C4DE0F6;             // game.l_tract_eq_l
}
