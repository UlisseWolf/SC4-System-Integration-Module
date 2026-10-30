////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: mixed occupancy (R, C and I on one lot: RC, RI, CI or RCI).
//
// A building is "mixed" when its exemplar has Allow Joint Occupancy (0x27812836) = true, its Purpose is
// Residence, Services, Office, Agriculture, Processing, Manufacturing or High Tech, and its Capacity Satisfied
// (0x27812834) lists types of at least two classes: residential (R$, R$$, R$$$), commercial (CS$, CS$$, CS$$$,
// CO$$, CO$$$) and industrial (I-A, I-D, I-M, I-HT). Every other building keeps the game's behaviour.
//
// For mixed lots the module:
//  1. replaces cSC4Lot::GetAvailableCapacity (vtable slot 52): the capacities of the classes are independent;
//     within one class the types stay exclusive, as in a building without joint occupancy;
//  2. replaces cSC4Lot::GetOccupancy (vtable slot 61): occupancy counts only the class of the building's
//     own purpose, so it never exceeds 100% and the secondary class cannot make the building decline;
//  3. wraps cSC4DemandSimulator::PerformCensus (both call sites) and adds the secondary types to the
//     census with the game's residential or job formula, so they count towards the RCI demand;
//  4. replaces cISC4TrafficSource::UpdateTrafficVolume of the lot (vtable 0x00AB1914, slot 4) for lots whose
//     travel population has a class other than their zone's: residents commute and industry ships freight
//     whatever the zone (see "hook 4" below);
//  5. extends cSC4Lot::GetJobs (vtable slot 64) with the secondary commercial and industrial jobs, so they reach
//     the lot's maximum trip capacity and commuters can end their trips there (see "hook 5" below).
// MixedTraffic=false in SC4SIM.ini turns 4 and 5 off and keeps the game's trips.
//
// The secondary types are filled by the game itself: cSC4GrowthDeveloper::DoIdle (0x006B5F20) calls
// cSC4TractDeveloper::UpdatePopulation (0x00706BC0, call at 0x006B6A65) for every growth developer and every
// tract. UpdatePopulation fills any lot whose occupant wealth equals the developer's wealth, asking the room
// through vtable slot 52, so hook 1 is enough for the CS$$ developer to fill the CS$$ jobs of an R$$ lot, with
// the game's random spread and schedule. A secondary type therefore needs the building's own wealth; the
// wealth of an industrial type is fixed by the game (WealthOf below).
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
#include <cmath>
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

	// Only the twelve developer types of the game (GetDeveloperType, 0x00695790) have a class.
	OccupantClass ClassOf(uint32_t developerType)
	{
		switch (developerType)
		{
		case 0x1010: case 0x1020: case 0x1030:
			return OccupantClass::Residential;
		case 0x3110: case 0x3120: case 0x3130: case 0x3320: case 0x3330:
			return OccupantClass::Commercial;
		case 0x4100: case 0x4200: case 0x4300: case 0x4400:
			return OccupantClass::Industrial;
		default:
			return OccupantClass::None;
		}
	}

	OccupantClass ClassOfPurpose(uint32_t purpose)
	{
		switch (purpose)
		{
		case 1: return OccupantClass::Residential;                        // Residence
		case 2: case 3: return OccupantClass::Commercial;                 // Services, Office
		case 5: case 6: case 7: case 8: return OccupantClass::Industrial; // Agriculture, Processing, Manufacturing, High Tech
		default: return OccupantClass::None;                              // Tourism, Other
		}
	}

	// The wealth the game gives a developer type (0x00695840): the nibble for R and C; fixed for industry.
	uint32_t WealthOf(uint32_t developerType)
	{
		switch (developerType)
		{
		case 0x4100: return 1;           // agriculture
		case 0x4200: case 0x4300: return 2; // processing (dirty), manufacturing
		case 0x4400: return 3;           // high tech
		default: return (developerType >> 4) & 0xF;
		}
	}

	// At least two classes among R, C and I, nothing else, and a purpose of one of those classes.
	bool IsMixed(const BuildingProfile& profile)
	{
		if (!profile.allowJointOccupancy || ClassOfPurpose(profile.purpose) == OccupantClass::None)
		{
			return false;
		}
		bool residential = false, commercial = false, industrial = false, other = false;
		ForEachCapacity(profile, [&](uint32_t type, uint16_t capacity)
		{
			if (capacity == 0) return;
			switch (ClassOf(type))
			{
			case OccupantClass::Residential: residential = true; break;
			case OccupantClass::Commercial: commercial = true; break;
			case OccupantClass::Industrial: industrial = true; break;
			default: other = true; break;
			}
		});
		return !other && (int(residential) + int(commercial) + int(industrial)) >= 2;
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

			if (occupantClass == OccupantClass::Commercial || occupantClass == OccupantClass::Industrial)
			{
				// Job branch (Services, Office and the four industries): census[developer type + 1] += capacity x desirability.
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
				const uint32_t wealth = WealthOf(type); // 0x1010 -> 1, 0x1020 -> 2, 0x1030 -> 3

				AddToCensus(demandSimulator, ((wealth + 0x210) << 4) + band, weighted * (share ? *share : 0.0f));
				AddToCensus(demandSimulator, (wealth << 4) + 0x1001, weighted);
			}
		});
	}

	// ---------------------------------------------------------------- hook 4: UpdateTrafficVolume (trips)
	//
	// cSC4Lot implements cISC4TrafficSource at lot + 0x1C (vtable 0x00AB1914). Slot 4, UpdateTrafficVolume
	// (0x006C27E0), turns the lot's travel population into trips, and chooses the trips by the lot's ZONE:
	//  * residential zone: commuters (trip type 0, 1 or 2 = R$, R$$, R$$$) from the travel population of the first
	//    R type that has one, n = max(1, floor(population x share)), spread over the 3 job wealths;
	//  * industrial zone: freight (trip type 3), n = ceil((I-A + I-D + I-M + I-HT) x freight factor), only with road
	//    access and without a travel designate;
	//  * commercial zone: no trips.
	// A lot keeps trips of one type only: the update erases the other types. The travel population itself
	// (cSC4Lot::AddTravelPopulation, 0x006C0BA0) already holds every type of the lot, secondary ones included.
	//
	// The hook keeps the original for every lot whose travel population only has the class of its zone. For the
	// others (residents in a C or I lot, industry in an R or C lot, or a designate that collects them) it builds
	// commuters and freight with the game's formulas and reconciles the trip list per trip type, with the game's
	// algorithm, without erasing the other type.

	constexpr uintptr_t kTrafficSourceVtableUpdateTrafficVolume = 0x00AB1924;
	constexpr uintptr_t kOriginalUpdateTrafficVolume = 0x006C27E0;
	constexpr uintptr_t kGameOperatorNew = 0x0090CF54;      // cdecl void* (size_t)
	constexpr uintptr_t kGameOperatorDelete = 0x0090CF63;   // cdecl void (void*)
	constexpr uintptr_t kTripListPushFront = 0x006C1E10;    // thiscall (list*, const cSC4TripData*), copies the data
	constexpr uintptr_t kTripListErase = 0x006C0FA0;        // thiscall (list*, node** out, node* it): *out = next
	constexpr uintptr_t kTripListClear = 0x006C0FF0;        // thiscall (list*)
	constexpr uintptr_t kCommuteShareSimPtr = 0x00B43DA4;   // object whose slot 30 gives the commuting share of a cell
	constexpr uintptr_t kTripRatesSimPtr = 0x00B43DA8;      // slot 26: job wealth split; slot 27: freight factor
	constexpr uintptr_t kPercentScale = 0x00A80AA0;         // 100.0

	constexpr size_t kTrafficSourceOffsetInLot = 0x1C;
	constexpr size_t kTrafficSourceTripList = 0x68;          // SC4List<cSC4TripData> (lot + 0x84)
	constexpr size_t kTrafficSourceRandomByte = 0x6C;        // per-lot byte used to pick one job wealth (lot + 0x88)

	constexpr size_t kLotHasRoadAccess = 28;
	constexpr size_t kLotGetZoneType = 31;
	constexpr size_t kLotIsInLivableState = 37;
	constexpr size_t kLotGetTravelPopulation = 56;
	constexpr size_t kLotGetTravelDesignate = 59;
	constexpr size_t kCommuteShareSlot = 30;                 // SC4Percentage (cellX, cellZ), hidden result pointer
	constexpr size_t kJobWealthSplitSlot = 26;               // (wealth 1..3, vector<float>* of 3)
	constexpr size_t kFreightFactorSlot = 27;                // float ()

	constexpr uint8_t kTripTypeFreight = 3;
	constexpr uint8_t kTripFlagPlaceholder = 2;             // cSC4TripData + 0x1E
	constexpr uint8_t kTripStatePending = 4;                // cSC4TripData + 0x1F
	constexpr uint8_t kTripStatePending2 = 5;

	// cSC4TripData (0x2C bytes): two vectors, then the trip fields. The list node is {next, prev, data}.
	struct TripData
	{
		uint32_t vector1[3];
		uint32_t vector2[3];
		uint32_t count;     // + 0x18
		uint8_t type;       // + 0x1C: 0..2 commuters of R$..R$$$, 3 freight
		uint8_t wealth;     // + 0x1D: job wealth index 0..2
		uint8_t flags;      // + 0x1E
		uint8_t state;      // + 0x1F
		uint16_t word20;
		uint16_t word22;
		uint32_t dword24;
		uint32_t dword28;
	};
	static_assert(sizeof(TripData) == 0x2C);

	struct TripNode
	{
		TripNode* next;
		TripNode* prev;
		TripData data;
	};

	struct TripList
	{
		TripNode* header;
	};

	using UpdateTrafficVolumeFn = bool(__thiscall*)(void* trafficSource);
	UpdateTrafficVolumeFn originalUpdateTrafficVolume = nullptr;
	bool mixedTraffic = true;

	void* GameNew(size_t size) { return reinterpret_cast<void*(__cdecl*)(size_t)>(kGameOperatorNew)(size); }
	void GameDelete(void* p) { reinterpret_cast<void(__cdecl*)(void*)>(kGameOperatorDelete)(p); }

	TripNode* EraseTrip(TripList* list, TripNode* node)
	{
		TripNode* next = nullptr;
		reinterpret_cast<TripNode**(__thiscall*)(TripList*, TripNode**, TripNode*)>(kTripListErase)(list, &next, node);
		return next;
	}

	void AddTrip(TripList* list, uint8_t type, uint8_t wealth, uint32_t count, uint8_t flags, uint8_t state)
	{
		TripData data{};
		data.count = count;
		data.type = type;
		data.wealth = wealth;
		data.flags = flags;
		data.state = state;
		reinterpret_cast<void(__thiscall*)(TripList*, const TripData*)>(kTripListPushFront)(list, &data);
	}

	uint16_t TravelPopulation(void* lot, uint32_t type)
	{
		return VCall<uint16_t>(lot, kLotGetTravelPopulation, type);
	}

	// The game's job wealth split for commuters of `residentWealth` (1..3): three fractions.
	void GetJobWealthSplit(uint32_t residentWealth, float split[3])
	{
		split[0] = split[1] = split[2] = 0.0f;
		void* const rates = *reinterpret_cast<void**>(kTripRatesSimPtr);
		if (!rates) return;
		// A game vector<float> of three zeros, allocated with the game's allocator like the original.
		struct { float* begin; float* end; float* capacity; } vec{};
		vec.begin = static_cast<float*>(GameNew(3 * sizeof(float)));
		if (!vec.begin) return;
		vec.begin[0] = vec.begin[1] = vec.begin[2] = 0.0f;
		vec.end = vec.capacity = vec.begin + 3;
		VCall<void>(rates, kJobWealthSplitSlot, residentWealth, static_cast<void*>(&vec));
		if (vec.begin && vec.end - vec.begin >= 3)
		{
			split[0] = vec.begin[0];
			split[1] = vec.begin[1];
			split[2] = vec.begin[2];
		}
		if (vec.begin) GameDelete(vec.begin);
	}

	// Commuter trips per job wealth, as UpdateTrafficVolume computes them. Returns the trip type (0..2) or -1.
	int ComputeCommuters(void* lot, void* trafficSource, uint16_t trips[3])
	{
		static constexpr uint32_t kResidentTypes[3] = { 0x1010, 0x1020, 0x1030 };
		uint16_t population = 0;
		int tripType = -1;
		for (int i = 0; i < 3 && population == 0; ++i)
		{
			population = TravelPopulation(lot, kResidentTypes[i]);
			if (population != 0) tripType = i;
		}
		if (tripType < 0) return -1;

		void* const shareSim = *reinterpret_cast<void**>(kCommuteShareSimPtr);
		if (!shareSim) return -1;
		int32_t cellX = 0, cellZ = 0;
		VCall<bool>(lot, kLotGetLocation, &cellX, &cellZ);
		float shareStorage = 0.0f;
		const float* const share = VCall<float*>(shareSim, kCommuteShareSlot, &shareStorage, cellX, cellZ);
		const double commuters = static_cast<double>(population) * (share ? *share : 0.0f);
		const uint32_t n = std::max<uint32_t>(1, static_cast<uint32_t>(std::floor(commuters)));
		const uint16_t count = static_cast<uint16_t>(std::min<uint32_t>(n, 0xFFFF));

		float split[3];
		GetJobWealthSplit(static_cast<uint32_t>(tripType + 1), split);
		if (count < 50)
		{
			// Few commuters: all of them to one job wealth, picked with the lot's own random byte.
			const uint8_t pick = *(static_cast<uint8_t*>(trafficSource) + kTrafficSourceRandomByte);
			const float scale = *reinterpret_cast<const float*>(kPercentScale);
			uint8_t cumulative = 1;
			for (int i = 0; i < 3; ++i)
			{
				cumulative = static_cast<uint8_t>(cumulative + static_cast<uint8_t>(std::lround(split[i] * scale)));
				if (pick < cumulative)
				{
					trips[i] = count;
					break;
				}
			}
		}
		else
		{
			for (int i = 0; i < 3; ++i)
			{
				trips[i] = static_cast<uint16_t>(std::lround(static_cast<double>(count) * split[i]));
			}
		}
		return tripType;
	}

	// Freight trips, as UpdateTrafficVolume computes them for an industrial lot (all on job wealth index 2).
	bool ComputeFreight(void* lot, uint16_t trips[3])
	{
		if (!VCall<bool>(lot, kLotHasRoadAccess) || VCall<void*>(lot, kLotGetTravelDesignate) != nullptr)
		{
			return false;
		}
		const uint32_t industry = TravelPopulation(lot, 0x4400) + TravelPopulation(lot, 0x4300)
			+ TravelPopulation(lot, 0x4200) + TravelPopulation(lot, 0x4100);
		if (industry == 0) return false;
		void* const rates = *reinterpret_cast<void**>(kTripRatesSimPtr);
		if (!rates) return false;
		const float factor = VCall<float>(rates, kFreightFactorSlot);
		const double freight = std::ceil(static_cast<double>(industry) * factor);
		trips[2] = static_cast<uint16_t>(std::clamp(freight, 0.0, 65535.0));
		return trips[2] != 0;
	}

	// The reconcile step of UpdateTrafficVolume for one trip type, leaving the other types alone.
	void ReconcileTrips(TripList* list, uint8_t type, const uint16_t trips[3])
	{
		TripNode* const end = list->header;
		for (uint8_t wealth = 0; wealth < 3; ++wealth)
		{
			const uint32_t wanted = trips[wealth];
			uint32_t have = 0;
			for (TripNode* node = end->next; node != end; node = node->next)
			{
				if (node->data.type == type && node->data.wealth == wealth) have += node->data.count;
			}

			if (have < wanted)
			{
				uint32_t missing = wanted - have;
				bool found = false;
				for (TripNode* node = end->next; node != end && missing != 0; node = node->next)
				{
					if (node->data.type != type || node->data.wealth != wealth) continue;
					found = true;
					if (node->data.state == kTripStatePending || node->data.state == kTripStatePending2)
					{
						node->data.count += missing;
						missing = 0;
					}
				}
				if (missing != 0)
				{
					AddTrip(list, type, wealth, missing, 0, found ? kTripStatePending : 0);
				}
			}
			else if (have > wanted)
			{
				uint32_t excess = have - wanted;
				// First the trips that wait for a route (state 4), then any trip of this wealth.
				for (int pass = 0; pass < 2 && excess != 0; ++pass)
				{
					for (TripNode* node = end->next; node != end && excess != 0;)
					{
						if (node->data.type != type || node->data.wealth != wealth
							|| (pass == 0 && node->data.state != kTripStatePending))
						{
							node = node->next;
							continue;
						}
						if (node->data.count > excess)
						{
							node->data.count -= excess;
							excess = 0;
							node = node->next;
						}
						else
						{
							excess -= node->data.count;
							node = EraseTrip(list, node);
						}
					}
				}
			}
		}
	}

	void UpdateMixedTraffic(void* lot, void* trafficSource, bool residentialZone)
	{
		TripList* const list = reinterpret_cast<TripList*>(static_cast<uint8_t*>(trafficSource) + kTrafficSourceTripList);
		if (!list->header) return;

		uint16_t commuters[3] = {}, freight[3] = {};
		int commuterType = -1;
		bool hasFreight = false;
		if (VCall<bool>(lot, kLotIsInLivableState))
		{
			commuterType = ComputeCommuters(lot, trafficSource, commuters);
			hasFreight = ComputeFreight(lot, freight);
		}
		const bool anyCommuters = commuterType >= 0 && (commuters[0] | commuters[1] | commuters[2]) != 0;

		if (!anyCommuters && !hasFreight)
		{
			// No trips: the game keeps a single placeholder trip.
			TripNode* const first = list->header->next;
			if (first != list->header && (first->data.flags & kTripFlagPlaceholder))
			{
				return;
			}
			reinterpret_cast<void(__thiscall*)(TripList*)>(kTripListClear)(list);
			AddTrip(list, residentialZone ? 0 : kTripTypeFreight, 0, 1, kTripFlagPlaceholder, 0);
			return;
		}

		// Drop the placeholder and the trip types this lot no longer makes.
		for (TripNode* node = list->header->next; node != list->header;)
		{
			const uint8_t type = node->data.type;
			const bool keep = !(node->data.flags & kTripFlagPlaceholder)
				&& ((anyCommuters && type == commuterType) || (hasFreight && type == kTripTypeFreight));
			node = keep ? node->next : EraseTrip(list, node);
		}
		if (anyCommuters) ReconcileTrips(list, static_cast<uint8_t>(commuterType), commuters);
		if (hasFreight) ReconcileTrips(list, kTripTypeFreight, freight);
	}

	bool __fastcall Hook_UpdateTrafficVolume(void* trafficSource, void* /*edx*/)
	{
		void* const lot = static_cast<uint8_t*>(trafficSource) - kTrafficSourceOffsetInLot;
		if (mixedTraffic)
		{
			// Zone types 1-3 residential, 4-6 commercial, 7-9 industrial (byte table at 0x006C2E20).
			const uint32_t zone = VCall<uint32_t>(lot, kLotGetZoneType);
			const bool residentialZone = zone >= 1 && zone <= 3;
			const bool industrialZone = zone >= 7 && zone <= 9;
			if (residentialZone || industrialZone || (zone >= 4 && zone <= 6))
			{
				const bool residents = TravelPopulation(lot, 0x1010) || TravelPopulation(lot, 0x1020) || TravelPopulation(lot, 0x1030);
				const bool industry = TravelPopulation(lot, 0x4100) || TravelPopulation(lot, 0x4200)
					|| TravelPopulation(lot, 0x4300) || TravelPopulation(lot, 0x4400);
				// The game handles the class of the zone; anything else needs the mixed update.
				const bool extra = (residents && !residentialZone) || (industry && !industrialZone);
				if (extra)
				{
					UpdateMixedTraffic(lot, trafficSource, residentialZone);
					return true;
				}
			}
		}
		return originalUpdateTrafficVolume(trafficSource);
	}

	// ---------------------------------------------------------------- hook 5: GetJobs (commute destinations)
	//
	// A lot is a commute destination only while its maximum trip capacity (lot property 0x8A0B4100, indices 0-3)
	// has room for the commuter's wealth (cSC4PathFinder::AtGoal 0x006D7960, CheckDestinationCapacity 0x006D5D80).
	// cSC4Lot::UpdateJobs (0x006BF6A0) copies the lot's travel jobs into that maximum with the traffic simulator's
	// SetMaxTripCapacity, and the travel jobs come from cSC4Lot::GetJobs (vtable slot 64, 0x006BF800), which reads
	// the primary type only:
	//   jobs[1..3] = CalculateJobsPerUnitOfDemand(primary type + 1) x GetPopulation(primary type)
	// For mixed lots the hook calls the original, then adds the same product for each secondary commercial or
	// industrial type, so commuters can end their trips at the secondary jobs. Secondary residents add nothing:
	// they are commuters, not jobs (hook 4 makes their trips).

	constexpr uintptr_t kLotVtableGetJobs = 0x00AB1AB0;
	constexpr uintptr_t kOriginalGetJobs = 0x006BF800;
	constexpr uintptr_t kDemandSimulatorPtr = 0x00B43D74;
	constexpr size_t kDemandCalculateJobsPerUnit = 8;  // (float jobs[4], demand ID, demand index): writes jobs[1..3]

	using GetJobsFn = bool(__thiscall*)(void* lot, float* jobs);
	GetJobsFn originalGetJobs = nullptr;

	bool __fastcall Hook_GetJobs(void* lot, void* /*edx*/, float* jobs)
	{
		const bool result = originalGetJobs(lot, jobs);
		if (!mixedTraffic || !jobs) return result;

		const BuildingProfile* const profile = GetMixedProfile(lot, BuildingDevelopmentSimulator());
		void* const demand = *reinterpret_cast<void**>(kDemandSimulatorPtr);
		if (!profile || !demand) return result;

		const OccupantClass primary = ClassOfPurpose(profile->purpose);
		ForEachCapacity(*profile, [&](uint32_t type, uint16_t)
		{
			const OccupantClass occupantClass = ClassOf(type);
			if (occupantClass == primary || occupantClass == OccupantClass::Residential || occupantClass == OccupantClass::None)
			{
				return;
			}
			const uint16_t population = GetPopulation(lot, type);
			if (population == 0) return;
			float perUnit[4] = {};
			VCall<void>(demand, kDemandCalculateJobsPerUnit, perUnit, type + 1, 0u);
			for (int i = 1; i <= 3; ++i)
			{
				jobs[i] += perUnit[i] * static_cast<float>(population);
			}
		});
		return result;
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
				const bool wealthMatches = WealthOf(type) == lotWealth;
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
			&& *reinterpret_cast<const uint32_t*>(kLotVtableGetOccupancy) == kOriginalGetOccupancy
			&& *reinterpret_cast<const uint32_t*>(kTrafficSourceVtableUpdateTrafficVolume) == kOriginalUpdateTrafficVolume
			&& *reinterpret_cast<const uint32_t*>(kLotVtableGetJobs) == kOriginalGetJobs;
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
		originalUpdateTrafficVolume = reinterpret_cast<UpdateTrafficVolumeFn>(kOriginalUpdateTrafficVolume);
		originalGetJobs = reinterpret_cast<GetJobsFn>(kOriginalGetJobs);
		ok = WriteDword(kLotVtableGetAvailableCapacity, reinterpret_cast<uint32_t>(&Hook_GetAvailableCapacity))
			&& WriteDword(kLotVtableGetOccupancy, reinterpret_cast<uint32_t>(&Hook_GetOccupancy))
			&& WriteDword(kTrafficSourceVtableUpdateTrafficVolume, reinterpret_cast<uint32_t>(&Hook_UpdateTrafficVolume))
			&& WriteDword(kLotVtableGetJobs, reinterpret_cast<uint32_t>(&Hook_GetJobs));
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
		WriteDword(kTrafficSourceVtableUpdateTrafficVolume, kOriginalUpdateTrafficVolume);
		WriteDword(kLotVtableGetJobs, kOriginalGetJobs);
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
	GetPrivateProfileStringW(L"MixedOccupancy", L"MixedTraffic", L"true", value, 32, ini.c_str());
	mixedTraffic = ParseBool(value, true);
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
		"MixedOccupancy: enabled (R, C and I joint occupancy: lot capacity, occupancy, census and traffic hooks, "
		"MixedOccupancy cheat). Mixed traffic: %s.", mixedTraffic ? "on" : "off (MixedTraffic=false)");
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
