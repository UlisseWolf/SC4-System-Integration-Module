// SC4 SIM: Afterlife - the calculation of AFTERLIFE.lua, with no game or Windows dependency.
// A fictitious, cosmetic mortality / afterlife statistic: it only reads native city values and never changes the
// simulation. See docs/afterlife.md.
#pragma once
#include <functional>
#include <vector>

namespace Afterlife
{
	// Inputs read from the game's Lua `game` table (adv_game_data.lua).
	struct GameInputs
	{
		double healthCoverage = 0; // game.g_health_coverage_p (0-100)
		double residentPopulation = 0; // game.g_city_r_population
		double crime = 0; // game.ga_crime
		double education = 0; // game.ga_education: city EQ, 0-200
		double lifeExpectancy = 0; // game.ga_life_exp (years); 0 = not available, the old per-capita rate is used
	};

	// One generation reset of the residential simulator (a tract whose average age went past its life expectancy and
	// was set back to the reset age), for one wealth level. SC4SIM.dll finds them in the age and population grids.
	struct DeathWave
	{
		double population = 0; // residents of that wealth in the tract
		double education = 0; // their EQ (0-200)
		bool premature = false; // the tract's life expectancy was below the reset age: illness / pollution, not old age
	};

	// Balance constants. They live in the DLL; SC4SIM.ini ([Afterlife], same names) overrides each one.
	// Defaults = the AFTERLIFE section of PACK_CONSTANTS.lua (package 0BB33100): mortality_ratio, judgment_ratio,
	// imp_ratio, redemption_ratio, hell_native_ratio and hell_native_fixed; since 1.9.2 the hell_native_ratio values are
	// 20 times the original ones, so Hell's natives outnumber the Sinners. tools/afterlife-test checks they match.
	struct Constants
	{
		// mortality_ratio
		double baseMortalityRatePerCapita = 0.0006, healthCoverageWeight = 1.5;
		// mortality_ratio (1.9.3): with game.ga_life_exp, deaths per month = residents * this / (12 * life expectancy)
		double lifeExpectancyDeathScale = 1.0;
		// death_waves (1.9.3): share of a reset tract's residents counted as deaths, cap on all wave deaths of a month
		// (share of the residents), extra Sinner share for premature deaths
		double waveDeathShare = 0.05, maxWaveDeathFractionPop = 0.01, prematureSinBonus = 0.1;
		// judgment_ratio (Winner / Sinner split). EDUCATION_REFERENCE is an EQ (0-200): 100 since 1.9.3 (was 500,
		// above the scale, so education always pushed toward Sinner)
		double educationReference = 100, baseSinRatio = 0.5, crimeWeight = 0.3, educationWeight = 0.3,
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
		// heaven_native_ratio (1.9.2): Heaven's natives (Heavenborn angels) per Winner (sum 1.91, so with the fixed
		// council they always outnumber the Winners)
		double angelRatio = 1.0, cherubRatio = 0.6, exorcistRatio = 0.3, seraphimRatio = 0.01;
		// heaven_native_fixed: the Seraphim council (Sera, Emily, the Speaker of God)
		double seraphimCouncil = 3;
	};

	// Mirror of afterlife_grid_var. The CUMULATIVE members survive from one cycle to the next.
	struct State
	{
		double healthQuality = 0, sinRatio = 0, lifeExpectancy = 0;
		double baseDeaths = 0, waveDeaths = 0, prematureDeaths = 0; // naturalDeaths = baseDeaths + old-age wave deaths
		double naturalDeaths = 0, impClientsThisCycle = 0, impKillsThisCycle = 0, totalDeathsThisCycle = 0;
		double winnerCountThisCycle = 0, sinnerCountThisCycle = 0, redeemedThisCycle = 0;
		double winnerPopulationHeaven = 0, sinnerPopulationHell = 0, redeemedTotal = 0, impKillsTotal = 0; // cumulative
		double impPopulation = 0, hellhoundPopulation = 0, succubusPopulation = 0, infestorPopulation = 0,
			possessorPopulation = 0, baphometPopulation = 0, reaperPopulation = 0, goetiaArsGoetiaPopulation = 0,
			fallenAngelsPopulation = 0, sevenDeadlySinsPopulation = 0, hellNativeTotal = 0;
		double angelPopulation = 0, cherubPopulation = 0, exorcistPopulation = 0, seraphimPopulation = 0,
			seraphimCouncilPopulation = 0, heavenNativeTotal = 0;
	};

	// Same as math.random(lo, hi) of Lua: an integer in [lo, hi].
	using RandomInt = std::function<int(int lo, int hi)>;

	// One monthly cycle, same order of operations as tools/afterlife-test/reference/AFTERLIFE.lua.
	void Step(State& state, const GameInputs& game, const std::vector<DeathWave>& waves, const Constants& constants,
		const RandomInt& random);

	// Hell's and Heaven's native populations from the standing Sinner and Winner populations (the end of Step; also
	// used after the cumulative populations are loaded with a city).
	void UpdateNatives(State& state, const Constants& constants);

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
