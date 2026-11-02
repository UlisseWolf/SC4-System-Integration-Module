////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: deathcare.
//
// The Afterlife counts the dead of every month and knows in which residential tract they died (the generation resets
// of the residential simulator; the other deaths are spread over the tracts by population). Deathcare gives those
// dead somewhere to go, in the spirit of Cities: Skylines, with SimCity 4's own service model (a radius and a
// capacity, like schools and hospitals) instead of vehicles:
//
//  * a cemetery (occupant group 0x1700 or 0x5C4DD101, or Deathcare Type 1) has a number of graves; every burial uses
//    one, and a full cemetery takes no one;
//  * a crematorium (occupant group 0x5C4DD100, or Deathcare Type 2) takes up to its capacity every month and never
//    fills;
//  * each month, in this order:
//     1. the dead of demolished cemeteries (relocation) go to any crematorium, then any cemetery, with room;
//     2. the dead of each tract, plus its uncollected dead of earlier months, go to the nearest crematorium in range
//        with room, then the nearest cemetery in range with free graves; the rest stays uncollected (at most
//        DEATHCARE_MAX_UNCOLLECTED_SHARE of the tract's residents);
//     3. the room crematoria have left empties cemeteries in their range that reached DEATHCARE_EMPTY_THRESHOLD; a
//        cemetery being emptied goes on until it is empty, fullest first;
//  * effects (DEATHCARE_EFFECTS): a tract with uncollected dead loses health quotient every month (the HQ grids of
//    the residential simulator, the same SetTractValue the game's monthly decay uses) and, with DEATHCARE_AURA, gets a
//    negative transient aura, which the Transient Aura data view of SC4DataViewExtensions shows as a map;
//  * notices, each at most once a year: uncollected dead (with their number and the worst neighborhood), cemeteries
//    nearly full, all cemeteries full;
//  * queries: before a deathcare building's query is shown (SC4QueryUIHooks), afterlife_grid_var.FACILITY_* hold
//    that building's own values (graves used, capacity, free, cremated and emptied this month).
//
// Capacity, radius and type come from the exemplar properties of New_Properties.xml-patches (0x5C4DD001-0x5C4DD003).
// A building without them gets DEATHCARE_GRAVES_PER_CELL graves per lot cell (cemetery) or
// DEATHCARE_CREMATION_PER_MONTH (crematorium), and DEATHCARE_RADIUS. Graves used, cemeteries being emptied,
// uncollected dead and the relocation backlog are saved with the Afterlife's totals.
//
// Checked in 1.1.641: cISC4ResidentialSimulator slot 24 (0x006F7300, GetHQGrids) has the same three AddRef'd out
// pointers as slot 25 (GetPopulationGrids); cISC4SimGrid GetTractValue is vtable +0x58 and SetTractValue +0x68, as used
// by the HQ decay in DoPopulationChange (0x006FA5D8). The transient aura grid is the one SC4DataViewExtensions reads
// (cISC4AuraSimulator::GetTransientAuraGrid).
//
////////////////////////////////////////////////////////////////////////////

#include "Deathcare.h"
#include "SimLog.h"
#include "SC4NotificationDialog.h"
#include "QueryUIHooks/cIBuildingQueryDialogHookTarget.h"
#include "QueryUIHooks/cIBuildingQueryHookServer.h"

