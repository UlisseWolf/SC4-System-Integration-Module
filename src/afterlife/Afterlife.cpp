////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: Afterlife.
//
// DLL version of AFTERLIFE.lua (the Afterlife engine of the NEON-derived pack: Winners in Heaven, Sinners in Hell,
// Hell's native population, I.M.P. clients and kills). It is fictitious and cosmetic, as the Lua was: it only
// READS native values of the Lua `game` table and never changes the simulation.
//
// How it replaces the Lua, with no game code patched (so it works on every SimCity 4 version):
//  * when a city opens, the DLL registers run_afterlife_balance_variables(ret_type) and
//    get_news_ticker_character(event_type) as global Lua functions of the city's advisor scripting context. They
//    used to replace AFTERLIFE.lua, which is no longer part of the pack: the DLL is now the only implementation, and
//    lua/afterlife_news.lua (in CAM_LUA_Controller.dat) calls them to fill the news ticker;
//  * the inputs are read from the `game` table (g_health_coverage_p, g_city_r_population, ga_crime, ga_education);
//  * the constants (mortality, judgment, I.M.P., redemption and Hell's native ratios) live in the DLL, as in the other
//    CAM scripts (CAM_LUA_Controller.dat keeps its constants inside the code): the defaults in AfterlifeModel.h are
//    the AFTERLIFE values of PACK_CONSTANTS.lua, each one overridable in the [Afterlife] section of SC4SIM.ini. The
//    calculation reads no constant from Lua, so it works even if PACK_CONSTANTS.lua is not loaded;
//  * get_news_ticker_character picks from the built-in roster of AfterlifeModel.cpp (60 characters, 7 events), or
//    from the Lua tables news_ticker_event_characters and hell_news_characters if a script defines them;
//  * after every cycle the result is copied into the Lua table afterlife_grid_var, so scripts and UIs that read it
//    directly see the same values as with the Lua version;
//  * the cumulative populations are kept by the DLL and start from zero when a city opens.
// The calculation itself is in AfterlifeModel.cpp, without game or Windows dependencies.
//
////////////////////////////////////////////////////////////////////////////

#include "Afterlife.h"
#include "AfterlifeModel.h"
#include "Deathcare.h"
#include "SimLog.h"

