////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: seaport demand cap fix (SimCity 4 1.1.641).
//
// Port of sc4-seaport-demand-cap-fix by Nicholas Hayes (Copyright (C) 2026, GNU LGPL 2.1 or later, see
// LICENSE-sc4-seaport-demand-cap-fix.txt), merged into SC4SIM.dll with the author's consent.
//
// cSC4DemandSimulator::EvaluateConnectionUtilization (0x006A3ED0) turns the month's seaport trips into cap relief:
// industrial (accumulator +0xAC, demands 0x4800 and 0x4900) and commercial (accumulator +0xA8, demand 0x3800). The
// game multiplies the trips by the wrong tuning value in each term: the industrial term reads +0xCC (property
// 0x4A1F38C7, the commercial seaport factor) and the commercial term reads +0xD8 (0x4A1F38CA, the industrial seaport
// factor). With the default tuning (2.0 and 20, same in Maxis and CAM) a seaport trip gives 2 of industrial relief
// instead of 20 and 20 of commercial relief instead of 2.
//
// As in the original DLL, the fix swaps the two operands of the FMUL instructions:
//   0x006A4A9E  fmul dword ptr [esi+0xCC]  ->  [esi+0xD8]   (industrial term)
//   0x006A4AB8  fmul dword ptr [esi+0xD8]  ->  [esi+0xCC]   (commercial term)
// SC4SIM checks the six bytes of both instructions before writing and restores them when the game closes. If
// SC4SeaportDemandCapFix.dll is installed next to SC4SIM.dll, SC4SIM leaves the patch and the cheat to it.
//
// This is a different bug from the staged port upgrades (PortUpgradeFix.cpp); the two fixes work together.
//
// Cheat LogIndustrialDemandValues (same ID as the original): writes to SC4SIM.log the supply, demand and cap of the
// industrial demands read by the original DLL (0x4300 and 0x4400, index 0x20000), plus, for checking the seaport
// relief itself, the industrial and commercial connection demands (0x4800, 0x3800) and the industrial caps 0x4A00,
// 0x4B00 and 0x4C00.
//
////////////////////////////////////////////////////////////////////////////

#include "SeaportDemandCapFix.h"
#include "SimLog.h"
#include "VersionDetection.h"