#include "cIGZCOM.h"
#include "cIGZString.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4AuraSimulator.h"
#include "cISC4City.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantFilter.h"
#include "cISC4OccupantManager.h"
#include "cISC4ResidentialSimulator.h"
#include "cISC4SimGrid.h"
#include "cISCLua.h"
#include "cISCPropertyHolder.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cS3DVector3.h"
#include "GZServPtrs.h"
#include "SC4Rect.h"
#include "SCPropertyUtil.h"
#include "StringResourceKey.h"
#include "StringResourceManager.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
	constexpr uint32_t kOccupantTypeBuilding = 0x278128A0;
	constexpr float kCellSize = 16.0f;
	constexpr uint32_t kLTEXTGroup = 0x6A231EA4;
	// Notices: caption, message (the message may hold #count#, #x#, #z#).
	constexpr uint32_t kTextUncollectedCaption = 0xBB3C4000, kTextUncollectedMessage = 0xBB3C4001;
	constexpr uint32_t kTextFullCaption = 0xBB3C4002, kTextFullMessage = 0xBB3C4003;
	constexpr uint32_t kTextNearlyFullCaption = 0xBB3C4004, kTextNearlyFullMessage = 0xBB3C4005;

	struct Settings
	{
		bool enabled = true;                 // DEATHCARE
		bool effects = true;                 // DEATHCARE_EFFECTS
		bool aura = true;                    // DEATHCARE_AURA
		double gravesPerCell = 40;           // DEATHCARE_GRAVES_PER_CELL (cemetery without a capacity property)
		double cremationPerMonth = 50;       // DEATHCARE_CREMATION_PER_MONTH (crematorium without a capacity property)
		double radius = 1024;                // DEATHCARE_RADIUS (meters, building without a radius property)
		double maxUncollectedShare = 0.1;    // DEATHCARE_MAX_UNCOLLECTED_SHARE (of a tract's residents)
		double hqPenaltyPerPercent = 0.5;    // DEATHCARE_HQ_PENALTY_PER_PERCENT (HQ per month per 1% uncollected)
		double maxHqPenalty = 3;             // DEATHCARE_MAX_HQ_PENALTY (HQ per month)
		double auraPerPercent = 4;           // DEATHCARE_AURA_PER_PERCENT (transient aura per 1% uncollected)
		double maxAura = 60;                 // DEATHCARE_AURA_MAX (strongest negative transient aura, up to 127)
		double emptyThreshold = 0.8;         // DEATHCARE_EMPTY_THRESHOLD (fill at which crematoria start emptying)
		double notifyUncollected = 50;       // DEATHCARE_NOTIFY_UNCOLLECTED (0 = never)
		double notifyNearlyFull = 0.8;       // DEATHCARE_NOTIFY_NEARLY_FULL (fill of a cemetery; 0 = never)
	} settings;

	enum class Kind : uint32_t { None = 0, Cemetery = 1, Crematorium = 2 };

	struct Facility
	{
		Kind kind = Kind::None;
		int32_t cellX = 0, cellZ = 0;
		double capacity = 0;      // graves, or bodies per month
		double radiusCells = 0;
		double used = 0;          // graves used (cemetery) or bodies this month (crematorium)
		double taken = 0;         // dead taken this month (burials or cremations)
		double emptied = 0;       // bodies moved out of (cemetery) or into (crematorium) by emptying this month
		bool emptying = false;    // cemetery being emptied
	};

	uint32_t Key(int32_t x, int32_t z) { return (static_cast<uint32_t>(x & 0xFFFF) << 16) | static_cast<uint32_t>(z & 0xFFFF); }
	int32_t KeyX(uint32_t key) { return static_cast<int32_t>(key >> 16); }
	int32_t KeyZ(uint32_t key) { return static_cast<int32_t>(key & 0xFFFF); }

	cISC4City* city = nullptr;
	std::unordered_map<uint32_t, double> gravesUsed;   // cemetery cell key -> graves used
	std::unordered_set<uint32_t> emptying;             // cemetery cell keys being emptied
	std::unordered_map<uint32_t, double> uncollected;  // tract key -> uncollected dead
	double relocation = 0;                             // dead of demolished cemeteries waiting for a place
	std::unordered_map<uint32_t, Facility> lastMonth;  // facility cell key -> last month's values (queries)
	Deathcare::Totals totals;
	uint32_t monthsSince[3] = { 12, 12, 12 };          // uncollected, full, nearly full notices
	bool loggedFacilities = false;

	double Dist2(double ax, double az, double bx, double bz) { return (ax - bx) * (ax - bx) + (az - bz) * (az - bz); }

	// ---------------------------------------------------------------- facilities

	bool IsFacility(cISC4Occupant* occupant)
	{
		if (!occupant || occupant->GetType() != static_cast<int32_t>(kOccupantTypeBuilding)) return false;
		if (occupant->IsOccupantGroup(Deathcare::kOccupantGroupCemetery)
			|| occupant->IsOccupantGroup(Deathcare::kOccupantGroupDeathcareCemetery)
			|| occupant->IsOccupantGroup(Deathcare::kOccupantGroupCrematorium)) return true;
		uint32_t type = 0;
		const cISCPropertyHolder* holder = occupant->AsPropertyHolder();
		return holder && SCPropertyUtil::GetPropertyValue(holder, Deathcare::kPropertyType, type) && type != 0;
	}

	class DeathcareFilter final : public cISC4OccupantFilter
	{
	public:
		bool QueryInterface(uint32_t riid, void** ppvObj) override
		{
			if (riid == GZIID_cISC4OccupantFilter || riid == GZIID_cIGZUnknown)
			{
				*ppvObj = static_cast<cISC4OccupantFilter*>(this);
				AddRef();
				return true;
			}
			return false;
		}
		uint32_t AddRef() override { return ++refCount; }
		uint32_t Release() override { return refCount > 0 ? --refCount : 0; }
		bool IsOccupantIncluded(cISC4Occupant* occupant) override { return IsFacility(occupant); }
		bool IsOccupantTypeIncluded(uint32_t type) override { return type == kOccupantTypeBuilding; }
		bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return true; }
	private:
		uint32_t refCount = 0;
	};

	bool DescribeFacility(cISC4Occupant* occupant, Facility& facility)
	{
		const cISCPropertyHolder* holder = occupant->AsPropertyHolder();
		uint32_t type = 0;
		if (holder && SCPropertyUtil::GetPropertyValue(holder, Deathcare::kPropertyType, type) && type != 0)
		{
			facility.kind = type == 2 ? Kind::Crematorium : type == 1 ? Kind::Cemetery : Kind::None;
		}
		else
		{
			facility.kind = occupant->IsOccupantGroup(Deathcare::kOccupantGroupCrematorium) ? Kind::Crematorium : Kind::Cemetery;
		}
		if (facility.kind == Kind::None) return false;

		cS3DVector3 position;
		if (!occupant->GetPosition(&position)) return false;
		facility.cellX = static_cast<int32_t>(std::floor(position.fX / kCellSize));
		facility.cellZ = static_cast<int32_t>(std::floor(position.fZ / kCellSize));

		uint32_t capacity = 0;
		if (holder && SCPropertyUtil::GetPropertyValue(holder, Deathcare::kPropertyCapacity, capacity) && capacity > 0)
		{
			facility.capacity = capacity;
		}
		else if (facility.kind == Kind::Crematorium)
		{
			facility.capacity = settings.cremationPerMonth;
		}
		else
		{
			SC4Rect<long> cells;
			const double area = occupant->GetBoundingCityCells(cells)
				? std::max<double>(1, static_cast<double>(cells.bottomRightX - cells.topLeftX + 1)
					* static_cast<double>(cells.bottomRightY - cells.topLeftY + 1)) : 1;
			facility.capacity = std::floor(area * settings.gravesPerCell);
		}
		float radius = 0;
		facility.radiusCells = (holder && SCPropertyUtil::GetPropertyValue(holder, Deathcare::kPropertyRadius, radius) && radius > 0
			? radius : settings.radius) / kCellSize;
		return true;
	}

	bool CollectFacility(cISC4Occupant* occupant, void* data)
	{
		Facility facility;
		if (DescribeFacility(occupant, facility))
		{
			if (facility.kind == Kind::Cemetery)
			{
				const uint32_t key = Key(facility.cellX, facility.cellZ);
				const auto it = gravesUsed.find(key);
				facility.used = it != gravesUsed.end() ? std::min(it->second, facility.capacity) : 0;
				facility.emptying = emptying.count(key) != 0;
			}
			static_cast<std::vector<Facility>*>(data)->push_back(facility);
		}
		return true;
	}

	std::vector<Facility> CollectFacilities()
	{
		std::vector<Facility> facilities;
		cISC4OccupantManager* const manager = city ? city->GetOccupantManager() : nullptr;
		if (manager)
		{
			DeathcareFilter filter;
			manager->IterateOccupants(CollectFacility, &facilities, nullptr, nullptr, &filter);
		}
		return facilities;
	}

	// Takes up to `amount` dead into `f`; returns how many it took.
	double Take(Facility& f, double amount)
	{
		const double taken = std::min(amount, std::max(0.0, f.capacity - f.used));
		if (taken <= 0) return 0;
		f.used += taken;
		f.taken += taken;
		(f.kind == Kind::Crematorium ? totals.cremated : totals.buried) += taken;
		return taken;
	}

	// ---------------------------------------------------------------- Lua

	cISCLua* CityLua()
	{
		cISC4AdvisorSystem* const advisors = city ? city->GetAdvisorSystem() : nullptr;
		return advisors ? advisors->GetScriptingContext() : nullptr;
	}

	void SetGridVar(cISCLua* lua, const char* field, double value)
	{
		lua->GetGlobal("afterlife_grid_var");
		if (!lua->IsTable(-1))
		{
			lua->Pop(1);
			lua->NewTable();
			lua->PushValue(-1);
			lua->SetGlobal("afterlife_grid_var");
		}
		lua->PushString(field);
		lua->PushNumber(value);
		lua->SetTable(-3);
		lua->Pop(1);
	}

	// ---------------------------------------------------------------- query values of one building

	class DeathcareQueryTarget final : public cIBuildingQueryDialogHookTarget
	{
	public:
		bool QueryInterface(uint32_t riid, void** ppvObj) override
		{
			if (riid == GZIID_cIBuildingQueryHookTarget || riid == GZIID_cIGZUnknown)
			{
				*ppvObj = static_cast<cIBuildingQueryDialogHookTarget*>(this);
				AddRef();
				return true;
			}
			return false;
		}
		uint32_t AddRef() override { return ++refCount; }
		uint32_t Release() override { return refCount > 0 ? --refCount : 0; }

		void BeforeDialogShown(cISC4Occupant* occupant) override
		{
			cISCLua* const lua = CityLua();
			if (!lua || !settings.enabled || !IsFacility(occupant)) return;
			Facility f;
			if (!DescribeFacility(occupant, f)) return;
			const auto it = lastMonth.find(Key(f.cellX, f.cellZ));
			if (it != lastMonth.end() && it->second.kind == f.kind)
			{
				f.used = it->second.used;
				f.taken = it->second.taken;
				f.emptied = it->second.emptied;
			}
			else if (f.kind == Kind::Cemetery)
			{
				const auto graves = gravesUsed.find(Key(f.cellX, f.cellZ));
				f.used = graves != gravesUsed.end() ? graves->second : 0;
			}
			SetGridVar(lua, "FACILITY_CAPACITY", f.capacity);
			SetGridVar(lua, "FACILITY_USED", f.used);
			SetGridVar(lua, "FACILITY_FREE", std::max(0.0, f.capacity - f.used));
			SetGridVar(lua, "FACILITY_FILL_PERCENT", f.capacity > 0 ? std::floor(100 * f.used / f.capacity) : 0);
			SetGridVar(lua, "FACILITY_TAKEN", f.taken);
			SetGridVar(lua, "FACILITY_EMPTIED", f.emptied);
		}

		void AfterDialogShown(cISC4Occupant*) override {}

	private:
		uint32_t refCount = 0;
	};

	DeathcareQueryTarget queryTarget;
	bool queryRegistered = false;

	bool GetHookServer(cRZAutoRefCount<cIBuildingQueryHookServer>& hookServer)
	{
		cIGZCOM* const com = GZCOM();
		return com && com->GetClassObject(GZCLSID_cIBuildingQueryHookServer, GZIID_cIBuildingQueryHookServer, hookServer.AsPPVoid());
	}

	// ---------------------------------------------------------------- effects

	using FloatGrid = cISC4SimGrid<float>;
	using GetHQGridsFn = bool(__thiscall*)(cISC4ResidentialSimulator*, FloatGrid**, FloatGrid**, FloatGrid**);
	constexpr int kGetHQGridsSlot = 24; // checked in 1.1.641: three out pointers, each AddRef'd

	double UncollectedPercent(const Deathcare::TractGrid& tracts, uint32_t key, double dead)
	{
		const int32_t x = KeyX(key), z = KeyZ(key);
		if (x >= tracts.countX || z >= tracts.countZ || dead <= 0) return 0;
		const double residents = tracts.population[static_cast<size_t>(z) * tracts.countX + x];
		return residents > 0 ? 100.0 * dead / residents : 0;
	}

	void LowerHealth(const Deathcare::TractGrid& tracts)
	{
		cISC4ResidentialSimulator* const sim = city ? city->GetResidentialSimulator() : nullptr;
		if (!sim || uncollected.empty()) return;
		FloatGrid* hq[3]{};
		const auto getGrids = reinterpret_cast<GetHQGridsFn>((*reinterpret_cast<void***>(sim))[kGetHQGridsSlot]);
		if (!getGrids(sim, &hq[0], &hq[1], &hq[2]) || !hq[0] || !hq[1] || !hq[2])
		{
			for (FloatGrid* grid : hq) if (grid) grid->Release();
			return;
		}
		if (hq[0]->GetTractCountX() == tracts.countX && hq[0]->GetTractCountZ() == tracts.countZ)
		{
			for (const auto& [key, dead] : uncollected)
			{
				const double percent = UncollectedPercent(tracts, key, dead);
				if (percent <= 0) continue;
				const float penalty = static_cast<float>(std::min(settings.maxHqPenalty, settings.hqPenaltyPerPercent * percent));
				for (FloatGrid* grid : hq)
				{
					grid->SetTractValue(KeyX(key), KeyZ(key), std::max(0.0f, grid->GetTractValue(KeyX(key), KeyZ(key)) - penalty));
				}
			}
		}
		for (FloatGrid* grid : hq) grid->Release();
	}

	// A negative transient aura over every tract with uncollected dead (the Transient Aura data view shows it).
	void ApplyAura(const Deathcare::TractGrid& tracts)
	{
		cISC4AuraSimulator* const auraSim = city ? city->GetAuraSimulator() : nullptr;
		cISC4SimGrid<int8_t>* const grid = auraSim ? auraSim->GetTransientAuraGrid() : nullptr;
		if (!grid || uncollected.empty()) return;
		const int32_t shift = grid->GetTractShift(), countX = grid->GetTractCountX(), countZ = grid->GetTractCountZ();
		for (const auto& [key, dead] : uncollected)
		{
			const double percent = UncollectedPercent(tracts, key, dead);
			if (percent <= 0) continue;
			const int8_t value = static_cast<int8_t>(-std::min({ settings.maxAura, 127.0, std::ceil(settings.auraPerPercent * percent) }));
			// the residential tract in aura tracts (the two grids may have different tract sizes)
			const int32_t x0 = (KeyX(key) << tracts.shift) >> shift, x1 = (((KeyX(key) + 1) << tracts.shift) - 1) >> shift;
			const int32_t z0 = (KeyZ(key) << tracts.shift) >> shift, z1 = (((KeyZ(key) + 1) << tracts.shift) - 1) >> shift;
			for (int32_t z = std::max(0, z0); z <= std::min(countZ - 1, z1); z++)
			{
				for (int32_t x = std::max(0, x0); x <= std::min(countX - 1, x1); x++)
				{
					if (grid->GetTractValue(x, z) > value) grid->SetTractValue(x, z, value);
				}
			}
		}
	}

	// ---------------------------------------------------------------- notices

	void Notify(uint32_t captionId, uint32_t messageId, double count, int32_t x, int32_t z)
	{
		cRZAutoRefCount<cIGZString> message, caption;
		if (!StringResourceManager::GetLocalizedString(StringResourceKey(kLTEXTGroup, messageId), message)
			|| !StringResourceManager::GetLocalizedString(StringResourceKey(kLTEXTGroup, captionId), caption))
		{
			return;
		}
		std::string text(message->ToChar(), message->Strlen());
		const auto replace = [&text](const std::string& token, const std::string& value)
		{
			for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size()))
			{
				text.replace(at, token.size(), value);
			}
		};
		char number[32]{};
		snprintf(number, sizeof(number), "%.0f", count);
		replace("#count#", number);
		replace("#x#", std::to_string(x));
		replace("#z#", std::to_string(z));
		SC4NotificationDialog::ShowDialog(cRZBaseString(text), *caption);
	}

	double ReadDouble(const wchar_t* ini, const wchar_t* key, double fallback)
	{
		wchar_t text[64]{};
		GetPrivateProfileStringW(L"Afterlife", key, L"", text, 64, ini);
		wchar_t* end = nullptr;
		const double value = std::wcstod(text, &end);
		return text[0] != L'\0' && end != text && std::isfinite(value) && value >= 0 ? value : fallback;
	}

	void ResetCityState()
	{
		gravesUsed.clear();
		emptying.clear();
		uncollected.clear();
		relocation = 0;
		lastMonth.clear();
		totals = Deathcare::Totals{};
		for (uint32_t& months : monthsSince) months = 12;
		loggedFacilities = false;
	}
}

