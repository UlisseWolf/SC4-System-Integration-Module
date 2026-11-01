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

	// Balance constants. They live in the DLL; SC4SIM.ini ([Afterlife], same names) overrides each one.
	// !! PLACEHOLDERS: only BASE_MORTALITY_RATE_PER_CAPITA (the ~0.06% per month of the original comment) and the two
	// fixed Hell counts (Fallen Angels 2, Seven Deadly Sins 7) come from AFTERLIFE.lua. Every other value is a neutral
	// starting point to be replaced with the pack's real balance.
	struct Constants
	{
		// mortality
		double baseMortalityRatePerCapita = 0.0006, healthCoverageWeight = 1.0;
		// judgment (Winner / Sinner split)
		double educationReference = 100, baseSinRatio = 0.5, crimeWeight = 0.3, educationWeight = 0.2,
			sinRatioMin = 0.05, sinRatioMax = 0.95;
		// I.M.P.
		double impClientRatio = 0.001, impSuccessChance = 0.5, impKillRandomVariance = 2, maxImpKillFractionPop = 0.01;
		// redemption
		double redemptionRatio = 0.001;
		// Hell's native population, share of the Sinner population
		double impRatio = 0.20, hellhoundRatio = 0.10, succubusRatio = 0.10, infestorRatio = 0.05, possessorRatio = 0.05,
			baphometRatio = 0.02, reaperRatio = 0.02, goetiaArsGoetiaRatio = 0.02;
		// Hell's fixed population
		double fallenAngels = 2, sevenDeadlySins = 7;
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
