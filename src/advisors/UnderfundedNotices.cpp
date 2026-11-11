////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: priorities of the Fixed Underfunded Notices (FUN) by CoriBoom, chosen in SC4SIM.ini.
//
// FUN (CAM_LUA_Controller.dat, Lua 0x4A5E8EF6 / 0x434FA499, package 7a76A499) deletes eight advisor notices and
// recreates them: the underfunded notices of hospitals, schools, libraries and museums (the latter two reuse Maxis's
// unused library and museum strike notices) and the strike notices of hospitals, schools, fire and police. CoriBoom
// shipped it in two files that differ only in four priorities: "Urgent" (every notice urgent) and "Med-High". CAM
// includes the Urgent one.
//
// The game copies an advice from its Lua record when it creates it: cSC4Advice::Init (0x00408C70) stores the GUID,
// finds the record in the global "advices" table and reads priority, frequency and timeout into the object
// (GetPriority, 0x00407AB0, returns the copy at +0x78). Clone (0x00407C50) calls Init again for every notice the game
// shows, so the record is read each time. This module changes the "priority" field of the eight records when a city
// opens, before any notice is shown, using the advisor scripting context. It changes only records whose title is the
// FUN text: without FUN installed, the Maxis notices are left alone. Priorities are names of tuning_constants
// (ADVICE_PRIORITY_*), read from the city's Lua so CAM's values apply, or numbers from 0 to 100.
//
////////////////////////////////////////////////////////////////////////////

#include "UnderfundedNotices.h"
#include "SimLog.h"

#include "cIGZMessage2Standard.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4City.h"
#include "cISCLua.h"

