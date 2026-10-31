// SC4 SIM: Afterlife - the calculation of AFTERLIFE.lua, with no game or Windows dependency.
// A fictitious, cosmetic mortality / afterlife statistic: it only reads native city values and never changes the
// simulation. See docs/afterlife.md.
#pragma once
#include <functional>

namespace Afterlife
{
	// Inputs read from the game's Lua `game` table (adv_game_data.lua).
	struct GameInputs
	{
		double healthCoverage = 0; // game.g_health_coverage_p (0-100)
		double residentPopulation = 0; // game.g_city_r_population
		double crime = 0; // game.ga_crime
		double education = 0; // game.ga_education
	};

	// Constants of PACK_CONSTANTS.lua, one member per Lua name (table.NAME).
	struct Constants
	{
		// mortality_ratio
		double baseMortalityRatePerCapita = 0, healthCoverageWeight = 0;
		// judgment_ratio
		double educationReference = 1, baseSinRatio = 0, crimeWeight = 0, educationWeight = 0, sinRatioMin = 0, sinRatioMax = 1;
		// imp_ratio
		double impClientRatio = 0, impSuccessChance = 0, impKillRandomVariance = 0, maxImpKillFractionPop = 0;
		// redemption_ratio
		double redemptionRatio = 0;
		// hell_native_ratio
		double impRatio = 0, hellhoundRatio = 0, succubusRatio = 0, infestorRatio = 0, possessorRatio = 0,
			baphometRatio = 0, reaperRatio = 0, goetiaArsGoetiaRatio = 0;
		// hell_native_fixed
		double fallenAngels = 0, sevenDeadlySins = 0;
	};

	// Mirror of afterlife_grid_var. The CUMULATIVE members survive from one cycle to the next.
	struct State
	{
		double healthQuality = 0, sinRatio = 0;
		double naturalDeaths = 0, impClientsThisCycle = 0, impKillsThisCycle = 0, totalDeathsThisCycle = 0;
		double winnerCountThisCycle = 0, sinnerCountThisCycle = 0, redeemedThisCycle = 0;
		double winnerPopulationHeaven = 0, sinnerPopulationHell = 0, redeemedTotal = 0, impKillsTotal = 0; // cumulative
		double impPopulation = 0, hellhoundPopulation = 0, succubusPopulation = 0, infestorPopulation = 0,
			possessorPopulation = 0, baphometPopulation = 0, reaperPopulation = 0, goetiaArsGoetiaPopulation = 0,
			fallenAngelsPopulation = 0, sevenDeadlySinsPopulation = 0, hellNativeTotal = 0;
	};

	// Same as math.random(lo, hi) of Lua: an integer in [lo, hi].
	using RandomInt = std::function<int(int lo, int hi)>;

	// run_afterlife_balance_variables(): one cycle, same order of operations as AFTERLIFE.lua.
	void Step(State& state, const GameInputs& game, const Constants& constants, const RandomInt& random);
}
