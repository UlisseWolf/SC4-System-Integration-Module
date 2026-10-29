////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: mixed residential + commercial occupancy (R + C on one lot).
//
// A building is "mixed" when its exemplar has Allow Joint Occupancy (0x27812836) = true, its Purpose is
// Residence, Services or Office, its Capacity Satisfied (0x27812834) lists at least one residential type
// (R$, R$$, R$$$) and at least one commercial type (CS$, CS$$, CS$$$, CO$$, CO$$$), and no industrial type.
// Every other building keeps the game's behaviour.
//
// For mixed lots the module:
//  1. replaces cSC4Lot::GetAvailableCapacity (vtable slot 52): residential and commercial capacities are
//     independent; within one class the wealth levels stay exclusive, as in a building without joint occupancy;
//  2. replaces cSC4Lot::GetOccupancy (vtable slot 61): occupancy counts only the class of the building's
//     own purpose, so it never exceeds 100% and the secondary class cannot make the building decline;
//  3. wraps cSC4DemandSimulator::PerformCensus (both call sites) and adds the secondary types to the
//     census with the game's residential or job formula, so they count towards the RCI demand.
//
// The secondary types are filled by the game itself: cSC4GrowthDeveloper::DoIdle (0x006B5F20) calls
// cSC4TractDeveloper::UpdatePopulation (0x00706BC0, call at 0x006B6A65) for every growth developer and every
// tract. UpdatePopulation fills any lot whose occupant wealth equals the developer's wealth, asking the room
// through vtable slot 52, so hook 1 is enough for the CS$$ developer to fill the CS$$ jobs of an R$$ lot, with
// the game's random spread and schedule. A secondary type therefore needs the building's own wealth.
// The MixedOccupancy cheat (and, with DebugLog, every month start) only writes the mixed lots to the log.
//
// SimCity 4 1.1.641 only. Every address is checked against the expected original bytes before patching;
// if one does not match, nothing is installed. The reverse-engineering notes are in docs/mixed-occupancy.md.
//
////////////////////////////////////////////////////////////////////////////

#include "MixedOccupancy.h"
#include "SimLog.h"
#include "VersionDetection.h"

#include "cIGZCheatCodeManager.h"
#include "cIGZMessage2Standard.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4Lot.h"
#include "cISC4LotManager.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "SC4List.h"