void Deathcare::LoadSettings(const wchar_t* ini)
{
	settings.enabled = GetPrivateProfileIntW(L"Afterlife", L"DEATHCARE", 1, ini) != 0;
	settings.effects = GetPrivateProfileIntW(L"Afterlife", L"DEATHCARE_EFFECTS", 1, ini) != 0;
	settings.aura = GetPrivateProfileIntW(L"Afterlife", L"DEATHCARE_AURA", 1, ini) != 0;
	settings.gravesPerCell = ReadDouble(ini, L"DEATHCARE_GRAVES_PER_CELL", settings.gravesPerCell);
	settings.cremationPerMonth = ReadDouble(ini, L"DEATHCARE_CREMATION_PER_MONTH", settings.cremationPerMonth);
	settings.radius = ReadDouble(ini, L"DEATHCARE_RADIUS", settings.radius);
	settings.maxUncollectedShare = ReadDouble(ini, L"DEATHCARE_MAX_UNCOLLECTED_SHARE", settings.maxUncollectedShare);
	settings.hqPenaltyPerPercent = ReadDouble(ini, L"DEATHCARE_HQ_PENALTY_PER_PERCENT", settings.hqPenaltyPerPercent);
	settings.maxHqPenalty = ReadDouble(ini, L"DEATHCARE_MAX_HQ_PENALTY", settings.maxHqPenalty);
	settings.auraPerPercent = ReadDouble(ini, L"DEATHCARE_AURA_PER_PERCENT", settings.auraPerPercent);
	settings.maxAura = ReadDouble(ini, L"DEATHCARE_AURA_MAX", settings.maxAura);
	settings.emptyThreshold = std::min(1.0, ReadDouble(ini, L"DEATHCARE_EMPTY_THRESHOLD", settings.emptyThreshold));
	settings.notifyUncollected = ReadDouble(ini, L"DEATHCARE_NOTIFY_UNCOLLECTED", settings.notifyUncollected);
	settings.notifyNearlyFull = ReadDouble(ini, L"DEATHCARE_NOTIFY_NEARLY_FULL", settings.notifyNearlyFull);
	SimLog::Write(SimLog::Level::Info, "Deathcare: %s, health effects %s, transient aura %s.", settings.enabled ? "on" : "off",
		settings.enabled && settings.effects ? "on" : "off", settings.enabled && settings.effects && settings.aura ? "on" : "off");
}