#include "cIGZMessage2Standard.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4City.h"
#include "cISC4ResidentialSimulator.h"
#include "cISC4SimGrid.h"
#include "cISCLua.h"
#include "cRZAutoRefCount.h"
#include "SCLuaUtil.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cwchar>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <vector>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessagePostCityInit = 0x26D31EC1;
	constexpr uint32_t kMessagePreCityShutdown = 0x26D31EC2;
	constexpr uint32_t kMessageSimNewMonth = 0x66956816;
	constexpr uint32_t kMessagePostSave = 0x26C63345;

	bool enabled = true;
	Afterlife::State state;
	Afterlife::Constants constants; // AfterlifeModel.h defaults, overridden by [Afterlife] in SC4SIM.ini
	cISC4City* currentCity = nullptr;

	// DLL-only settings of [Afterlife] (not part of the calculation).
	struct Settings
	{
		bool deathWaves = true; // DEATH_WAVES: read the residential simulator's generation resets
		int resetAge = 40; // TRACT_RESET_AGE: the age a tract is set back to (tuning 0xCA027307)
		int chronicIntervalMonths = 12; // CHRONIC_RESET_INTERVAL_MONTHS: a tract reset every month counts once per this
		bool saveWithCity = true; // SAVE_WITH_CITY: keep the cumulative populations in SC4SIM_Afterlife.ini
	} settings;
	std::filesystem::path stateFile;
	bool warnedMissing = false;
	std::mt19937 generator;
	bool seeded = false;

	// math.randomseed() of the Lua was seeded once with game.g_population; the DLL has real entropy.
	int RandomInt(int lo, int hi)
	{
		if (!seeded)
		{
			std::random_device device;
			generator.seed(device());
			seeded = true;
		}
		if (hi < lo) hi = lo;
		return std::uniform_int_distribution<int>(lo, hi)(generator);
	}

	// ---------------------------------------------------------------- constants (SC4SIM.ini)

	struct ConstantKey
	{
		const wchar_t* key;
		double Afterlife::Constants::* member;
	};

	// One INI key per constant; the key is the constant's name.
	constexpr ConstantKey kConstantKeys[] = {
		{ L"BASE_MORTALITY_RATE_PER_CAPITA", &Afterlife::Constants::baseMortalityRatePerCapita },
		{ L"HEALTH_COVERAGE_WEIGHT", &Afterlife::Constants::healthCoverageWeight },
		{ L"EDUCATION_REFERENCE", &Afterlife::Constants::educationReference },
		{ L"BASE_SIN_RATIO", &Afterlife::Constants::baseSinRatio },
		{ L"CRIME_WEIGHT", &Afterlife::Constants::crimeWeight },
		{ L"EDUCATION_WEIGHT", &Afterlife::Constants::educationWeight },
		{ L"SIN_RATIO_MIN", &Afterlife::Constants::sinRatioMin },
		{ L"SIN_RATIO_MAX", &Afterlife::Constants::sinRatioMax },
		{ L"IMP_CLIENT_RATIO", &Afterlife::Constants::impClientRatio },
		{ L"IMP_SUCCESS_CHANCE", &Afterlife::Constants::impSuccessChance },
		{ L"IMP_KILL_RANDOM_VARIANCE", &Afterlife::Constants::impKillRandomVariance },
		{ L"MAX_IMP_KILL_FRACTION_POP", &Afterlife::Constants::maxImpKillFractionPop },
		{ L"REDEMPTION_RATIO", &Afterlife::Constants::redemptionRatio },
		{ L"HELL_IMP_RATIO", &Afterlife::Constants::impRatio },
		{ L"HELL_HELLHOUND_RATIO", &Afterlife::Constants::hellhoundRatio },
		{ L"HELL_SUCCUBUS_RATIO", &Afterlife::Constants::succubusRatio },
		{ L"HELL_INFESTOR_RATIO", &Afterlife::Constants::infestorRatio },
		{ L"HELL_POSSESSOR_RATIO", &Afterlife::Constants::possessorRatio },
		{ L"HELL_BAPHOMET_RATIO", &Afterlife::Constants::baphometRatio },
		{ L"HELL_REAPER_RATIO", &Afterlife::Constants::reaperRatio },
		{ L"HELL_GOETIA_ARS_GOETIA_RATIO", &Afterlife::Constants::goetiaArsGoetiaRatio },
		{ L"FALLEN_ANGELS", &Afterlife::Constants::fallenAngels },
		{ L"SEVEN_DEADLY_SINS", &Afterlife::Constants::sevenDeadlySins },
		{ L"LIFE_EXPECTANCY_DEATH_SCALE", &Afterlife::Constants::lifeExpectancyDeathScale },
		{ L"WAVE_DEATH_SHARE", &Afterlife::Constants::waveDeathShare },
		{ L"MAX_WAVE_DEATH_FRACTION_POP", &Afterlife::Constants::maxWaveDeathFractionPop },
		{ L"PREMATURE_SIN_BONUS", &Afterlife::Constants::prematureSinBonus },
		{ L"HEAVEN_ANGEL_RATIO", &Afterlife::Constants::angelRatio },
		{ L"HEAVEN_CHERUB_RATIO", &Afterlife::Constants::cherubRatio },
		{ L"HEAVEN_EXORCIST_RATIO", &Afterlife::Constants::exorcistRatio },
		{ L"HEAVEN_SERAPHIM_RATIO", &Afterlife::Constants::seraphimRatio },
		{ L"SERAPHIM_COUNCIL", &Afterlife::Constants::seraphimCouncil },
	};

	void LoadConstants(const wchar_t* ini)
	{
		for (const ConstantKey& entry : kConstantKeys)
		{
			wchar_t text[64]{};
			GetPrivateProfileStringW(L"Afterlife", entry.key, L"", text, 64, ini);
			if (text[0] == L'\0')
			{
				continue; // not set: the default of AfterlifeModel.h
			}
			wchar_t* end = nullptr;
			const double parsed = std::wcstod(text, &end);
			if (end == text || !std::isfinite(parsed))
			{
				SimLog::Write(SimLog::Level::Warning, "Afterlife: %ls=%ls is not a number: the default stays.", entry.key, text);
				continue;
			}
			constants.*entry.member = parsed;
		}
		if (constants.educationReference == 0)
		{
			SimLog::Write(SimLog::Level::Warning, "Afterlife: EDUCATION_REFERENCE=0 would divide by zero: set to 1.");
			constants.educationReference = 1;
		}
	}

	// ---------------------------------------------------------------- Lua helpers

	// table.field as a number; false if the table or the field is missing or not a number.
	bool GetNumber(cISCLua* lua, const char* table, const char* field, double& out)
	{
		bool ok = false;
		lua->GetGlobal(table);
		if (lua->IsTable(-1))
		{
			lua->PushString(field);
			lua->GetTable(-2);
			if (lua->IsNumber(-1))
			{
				out = lua->ToNumber(-1);
				ok = true;
			}
			lua->Pop(1);
		}
		lua->Pop(1);
		return ok;
	}

	void SetNumber(cISCLua* lua, const char* table, const char* field, double value)
	{
		lua->GetGlobal(table);
		if (!lua->IsTable(-1))
		{
			lua->Pop(1);
			lua->NewTable();
			lua->PushValue(-1);
			lua->SetGlobal(table);
		}
		lua->PushString(field);
		lua->PushNumber(value);
		lua->SetTable(-3);
		lua->Pop(1);
	}

	bool IsGlobalFunction(cISCLua* lua, const char* name)
	{
		lua->GetGlobal(name);
		const bool result = lua->IsFunction(-1);
		lua->Pop(1);
		return result;
	}

	// ---------------------------------------------------------------- inputs and constants

	struct NumberBinding
	{
		const char* table;
		const char* field;
		double* target;
	};

	// Reads every binding. A missing value is listed in `missing` and keeps the target's default.
	void ReadBindings(cISCLua* lua, const std::vector<NumberBinding>& bindings, std::vector<std::string>& missing)
	{
		for (const NumberBinding& binding : bindings)
		{
			if (!GetNumber(lua, binding.table, binding.field, *binding.target))
			{
				missing.push_back(std::string(binding.table) + "." + binding.field);
			}
		}
	}

	bool ReadInputs(cISCLua* lua, Afterlife::GameInputs& game)
	{
		std::vector<std::string> missing;
		ReadBindings(lua, {
			{ "game", "g_health_coverage_p", &game.healthCoverage },
			{ "game", "g_city_r_population", &game.residentPopulation },
			{ "game", "ga_crime", &game.crime },
			{ "game", "ga_education", &game.education },
		}, missing);

		// game.ga_life_exp is optional: without it the old per-capita mortality is used.
		if (!GetNumber(lua, "game", "ga_life_exp", game.lifeExpectancy))
		{
			game.lifeExpectancy = 0;
		}

		if (missing.empty())
		{
			return true;
		}
		if (!warnedMissing)
		{
			warnedMissing = true;
			std::string list;
			for (const std::string& name : missing) list += (list.empty() ? "" : ", ") + name;
			SimLog::Write(SimLog::Level::Warning, "Afterlife: %zu game value(s) missing, nothing calculated: %s",
				missing.size(), list.c_str());
		}
		return false;
	}

	// ---------------------------------------------------------------- afterlife_grid_var

	struct StateField
	{
		const char* name;
		double Afterlife::State::* member;
	};

	// Every field of afterlife_grid_var, and the member that holds it.
	constexpr StateField kStateFields[] = {
		{ "HEALTH_QUALITY", &Afterlife::State::healthQuality },
		{ "LIFE_EXPECTANCY", &Afterlife::State::lifeExpectancy },
		{ "BASE_DEATHS", &Afterlife::State::baseDeaths },
		{ "WAVE_DEATHS", &Afterlife::State::waveDeaths },
		{ "PREMATURE_DEATHS", &Afterlife::State::prematureDeaths },
		{ "SIN_RATIO", &Afterlife::State::sinRatio },
		{ "NATURAL_DEATHS", &Afterlife::State::naturalDeaths },
		{ "IMP_CLIENTS_THIS_CYCLE", &Afterlife::State::impClientsThisCycle },
		{ "IMP_KILLS_THIS_CYCLE", &Afterlife::State::impKillsThisCycle },
		{ "TOTAL_DEATHS_THIS_CYCLE", &Afterlife::State::totalDeathsThisCycle },
		{ "WINNER_COUNT_THIS_CYCLE", &Afterlife::State::winnerCountThisCycle },
		{ "SINNER_COUNT_THIS_CYCLE", &Afterlife::State::sinnerCountThisCycle },
		{ "REDEEMED_THIS_CYCLE", &Afterlife::State::redeemedThisCycle },
		{ "WINNER_POPULATION_HEAVEN", &Afterlife::State::winnerPopulationHeaven },
		{ "SINNER_POPULATION_HELL", &Afterlife::State::sinnerPopulationHell },
		{ "REDEEMED_TOTAL", &Afterlife::State::redeemedTotal },
		{ "IMP_KILLS_TOTAL", &Afterlife::State::impKillsTotal },
		{ "IMP_POPULATION", &Afterlife::State::impPopulation },
		{ "HELLHOUND_POPULATION", &Afterlife::State::hellhoundPopulation },
		{ "SUCCUBUS_POPULATION", &Afterlife::State::succubusPopulation },
		{ "INFESTOR_POPULATION", &Afterlife::State::infestorPopulation },
		{ "POSSESSOR_POPULATION", &Afterlife::State::possessorPopulation },
		{ "BAPHOMET_POPULATION", &Afterlife::State::baphometPopulation },
		{ "REAPER_POPULATION", &Afterlife::State::reaperPopulation },
		{ "GOETIA_ARS_GOETIA_POPULATION", &Afterlife::State::goetiaArsGoetiaPopulation },
		{ "FALLEN_ANGELS_POPULATION", &Afterlife::State::fallenAngelsPopulation },
		{ "SEVEN_DEADLY_SINS_POPULATION", &Afterlife::State::sevenDeadlySinsPopulation },
		{ "HELL_NATIVE_TOTAL", &Afterlife::State::hellNativeTotal },
		{ "ANGEL_POPULATION", &Afterlife::State::angelPopulation },
		{ "CHERUB_POPULATION", &Afterlife::State::cherubPopulation },
		{ "EXORCIST_POPULATION", &Afterlife::State::exorcistPopulation },
		{ "SERAPHIM_POPULATION", &Afterlife::State::seraphimPopulation },
		{ "SERAPHIM_COUNCIL_POPULATION", &Afterlife::State::seraphimCouncilPopulation },
		{ "HEAVEN_NATIVE_TOTAL", &Afterlife::State::heavenNativeTotal },
	};

	void MirrorState(cISCLua* lua)
	{
		for (const StateField& field : kStateFields)
		{
			SetNumber(lua, "afterlife_grid_var", field.name, state.*field.member);
		}
		const Deathcare::Totals& care = Deathcare::GetTotals();
		const std::pair<const char*, double> deathcare[] = {
			{ "DEATHCARE_COLLECTED", care.collected },
			{ "DEATHCARE_CREMATED", care.cremated },
			{ "DEATHCARE_BURIED", care.buried },
			{ "DEATHCARE_UNCOLLECTED", care.uncollected },
			{ "GRAVES_USED", care.gravesUsed },
			{ "GRAVES_TOTAL", care.gravesTotal },
			{ "CREMATION_CAPACITY", care.cremationCapacity },
			{ "DEATHCARE_RELOCATION", care.relocation },
			{ "DEATHCARE_EMPTIED", care.emptied },
			{ "CEMETERIES_NEARLY_FULL", care.cemeteriesNearlyFull },
		};
		for (const auto& [name, value] : deathcare)
		{
			SetNumber(lua, "afterlife_grid_var", name, value);
		}
	}

	// ret_type letter -> value, as in the Lua. nullptr for an unknown letter.
	const double Afterlife::State::* ReturnMember(char type)
	{
		switch (type)
		{
		case 'D': return &Afterlife::State::totalDeathsThisCycle;
		case 'W': return &Afterlife::State::winnerCountThisCycle;
		case 'S': return &Afterlife::State::sinnerCountThisCycle;
		case 'C': return &Afterlife::State::impClientsThisCycle;
		case 'K': return &Afterlife::State::impKillsThisCycle;
		case 'R': return &Afterlife::State::redeemedThisCycle;
		case 'H': return &Afterlife::State::sinnerPopulationHell;
		case 'P': return &Afterlife::State::winnerPopulationHeaven;
		case 'T': return &Afterlife::State::impKillsTotal;
		case 'N': return &Afterlife::State::hellNativeTotal;
		case 'A': return &Afterlife::State::heavenNativeTotal;
		default: return nullptr;
		}
	}

	// ---------------------------------------------------------------- Lua functions

	// run_afterlife_balance_variables(ret_type)
	int32_t RunAfterlifeBalanceVariables(lua_State* luaState)
	{
		cRZAutoRefCount<cISCLua> lua = SCLuaUtil::GetISCLuaFromFunctionState(luaState);
		if (!lua)
		{
			return 0;
		}

		// Since 1.9.3 the cycle runs on the game's new-month message (RunMonthlyCycle), with the residential
		// simulator; this function only returns the current values, so calling it more than once a month is harmless.
		MirrorState(lua);

		if (lua->IsString(1))
		{
			const char* type = lua->ToString(1);
			if (type && type[0] && !type[1])
			{
				if (const auto member = ReturnMember(type[0]))
				{
					lua->PushNumber(state.*member);
					return 1;
				}
			}
		}
		return 0; // unknown ret_type: nil, as the Lua
	}

	bool warnedBuiltInRoster = false;

	// Pushes a hell_news_characters-style table for a built-in roster entry.
	void PushNewsCharacter(cISCLua* lua, const Afterlife::NewsCharacter& character)
	{
		const char* const fields[][2] = {
			{ "full_name", character.fullName },
			{ "role", character.role },
			{ "source", character.source },
			{ "species", character.species },
			{ "ticker_text_id", character.tickerTextId },
		};
		lua->NewTable();
		for (const auto& field : fields)
		{
			lua->PushString(field[0]);
			lua->PushString(field[1]);
			lua->SetTable(-3);
		}
	}

	bool IsGlobalTable(cISCLua* lua, const char* name)
	{
		lua->GetGlobal(name);
		const bool result = lua->IsTable(-1);
		lua->Pop(1);
		return result;
	}

	// get_news_ticker_character(event_type): hell_news_characters[random pick of news_ticker_event_characters[event_type]]
	// The roster is built into the DLL (AfterlifeModel.cpp, generated from tools/afterlife-news/roster.py). If a Lua
	// script defines news_ticker_event_characters and hell_news_characters, those tables are used instead.
	int32_t GetNewsTickerCharacter(lua_State* luaState)
	{
		cRZAutoRefCount<cISCLua> lua = SCLuaUtil::GetISCLuaFromFunctionState(luaState);
		if (!lua)
		{
			return 0;
		}
		if (!lua->IsString(1))
		{
			return 0;
		}
		const char* eventType = lua->ToString(1);

		if (!IsGlobalTable(lua, "news_ticker_event_characters") || !IsGlobalTable(lua, "hell_news_characters"))
		{
			if (!warnedBuiltInRoster)
			{
				warnedBuiltInRoster = true;
				SimLog::Write(SimLog::Level::Info,
					"Afterlife: news ticker uses the built-in roster (no news_ticker_event_characters / hell_news_characters in Lua).");
			}
			const Afterlife::NewsCharacter* character = Afterlife::PickNewsCharacter(eventType, RandomInt);
			if (!character)
			{
				return 0; // unknown event type: nil, as the Lua
			}
			PushNewsCharacter(lua, *character);
			return 1;
		}

		lua->GetGlobal("news_ticker_event_characters"); // events
		lua->PushString(eventType);
		lua->GetTable(-2); // events, candidates
		if (!lua->IsTable(-1))
		{
			return 0;
		}
		const int32_t count = lua->GetN(-1);
		if (count <= 0)
		{
			return 0;
		}
		lua->RawGetI(-1, RandomInt(1, count)); // events, candidates, pick
		lua->GetGlobal("hell_news_characters"); // events, candidates, pick, characters
		lua->PushValue(-2);
		lua->GetTable(-2); // events, candidates, pick, characters, entry
		if (lua->IsNil(-1))
		{
			return 0;
		}
		return 1; // the entry is the result, on top of the stack
	}

	// ---------------------------------------------------------------- generation resets (residential simulator)

	// The residential simulator keeps one average age per tract. When it passes the tract's life expectancy the game
	// sets it back to the reset age (default 40) and cuts EQ and HQ: "a generation died" (docs/afterlife.md). Nothing
	// of this is saved, so the DLL keeps last month's ages itself and compares.
	struct TractMemory
	{
		int32_t countX = 0, countZ = 0;
		std::vector<uint8_t> age; // last month's average age
		std::vector<uint16_t> monthsSinceChronic; // months since a chronic (every month) reset was last counted
		bool valid = false;
	} tracts;

	using PopulationGrid = cISC4SimGrid<uint16_t>;
	using GetPopulationGridsFn = bool(__thiscall*)(cISC4ResidentialSimulator*, PopulationGrid**, PopulationGrid**, PopulationGrid**);
	constexpr int kGetPopulationGridsSlot = 25; // checked in 1.1.641: three out pointers, each AddRef'd

	bool loggedGrids = false;

	// Also fills `grid` (residents per tract, for deathcare) and `waveTract` (the tract index of each wave).
	std::vector<Afterlife::DeathWave> FindDeathWaves(cISC4City* city, uint32_t& resetTracts, Deathcare::TractGrid& grid,
		std::vector<size_t>& waveTract)
	{
		std::vector<Afterlife::DeathWave> waves;
		resetTracts = 0;
		grid = Deathcare::TractGrid{};
		waveTract.clear();
		cISC4ResidentialSimulator* const sim = city ? city->GetResidentialSimulator() : nullptr;
		if (!sim || (!settings.deathWaves && !Deathcare::Enabled()))
		{
			return waves;
		}
		// slot 41 returns the age grid itself (no AddRef); gzcom-dll declares it as int32_t.
		auto* const ageGrid = reinterpret_cast<cISC4SimGrid<uint8_t>*>(static_cast<intptr_t>(sim->GetAverageAgeMap()));
		PopulationGrid* population[3]{};
		const auto getGrids = reinterpret_cast<GetPopulationGridsFn>((*reinterpret_cast<void***>(sim))[kGetPopulationGridsSlot]);
		if (!ageGrid || !getGrids(sim, &population[0], &population[1], &population[2]) || !population[0] || !population[1] || !population[2])
		{
			for (PopulationGrid* populationGrid : population) if (populationGrid) populationGrid->Release();
			return waves;
		}

		const int32_t countX = ageGrid->GetTractCountX(), countZ = ageGrid->GetTractCountZ();
		const int32_t shift = ageGrid->GetTractShift();
		const bool sameTracts = population[0]->GetTractShift() == shift && population[0]->GetTractCountX() == countX
			&& population[0]->GetTractCountZ() == countZ;
		if (!loggedGrids)
		{
			loggedGrids = true;
			SimLog::Write(SimLog::Level::Info, "Afterlife: age grid %dx%d tracts of %d cells, population grid tract size %d, reset age %d.",
				countX, countZ, 1 << shift, population[0]->GetTractSize(), settings.resetAge);
		}

		const size_t count = static_cast<size_t>(countX) * static_cast<size_t>(countZ);
		grid.countX = countX;
		grid.countZ = countZ;
		grid.shift = shift;
		grid.population.assign(count, 0.0);
		const bool comparable = settings.deathWaves && tracts.valid && tracts.countX == countX && tracts.countZ == countZ;
		if (!comparable)
		{
			tracts.countX = countX;
			tracts.countZ = countZ;
			tracts.age.assign(count, 0);
			tracts.monthsSinceChronic.assign(count, 0xFFFF);
		}

		for (int32_t z = 0; z < countZ; z++)
		{
			for (int32_t x = 0; x < countX; x++)
			{
				const size_t index = static_cast<size_t>(z) * countX + x;
				const uint8_t age = ageGrid->GetTractValue(x, z);
				const uint8_t previous = tracts.age[index];
				tracts.age[index] = age;
				if (tracts.monthsSinceChronic[index] < 0xFFFF) tracts.monthsSinceChronic[index]++;
				const int32_t cellX = x << shift, cellZ = z << shift;
				double residents[3]{};
				double total = 0;
				for (int w = 0; w < 3; w++)
				{
					residents[w] = sameTracts ? population[w]->GetTractValue(x, z) : population[w]->GetCellValue(cellX, cellZ);
					total += residents[w];
				}
				grid.population[index] = total;
				if (!comparable || age != settings.resetAge || total <= 0)
				{
					continue;
				}
				// GetCellLifeExpectancy returns a byte; gzcom-dll declares an int32_t.
				const int32_t lifeExpectancy = sim->GetCellLifeExpectancy(cellX, cellZ) & 0xFF;
				const bool transition = previous != age;
				const bool chronic = lifeExpectancy < settings.resetAge; // resets on every monthly visit
				bool counted = false, premature = false;
				if (transition && !chronic)
				{
					counted = true; // old age
				}
				else if (chronic && (transition || tracts.monthsSinceChronic[index] >= settings.chronicIntervalMonths))
				{
					counted = true; // life expectancy below the reset age: illness, pollution
					premature = true;
					tracts.monthsSinceChronic[index] = 0;
				}
				if (!counted)
				{
					continue;
				}
				resetTracts++;
				for (int w = 0; w < 3; w++)
				{
					if (residents[w] <= 0) continue;
					const auto wealth = static_cast<cISC4BuildingOccupant::WealthType>(w + 1);
					waves.push_back({ residents[w], sim->GetCellEQByWealth(cellX, cellZ, wealth), premature });
					waveTract.push_back(index);
				}
			}
		}
		tracts.valid = settings.deathWaves;
		for (PopulationGrid* populationGrid : population) populationGrid->Release();
		return waves;
	}

	// ---------------------------------------------------------------- monthly cycle

	cISCLua* CityLua(cISC4City* city)
	{
		cISC4AdvisorSystem* const advisors = city ? city->GetAdvisorSystem() : nullptr;
		return advisors ? advisors->GetScriptingContext() : nullptr;
	}

	// Where this month's dead are: each wave's deaths in its tract, the rest (base deaths, premature deaths beyond the
	// waves never happen, I.M.P. victims) spread over the populated tracts by population, largest remainders first.
	std::vector<double> DeathsPerTract(const Deathcare::TractGrid& grid, const std::vector<Afterlife::DeathWave>& waves,
		const std::vector<size_t>& waveTract)
	{
		std::vector<double> deaths(grid.population.size(), 0.0);
		double placed = 0;
		for (size_t i = 0; i < waves.size() && i < waveTract.size(); i++)
		{
			deaths[waveTract[i]] += waves[i].deaths;
			placed += waves[i].deaths;
		}
		const double rest = std::max(0.0, state.totalDeathsThisCycle - placed);
		double residents = 0;
		for (double p : grid.population) residents += p;
		if (rest <= 0 || residents <= 0) return deaths;
		std::vector<std::pair<double, size_t>> remainders;
		double given = 0;
		for (size_t i = 0; i < grid.population.size(); i++)
		{
			if (grid.population[i] <= 0) continue;
			const double share = rest * grid.population[i] / residents;
			const double whole = std::floor(share);
			deaths[i] += whole;
			given += whole;
			remainders.emplace_back(share - whole, i);
		}
		std::sort(remainders.begin(), remainders.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		for (size_t k = 0; k < remainders.size() && given < rest; k++, given++) deaths[remainders[k].second] += 1;
		return deaths;
	}

	// On the game's new-month message, the same message that starts the residential simulator's monthly cycle.
	void RunMonthlyCycle()
	{
		cISCLua* const lua = CityLua(currentCity);
		if (!lua)
		{
			return;
		}
		Afterlife::GameInputs game;
		if (!ReadInputs(lua, game))
		{
			return; // state unchanged
		}
		uint32_t resetTracts = 0;
		Deathcare::TractGrid grid;
		std::vector<size_t> waveTract;
		std::vector<Afterlife::DeathWave> waves = FindDeathWaves(currentCity, resetTracts, grid, waveTract);
		Afterlife::Step(state, game, waves, constants, RandomInt);
		if (Deathcare::Enabled() && !grid.population.empty())
		{
			Deathcare::RunMonth(grid, DeathsPerTract(grid, waves, waveTract));
		}
		MirrorState(lua);
		if (resetTracts > 0)
		{
			SimLog::Write(SimLog::Level::Info, "Afterlife: %u tract reset(s) this month: %.0f wave death(s), %.0f premature.",
				resetTracts, state.waveDeaths, state.prematureDeaths);
		}
	}

	// ---------------------------------------------------------------- saved with the city

	// The game saves nothing about deaths, so the cumulative populations go to SC4SIM_Afterlife.ini (next to the DLL),
	// one section per city serial number, written when the game saves the city and read when it opens.
	struct SavedField
	{
		const wchar_t* key;
		double Afterlife::State::* member;
	};
	constexpr SavedField kSavedFields[] = {
		{ L"WINNER_POPULATION_HEAVEN", &Afterlife::State::winnerPopulationHeaven },
		{ L"SINNER_POPULATION_HELL", &Afterlife::State::sinnerPopulationHell },
		{ L"REDEEMED_TOTAL", &Afterlife::State::redeemedTotal },
		{ L"IMP_KILLS_TOTAL", &Afterlife::State::impKillsTotal },
	};

	std::wstring CitySection(cISC4City* city)
	{
		wchar_t section[32]{};
		swprintf(section, 32, L"City.%08X", city ? city->GetCitySerialNumber() : 0u);
		return section;
	}

	// The whole section as key -> value (keys upper case).
	std::map<std::wstring, std::wstring> ReadSection(const std::wstring& section)
	{
		std::map<std::wstring, std::wstring> keys;
		std::vector<wchar_t> buffer(1 << 15);
		for (;;)
		{
			const DWORD length = GetPrivateProfileSectionW(section.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()),
				stateFile.c_str());
			if (length + 2 < buffer.size() || buffer.size() >= (1u << 24)) break;
			buffer.resize(buffer.size() * 2);
		}
		for (const wchar_t* line = buffer.data(); *line; line += std::wcslen(line) + 1)
		{
			const wchar_t* equals = std::wcschr(line, L'=');
			if (!equals) continue;
			std::wstring key(line, equals);
			for (wchar_t& c : key) c = static_cast<wchar_t>(std::towupper(c));
			keys[key] = equals + 1;
		}
		return keys;
	}

	void LoadState(cISC4City* city)
	{
		if (!settings.saveWithCity || !city || stateFile.empty())
		{
			return;
		}
		const std::wstring section = CitySection(city);
		const std::map<std::wstring, std::wstring> keys = ReadSection(section);
		int loaded = 0;
		for (const SavedField& field : kSavedFields)
		{
			const auto it = keys.find(field.key);
			if (it == keys.end()) continue;
			wchar_t* end = nullptr;
			const double value = std::wcstod(it->second.c_str(), &end);
			if (end != it->second.c_str() && std::isfinite(value) && value >= 0)
			{
				state.*field.member = std::floor(value);
				loaded++;
			}
		}
		Deathcare::LoadKeys(keys);
		if (loaded > 0)
		{
			Afterlife::UpdateNatives(state, constants);
			SimLog::Write(SimLog::Level::Info, "Afterlife: %ls loaded: %.0f in Heaven, %.0f in Hell.", section.c_str(),
				state.winnerPopulationHeaven, state.sinnerPopulationHell);
		}
	}

	// Rewrites the whole section, so cemeteries that are gone and tracts with no uncollected dead leave no stale keys.
	void SaveState(cISC4City* city)
	{
		if (!settings.saveWithCity || !city || stateFile.empty())
		{
			return;
		}
		std::wstring text;
		const auto write = [&text](const std::wstring& key, const std::wstring& value)
		{
			text += key;
			text += L'=';
			text += value;
			text += L'\0';
		};
		for (const SavedField& field : kSavedFields)
		{
			wchar_t value[64]{};
			swprintf(value, 64, L"%.0f", state.*field.member);
			write(field.key, value);
		}
		Deathcare::SaveKeys(write);
		text += L'\0';
		WritePrivateProfileSectionW(CitySection(city).c_str(), text.c_str(), stateFile.c_str());
	}

	// ---------------------------------------------------------------- registration

	void RegisterFunctions(cISC4City* city)
	{
		cISC4AdvisorSystem* const advisors = city ? city->GetAdvisorSystem() : nullptr;
		cISCLua* const lua = advisors ? advisors->GetScriptingContext() : nullptr;
		if (!lua)
		{
			SimLog::Write(SimLog::Level::Error, "Afterlife: city opened without an advisor scripting context: nothing registered.");
			return;
		}

		struct Entry
		{
			const char* name;
			lua_CFunction function;
		};
		const Entry entries[] = {
			{ "run_afterlife_balance_variables", RunAfterlifeBalanceVariables },
			{ "get_news_ticker_character", GetNewsTickerCharacter },
		};
		for (const Entry& entry : entries)
		{
			const bool replaces = IsGlobalFunction(lua, entry.name);
			const auto status = SCLuaUtil::RegisterLuaFunction(lua, nullptr, entry.name, entry.function);
			if (status == SCLuaUtil::RegisterLuaFunctionStatus::Ok)
			{
				SimLog::Write(SimLog::Level::Info, "Afterlife: %s registered%s.", entry.name,
					replaces ? " (replaces the Lua function)" : "");
			}
			else
			{
				SimLog::Write(SimLog::Level::Error, "Afterlife: could not register %s (status %d): the Lua version stays.",
					entry.name, static_cast<int>(status));
			}
		}
	}
}