#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	// ---------------------------------------------------------------- messages, settings

	constexpr uint32_t kMessageCheatIssued = 0x230E27AC;
	constexpr uint32_t kMessagePostCityInit = 0x26D31EC1;
	constexpr uint32_t kMessagePreCityShutdown = 0x26D31EC2;
	constexpr uint32_t kMessageSimNewMonth = 0x66956816;
	constexpr uint32_t kMixedOccupancyCheatID = 0x8E9F3622;

	bool enabled = true;
	bool debugLog = false;
	bool installed = false;
	cISC4City* city = nullptr;
	cRZAutoRefCount<cIGZCheatCodeManager> cheatManager;

	// ---------------------------------------------------------------- SimCity 4 1.1.641

	// cSC4Lot vtable (0x00AB19B0): slot 52 GetAvailableCapacity, slot 61 GetOccupancy.
	constexpr uintptr_t kLotVtableGetAvailableCapacity = 0x00AB1A80;
	constexpr uintptr_t kOriginalGetAvailableCapacity = 0x006C0640;
	constexpr uintptr_t kLotVtableGetOccupancy = 0x00AB1AA4;
	constexpr uintptr_t kOriginalGetOccupancy = 0x006C0CF0;

	// cSC4DemandSimulator::PerformCensus(cellX, cellZ) and its two callers (in 0x006A7020).
	constexpr uintptr_t kPerformCensus = 0x006A5A00;
	constexpr uintptr_t kPerformCensusCallSites[] = { 0x006A7475, 0x006A74AB };

	// Building development simulator pointer, used by every cSC4Lot capacity function.
	constexpr uintptr_t kBuildingDevelopmentSimulatorPtr = 0x00B43D68;
	// map<uint32_t, float>::operator[] of the demand census (cSC4DemandSimulator + 0x1C).
	constexpr uintptr_t kCensusMapSubscript = 0x006A3170;
	// float constants read by PerformCensus and UpdatePopulation.
	constexpr uintptr_t kDesirabilityScale = 0x00A823A4;   // 0.01: desirability -> 0..1
	constexpr uintptr_t kResidentialBandScale = 0x00AB0E84; // 0.01996: 0..255 -> census band 1..4

	// cSC4Lot vtable slots.
	constexpr size_t kLotGetLocation = 21;
	constexpr size_t kLotGetState = 33;
	constexpr size_t kLotHasCapacity = 38;
	constexpr size_t kLotGetOccupantWealth = 41;
	constexpr size_t kLotGetBuildingType = 46;
	constexpr size_t kLotGetCapacity = 50;            // (DeveloperType, bool)
	constexpr size_t kLotGetPopulation = 53;
	// cSC4BuildingDevelopmentSimulator vtable slots.
	constexpr size_t kBdsGetDeveloper = 32;           // (DeveloperType)
	constexpr size_t kBdsGetBuildingProfile = 39;
	// cSC4GrowthDeveloper / cSC4SimGrid<int16_t> / cSC4LotManager vtable slots.
	constexpr size_t kGrowthDevGetDesirabilityMap = 13;
	constexpr size_t kGridGetCellValue = 7;
	constexpr size_t kLotManagerGetLot = 6;           // (cellX, cellZ, bool)
	constexpr size_t kLotManagerGetLotsInCellRect = 7;
	// Object at cSC4DemandSimulator + 0x4C, used by the residential branch of PerformCensus.
	constexpr size_t kResidentialShareSlot = 30;      // SC4Percentage (cellX, cellZ), returned by hidden pointer
	constexpr size_t kResidentialBandSlot = 35;       // float (cellX, cellZ)

	// cSC4DemandSimulator members read by PerformCensus.
	constexpr size_t kDemandCensusMap = 0x1C;
	constexpr size_t kDemandBuildingDevSim = 0x3C;
	constexpr size_t kDemandLotManager = 0x40;
	constexpr size_t kDemandResidentialSim = 0x4C;

	constexpr uint8_t kLotStateOccupied = 3;
	constexpr uint8_t kLotStateVacant = 4;

	template <typename R, typename... Args>
	R VCall(const void* object, size_t slot, Args... args)
	{
		using Fn = R(__thiscall*)(const void*, Args...);
		const Fn* const vtable = *static_cast<const Fn* const*>(object);
		return vtable[slot](object, args...);
	}

	// ---------------------------------------------------------------- building profile (STLport layout)

	struct RbNode
	{
		uint32_t color;
		RbNode* parent;
		RbNode* left;
		RbNode* right;
		uint32_t key;    // DeveloperType
		uint16_t value;  // capacity
	};

	struct CapacityMap   // map<DeveloperType, uint16_t>
	{
		RbNode* header;  // header->parent is the root
		uint32_t count;
		uint32_t compare;
	};

	struct BuildingProfile
	{
		uint32_t purpose;
		uint32_t wealth;
		uint16_t currentOccupantCapacity;
		CapacityMap* groupsBegin;      // vector<map<DeveloperType, uint16_t>>
		CapacityMap* groupsEnd;
		CapacityMap* groupsCapacity;
		bool allowJointOccupancy;
	};
	// Natural x86 alignment gives the game's offsets: RbNode key 0x10 / value 0x14, BuildingProfile groups 0x0C /
	// joint occupancy flag 0x18 (offsetof is not a constant expression with the MSVC headers under clang-cl).
	static_assert(sizeof(CapacityMap) == 12);
	static_assert(sizeof(RbNode) == 0x18);
	static_assert(sizeof(BuildingProfile) == 0x1C);

	template <typename F>
	void ForEachNode(const RbNode* node, F&& f)
	{
		// The trees are tiny (one entry per Capacity Satisfied pair): plain recursion.
		if (!node) return;
		ForEachNode(node->left, f);
		f(node->key, node->value);
		ForEachNode(node->right, f);
	}

	template <typename F>
	void ForEachCapacity(const CapacityMap& map, F&& f)
	{
		if (map.header) ForEachNode(map.header->parent, f);
	}

	template <typename F>
	void ForEachCapacity(const BuildingProfile& profile, F&& f)
	{
		for (const CapacityMap* group = profile.groupsBegin; group && group < profile.groupsEnd; ++group)
		{
			ForEachCapacity(*group, f);
		}
	}

	enum class OccupantClass { None, Residential, Commercial, Industrial };

	OccupantClass ClassOf(uint32_t developerType)
	{
		switch (developerType & 0xF000)
		{
		case 0x1000: return OccupantClass::Residential;
		case 0x3000: return OccupantClass::Commercial;
		case 0x4000: return OccupantClass::Industrial;
		default: return OccupantClass::None;
		}
	}

	OccupantClass ClassOfPurpose(uint32_t purpose)
	{
		switch (purpose)
		{
		case 1: return OccupantClass::Residential;        // Residence
		case 2: case 3: return OccupantClass::Commercial; // Services, Office
		default: return OccupantClass::None;
		}
	}

	bool IsMixed(const BuildingProfile& profile)
	{
		if (!profile.allowJointOccupancy || ClassOfPurpose(profile.purpose) == OccupantClass::None)
		{
			return false;
		}
		bool residential = false, commercial = false, industrial = false;
		ForEachCapacity(profile, [&](uint32_t type, uint16_t capacity)
		{
			if (capacity == 0) return;
			switch (ClassOf(type))
			{
			case OccupantClass::Residential: residential = true; break;
			case OccupantClass::Commercial: commercial = true; break;
			case OccupantClass::Industrial: industrial = true; break;
			default: break;
			}
		});
		return residential && commercial && !industrial;
	}

	void* BuildingDevelopmentSimulator()
	{
		return *reinterpret_cast<void**>(kBuildingDevelopmentSimulatorPtr);
	}

	// The profile of the lot's building, as cSC4Lot::GetCapacity reads it; nullptr without a building.
	const BuildingProfile* GetProfile(void* lot, void* bds)
	{
		if (!lot || !bds) return nullptr;
		const uint32_t buildingType = VCall<uint32_t>(lot, kLotGetBuildingType, true);
		if (buildingType == 0) return nullptr;
		return VCall<const BuildingProfile*>(bds, kBdsGetBuildingProfile, buildingType);
	}

	const BuildingProfile* GetMixedProfile(void* lot, void* bds)
	{
		const BuildingProfile* profile = GetProfile(lot, bds);
		return profile && IsMixed(*profile) ? profile : nullptr;
	}

	uint16_t GetPopulation(void* lot, uint32_t type)
	{
		return VCall<uint16_t>(lot, kLotGetPopulation, type);
	}

	// Desirability of the developer of `type` at the cell, 0..1, as PerformCensus and UpdatePopulation scale it.
	bool GetDesirability(void* bds, uint32_t type, int32_t cellX, int32_t cellZ, float& result)
	{
		void* const developer = VCall<void*>(bds, kBdsGetDeveloper, type);
		void* const grid = developer ? VCall<void*>(developer, kGrowthDevGetDesirabilityMap) : nullptr;
		if (!grid) return false;
		const int16_t value = VCall<int16_t>(grid, kGridGetCellValue, cellX, cellZ);
		const float scale = *reinterpret_cast<const float*>(kDesirabilityScale);
		result = std::clamp(static_cast<float>(value) * scale, 0.0f, 1.0f);
		return true;
	}

	// ---------------------------------------------------------------- hook 1: GetAvailableCapacity

	using GetAvailableCapacityFn = uint16_t(__thiscall*)(void* lot, uint32_t type, bool force);
	GetAvailableCapacityFn originalGetAvailableCapacity = nullptr;

	uint16_t __fastcall Hook_GetAvailableCapacity(void* lot, void* /*edx*/, uint32_t type, bool force)
	{
		const BuildingProfile* const profile = GetMixedProfile(lot, BuildingDevelopmentSimulator());
		if (!profile)
		{
			return originalGetAvailableCapacity(lot, type, force);
		}

		// Same state test as the game: occupied, or occupied/vacant when forced.
		const uint8_t state = static_cast<uint8_t>(VCall<uint32_t>(lot, kLotGetState));
		if (force ? (state != kLotStateOccupied && state != kLotStateVacant) : state != kLotStateOccupied)
		{
			return 0;
		}

		// The group that holds this type; the game compares types only within one group.
		for (const CapacityMap* group = profile->groupsBegin; group < profile->groupsEnd; ++group)
		{
			bool found = false;
			uint16_t capacity = 0;
			ForEachCapacity(*group, [&](uint32_t t, uint16_t c) { if (t == type) { found = true; capacity = c; } });
			if (!found) continue;

			// The other class never reduces the room. Within one class the game's rule without joint occupancy
			// applies: the wealth levels of a class (R$ / R$$ / R$$$, CS$ / CS$$ / ...) are exclusive, the first
			// one to move in takes the building and the others see no room, as in the unmodified building.
			const OccupantClass occupantClass = ClassOf(type);
			bool taken = false;
			ForEachCapacity(*group, [&](uint32_t t, uint16_t c)
			{
				if (t != type && c != 0 && ClassOf(t) == occupantClass && GetPopulation(lot, t) != 0)
				{
					taken = true;
				}
			});
			return taken ? 0 : capacity;
		}
		return 0;
	}

	// ---------------------------------------------------------------- hook 2: GetOccupancy

	// Returns an SC4Percentage (one float) through a hidden pointer.
	using GetOccupancyFn = float*(__thiscall*)(void* lot, float* result);
	GetOccupancyFn originalGetOccupancy = nullptr;

	float* __fastcall Hook_GetOccupancy(void* lot, void* /*edx*/, float* result)
	{
		const BuildingProfile* const profile = GetMixedProfile(lot, BuildingDevelopmentSimulator());
		if (profile)
		{
			const OccupantClass primary = ClassOfPurpose(profile->purpose);
			uint32_t population = 0, capacity = 0;
			ForEachCapacity(*profile, [&](uint32_t t, uint16_t c)
			{
				if (ClassOf(t) == primary && c != 0)
				{
					population += GetPopulation(lot, t);
					capacity += c;
				}
			});
			if (capacity != 0)
			{
				*result = std::min(1.0f, static_cast<float>(population) / static_cast<float>(capacity));
				return result;
			}
		}
		return originalGetOccupancy(lot, result);
	}

	// ---------------------------------------------------------------- hook 3: PerformCensus

	using PerformCensusFn = void(__thiscall*)(void* demandSimulator, int32_t cellX, int32_t cellZ);
	using CensusSubscriptFn = float*(__thiscall*)(void* map, const uint32_t* key);
	const PerformCensusFn originalPerformCensus = reinterpret_cast<PerformCensusFn>(kPerformCensus);

	template <typename T>
	T Member(void* object, size_t offset)
	{
		return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
	}

	void AddToCensus(void* demandSimulator, uint32_t key, float value)
	{
		void* const map = static_cast<uint8_t*>(demandSimulator) + kDemandCensusMap;
		float* const entry = reinterpret_cast<CensusSubscriptFn>(kCensusMapSubscript)(map, &key);
		if (entry) *entry += value;
	}

	void __fastcall Hook_PerformCensus(void* demandSimulator, void* /*edx*/, int32_t cellX, int32_t cellZ)
	{
		originalPerformCensus(demandSimulator, cellX, cellZ);

		void* const lotManager = Member<void*>(demandSimulator, kDemandLotManager);
		void* const bds = Member<void*>(demandSimulator, kDemandBuildingDevSim);
		void* const lot = lotManager ? VCall<void*>(lotManager, kLotManagerGetLot, cellX, cellZ, true) : nullptr;
		if (!lot || !VCall<bool>(lot, kLotHasCapacity))
		{
			return;
		}
		const BuildingProfile* const profile = GetMixedProfile(lot, bds);
		if (!profile)
		{
			return;
		}

		// Same formulas as the two branches of PerformCensus, for the types the game skipped.
		const OccupantClass primary = ClassOfPurpose(profile->purpose);
		ForEachCapacity(*profile, [&](uint32_t type, uint16_t)
		{
			const OccupantClass occupantClass = ClassOf(type);
			if (occupantClass == primary || occupantClass == OccupantClass::None) return;

			float desirability = 0.0f;
			if (!GetDesirability(bds, type, cellX, cellZ, desirability)) return;
			const uint16_t capacity = VCall<uint16_t>(lot, kLotGetCapacity, type, false);
			const float weighted = static_cast<float>(capacity) * desirability;

			if (occupantClass == OccupantClass::Commercial)
			{
				// Job branch: census[developer type + 1] += capacity x desirability.
				AddToCensus(demandSimulator, type + 1, weighted);
			}
			else if (occupantClass == OccupantClass::Residential)
			{
				// Residential branch.
				void* const residentialSim = Member<void*>(demandSimulator, kDemandResidentialSim);
				if (!residentialSim) return;
				float shareStorage = 0.0f;
				const float* const share = VCall<float*>(residentialSim, kResidentialShareSlot, &shareStorage, cellX, cellZ);
				const float bandValue = VCall<float>(residentialSim, kResidentialBandSlot, cellX, cellZ);
				const uint8_t bandByte = static_cast<uint8_t>(static_cast<int32_t>(bandValue));
				const float bandScale = *reinterpret_cast<const float*>(kResidentialBandScale);
				const int32_t band = std::clamp(static_cast<int32_t>(bandByte * bandScale + 1.0f), 1, 4);
				const uint32_t wealth = (type >> 4) & 0xF; // 0x1010 -> 1, 0x1020 -> 2, 0x1030 -> 3

				AddToCensus(demandSimulator, ((wealth + 0x210) << 4) + band, weighted * (share ? *share : 0.0f));
				AddToCensus(demandSimulator, (wealth << 4) + 0x1001, weighted);
			}
		});
	}

	// ---------------------------------------------------------------- log report (read only)

	// Writes every mixed lot with the population and capacity of each type. Nothing is changed: the game's
	// UpdatePopulation fills the secondary types (see the header comment).
	void ReportMixedLots(SimLog::Level level)
	{
		void* const bds = BuildingDevelopmentSimulator();
		cISC4LotManager* const lotManager = city ? city->GetLotManager() : nullptr;
		if (!bds || !lotManager)
		{
			return;
		}

		const int32_t maxX = static_cast<int32_t>(city->CellCountX()) - 1;
		const int32_t maxZ = static_cast<int32_t>(city->CellCountZ()) - 1;
		SC4List<cISC4Lot*> lots;
		VCall<bool>(lotManager, kLotManagerGetLotsInCellRect, &lots, 0, 0, maxX, maxZ, true);

		uint32_t mixedLots = 0, filledSecondary = 0, emptySecondary = 0;
		for (cISC4Lot* lot : lots)
		{
			const BuildingProfile* const profile = lot ? GetMixedProfile(lot, bds) : nullptr;
			if (!profile)
			{
				continue;
			}
			++mixedLots;

			int32_t cellX = 0, cellZ = 0;
			VCall<bool>(lot, kLotGetLocation, &cellX, &cellZ);
			const uint32_t state = VCall<uint32_t>(lot, kLotGetState) & 0xFF;
			const uint32_t lotWealth = VCall<uint32_t>(lot, kLotGetOccupantWealth) & 0xFF;
			const OccupantClass primary = ClassOfPurpose(profile->purpose);
			SimLog::Write(level, "MixedOccupancy: lot at (%d, %d), building 0x%08X, purpose %u, wealth %u, state %u:",
				cellX, cellZ, VCall<uint32_t>(lot, kLotGetBuildingType, true), profile->purpose, lotWealth, state);

			ForEachCapacity(*profile, [&](uint32_t type, uint16_t)
			{
				const OccupantClass occupantClass = ClassOf(type);
				const uint16_t capacity = VCall<uint16_t>(lot, kLotGetCapacity, type, false);
				const uint16_t population = GetPopulation(lot, type);
				const bool secondary = occupantClass != primary;
				// UpdatePopulation only fills a lot whose occupant wealth equals the developer's wealth.
				const bool wealthMatches = ((type >> 4) & 0xF) == lotWealth;
				if (secondary)
				{
					++(population != 0 ? filledSecondary : emptySecondary);
				}
				SimLog::Write(level, "  type 0x%04X (%s): population %u / capacity %u%s", type,
					secondary ? "secondary" : "primary", population, capacity,
					secondary && !wealthMatches ? " - wealth differs from the lot's: the game never fills it" : "");
			});
		}
		SimLog::Write(level, "MixedOccupancy: %u mixed lot(s); secondary types with population: %u, empty: %u.",
			mixedLots, filledSecondary, emptySecondary);
	}

	// ---------------------------------------------------------------- patching

	bool WriteDword(uintptr_t address, uint32_t value)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(address), 4, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
		*reinterpret_cast<uint32_t*>(address) = value;
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), 4);
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<void*>(address), 4, oldProtect, &ignored);
		return true;
	}

	uint32_t CallTarget(uintptr_t site)
	{
		return static_cast<uint32_t>(site + 5 + *reinterpret_cast<const int32_t*>(site + 1));
	}

	bool IsCallTo(uintptr_t site, uintptr_t target)
	{
		return *reinterpret_cast<const uint8_t*>(site) == 0xE8 && CallTarget(site) == target;
	}

	bool PatchCall(uintptr_t site, const void* target)
	{
		return WriteDword(site + 1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) - (site + 5)));
	}

	bool Install()
	{
		const uint16_t version = VersionDetection::GetInstance().GetGameVersion();
		if (version != 641)
		{
			SimLog::Write(SimLog::Level::Error, "MixedOccupancy: requires game version 641, found %u: not installed.", version);
			return false;
		}

		// Every site must still hold the game's original code: another DLL may have patched it.
		bool ok = *reinterpret_cast<const uint32_t*>(kLotVtableGetAvailableCapacity) == kOriginalGetAvailableCapacity
			&& *reinterpret_cast<const uint32_t*>(kLotVtableGetOccupancy) == kOriginalGetOccupancy;
		for (uintptr_t site : kPerformCensusCallSites)
		{
			ok = ok && IsCallTo(site, kPerformCensus);
		}
		if (!ok)
		{
			SimLog::Write(SimLog::Level::Error,
				"MixedOccupancy: the game code differs from 1.1.641 (another DLL patches the same functions?): not installed.");
			return false;
		}

		originalGetAvailableCapacity = reinterpret_cast<GetAvailableCapacityFn>(kOriginalGetAvailableCapacity);
		originalGetOccupancy = reinterpret_cast<GetOccupancyFn>(kOriginalGetOccupancy);
		ok = WriteDword(kLotVtableGetAvailableCapacity, reinterpret_cast<uint32_t>(&Hook_GetAvailableCapacity))
			&& WriteDword(kLotVtableGetOccupancy, reinterpret_cast<uint32_t>(&Hook_GetOccupancy));
		for (uintptr_t site : kPerformCensusCallSites)
		{
			ok = ok && PatchCall(site, reinterpret_cast<const void*>(&Hook_PerformCensus));
		}
		if (!ok)
		{
			SimLog::Write(SimLog::Level::Error, "MixedOccupancy: VirtualProtect failed (error %lu).", GetLastError());
		}
		return ok;
	}

	void Uninstall()
	{
		WriteDword(kLotVtableGetAvailableCapacity, kOriginalGetAvailableCapacity);
		WriteDword(kLotVtableGetOccupancy, kOriginalGetOccupancy);
		for (uintptr_t site : kPerformCensusCallSites)
		{
			PatchCall(site, reinterpret_cast<const void*>(kPerformCensus));
		}
	}

	bool ParseBool(std::wstring value, bool fallback)
	{
		for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
		if (value == L"true" || value == L"1" || value == L"yes" || value == L"on") return true;
		if (value == L"false" || value == L"0" || value == L"no" || value == L"off") return false;
		return fallback;
	}
}

