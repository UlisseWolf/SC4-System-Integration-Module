////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: fix of the port upgrade bug (SimCity 4 1.1.641).
//
// Ports (seaports and airports) are managed by the port developer (cSC4PortDeveloper, vtable 0x00AB2790). It keeps
// one entry per port: cell x at +0x0E, cell z at +0x10, the lot at +0x20, a "refresh the lot" flag at +0x0C, an
// "upgrade in progress" flag at +0x18 and, at +0x1C, the utilisation (usage / capacity) last applied to the demand
// simulator.
//
// A building whose exemplar has 0x27812844 = true gets its Demand Satisfied (0x27812840) and Demand Created
// (0x27812841) from the port developer instead of the demand simulator's lot state handler (0x006A9470 skips it).
// Every month (0x006E9980) the port developer calls cSC4DemandSimulator::UpdateOccupantEffects (vtable slot 7,
// 0x006A6C90) with the lot's current building type, the utilisation at +0x1C and the new utilisation: the demand
// simulator removes old x values and adds new x values of that building type (0x006A5920), and +0x1C takes the new
// utilisation. Maxis and PEG seaports have no such properties; the BSC Functional Seaports give every stage Demand
// Satisfied on the industrial caps 0x4A00, 0x4B00 and 0x4C00, growing with the stage (BRT Seaport: 2400 at stage 1,
// 35200 at stage 8), plus jobs, all with 0x27812844 = true.
//
// The upgrade (0x006E92A0, called from 0x006E9C0E) sets the upgrade flag, demolishes the old lot and builds the next
// stage in the same entry, without touching +0x1C. The old lot's demolition message (0x006E9EA0, state 5) sees the
// flag, clears it and does not remove anything. On the next month the port developer passes the new building type:
// the demand simulator removes old utilisation x the NEW stage's values, while old utilisation x the OLD stage's
// values had been added. Every upgrade loses utilisation x (new value - old value) of each cap for good (it is saved
// with the city): a port that climbs from stage 1 to stage 8 brings no more relief than at stage 1, goes negative as
// soon as its utilisation drops (the upgrades add capacity) and leaves a permanent negative balance when demolished.
// Jobs drift the same way, by much smaller amounts.
//
// The fix redirects the call at 0x006E9C0E. Before the game's upgrade, it removes the effects of the stage being
// replaced, with that stage's building type and the utilisation at +0x1C (UpdateOccupantEffects(type, u, 0)), and
// sets +0x1C to 0. The next monthly update then adds the new stage's values from zero, so each stage takes out exactly
// what it put in. Cities saved with the bug keep the relief already lost; the fix stops further losses.
//
////////////////////////////////////////////////////////////////////////////

#include "PortUpgradeFix.h"
#include "SimLog.h"
#include "VersionDetection.h"

