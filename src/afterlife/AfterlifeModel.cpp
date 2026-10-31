// SC4 SIM: Afterlife calculation. A line-by-line port of run_afterlife_balance_variables() of AFTERLIFE.lua.
// Lua numbers are doubles, so every value here is a double and math.floor is std::floor.
#include "AfterlifeModel.h"

#include <algorithm>
#include <cmath>

void Afterlife::Step(State& s, const GameInputs& game, const Constants& k, const RandomInt& random)
{
	// Natural mortality, worsened by poor health coverage.
	s.healthQuality = std::max(std::min(game.healthCoverage / 100, 1.0), 0.0);
	const double mortalityRate = k.baseMortalityRatePerCapita * (1 + k.healthCoverageWeight * (1 - s.healthQuality));
	s.naturalDeaths = std::floor(game.residentPopulation * mortalityRate);

	// Winner/Sinner judgment ratio, shifted by crime and education, clamped away from 0% and 100%.
	const double educationFactor = (game.education / k.educationReference) - 1;
	s.sinRatio = k.baseSinRatio + (k.crimeWeight * (game.crime / 100)) - (k.educationWeight * educationFactor);
	s.sinRatio = std::max(k.sinRatioMin, std::min(k.sinRatioMax, s.sinRatio));

	// I.M.P.: a share of the STANDING Hell population (as it was at the start of the cycle) hires a hit.
	s.impClientsThisCycle = std::floor(s.sinnerPopulationHell * k.impClientRatio);
	const double expectedKills = s.impClientsThisCycle * k.impSuccessChance;
	// math.random(-v, v) of Lua needs an integer v >= 0 (an empty interval is a Lua error).
	const int variance = static_cast<int>(std::max(std::floor(k.impKillRandomVariance), 0.0));
	const double jitter = random(-variance, variance);
	const double killCap = std::floor(game.residentPopulation * k.maxImpKillFractionPop);
	s.impKillsThisCycle = std::max(std::floor(expectedKills + jitter), 0.0);
	s.impKillsThisCycle = std::min(s.impKillsThisCycle, killCap);

	// Redemption: drawn from the Sinners already in Hell at the start of the cycle.
	s.redeemedThisCycle = std::min(std::floor(s.sinnerPopulationHell * k.redemptionRatio), s.sinnerPopulationHell);
	s.sinnerPopulationHell -= s.redeemedThisCycle;
	s.winnerPopulationHeaven += s.redeemedThisCycle;
	s.redeemedTotal += s.redeemedThisCycle;

	// Deaths of this cycle, judged with the same ratio.
	s.totalDeathsThisCycle = s.naturalDeaths + s.impKillsThisCycle;
	s.winnerCountThisCycle = std::floor(s.totalDeathsThisCycle * (1 - s.sinRatio));
	s.sinnerCountThisCycle = s.totalDeathsThisCycle - s.winnerCountThisCycle;

	// Standing populations (cumulative).
	s.winnerPopulationHeaven += s.winnerCountThisCycle;
	s.sinnerPopulationHell += s.sinnerCountThisCycle;
	s.impKillsTotal += s.impKillsThisCycle;

	// Hell's native population: a fresh snapshot of the Sinner population, not accumulated.
	s.impPopulation = std::floor(s.sinnerPopulationHell * k.impRatio);
	s.hellhoundPopulation = std::floor(s.sinnerPopulationHell * k.hellhoundRatio);
	s.succubusPopulation = std::floor(s.sinnerPopulationHell * k.succubusRatio);
	s.infestorPopulation = std::floor(s.sinnerPopulationHell * k.infestorRatio);
	s.possessorPopulation = std::floor(s.sinnerPopulationHell * k.possessorRatio);
	s.baphometPopulation = std::floor(s.sinnerPopulationHell * k.baphometRatio);
	s.reaperPopulation = std::floor(s.sinnerPopulationHell * k.reaperRatio);
	s.goetiaArsGoetiaPopulation = std::floor(s.sinnerPopulationHell * k.goetiaArsGoetiaRatio);
	s.fallenAngelsPopulation = k.fallenAngels;
	s.sevenDeadlySinsPopulation = k.sevenDeadlySins;

	s.hellNativeTotal = s.impPopulation + s.hellhoundPopulation + s.succubusPopulation + s.infestorPopulation
		+ s.possessorPopulation + s.baphometPopulation + s.reaperPopulation + s.goetiaArsGoetiaPopulation
		+ s.fallenAngelsPopulation + s.sevenDeadlySinsPopulation;
}
