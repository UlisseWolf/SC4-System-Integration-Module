// SC4 SIM: Afterlife calculation. Started as a line-by-line port of run_afterlife_balance_variables() of AFTERLIFE.lua;
// tools/afterlife-test/reference/AFTERLIFE.lua is kept in step with it as the test reference.
// Lua numbers are doubles, so every value here is a double and math.floor is std::floor.
#include "AfterlifeModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace
{
	double Clamp(double value, double lo, double hi) { return std::max(lo, std::min(hi, value)); }

	// Winner/Sinner judgment ratio for a given EQ, shifted by crime, clamped away from 0% and 100%.
	double SinRatio(const Afterlife::Constants& k, double crime, double education, double bonus)
	{
		const double educationFactor = (education / k.educationReference) - 1;
		const double ratio = k.baseSinRatio + (k.crimeWeight * (crime / 100)) - (k.educationWeight * educationFactor) + bonus;
		return Clamp(ratio, k.sinRatioMin, k.sinRatioMax);
	}
}

void Afterlife::Step(State& s, const GameInputs& game, std::vector<DeathWave>& waves, const Constants& k,
	const RandomInt& random)
{
	// Base mortality. With the game's life expectancy: each generation is replaced about once per life expectancy,
	// so residents / (12 * life expectancy) die each month. Without it, the old per-capita rate worsened by poor health
	// coverage.
	s.healthQuality = Clamp(game.healthCoverage / 100, 0.0, 1.0);
	s.lifeExpectancy = game.lifeExpectancy;
	if (game.lifeExpectancy > 0)
	{
		s.baseDeaths = std::floor(game.residentPopulation * k.lifeExpectancyDeathScale / (12 * game.lifeExpectancy));
	}
	else
	{
		const double mortalityRate = k.baseMortalityRatePerCapita * (1 + k.healthCoverageWeight * (1 - s.healthQuality));
		s.baseDeaths = std::floor(game.residentPopulation * mortalityRate);
	}

	// City judgment ratio: base deaths and I.M.P. victims.
	s.sinRatio = SinRatio(k, game.crime, game.education, 0);

	// Generation resets of the residential simulator: a share of each reset tract's residents, judged with the EQ of
	// their wealth level (plus a Sinner bonus for premature deaths), up to a monthly cap.
	double waveCap = std::floor(game.residentPopulation * k.maxWaveDeathFractionPop);
	double oldAgeWaveDeaths = 0, waveWinners = 0;
	s.prematureDeaths = 0;
	for (DeathWave& wave : waves)
	{
		const double deaths = std::min(std::floor(wave.population * k.waveDeathShare), waveCap);
		wave.deaths = std::max(deaths, 0.0);
		if (deaths <= 0) continue;
		waveCap -= deaths;
		(wave.premature ? s.prematureDeaths : oldAgeWaveDeaths) += deaths;
		const double ratio = SinRatio(k, game.crime, wave.education, wave.premature ? k.prematureSinBonus : 0);
		waveWinners += std::floor(deaths * (1 - ratio));
	}
	s.waveDeaths = oldAgeWaveDeaths + s.prematureDeaths;
	s.naturalDeaths = s.baseDeaths + oldAgeWaveDeaths;

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

	// Deaths of this cycle and their judgment.
	s.totalDeathsThisCycle = s.naturalDeaths + s.prematureDeaths + s.impKillsThisCycle;
	s.winnerCountThisCycle = std::floor((s.baseDeaths + s.impKillsThisCycle) * (1 - s.sinRatio)) + waveWinners;
	s.sinnerCountThisCycle = s.totalDeathsThisCycle - s.winnerCountThisCycle;

	// Standing populations (cumulative).
	s.winnerPopulationHeaven += s.winnerCountThisCycle;
	s.sinnerPopulationHell += s.sinnerCountThisCycle;
	s.impKillsTotal += s.impKillsThisCycle;

	UpdateNatives(s, k);
}