#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4Lot.h"
#include "cISC4LotManager.h"
#include "GZServPtrs.h"

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

	// call cSC4PortDeveloper::UpgradePort (E8 rel32) in the port developer's upgrade loop.
	constexpr uintptr_t kUpgradeCallSite = 0x006E9C0E;
	constexpr uintptr_t kUpgradePort = 0x006E92A0;
	// cSC4DemandSimulator vtable slot 7: UpdateOccupantEffects(uint32 buildingType, float const& old, float const& new).
	constexpr uintptr_t kDemandSimulatorSlot7 = 0x00AB0E64;
	constexpr uintptr_t kUpdateOccupantEffects = 0x006A6C90;

	// Port entry fields.
	constexpr size_t kEntryRefreshLot = 0x0C;
	constexpr size_t kEntryCellX = 0x0E;
	constexpr size_t kEntryCellZ = 0x10;
	constexpr size_t kEntryUtilisation = 0x1C;
	constexpr size_t kEntryLot = 0x20;

	using UpgradePortFn = bool(__thiscall*)(void* portDeveloper, uint8_t* entry, uint32_t lotConfigurationID);
	using UpdateOccupantEffectsFn = void(__thiscall*)(void* demandSimulator, uint32_t buildingType, const float& oldValue, const float& newValue);

	bool enabled = true;
	bool patched = false;
	uint8_t originalCall[5]{};
	unsigned correctedUpgrades = 0;

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

	bool WriteCode(uintptr_t address, const uint8_t* bytes, size_t size)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
		std::memcpy(reinterpret_cast<void*>(address), bytes, size);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &ignored);
		return true;
	}

	uintptr_t CallTarget(uintptr_t callSite)
	{
		const auto* code = reinterpret_cast<const uint8_t*>(callSite);
		if (code[0] != 0xE8) return 0;
		int32_t rel = 0;
		std::memcpy(&rel, code + 1, sizeof(rel));
		return callSite + 5 + rel;
	}

	// The lot of a port entry. When the "refresh" flag is set (after loading a city) the game looks the lot up again by
	// cell; do the same without touching the entry, the game refreshes it itself.
	cISC4Lot* EntryLot(cISC4City* city, uint8_t* entry)
	{
		if (entry[kEntryRefreshLot] == 0)
		{
			return *reinterpret_cast<cISC4Lot**>(entry + kEntryLot);
		}
		cISC4LotManager* lotManager = city->GetLotManager();
		if (!lotManager) return nullptr;
		const int32_t x = *reinterpret_cast<int16_t*>(entry + kEntryCellX);
		const int32_t z = *reinterpret_cast<int16_t*>(entry + kEntryCellZ);
		return lotManager->GetLot(x, z, true);
	}

	// Removes the effects of the stage about to be replaced, then lets the game upgrade.
	bool __fastcall UpgradePortHook(void* portDeveloper, void* /*edx*/, uint8_t* entry, uint32_t lotConfigurationID)
	{
		if (entry)
		{
			float& utilisation = *reinterpret_cast<float*>(entry + kEntryUtilisation);
			if (utilisation != 0.0f)
			{
				cISC4AppPtr app;
				cISC4City* city = app ? app->GetCity() : nullptr;
				void* demandSimulator = city ? static_cast<void*>(city->GetDemandSimulator()) : nullptr;
				cISC4Lot* lot = city ? EntryLot(city, entry) : nullptr;
				const uint32_t buildingType = lot ? lot->GetBuildingType(false) : 0;
				if (demandSimulator && buildingType != 0)
				{
					const float zero = 0.0f;
					const float old = utilisation;
					auto update = reinterpret_cast<UpdateOccupantEffectsFn>((*reinterpret_cast<void***>(demandSimulator))[7]);
					update(demandSimulator, buildingType, old, zero);
					utilisation = 0.0f;
					++correctedUpgrades;
					SimLog::Write(SimLog::Level::Info,
						"PortUpgradeFix: port at cell (%d, %d) upgrading from building 0x%08X: removed its demand effects at utilisation %.2f.",
						static_cast<int>(*reinterpret_cast<int16_t*>(entry + kEntryCellX)),
						static_cast<int>(*reinterpret_cast<int16_t*>(entry + kEntryCellZ)),
						static_cast<unsigned>(buildingType), static_cast<double>(old));
				}
			}
		}
		return reinterpret_cast<UpgradePortFn>(kUpgradePort)(portDeveloper, entry, lotConfigurationID);
	}
}

void PortUpgradeFix::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0)
	{
		return;
	}
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");
	enabled = ReadBool(ini, L"PortUpgradeFix", true);
}

void PortUpgradeFix::PostAppInit()
{
	if (!enabled)
	{
		SimLog::Write(SimLog::Level::Info, "PortUpgradeFix: disabled in SC4SIM.ini ([GameFixes] PortUpgradeFix=false).");
		return;
	}
	const uint16_t version = VersionDetection::GetInstance().GetGameVersion();
	if (version != kSupportedGameVersion)
	{
		SimLog::Write(SimLog::Level::Warning, "PortUpgradeFix: game version %u, the fix needs 1.1.641: not installed.", version);
		return;
	}
	const uintptr_t slot7 = *reinterpret_cast<const uintptr_t*>(kDemandSimulatorSlot7);
	if (CallTarget(kUpgradeCallSite) != kUpgradePort || slot7 != kUpdateOccupantEffects)
	{
		SimLog::Write(SimLog::Level::Warning,
			"PortUpgradeFix: the port upgrade call at 0x%08X is not the expected code (another DLL patches it?): not installed.",
			static_cast<unsigned>(kUpgradeCallSite));
		return;
	}

	std::memcpy(originalCall, reinterpret_cast<const void*>(kUpgradeCallSite), sizeof(originalCall));
	uint8_t call[5]{ 0xE8 };
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&UpgradePortHook) - (kUpgradeCallSite + 5));
	std::memcpy(call + 1, &rel, sizeof(rel));
	patched = WriteCode(kUpgradeCallSite, call, sizeof(call));
	SimLog::Write(patched ? SimLog::Level::Info : SimLog::Level::Error, patched
		? "PortUpgradeFix: port upgrades remove the old stage's demand effects before the new stage replaces it."
		: "PortUpgradeFix: could not patch the port upgrade call.");
}

void PortUpgradeFix::PostAppShutdown()
{
	if (patched)
	{
		WriteCode(kUpgradeCallSite, originalCall, sizeof(originalCall));
		patched = false;
		if (correctedUpgrades)
		{
			SimLog::Write(SimLog::Level::Info, "PortUpgradeFix: %u port upgrade(s) corrected this session.", correctedUpgrades);
		}
	}
}