void Afterlife::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0)
	{
		return;
	}
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");
	LoadConstants(ini.c_str());
	settings.deathWaves = GetPrivateProfileIntW(L"Afterlife", L"DEATH_WAVES", 1, ini.c_str()) != 0;
	settings.resetAge = static_cast<int>(GetPrivateProfileIntW(L"Afterlife", L"TRACT_RESET_AGE", 40, ini.c_str()));
	settings.chronicIntervalMonths = std::max(1, static_cast<int>(GetPrivateProfileIntW(L"Afterlife", L"CHRONIC_RESET_INTERVAL_MONTHS", 12, ini.c_str())));
	settings.saveWithCity = GetPrivateProfileIntW(L"Afterlife", L"SAVE_WITH_CITY", 1, ini.c_str()) != 0;
	stateFile = std::filesystem::path(modulePath).replace_filename(L"SC4SIM_Afterlife.ini");
	Deathcare::LoadSettings(ini.c_str());
	wchar_t value[32]{};
	GetPrivateProfileStringW(L"Afterlife", L"Enabled", L"true", value, 32, ini.c_str());
	std::wstring text = value;
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	SimLog::Write(SimLog::Level::Info, enabled ? "Afterlife: enabled." : "Afterlife: disabled in SC4SIM.ini (the Lua version stays).");
}

void Afterlife::DoMessage(cIGZMessage2* message)
{
	if (!enabled || !message)
	{
		return;
	}
	switch (message->GetType())
	{
	case kMessagePostCityInit:
		state = Afterlife::State{};
		tracts = TractMemory{};
		warnedMissing = false;
		warnedBuiltInRoster = false;
		currentCity = static_cast<cISC4City*>(static_cast<cIGZMessage2Standard*>(message)->GetVoid1());
		Deathcare::SetCity(currentCity);
		RegisterFunctions(currentCity);
		LoadState(currentCity);
		if (cISCLua* const lua = CityLua(currentCity)) MirrorState(lua);
		break;
	case kMessageSimNewMonth:
		RunMonthlyCycle();
		break;
	case kMessagePostSave:
		SaveState(currentCity);
		break;
	case kMessagePreCityShutdown:
		state = Afterlife::State{};
		tracts = TractMemory{};
		Deathcare::SetCity(nullptr);
		currentCity = nullptr;
		break;
	}
}