void Afterlife::UpdateNatives(State& s, const Constants& k)
{
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

	// Hell's natives always outnumber the Sinners. The default ratios already give about 2.3 natives per Sinner plus
	// the fixed 9; if SC4SIM.ini lowers them, the missing natives are Imps, the most common kind.
	if (s.hellNativeTotal <= s.sinnerPopulationHell)
	{
		const double shortfall = s.sinnerPopulationHell + 1 - s.hellNativeTotal;
		s.impPopulation += shortfall;
		s.hellNativeTotal += shortfall;
	}

	// Heaven's native population (Heavenborn angels): a fresh snapshot of the Winner population, as in Hell.
	s.angelPopulation = std::floor(s.winnerPopulationHeaven * k.angelRatio);
	s.cherubPopulation = std::floor(s.winnerPopulationHeaven * k.cherubRatio);
	s.exorcistPopulation = std::floor(s.winnerPopulationHeaven * k.exorcistRatio);
	s.seraphimPopulation = std::floor(s.winnerPopulationHeaven * k.seraphimRatio);
	s.seraphimCouncilPopulation = k.seraphimCouncil;
	s.heavenNativeTotal = s.angelPopulation + s.cherubPopulation + s.exorcistPopulation + s.seraphimPopulation
		+ s.seraphimCouncilPopulation;

	// Heaven's natives always outnumber the Winners; if SC4SIM.ini lowers the ratios, the missing natives are Angels.
	if (s.heavenNativeTotal <= s.winnerPopulationHeaven)
	{
		const double shortfall = s.winnerPopulationHeaven + 1 - s.heavenNativeTotal;
		s.angelPopulation += shortfall;
		s.heavenNativeTotal += shortfall;
	}
}

// ------------------------------------------------------------------ news ticker roster (PACK_CONSTANTS.lua)

namespace
{
	using Afterlife::NewsCharacter;
	using Afterlife::NewsEvent;

	template <int N>
	constexpr NewsEvent Event(const char* type, const char* const (&characters)[N])
	{
		return NewsEvent{ type, characters, N };
	}

