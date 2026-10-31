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
//    replace the functions of AFTERLIFE.lua, which the game has already compiled by then; the Lua stays a fallback
//    for players without the DLL, and every script that calls those functions works unchanged;
//  * the inputs are read from the `game` table (g_health_coverage_p, g_city_r_population, ga_crime, ga_education),
//    the constants from the tables of PACK_CONSTANTS.lua (mortality_ratio, judgment_ratio, imp_ratio,
//    redemption_ratio, hell_native_ratio, hell_native_fixed, news_ticker_event_characters, hell_news_characters),
//    so the balance stays in that file;
//  * after every cycle the result is copied into the Lua table afterlife_grid_var, so scripts and UIs that read it
//    directly see the same values as with the Lua version;
//  * the cumulative populations are kept by the DLL and start from zero when a city opens.
// The calculation itself is in AfterlifeModel.cpp, without game or Windows dependencies.
//
////////////////////////////////////////////////////////////////////////////

#include "Afterlife.h"
#include "AfterlifeModel.h"
#include "SimLog.h"

#include "cIGZMessage2Standard.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4City.h"
#include "cISCLua.h"
#include "cRZAutoRefCount.h"
#include "SCLuaUtil.h"

#include <Windows.h>
#include <cwctype>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessagePostCityInit = 0x26D31EC1;
	constexpr uint32_t kMessagePreCityShutdown = 0x26D31EC2;

	bool enabled = true;
	Afterlife::State state;
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

	bool ReadInputs(cISCLua* lua, Afterlife::GameInputs& game, Afterlife::Constants& k)
	{
		std::vector<std::string> missing;
		ReadBindings(lua, {
			{ "game", "g_health_coverage_p", &game.healthCoverage },
			{ "game", "g_city_r_population", &game.residentPopulation },
			{ "game", "ga_crime", &game.crime },
			{ "game", "ga_education", &game.education },

			{ "mortality_ratio", "BASE_MORTALITY_RATE_PER_CAPITA", &k.baseMortalityRatePerCapita },
			{ "mortality_ratio", "HEALTH_COVERAGE_WEIGHT", &k.healthCoverageWeight },

			{ "judgment_ratio", "EDUCATION_REFERENCE", &k.educationReference },
			{ "judgment_ratio", "BASE_SIN_RATIO", &k.baseSinRatio },
			{ "judgment_ratio", "CRIME_WEIGHT", &k.crimeWeight },
			{ "judgment_ratio", "EDUCATION_WEIGHT", &k.educationWeight },
			{ "judgment_ratio", "SIN_RATIO_MIN", &k.sinRatioMin },
			{ "judgment_ratio", "SIN_RATIO_MAX", &k.sinRatioMax },

			{ "imp_ratio", "IMP_CLIENT_RATIO", &k.impClientRatio },
			{ "imp_ratio", "IMP_SUCCESS_CHANCE", &k.impSuccessChance },
			{ "imp_ratio", "IMP_KILL_RANDOM_VARIANCE", &k.impKillRandomVariance },
			{ "imp_ratio", "MAX_IMP_KILL_FRACTION_POP", &k.maxImpKillFractionPop },

			{ "redemption_ratio", "REDEMPTION_RATIO", &k.redemptionRatio },

			{ "hell_native_ratio", "IMP_RATIO", &k.impRatio },
			{ "hell_native_ratio", "HELLHOUND_RATIO", &k.hellhoundRatio },
			{ "hell_native_ratio", "SUCCUBUS_RATIO", &k.succubusRatio },
			{ "hell_native_ratio", "INFESTOR_RATIO", &k.infestorRatio },
			{ "hell_native_ratio", "POSSESSOR_RATIO", &k.possessorRatio },
			{ "hell_native_ratio", "BAPHOMET_RATIO", &k.baphometRatio },
			{ "hell_native_ratio", "REAPER_RATIO", &k.reaperRatio },
			{ "hell_native_ratio", "GOETIA_ARS_GOETIA_RATIO", &k.goetiaArsGoetiaRatio },

			{ "hell_native_fixed", "FALLEN_ANGELS", &k.fallenAngels },
			{ "hell_native_fixed", "SEVEN_DEADLY_SINS", &k.sevenDeadlySins },
		}, missing);

		if (missing.empty())
		{
			return true;
		}
		if (!warnedMissing)
		{
			warnedMissing = true;
			std::string list;
			for (const std::string& name : missing) list += (list.empty() ? "" : ", ") + name;
			SimLog::Write(SimLog::Level::Warning,
				"Afterlife: %zu value(s) missing, nothing calculated (is PACK_CONSTANTS.lua loaded?): %s",
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
	};

	void MirrorState(cISCLua* lua)
	{
		for (const StateField& field : kStateFields)
		{
			SetNumber(lua, "afterlife_grid_var", field.name, state.*field.member);
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

		Afterlife::GameInputs game;
		Afterlife::Constants constants;
		if (!ReadInputs(lua, game, constants))
		{
			return 0; // nil, state unchanged
		}

		Afterlife::Step(state, game, constants, RandomInt);
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

	// get_news_ticker_character(event_type): hell_news_characters[random pick of news_ticker_event_characters[event_type]]
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

		lua->GetGlobal("news_ticker_event_characters"); // events
		if (!lua->IsTable(-1))
		{
			return 0;
		}
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
		if (!lua->IsTable(-1))
		{
			return 0;
		}
		lua->PushValue(-2);
		lua->GetTable(-2); // events, candidates, pick, characters, entry
		if (lua->IsNil(-1))
		{
			return 0;
		}
		return 1; // the entry is the result, on top of the stack
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
		warnedMissing = false;
		RegisterFunctions(static_cast<cISC4City*>(static_cast<cIGZMessage2Standard*>(message)->GetVoid1()));
		break;
	case kMessagePreCityShutdown:
		state = Afterlife::State{};
		break;
	}
}