bool Deathcare::Enabled() { return settings.enabled; }

void Deathcare::SetCity(cISC4City* newCity)
{
	ResetCityState();
	if (queryRegistered)
	{
		cRZAutoRefCount<cIBuildingQueryHookServer> server;
		if (GetHookServer(server)) server->RemoveNotification(&queryTarget);
		queryRegistered = false;
	}
	city = newCity;
	if (city && settings.enabled)
	{
		cRZAutoRefCount<cIBuildingQueryHookServer> server;
		queryRegistered = GetHookServer(server) && server->AddNotification(&queryTarget);
		if (!queryRegistered)
		{
			SimLog::Write(SimLog::Level::Warning, "Deathcare: SC4QueryUIHooks.dll not found: deathcare queries show no values of their own building.");
		}
	}
}

void Deathcare::RunMonth(const TractGrid& tracts, const std::vector<double>& deathsPerTract)
{
	if (!settings.enabled || !city || tracts.countX <= 0 || tracts.countZ <= 0) return;
	std::vector<Facility> facilities = CollectFacilities();
	totals = Totals{};

	// Cemeteries that are gone: their dead wait for a new place.
	std::unordered_set<uint32_t> present;
	for (const Facility& f : facilities) if (f.kind == Kind::Cemetery) present.insert(Key(f.cellX, f.cellZ));
	for (auto it = gravesUsed.begin(); it != gravesUsed.end();)
	{
		if (present.count(it->first)) { ++it; continue; }
		relocation += it->second;
		emptying.erase(it->first);
		it = gravesUsed.erase(it);
	}

	for (const Facility& f : facilities)
	{
		if (f.kind == Kind::Cemetery) { totals.cemeteries++; totals.gravesTotal += f.capacity; }
		else { totals.crematoria++; totals.cremationCapacity += f.capacity; }
	}
	if (!loggedFacilities)
	{
		loggedFacilities = true;
		SimLog::Write(SimLog::Level::Info, "Deathcare: %.0f cemetery(ies) with %.0f graves, %.0f crematorium(s) taking %.0f a month.",
			totals.cemeteries, totals.gravesTotal, totals.crematoria, totals.cremationCapacity);
	}

	// 1. relocation, anywhere in the city: crematoria first
	for (Kind kind : { Kind::Crematorium, Kind::Cemetery })
	{
		for (Facility& f : facilities)
		{
			if (relocation <= 0) break;
			if (f.kind == kind) relocation -= Take(f, relocation);
		}
	}

	// 2. each tract: nearest crematorium in range, then nearest cemetery in range
	std::vector<size_t> order;
	double worstPercent = 0;
	uint32_t worstKey = 0;
	for (int32_t z = 0; z < tracts.countZ; z++)
	{
		for (int32_t x = 0; x < tracts.countX; x++)
		{
			const size_t index = static_cast<size_t>(z) * tracts.countX + x;
			const uint32_t key = Key(x, z);
			const auto backlog = uncollected.find(key);
			double pending = (index < deathsPerTract.size() ? deathsPerTract[index] : 0)
				+ (backlog != uncollected.end() ? backlog->second : 0);
			if (pending <= 0) continue;

			const double half = std::ldexp(1.0, tracts.shift) / 2;
			const double cx = std::ldexp(static_cast<double>(x), tracts.shift) + half;
			const double cz = std::ldexp(static_cast<double>(z), tracts.shift) + half;
			order.clear();
			for (size_t i = 0; i < facilities.size(); i++)
			{
				const Facility& f = facilities[i];
				if (Dist2(f.cellX, f.cellZ, cx, cz) <= f.radiusCells * f.radiusCells) order.push_back(i);
			}
			std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
			{
				const Facility& fa = facilities[a];
				const Facility& fb = facilities[b];
				if (fa.kind != fb.kind) return fa.kind == Kind::Crematorium;
				return Dist2(fa.cellX, fa.cellZ, cx, cz) < Dist2(fb.cellX, fb.cellZ, cx, cz);
			});
			for (size_t i : order)
			{
				pending -= Take(facilities[i], pending);
				if (pending <= 0) break;
			}

			pending = std::min(pending, std::floor(tracts.population[index] * settings.maxUncollectedShare));
			if (pending > 0)
			{
				uncollected[key] = pending;
				const double percent = 100.0 * pending / std::max(1.0, tracts.population[index]);
				if (percent > worstPercent) { worstPercent = percent; worstKey = key; }
			}
			else if (backlog != uncollected.end())
			{
				uncollected.erase(backlog);
			}
		}
	}

	// 3. crematoria with room left empty the cemeteries in their range that reached the threshold, fullest first
	for (Facility& c : facilities)
	{
		if (c.kind == Kind::Cemetery && c.capacity > 0 && c.used >= settings.emptyThreshold * c.capacity) c.emptying = true;
	}
	for (Facility& oven : facilities)
	{
		if (oven.kind != Kind::Crematorium) continue;
		double spare = std::max(0.0, oven.capacity - oven.used);
		order.clear();
		for (size_t i = 0; i < facilities.size(); i++)
		{
			const Facility& c = facilities[i];
			if (c.kind == Kind::Cemetery && c.emptying && c.used > 0
				&& Dist2(c.cellX, c.cellZ, oven.cellX, oven.cellZ) <= oven.radiusCells * oven.radiusCells) order.push_back(i);
		}
		std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
		{
			return facilities[a].used / facilities[a].capacity > facilities[b].used / facilities[b].capacity;
		});
		for (size_t i : order)
		{
			if (spare <= 0) break;
			Facility& c = facilities[i];
			const double moved = std::min(spare, c.used);
			c.used -= moved;
			c.emptied += moved;
			oven.used += moved;
			oven.emptied += moved;
			spare -= moved;
			totals.emptied += moved;
			if (c.used <= 0) c.emptying = false;
		}
	}

	gravesUsed.clear();
	emptying.clear();
	lastMonth.clear();
	for (const Facility& f : facilities)
	{
		const uint32_t key = Key(f.cellX, f.cellZ);
		lastMonth[key] = f;
		if (f.kind != Kind::Cemetery) continue;
		if (f.used > 0) gravesUsed[key] = f.used;
		if (f.emptying) emptying.insert(key);
		totals.gravesUsed += f.used;
		if (f.capacity > 0 && f.used >= settings.notifyNearlyFull * f.capacity) totals.cemeteriesNearlyFull++;
	}
	totals.collected = totals.cremated + totals.buried;
	totals.relocation = relocation;
	for (const auto& [key, dead] : uncollected) totals.uncollected += dead;

	if (settings.effects)
	{
		LowerHealth(tracts);
		if (settings.aura) ApplyAura(tracts);
	}

	for (uint32_t& months : monthsSince) months++;
	const bool allFull = totals.gravesTotal > 0 && totals.gravesUsed >= totals.gravesTotal && totals.cremationCapacity <= 0;
	if (settings.notifyUncollected > 0 && totals.uncollected >= settings.notifyUncollected && monthsSince[allFull ? 1 : 0] >= 12)
	{
		monthsSince[allFull ? 1 : 0] = 0;
		const int32_t cellX = (KeyX(worstKey) << tracts.shift) + (1 << tracts.shift) / 2;
		const int32_t cellZ = (KeyZ(worstKey) << tracts.shift) + (1 << tracts.shift) / 2;
		Notify(allFull ? kTextFullCaption : kTextUncollectedCaption, allFull ? kTextFullMessage : kTextUncollectedMessage,
			totals.uncollected, cellX, cellZ);
	}
	else if (settings.notifyNearlyFull > 0 && totals.cemeteriesNearlyFull > 0 && !allFull && monthsSince[2] >= 12)
	{
		monthsSince[2] = 0;
		Notify(kTextNearlyFullCaption, kTextNearlyFullMessage, totals.cemeteriesNearlyFull, 0, 0);
	}
	SimLog::Write(SimLog::Level::Debug,
		"Deathcare: %.0f collected (%.0f cremated, %.0f buried), %.0f uncollected, %.0f relocating, %.0f emptied, graves %.0f/%.0f.",
		totals.collected, totals.cremated, totals.buried, totals.uncollected, totals.relocation, totals.emptied,
		totals.gravesUsed, totals.gravesTotal);
}