	// BEGIN GENERATED ROSTER (tools/afterlife-news/build_news.py, from roster.py: do not edit by hand)
	constexpr NewsCharacter kCharacters[] = {
		{ "KATIE_KILLJOY", "Katie Killjoy", "Anchor, 670 News", "Hazbin Hotel", "Sinner Demon", "text@BB3C1000" },
		{ "TOM_TRENCH", "Tom Trench", "Co-Anchor, 670 News", "Hazbin Hotel", "Sinner Demon", "text@BB3C1001" },
		{ "CHARLIE_MORNINGSTAR", "Charlie Morningstar", "Hotel Manager, Princess of Hell", "Hazbin Hotel", "Royal Goetia (Hellborn)", "text@BB3C1010" },
		{ "VAGGIE", "Vaggie", "Hotel Co-Manager, former Exorcist", "Hazbin Hotel", "Fallen Angel (former Exorcist)", "text@BB3C1011" },
		{ "ALASTOR", "Alastor", "Overlord, Radio Demon", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1012" },
		{ "VOX", "Vox", "Overlord, VoxTek CEO", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1013" },
		{ "VALENTINO", "Valentino", "Overlord, Studio Owner", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1014" },
		{ "BLITZO", "Blitzo", "C.E.O., I.M.P.", "Helluva Boss", "Imp (Hellborn)", "text@BB3C1020" },
		{ "MOXXIE", "Moxxie", "Field Agent, I.M.P.", "Helluva Boss", "Imp (Hellborn)", "text@BB3C1021" },
		{ "MILLIE", "Millie", "Field Agent, I.M.P.", "Helluva Boss", "Imp (Hellborn)", "text@BB3C1022" },
		{ "LOONA", "Loona", "Receptionist, I.M.P.", "Helluva Boss", "Hellhound (Hellborn)", "text@BB3C1023" },
		{ "STOLAS", "Stolas", "Ars Goetia Royalty", "Helluva Boss", "Royal Goetia (Hellborn)", "text@BB3C1024" },
		{ "VEROSIKA_MAYDAY", "Verosika Mayday", "Pop Star", "Helluva Boss", "Succubus (Hellborn)", "text@BB3C1025" },
		{ "LUCIFER_MORNINGSTAR", "Lucifer Morningstar", "King of Hell, Sin of Pride", "Hazbin Hotel", "Fallen Angel", "text@BB3C1030" },
		{ "LILITH", "Lilith", "Queen of Hell, the first woman", "Hazbin Hotel", "First Woman (Queen of Hell)", "text@BB3C1031" },
		{ "ANGEL_DUST", "Angel Dust", "Hotel Patron, Porn Star", "Hazbin Hotel", "Sinner Demon", "text@BB3C1032" },
		{ "HUSK", "Husk", "Hotel Bartender, former Overlord", "Hazbin Hotel", "Sinner Demon", "text@BB3C1033" },
		{ "NIFFTY", "Niffty", "Hotel Housekeeper", "Hazbin Hotel", "Sinner Demon", "text@BB3C1034" },
		{ "SIR_PENTIOUS", "Sir Pentious", "Hotel Patron, redeemed into Heaven", "Hazbin Hotel", "Sinner Demon (Redeemed)", "text@BB3C1035" },
		{ "EGG_BOIS", "Egg Bois", "Sir Pentious' Henchmen", "Hazbin Hotel", "Egg Minions", "text@BB3C1036" },
		{ "FAT_NUGGETS", "Fat Nuggets", "Angel Dust's Pet Pig", "Hazbin Hotel", "Pig (Hell)", "text@BB3C1037" },
		{ "CHERRI_BOMB", "Cherri Bomb", "Angel Dust's Friend, Arsonist", "Hazbin Hotel", "Sinner Demon", "text@BB3C1038" },
		{ "VELVETTE", "Velvette", "Overlord, Fashion Influencer", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1039" },
		{ "CARMILLA_CARMINE", "Carmilla Carmine", "Overlord, Arms Dealer", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C103A" },
		{ "ODETTE_CARMINE", "Odette Carmine", "Carmilla's Daughter", "Hazbin Hotel", "Sinner Demon", "text@BB3C103B" },
		{ "CLARA_CARMINE", "Clara Carmine", "Carmilla's Daughter", "Hazbin Hotel", "Sinner Demon", "text@BB3C103C" },
		{ "ZESTIAL", "Zestial", "Overlord, Eldest Overlord", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C103D" },
		{ "ROSIE", "Rosie", "Overlord, Cannibal Town", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C103E" },
		{ "MIMZY", "Mimzy", "Singer, Alastor's Acquaintance", "Hazbin Hotel", "Sinner Demon", "text@BB3C103F" },
		{ "SUSAN", "Susan", "Hell Resident", "Hazbin Hotel", "Sinner Demon", "text@BB3C1040" },
		{ "BAXTER", "Baxter", "Hotel Guest, Scientist", "Hazbin Hotel", "Sinner Demon", "text@BB3C1041" },
		{ "ZEEZI", "Zeezi", "Overlord", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1042" },
		{ "PRICK", "Prick", "Overlord", "Hazbin Hotel", "Overlord (Sinner-origin)", "text@BB3C1043" },
		{ "ADAM", "Adam", "First Man, Exorcist Leader", "Hazbin Hotel", "Angel (Winner)", "text@BB3C1044" },
		{ "LUTE", "Lute", "Exorcist Lieutenant", "Hazbin Hotel", "Exorcist (Angel)", "text@BB3C1045" },
		{ "ABEL", "Abel", "Exorcist, Son of Adam", "Hazbin Hotel", "Angel (Winner)", "text@BB3C1046" },
		{ "SERA", "Sera", "High Seraphim of Heaven", "Hazbin Hotel", "Seraphim (Angel)", "text@BB3C1047" },
		{ "EMILY", "Emily", "Seraphim, Heaven", "Hazbin Hotel", "Seraphim (Angel)", "text@BB3C1048" },
		{ "ST_PETER", "St. Peter", "Gatekeeper of Heaven", "Hazbin Hotel", "Angel (Winner)", "text@BB3C1049" },
		{ "SPEAKER_OF_GOD", "The Speaker of God", "Voice of Heaven", "Hazbin Hotel", "Angel", "text@BB3C104A" },
		{ "OCTAVIA", "Octavia", "Stolas' Daughter", "Helluva Boss", "Royal Goetia (Hellborn)", "text@BB3C1050" },
		{ "STELLA", "Stella", "Stolas' Ex-Wife", "Helluva Boss", "Royal Goetia (Hellborn)", "text@BB3C1051" },
		{ "ANDREALPHUS", "Andrealphus", "Stella's Brother", "Helluva Boss", "Royal Goetia (Hellborn)", "text@BB3C1052" },
		{ "PAIMON", "Paimon", "Stolas' Father", "Helluva Boss", "Royal Goetia (Hellborn)", "text@BB3C1053" },
		{ "VASSAGO", "Vassago", "Stolas' Friend", "Helluva Boss", "Ars Goetia (Hellborn)", "text@BB3C1054" },
		{ "ASMODEUS", "Asmodeus", "Sin of Lust, Ozzie's Owner", "Helluva Boss", "Sin (Hellborn)", "text@BB3C1055" },
		{ "BEELZEBUB", "Beelzebub", "Sin of Gluttony", "Helluva Boss", "Sin (Hellborn)", "text@BB3C1056" },
		{ "SATAN", "Satan", "Sin of Wrath", "Helluva Boss", "Sin (Hellborn)", "text@BB3C1057" },
		{ "MAMMON", "Mammon", "Sin of Greed", "Helluva Boss", "Sin (Hellborn)", "text@BB3C1058" },
		{ "FIZZAROLLI", "Fizzarolli", "Performer, Ozzie's Partner", "Helluva Boss", "Imp (Hellborn)", "text@BB3C1059" },
		{ "ROBO_FIZZ", "Robo Fizz", "Mammon's Robot Merchandise", "Helluva Boss", "Robot", "text@BB3C105A" },
		{ "STRIKER", "Striker", "Imp Assassin", "Helluva Boss", "Imp (Hellborn)", "text@BB3C105B" },
		{ "CRIMSON", "Crimson", "Imp Mob Boss, Moxxie's Father", "Helluva Boss", "Imp (Hellborn)", "text@BB3C105C" },
		{ "CASH_BUCKZO", "Cash Buckzo", "Blitzo's Father", "Helluva Boss", "Imp (Hellborn)", "text@BB3C105D" },
		{ "BARBIE_WIRE", "Barbie Wire", "Blitzo's Twin Sister", "Helluva Boss", "Imp (Hellborn)", "text@BB3C105E" },
		{ "SALLIE_MAY", "Sallie May", "Millie's Sister", "Helluva Boss", "Imp (Hellborn)", "text@BB3C105F" },
		{ "VORTEX", "Vortex", "Loona's Friend", "Helluva Boss", "Hellhound (Hellborn)", "text@BB3C1060" },
		{ "GLITZ_AND_GLAM", "Glitz & Glam", "Imp Twin Performers", "Helluva Boss", "Imp (Hellborn)", "text@BB3C1061" },
		{ "LEVIATHAN", "Leviathan", "Sin of Envy", "Helluva Boss", "Sin (Hellborn)", "text@BB3C1062" },
		{ "BELPHEGOR", "Belphegor", "Sin of Sloth", "Helluva Boss", "Sin (Hellborn)", "text@BB3C1063" },
		{ "NEWS_670_HEALTH_DESK", "670 News Health Desk", "Health Desk, 670 News", "SC4SIM", "Sinner Demon", "text@BB3C1064" },
		{ "GATE_INTAKE_CLERK", "Hell's Intake Clerk", "Intake Clerk, Gates of Hell", "SC4SIM", "Sinner Demon", "text@BB3C1065" },
		{ "NEWS_670_CITY_DESK", "670 News City Desk", "City Desk, 670 News", "SC4SIM", "Sinner Demon", "text@BB3C1066" },
		{ "GRAVEDIGGERS_UNION", "Gravediggers' Union", "Union of Gravediggers", "SC4SIM", "Human (Earth)", "text@BB3C1067" },
		{ "CEMETERY_CARETAKER", "Cemetery Caretaker", "Caretaker, City Cemetery", "SC4SIM", "Human (Earth)", "text@BB3C1068" },
		{ "CREMATORIUM_MANAGER", "Crematorium Manager", "Manager, City Crematorium", "SC4SIM", "Human (Earth)", "text@BB3C1069" },
	};

	constexpr const char* kEvent_NATURAL_DEATH_REPORT[] = { "KATIE_KILLJOY", "TOM_TRENCH" };
	constexpr const char* kEvent_IMP_KILL_REPORT[] = { "BLITZO", "MOXXIE", "MILLIE", "STRIKER" };
	constexpr const char* kEvent_REDEMPTION_REPORT[] = { "CHARLIE_MORNINGSTAR", "VAGGIE", "ANGEL_DUST", "HUSK", "NIFFTY", "SIR_PENTIOUS", "EGG_BOIS", "FAT_NUGGETS", "CHERRI_BOMB", "BAXTER" };
	constexpr const char* kEvent_OVERLORD_REPORT[] = { "ALASTOR", "VOX", "VALENTINO", "VELVETTE", "CARMILLA_CARMINE", "ODETTE_CARMINE", "CLARA_CARMINE", "ZESTIAL", "ROSIE", "ZEEZI", "PRICK", "MIMZY", "SUSAN" };
	constexpr const char* kEvent_HELL_NATIVE_REPORT[] = { "STOLAS", "LOONA", "VEROSIKA_MAYDAY", "OCTAVIA", "STELLA", "ANDREALPHUS", "PAIMON", "VASSAGO", "FIZZAROLLI", "ROBO_FIZZ", "CRIMSON", "CASH_BUCKZO", "BARBIE_WIRE", "SALLIE_MAY", "VORTEX", "GLITZ_AND_GLAM" };
	constexpr const char* kEvent_SEVEN_DEADLY_SINS_REPORT[] = { "LUCIFER_MORNINGSTAR", "LILITH", "ASMODEUS", "BEELZEBUB", "SATAN", "MAMMON", "LEVIATHAN", "BELPHEGOR" };
	constexpr const char* kEvent_HEAVEN_REPORT[] = { "ADAM", "LUTE", "ABEL", "SERA", "EMILY", "ST_PETER", "SPEAKER_OF_GOD" };
	constexpr const char* kEvent_PREMATURE_DEATH_REPORT[] = { "NEWS_670_HEALTH_DESK", "GATE_INTAKE_CLERK" };
	constexpr const char* kEvent_UNCOLLECTED_DEAD_REPORT[] = { "NEWS_670_CITY_DESK", "GRAVEDIGGERS_UNION" };
	constexpr const char* kEvent_CEMETERY_FULL_REPORT[] = { "CEMETERY_CARETAKER", "CREMATORIUM_MANAGER" };

	constexpr NewsEvent kEvents[] = {
		Event("NATURAL_DEATH_REPORT", kEvent_NATURAL_DEATH_REPORT),
		Event("IMP_KILL_REPORT", kEvent_IMP_KILL_REPORT),
		Event("REDEMPTION_REPORT", kEvent_REDEMPTION_REPORT),
		Event("OVERLORD_REPORT", kEvent_OVERLORD_REPORT),
		Event("HELL_NATIVE_REPORT", kEvent_HELL_NATIVE_REPORT),
		Event("SEVEN_DEADLY_SINS_REPORT", kEvent_SEVEN_DEADLY_SINS_REPORT),
		Event("HEAVEN_REPORT", kEvent_HEAVEN_REPORT),
		Event("PREMATURE_DEATH_REPORT", kEvent_PREMATURE_DEATH_REPORT),
		Event("UNCOLLECTED_DEAD_REPORT", kEvent_UNCOLLECTED_DEAD_REPORT),
		Event("CEMETERY_FULL_REPORT", kEvent_CEMETERY_FULL_REPORT),
	};
	// END GENERATED ROSTER

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