#include "cIGZCheatCodeManager.h"
#include "cIGZMessage2Standard.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4Demand.h"
#include "cISC4DemandSimulator.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "SC4Percentage.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint16_t kSupportedGameVersion = 641;
	constexpr uint32_t kMessageCheatIssued = 0x230E27AC;
	constexpr uint32_t kCheatLogIndustrialDemandValues = 0x1BAF4856;
	constexpr const char* kCheatLogIndustrialDemandValuesName = "LogIndustrialDemandValues";

	struct Site
	{
		const char* name;
		uintptr_t instruction;   // FMUL dword ptr [esi+disp32]: D8 8E disp32
		uint32_t original;
		uint32_t fixed;
	};

	const Site sites[] = {
		{ "industrial", 0x006A4A9E, 0xCC, 0xD8 },
		{ "commercial", 0x006A4AB8, 0xD8, 0xCC },
	};

	bool enabled = true;
	bool patched = false;
	bool cheatRegistered = false;
	cRZAutoRefCount<cIGZCheatCodeManager> cheatManager;

	std::filesystem::path ModuleFolder()
	{
		wchar_t modulePath[MAX_PATH]{};
		if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return {};
		return std::filesystem::path(modulePath).parent_path();
	}

	bool ReadBool(const std::filesystem::path& ini, const wchar_t* key, bool defaultValue)
	{
		wchar_t value[32]{};
		GetPrivateProfileStringW(L"GameFixes", key, defaultValue ? L"true" : L"false", value, 32, ini.c_str());
		std::wstring text = value;
		for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
		if (text == L"false" || text == L"0" || text == L"no" || text == L"off") return false;
		if (text == L"true" || text == L"1" || text == L"yes" || text == L"on") return true;
		return defaultValue;
	}

	// D8 8E followed by the expected displacement.
	bool SiteHas(const Site& site, uint32_t displacement)
	{
		const auto* code = reinterpret_cast<const uint8_t*>(site.instruction);
		uint32_t current = 0;
		std::memcpy(&current, code + 2, sizeof(current));
		return code[0] == 0xD8 && code[1] == 0x8E && current == displacement;
	}

	bool WriteDisplacement(const Site& site, uint32_t displacement)
	{
		void* address = reinterpret_cast<void*>(site.instruction + 2);
		DWORD oldProtect = 0;
		if (!VirtualProtect(address, sizeof(displacement), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
		std::memcpy(address, &displacement, sizeof(displacement));
		FlushInstructionCache(GetCurrentProcess(), address, sizeof(displacement));
		DWORD ignored = 0;
		VirtualProtect(address, sizeof(displacement), oldProtect, &ignored);
		return true;
	}

	void LogDemand(cISC4DemandSimulator* demandSimulator, uint32_t id, uint32_t index, const char* label)
	{
		const cISC4Demand* demand = demandSimulator->GetDemand(id, index);
		if (!demand)
		{
			SimLog::Write(SimLog::Level::Info, "LogIndustrialDemandValues: demand 0x%04X (index 0x%X, %s): not found.", id, index, label);
			return;
		}
		const SC4Percentage* cap = demand->GetDemandCap();
		SimLog::Write(SimLog::Level::Info,
			"LogIndustrialDemandValues: demand 0x%08X (%s): supply %.1f, demand %.1f, cap %.3f.",
			demand->GetId(), label, static_cast<double>(demand->QuerySupplyValue()),
			static_cast<double>(demand->QueryDemandValue()), cap ? static_cast<double>(cap->percentage) : 0.0);
	}

	void LogIndustrialDemandValues()
	{
		cISC4AppPtr app;
		cISC4City* city = app ? app->GetCity() : nullptr;
		cISC4DemandSimulator* demandSimulator = city ? city->GetDemandSimulator() : nullptr;
		if (!demandSimulator)
		{
			SimLog::Write(SimLog::Level::Info, "LogIndustrialDemandValues: no city open.");
			return;
		}
		SimLog::Write(SimLog::Level::Info, "LogIndustrialDemandValues: seaport demand cap fix %s.",
			patched ? "active" : "not active in SC4SIM");
		LogDemand(demandSimulator, 0x4300, 0x20000, "I-M");
		LogDemand(demandSimulator, 0x4400, 0x20000, "I-HT");
		LogDemand(demandSimulator, 0x4800, 0, "Industrial Connections");
		LogDemand(demandSimulator, 0x4A00, 0, "I-D Cap");
		LogDemand(demandSimulator, 0x4B00, 0, "I-M Cap");
		LogDemand(demandSimulator, 0x4C00, 0, "I-HT Cap");
		LogDemand(demandSimulator, 0x3800, 0, "Commercial Connections");
	}
}

void SeaportDemandCapFix::OnStart()
{
	const std::filesystem::path folder = ModuleFolder();
	if (folder.empty()) return;
	enabled = ReadBool(folder / L"SC4SIM.ini", L"SeaportDemandCapFix", true);
}

void SeaportDemandCapFix::PostAppInit()
{
	if (!enabled)
	{
		SimLog::Write(SimLog::Level::Info, "SeaportDemandCapFix: disabled in SC4SIM.ini ([GameFixes] SeaportDemandCapFix=false).");
		return;
	}
	if (std::filesystem::exists(ModuleFolder() / L"SC4SeaportDemandCapFix.dll"))
	{
		SimLog::Write(SimLog::Level::Warning,
			"SeaportDemandCapFix: SC4SeaportDemandCapFix.dll is installed next to SC4SIM.dll; the standalone DLL applies the "
			"patch and the cheat. Remove it to use the SC4SIM version.");
		return;
	}
	const uint16_t version = VersionDetection::GetInstance().GetGameVersion();
	if (version != kSupportedGameVersion)
	{
		SimLog::Write(SimLog::Level::Warning, "SeaportDemandCapFix: game version %u, the fix needs 1.1.641: not installed.", version);
		return;
	}

	bool alreadyFixed = true;
	for (const Site& site : sites)
	{
		if (SiteHas(site, site.fixed)) continue;
		alreadyFixed = false;
		if (!SiteHas(site, site.original))
		{
			SimLog::Write(SimLog::Level::Warning,
				"SeaportDemandCapFix: the %s seaport term at 0x%08X is not the expected code (another DLL?): not patched.",
				site.name, static_cast<unsigned>(site.instruction));
			return;
		}
	}
	if (alreadyFixed)
	{
		SimLog::Write(SimLog::Level::Info, "SeaportDemandCapFix: the seaport terms are already fixed by another DLL: left as they are.");
		return;
	}

	patched = true;
	for (const Site& site : sites)
	{
		if (!SiteHas(site, site.fixed)) patched = WriteDisplacement(site, site.fixed) && patched;
	}
	SimLog::Write(patched ? SimLog::Level::Info : SimLog::Level::Error, patched
		? "SeaportDemandCapFix: seaport trips use the industrial factor for industrial relief and the commercial factor for commercial relief "
		  "(port of sc4-seaport-demand-cap-fix by Nicholas Hayes)."
		: "SeaportDemandCapFix: could not patch the seaport terms.");

	cISC4AppPtr app;
	cIGZCheatCodeManager* cheats = app ? app->GetCheatCodeManager() : nullptr;
	if (cheats)
	{
		cheatManager = cheats;
		cheatRegistered = cheats->RegisterCheatCode(kCheatLogIndustrialDemandValues, cRZBaseString(kCheatLogIndustrialDemandValuesName));
	}
}

void SeaportDemandCapFix::PostAppShutdown()
{
	if (cheatManager)
	{
		if (cheatRegistered)
		{
			cheatManager->UnregisterCheatCode(kCheatLogIndustrialDemandValues);
			cheatRegistered = false;
		}
		cheatManager.Reset();
	}
	if (patched)
	{
		for (const Site& site : sites)
		{
			if (SiteHas(site, site.fixed)) WriteDisplacement(site, site.original);
		}
		patched = false;
	}
}

void SeaportDemandCapFix::DoMessage(cIGZMessage2* message)
{
	if (message->GetType() != kMessageCheatIssued || !cheatRegistered) return;
	auto* standardMessage = static_cast<cIGZMessage2Standard*>(message);
	if (static_cast<uint32_t>(standardMessage->GetData1()) == kCheatLogIndustrialDemandValues)
	{
		LogIndustrialDemandValues();
	}
}