const Deathcare::Totals& Deathcare::GetTotals() { return totals; }

void Deathcare::LoadKeys(const std::map<std::wstring, std::wstring>& keys)
{
	gravesUsed.clear();
	emptying.clear();
	uncollected.clear();
	relocation = 0;
	for (const auto& [key, text] : keys)
	{
		int x = 0, z = 0;
		wchar_t* end = nullptr;
		const double value = std::wcstod(text.c_str(), &end);
		if (end == text.c_str() || !std::isfinite(value) || value <= 0) continue;
		if (swscanf(key.c_str(), L"GRAVES.%d.%d", &x, &z) == 2) gravesUsed[Key(x, z)] = std::floor(value);
		else if (swscanf(key.c_str(), L"EMPTYING.%d.%d", &x, &z) == 2) emptying.insert(Key(x, z));
		else if (swscanf(key.c_str(), L"UNCOLLECTED.%d.%d", &x, &z) == 2) uncollected[Key(x, z)] = std::floor(value);
		else if (key == L"RELOCATION") relocation = std::floor(value);
	}
}

void Deathcare::SaveKeys(const std::function<void(const std::wstring&, const std::wstring&)>& write)
{
	wchar_t key[64]{}, value[64]{};
	const auto put = [&](const wchar_t* format, uint32_t k, double v)
	{
		swprintf(key, 64, format, KeyX(k), KeyZ(k));
		swprintf(value, 64, L"%.0f", v);
		write(key, value);
	};
	for (const auto& [k, used] : gravesUsed) if (used > 0) put(L"GRAVES.%d.%d", k, used);
	for (uint32_t k : emptying) put(L"EMPTYING.%d.%d", k, 1);
	for (const auto& [k, dead] : uncollected) if (dead > 0) put(L"UNCOLLECTED.%d.%d", k, dead);
	if (relocation > 0)
	{
		swprintf(value, 64, L"%.0f", relocation);
		write(L"RELOCATION", value);
	}
}
