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
	// Defaults = the AFTERLIFE section of PACK_CONSTANTS.lua (package 0BB33100): mortality_ratio, judgment_ratio,
	// imp_ratio, redemption_ratio, hell_native_ratio and hell_native_fixed; since 1.9.2 the hell_native_ratio values are
	// 20 times the original ones, so Hell's natives outnumber the Sinners. tools/afterlife-test checks they match.
	struct Constants
	{
		// mortality_ratio
		double baseMortalityRatePerCapita = 0.0006, healthCoverageWeight = 1.5;
		// judgment_ratio (Winner / Sinner split)
		double educationReference = 500, baseSinRatio = 0.5, crimeWeight = 0.3, educationWeight = 0.3,
			sinRatioMin = 0.1, sinRatioMax = 0.9;
		// imp_ratio
		double impClientRatio = 0.002, impSuccessChance = 0.15, impKillRandomVariance = 2, maxImpKillFractionPop = 0.01;
		// redemption_ratio
		double redemptionRatio = 0.0015;
		// hell_native_ratio: Hell's natives per Sinner (sum 2.291, so the natives always outnumber the Sinners)
		double impRatio = 1.0, hellhoundRatio = 0.6, succubusRatio = 0.4, infestorRatio = 0.16,
			possessorRatio = 0.1, baphometRatio = 0.02, reaperRatio = 0.01, goetiaArsGoetiaRatio = 0.001;
		// hell_native_fixed: Hell's fixed population
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

	// One entry of hell_news_characters (PACK_CONSTANTS.lua). `key` is the table key, e.g. "BLITZO".
	struct NewsCharacter
	{
		const char* key;
		const char* fullName;
		const char* role;
		const char* source;
		const char* species;
		const char* tickerTextId;
	};

	// One entry of news_ticker_event_characters: an event type and the keys of the characters on topic for it.
	struct NewsEvent
	{
		const char* type;
		const char* const* characters;
		int count;
	};

	// The built-in copies of the two roster tables, used when the pack's Lua does not define them.
	const NewsCharacter* NewsCharacters(int& count);
	const NewsEvent* NewsEvents(int& count);
	const NewsCharacter* FindNewsCharacter(const char* key);

	// get_news_ticker_character(event_type) on the built-in roster: nullptr for an unknown event type.
	const NewsCharacter* PickNewsCharacter(const char* eventType, const RandomInt& random);
}