#include <Windows.h>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessagePostCityInit = 0x26D31EC1;

	struct Notice
	{
		const wchar_t* key;     // SC4SIM.ini key
		const char* name;       // log name
		uint32_t guid;          // advice GUID
		const char* funTitle;   // FUN title LTEXT reference, prefix of the record's title
		const char* urgent;     // priority in the "Urgent" file
		const char* medHigh;    // priority in the "Med-High" file
		std::string priority;   // chosen: tuning_constants name or number
	};

	std::array<Notice, 8> notices = { {
		{ L"HospitalUnderfunded", "hospital underfunded", 0x2A71398D, "text@ea5655d2", "URGENT", "MED_HIGH", {} },
		{ L"HealthStrike", "health strike", 0xEA5E97A8, "text@8a565562", "URGENT", "URGENT", {} },
		{ L"SchoolUnderfunded", "school underfunded", 0xAA713E17, "text@2a565623", "URGENT", "HIGH", {} },
		{ L"SchoolStrike", "school strike", 0x0A5E79A1, "text@6a56561b", "URGENT", "URGENT", {} },
		{ L"LibraryUnderfunded", "library underfunded", 0xEAABFE7E, "text@8aabfe88", "URGENT", "MEDIUM", {} },
		{ L"MuseumUnderfunded", "museum underfunded", 0xEAAC049A, "text@0aac0428", "URGENT", "MEDIUM", {} },
		{ L"FireStrike", "fire strike", 0x2A64D230, "text@0a3abb29", "URGENT", "URGENT", {} },
		{ L"PoliceStrike", "police strike", 0xCA5E732A, "text@aa564132", "URGENT", "URGENT", {} },
	} };

	bool enabled = true;
	std::string presetName = "Urgent";

	std::wstring ReadString(const std::filesystem::path& ini, const wchar_t* key, const wchar_t* fallback)
	{
		wchar_t value[64]{};
		GetPrivateProfileStringW(L"UnderfundedNotices", key, fallback, value, 64, ini.c_str());
		return value;
	}

	std::string Normalize(const std::wstring& text)
	{
		std::string result;
		for (wchar_t c : text)
		{
			if (c == L' ' || c == L'\t' || c == L'-' || c == L'_') continue;
			result.push_back(static_cast<char>(std::towlower(c)));
		}
		return result;
	}

	// SC4SIM.ini value -> tuning_constants suffix (ADVICE_PRIORITY_<suffix>) or a number from 0 to 100; empty if invalid.
	std::string PriorityFromIni(const std::wstring& text)
	{
		const std::string value = Normalize(text);
		if (value.empty()) return {};
		static const std::array<std::pair<const char*, const char*>, 8> names = { {
			{ "verylow", "VERY_LOW" }, { "low", "LOW" }, { "medlow", "MED_LOW" }, { "medmedlow", "MED_MED_LOW" },
			{ "medium", "MEDIUM" }, { "medhigh", "MED_HIGH" }, { "high", "HIGH" }, { "urgent", "URGENT" } } };
		for (const auto& [ini, constant] : names)
		{
			if (value == ini) return constant;
		}
		char* end = nullptr;
		const long number = std::strtol(value.c_str(), &end, 10);
		if (end != value.c_str() && *end == '\0' && number >= 0 && number <= 100) return value;
		return {};
	}

	bool IsNumber(const std::string& priority)
	{
		return !priority.empty() && std::isdigit(static_cast<unsigned char>(priority.front()));
	}

	// Value of tuning_constants.ADVICE_PRIORITY_<suffix> in the city's Lua, or the number itself.
	bool ResolvePriority(cISCLua* lua, const std::string& priority, double& value)
	{
		if (IsNumber(priority))
		{
			value = std::strtod(priority.c_str(), nullptr);
			return true;
		}
		const int32_t top = lua->GetTop();
		bool ok = false;
		lua->GetGlobal("tuning_constants");
		if (lua->IsTable(-1))
		{
			const std::string field = "ADVICE_PRIORITY_" + priority;
			lua->PushString(field.c_str());
			lua->GetTable(-2);
			if (lua->IsNumber(-1))
			{
				value = lua->ToNumber(-1);
				ok = true;
			}
		}
		lua->SetTop(top);
		return ok;
	}

	bool StartsWithIgnoreCase(const char* text, const char* prefix)
	{
		for (; *prefix; ++text, ++prefix)
		{
			if (!*text || std::tolower(static_cast<unsigned char>(*text)) != std::tolower(static_cast<unsigned char>(*prefix))) return false;
		}
		return true;
	}

	void Apply(cISC4City* city)
	{
		cISC4AdvisorSystem* advisors = city ? city->GetAdvisorSystem() : nullptr;
		cISCLua* lua = advisors ? advisors->GetScriptingContext() : nullptr;
		if (!lua)
		{
			SimLog::Write(SimLog::Level::Warning, "UnderfundedNotices: no advisor scripting context: priorities not set.");
			return;
		}

		std::array<double, notices.size()> values{};
		for (size_t k = 0; k < notices.size(); ++k)
		{
			if (!ResolvePriority(lua, notices[k].priority, values[k]))
			{
				SimLog::Write(SimLog::Level::Warning, "UnderfundedNotices: tuning_constants.ADVICE_PRIORITY_%s is not defined: %s keeps its priority.",
					notices[k].priority.c_str(), notices[k].name);
				values[k] = -1.0;
			}
		}

		const int32_t top = lua->GetTop();
		lua->GetGlobal("advices");
		if (!lua->IsTable(-1))
		{
			lua->SetTop(top);
			SimLog::Write(SimLog::Level::Warning, "UnderfundedNotices: the advisor Lua has no advices table: priorities not set.");
			return;
		}
		lua->PushString("n");
		lua->GetTable(-2);
		const int32_t count = lua->IsNumber(-1) ? static_cast<int32_t>(lua->ToNumber(-1)) : 0;
		lua->Pop(1);

		unsigned set = 0;
		for (int32_t i = 1; i <= count; ++i)
		{
			lua->RawGetI(-1, i);
			if (lua->IsTable(-1))
			{
				lua->PushString("guid");
				lua->GetTable(-2);
				const uint32_t guid = lua->IsNumber(-1) ? static_cast<uint32_t>(static_cast<int64_t>(lua->ToNumber(-1))) : 0;
				lua->Pop(1);
				for (size_t k = 0; k < notices.size(); ++k)
				{
					if (notices[k].guid != guid || values[k] < 0.0) continue;
					lua->PushString("title");
					lua->GetTable(-2);
					const bool fun = lua->IsString(-1) && StartsWithIgnoreCase(lua->ToString(-1), notices[k].funTitle);
					lua->Pop(1);
					if (!fun) break;
					lua->PushString("priority");
					lua->PushNumber(values[k]);
					lua->SetTable(-3);
					++set;
					SimLog::Write(SimLog::Level::Info, "UnderfundedNotices: %s (0x%08X) priority %.0f.", notices[k].name, guid, values[k]);
					break;
				}
			}
			lua->Pop(1);
		}
		lua->SetTop(top);

		if (set == 0)
		{
			SimLog::Write(SimLog::Level::Warning,
				"UnderfundedNotices: no FUN notice found (Fixed Underfunded Notices not in CAM_LUA_Controller.dat?): nothing changed.");
		}
		else
		{
			SimLog::Write(SimLog::Level::Info, "UnderfundedNotices: %u of %u FUN notices set (preset %s).",
				set, static_cast<unsigned>(notices.size()), presetName.c_str());
		}
	}
}

void UnderfundedNotices::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	const std::string enabledText = Normalize(ReadString(ini, L"Enabled", L"true"));
	enabled = !(enabledText == "false" || enabledText == "0" || enabledText == "no" || enabledText == "off");

	const std::string preset = Normalize(ReadString(ini, L"Preset", L"Urgent"));
	const bool medHigh = preset == "medhigh";
	presetName = medHigh ? "MedHigh" : "Urgent";
	if (!medHigh && preset != "urgent")
	{
		SimLog::Write(SimLog::Level::Warning, "UnderfundedNotices: unknown Preset in SC4SIM.ini (Urgent or MedHigh): Urgent used.");
	}

	for (Notice& notice : notices)
	{
		notice.priority = medHigh ? notice.medHigh : notice.urgent;
		const std::wstring text = ReadString(ini, notice.key, L"");
		if (text.empty()) continue;
		const std::string priority = PriorityFromIni(text);
		if (priority.empty())
		{
			SimLog::Write(SimLog::Level::Warning,
				"UnderfundedNotices: invalid priority for %s in SC4SIM.ini (VeryLow, Low, MedLow, MedMedLow, Medium, MedHigh, High, "
				"Urgent or 0-100): preset used.", notice.name);
			continue;
		}
		notice.priority = priority;
	}
}

void UnderfundedNotices::DoMessage(cIGZMessage2* message)
{
	if (!enabled || message->GetType() != kMessagePostCityInit) return;
	Apply(static_cast<cISC4City*>(static_cast<cIGZMessage2Standard*>(message)->GetVoid1()));
}
