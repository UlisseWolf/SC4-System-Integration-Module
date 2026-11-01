// SC4 SIM: Afterlife calculation. A line-by-line port of run_afterlife_balance_variables() of AFTERLIFE.lua.
// Lua numbers are doubles, so every value here is a double and math.floor is std::floor.
#include "AfterlifeModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

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

// ------------------------------------------------------------------ news ticker roster (PACK_CONSTANTS.lua)

namespace
{
	using Afterlife::NewsCharacter;
	using Afterlife::NewsEvent;

	constexpr NewsCharacter kCharacters[] = {
		{ "KATIE_KILLJOY", "Katie Killjoy", "Anchor, 670 News", "Hazbin Hotel", "Sinner Demon", "text@BB3C1000" },
		{ "TOM_TRENCH", "Tom Trench", "Co-Anchor, 670 News", "Hazbin Hotel", "Sinner Demon", "text@BB3C1001" },
		{ "CHARLIE_MORNINGSTAR", "Charlie Morningstar", "Hotel Manager, Princess of Hell", "Hazbin Hotel", "Royal Goetia (Hellborn)", "text@BB3C1010" },
		{ "VAGGIE", "Vaggie", "Hotel Co-Manager", "Hazbin Hotel", "Sinner Demon", "text@BB3C1011" },
		{ "ALASTOR", "Alastor", "Overlord, Radio Demon", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1012" },
		{ "VOX", "Vox", "Overlord, VoxTek CEO", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1013" },
		{ "VALENTINO", "Valentino", "Overlord, Studio Owner", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1014" },
		{ "BLITZO", "Blitzo", "C.E.O., I.M.P.", "Helluva Boss", "Imp (Hellborn)", "text@BB3C1020" },
		{ "MOXXIE", "Moxxie", "Field Agent, I.M.P.", "Helluva Boss", "Imp (Hellborn)", "text@BB3C1021" },
		{ "MILLIE", "Millie", "Field Agent, I.M.P.", "Helluva Boss", "Imp (Hellborn)", "text@BB3C1022" },
		{ "LOONA", "Loona", "Receptionist, I.M.P.", "Helluva Boss", "Hellhound (Hellborn)", "text@BB3C1023" },
		{ "STOLAS", "Stolas", "Ars Goetia Royalty", "Helluva Boss", "Royal Goetia (Hellborn)", "text@BB3C1024" },
		{ "VEROSIKA_MAYDAY", "Verosika Mayday", "Pop Star", "Helluva Boss", "Succubus (Hellborn)", "text@BB3C1025" },
	};

	constexpr const char* kNaturalDeath[] = { "KATIE_KILLJOY", "TOM_TRENCH" };
	constexpr const char* kImpKill[] = { "BLITZO", "MOXXIE", "MILLIE" };
	constexpr const char* kRedemption[] = { "CHARLIE_MORNINGSTAR", "VAGGIE" };
	constexpr const char* kOverlord[] = { "ALASTOR", "VOX", "VALENTINO" };
	constexpr const char* kHellNative[] = { "STOLAS", "LOONA", "VEROSIKA_MAYDAY" };

	template <int N>
	constexpr NewsEvent Event(const char* type, const char* const (&characters)[N])
	{
		return NewsEvent{ type, characters, N };
	}

	constexpr NewsEvent kEvents[] = {
		Event("NATURAL_DEATH_REPORT", kNaturalDeath),
		Event("IMP_KILL_REPORT", kImpKill),
		Event("REDEMPTION_REPORT", kRedemption),
		Event("OVERLORD_REPORT", kOverlord),
		Event("HELL_NATIVE_REPORT", kHellNative),
	};
}

const Afterlife::NewsCharacter* Afterlife::NewsCharacters(int& count)
{
	count = static_cast<int>(std::size(kCharacters));
	return kCharacters;
}

const Afterlife::NewsEvent* Afterlife::NewsEvents(int& count)
{
	count = static_cast<int>(std::size(kEvents));
	return kEvents;
}

const Afterlife::NewsCharacter* Afterlife::FindNewsCharacter(const char* key)
{
	if (!key) return nullptr;
	for (const NewsCharacter& character : kCharacters)
	{
		if (std::strcmp(character.key, key) == 0) return &character;
	}
	return nullptr;
}

const Afterlife::NewsCharacter* Afterlife::PickNewsCharacter(const char* eventType, const RandomInt& random)
{
	if (!eventType) return nullptr;
	for (const NewsEvent& event : kEvents)
	{
		if (std::strcmp(event.type, eventType) == 0)
		{
			if (event.count <= 0) return nullptr;
			// candidates[math.random(1, #candidates)], 1-based as in the Lua
			const int pick = std::max(1, std::min(event.count, random(1, event.count)));
			return FindNewsCharacter(event.characters[pick - 1]);
		}
	}
	return nullptr;
}