void MixedOccupancy::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[32]{};
	GetPrivateProfileStringW(L"MixedOccupancy", L"Enabled", L"true", value, 32, ini.c_str());
	enabled = ParseBool(value, true);
	GetPrivateProfileStringW(L"MixedOccupancy", L"DebugLog", L"false", value, 32, ini.c_str());
	debugLog = ParseBool(value, false);
}

void MixedOccupancy::PostAppInit()
{
	if (!enabled)
	{
		SimLog::Write(SimLog::Level::Info, "MixedOccupancy: disabled in SC4SIM.ini.");
		return;
	}
	if (!Install())
	{
		return;
	}
	installed = true;

	cISC4AppPtr app;
	if (cIGZCheatCodeManager* cheats = app ? app->GetCheatCodeManager() : nullptr)
	{
		if (cheats->RegisterCheatCode(kMixedOccupancyCheatID, cRZBaseString("MixedOccupancy")))
		{
			cheatManager = cheats;
		}
	}
	SimLog::Write(SimLog::Level::Info,
		"MixedOccupancy: enabled (R + C joint occupancy: lot capacity, occupancy and census hooks, MixedOccupancy cheat).");
}

void MixedOccupancy::PostAppShutdown()
{
	if (cheatManager)
	{
		cheatManager->UnregisterCheatCode(kMixedOccupancyCheatID);
		cheatManager.Reset();
	}
	if (installed)
	{
		Uninstall();
		installed = false;
	}
	city = nullptr;
}

void MixedOccupancy::DoMessage(cIGZMessage2* message)
{
	if (!installed || !message)
	{
		return;
	}

	auto* const standardMessage = static_cast<cIGZMessage2Standard*>(message);
	switch (message->GetType())
	{
	case kMessagePostCityInit:
		city = static_cast<cISC4City*>(standardMessage->GetVoid1());
		break;
	case kMessagePreCityShutdown:
		city = nullptr;
		break;
	case kMessageSimNewMonth:
		if (debugLog)
		{
			ReportMixedLots(SimLog::Level::Debug);
		}
		break;
	case kMessageCheatIssued:
		if (static_cast<uint32_t>(standardMessage->GetData1()) == kMixedOccupancyCheatID)
		{
			ReportMixedLots(SimLog::Level::Info);
		}
		break;
	}
}
