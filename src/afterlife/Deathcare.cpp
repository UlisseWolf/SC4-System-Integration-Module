////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: deathcare.
//
// The Afterlife counts the dead of every month and, since 1.9.3, knows in which residential tract they died (the
// generation resets of the residential simulator; the other deaths are spread over the tracts by population). Deathcare
// gives those dead somewhere to go, in the spirit of Cities: Skylines, but with SimCity 4's own service model (a radius
// and a capacity, like schools and hospitals) instead of vehicles:
//
//  * a cemetery (Maxis occupant group 0x1700, SC4SIM 0x5C4DD101, or Deathcare Type 1) has a number of graves; every
//    burial fills one, and
//    a full cemetery takes no one;
//  * a crematorium (occupant group 0x5C4DD100, or Deathcare Type 2) takes up to its capacity every month and never
//    fills;
//  * each month the dead of each tract, plus the uncollected dead of earlier months, go to the nearest crematorium in
//    range with room, then to the nearest cemetery in range with free graves; the rest stays uncollected in the tract
//    (at most DEATHCARE_MAX_UNCOLLECTED_SHARE of its residents);
//  * with DEATHCARE_EFFECTS, a tract with uncollected dead loses health quotient each month (the HQ grids of the
//    residential simulator, the same SetTractValue the game's monthly decay uses), so its life expectancy falls and
//    more of its residents die: the vicious circle of an unserved district;
//  * a notification says when the uncollected dead pass DEATHCARE_NOTIFY_UNCOLLECTED, at most once a year.
//
// Capacity, radius and type come from the exemplar properties of New_Properties.xml-patches (0x5C4DD001-0x5C4DD003);
// a Maxis cemetery without them gets DEATHCARE_GRAVES_PER_CELL graves per lot cell and DEATHCARE_RADIUS. The graves
// used by each cemetery and the uncollected dead of each tract are saved with the Afterlife's totals.
//
// Checked in 1.1.641: cISC4ResidentialSimulator slot 24 (0x006F7300, GetHQGrids) has the same three AddRef'd out
// pointers as slot 25 (GetPopulationGrids); cISC4SimGrid GetTractValue is vtable +0x58 and SetTractValue +0x68, as used
// by the HQ decay in DoPopulationChange (0x006FA5D8).
//
////////////////////////////////////////////////////////////////////////////

#include "Deathcare.h"
#include "SimLog.h"
#include "SC4NotificationDialog.h"

#include "cISC4City.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantFilter.h"
#include "cISC4OccupantManager.h"
#include "cISC4ResidentialSimulator.h"
#include "cISC4SimGrid.h"
#include "cISCPropertyHolder.h"
#include "cS3DVector3.h"
#include "SC4Rect.h"
#include "SCPropertyUtil.h"
#include "StringResourceKey.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <unordered_map>

namespace
{
	constexpr uint32_t kOccupantTypeBuilding = 0x278128A0;
	constexpr float kCellSize = 16.0f;
	constexpr uint32_t kLTEXTGroup = 0x6A231EA4;
	constexpr uint32_t kTextUncollectedCaption = 0xBB3C4000, kTextUncollectedMessage = 0xBB3C4001;
	constexpr uint32_t kTextFullCaption = 0xBB3C4002, kTextFullMessage = 0xBB3C4003;

	struct Settings
	{
		bool enabled = true;                 // DEATHCARE
		bool effects = true;                 // DEATHCARE_EFFECTS
		double gravesPerCell = 40;           // DEATHCARE_GRAVES_PER_CELL (cemetery without a capacity property)
		double cremationPerMonth = 50;       // DEATHCARE_CREMATION_PER_MONTH (crematorium without a capacity property)
		double radius = 1024;                // DEATHCARE_RADIUS (meters, building without a radius property)
		double maxUncollectedShare = 0.1;    // DEATHCARE_MAX_UNCOLLECTED_SHARE (of a tract's residents)
		double hqPenaltyPerPercent = 0.5;    // DEATHCARE_HQ_PENALTY_PER_PERCENT (HQ per month per 1% uncollected)
		double maxHqPenalty = 3;             // DEATHCARE_MAX_HQ_PENALTY (HQ per month)
		double notifyUncollected = 50;       // DEATHCARE_NOTIFY_UNCOLLECTED (0 = never)
	} settings;

	enum class Kind : uint32_t { None = 0, Cemetery = 1, Crematorium = 2 };

	struct Facility
	{
		Kind kind = Kind::None;
		int32_t cellX = 0, cellZ = 0;
		double capacity = 0;      // graves, or bodies per month
		double radiusCells = 0;
		double used = 0;          // graves used (cemetery) or bodies this month (crematorium)
	};

	uint32_t Key(int32_t x, int32_t z) { return (static_cast<uint32_t>(x & 0xFFFF) << 16) | static_cast<uint32_t>(z & 0xFFFF); }

	std::unordered_map<uint32_t, double> gravesUsed;   // cemetery cell key -> graves used
	std::unordered_map<uint32_t, double> uncollected;  // tract key -> uncollected dead
	Deathcare::Totals totals;
	uint32_t monthsSinceNotice = 12;
	bool loggedFacilities = false;

	// ---------------------------------------------------------------- facilities

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
		bool IsOccupantIncluded(cISC4Occupant* occupant) override
		{
			if (!occupant || !IsOccupantTypeIncluded(occupant->GetType())) return false;
			if (occupant->IsOccupantGroup(Deathcare::kOccupantGroupCemetery)
				|| occupant->IsOccupantGroup(Deathcare::kOccupantGroupDeathcareCemetery)
				|| occupant->IsOccupantGroup(Deathcare::kOccupantGroupCrematorium)) return true;
			uint32_t type = 0;
			const cISCPropertyHolder* holder = occupant->AsPropertyHolder();
			return holder && SCPropertyUtil::GetPropertyValue(holder, Deathcare::kPropertyType, type) && type != 0;
		}
		bool IsOccupantTypeIncluded(uint32_t type) override { return type == kOccupantTypeBuilding; }
		bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return true; }
	private:
		uint32_t refCount = 0;
	};

	bool CollectFacility(cISC4Occupant* occupant, void* data)
	{
		auto& facilities = *static_cast<std::vector<Facility>*>(data);
		const cISCPropertyHolder* holder = occupant->AsPropertyHolder();
		Facility facility;
		uint32_t type = 0;
		if (holder && SCPropertyUtil::GetPropertyValue(holder, Deathcare::kPropertyType, type) && type != 0)
		{
			facility.kind = type == 2 ? Kind::Crematorium : type == 1 ? Kind::Cemetery : Kind::None;
		}
		else
		{
			facility.kind = occupant->IsOccupantGroup(Deathcare::kOccupantGroupCrematorium) ? Kind::Crematorium : Kind::Cemetery;
		}
		if (facility.kind == Kind::None) return true;

		cS3DVector3 position;
		if (!occupant->GetPosition(&position)) return true;
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
			const bool haveCells = occupant->GetBoundingCityCells(cells);
			const double area = haveCells ? std::max<double>(1, static_cast<double>(cells.bottomRightX - cells.topLeftX + 1)
				* static_cast<double>(cells.bottomRightY - cells.topLeftY + 1)) : 1;
			facility.capacity = std::floor(area * settings.gravesPerCell);
		}
		float radius = 0;
		facility.radiusCells = (holder && SCPropertyUtil::GetPropertyValue(holder, Deathcare::kPropertyRadius, radius) && radius > 0
			? radius : settings.radius) / kCellSize;
		if (facility.kind == Kind::Cemetery)
		{
			const auto it = gravesUsed.find(Key(facility.cellX, facility.cellZ));
			facility.used = it != gravesUsed.end() ? std::min(it->second, facility.capacity) : 0;
		}
		facilities.push_back(facility);
		return true;
	}

	std::vector<Facility> CollectFacilities(cISC4City* city)
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

	// ---------------------------------------------------------------- health effect

	using FloatGrid = cISC4SimGrid<float>;
	using GetHQGridsFn = bool(__thiscall*)(cISC4ResidentialSimulator*, FloatGrid**, FloatGrid**, FloatGrid**);
	constexpr int kGetHQGridsSlot = 24; // checked in 1.1.641: three out pointers, each AddRef'd

	void LowerHealth(cISC4City* city, const Deathcare::TractGrid& tracts)
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
				const int32_t x = static_cast<int32_t>(key >> 16), z = static_cast<int32_t>(key & 0xFFFF);
				if (x >= tracts.countX || z >= tracts.countZ || dead <= 0) continue;
				const double residents = tracts.population[static_cast<size_t>(z) * tracts.countX + x];
				if (residents <= 0) continue;
				const float penalty = static_cast<float>(std::min(settings.maxHqPenalty,
					settings.hqPenaltyPerPercent * 100.0 * dead / residents));
				for (FloatGrid* grid : hq)
				{
					grid->SetTractValue(x, z, std::max(0.0f, grid->GetTractValue(x, z) - penalty));
				}
			}
		}
		for (FloatGrid* grid : hq) grid->Release();
	}

	double ReadDouble(const wchar_t* ini, const wchar_t* key, double fallback)
	{
		wchar_t text[64]{};
		GetPrivateProfileStringW(L"Afterlife", key, L"", text, 64, ini);
		wchar_t* end = nullptr;
		const double value = std::wcstod(text, &end);
		return text[0] != L'\0' && end != text && std::isfinite(value) && value >= 0 ? value : fallback;
	}
}

void Deathcare::LoadSettings(const wchar_t* ini)
{
	settings.enabled = GetPrivateProfileIntW(L"Afterlife", L"DEATHCARE", 1, ini) != 0;
	settings.effects = GetPrivateProfileIntW(L"Afterlife", L"DEATHCARE_EFFECTS", 1, ini) != 0;
	settings.gravesPerCell = ReadDouble(ini, L"DEATHCARE_GRAVES_PER_CELL", settings.gravesPerCell);
	settings.cremationPerMonth = ReadDouble(ini, L"DEATHCARE_CREMATION_PER_MONTH", settings.cremationPerMonth);
	settings.radius = ReadDouble(ini, L"DEATHCARE_RADIUS", settings.radius);
	settings.maxUncollectedShare = ReadDouble(ini, L"DEATHCARE_MAX_UNCOLLECTED_SHARE", settings.maxUncollectedShare);
	settings.hqPenaltyPerPercent = ReadDouble(ini, L"DEATHCARE_HQ_PENALTY_PER_PERCENT", settings.hqPenaltyPerPercent);
	settings.maxHqPenalty = ReadDouble(ini, L"DEATHCARE_MAX_HQ_PENALTY", settings.maxHqPenalty);
	settings.notifyUncollected = ReadDouble(ini, L"DEATHCARE_NOTIFY_UNCOLLECTED", settings.notifyUncollected);
	SimLog::Write(SimLog::Level::Info, "Deathcare: %s, health effects %s.", settings.enabled ? "on" : "off",
		settings.enabled && settings.effects ? "on" : "off");
}

bool Deathcare::Enabled() { return settings.enabled; }

void Deathcare::Reset()
{
	gravesUsed.clear();
	uncollected.clear();
	totals = Totals{};
	monthsSinceNotice = 12;
	loggedFacilities = false;
}

void Deathcare::RunMonth(cISC4City* city, const TractGrid& tracts, const std::vector<double>& deathsPerTract)
{
	if (!settings.enabled || tracts.countX <= 0 || tracts.countZ <= 0) return;
	std::vector<Facility> facilities = CollectFacilities(city);
	totals = Totals{};
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

	std::vector<size_t> order(facilities.size());
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

			// Tract centre in cells; crematoria first, then cemeteries, each by distance.
			const double half = std::ldexp(1.0, tracts.shift) / 2;
			const double cx = std::ldexp(static_cast<double>(x), tracts.shift) + half;
			const double cz = std::ldexp(static_cast<double>(z), tracts.shift) + half;
			order.clear();
			for (size_t i = 0; i < facilities.size(); i++)
			{
				const double dx = facilities[i].cellX - cx, dz = facilities[i].cellZ - cz;
				if (dx * dx + dz * dz <= facilities[i].radiusCells * facilities[i].radiusCells) order.push_back(i);
			}
			std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
			{
				const Facility& fa = facilities[a];
				const Facility& fb = facilities[b];
				if (fa.kind != fb.kind) return fa.kind == Kind::Crematorium;
				const double da = (fa.cellX - cx) * (fa.cellX - cx) + (fa.cellZ - cz) * (fa.cellZ - cz);
				const double db = (fb.cellX - cx) * (fb.cellX - cx) + (fb.cellZ - cz) * (fb.cellZ - cz);
				return da < db;
			});
			for (size_t i : order)
			{
				Facility& f = facilities[i];
				const double taken = std::min(pending, std::max(0.0, f.capacity - f.used));
				if (taken <= 0) continue;
				f.used += taken;
				pending -= taken;
				(f.kind == Kind::Crematorium ? totals.cremated : totals.buried) += taken;
				if (pending <= 0) break;
			}

			const double cap = std::floor(tracts.population[index] * settings.maxUncollectedShare);
			pending = std::min(pending, cap);
			if (pending > 0) uncollected[key] = pending;
			else if (backlog != uncollected.end()) uncollected.erase(backlog);
		}
	}

	gravesUsed.clear();
	for (const Facility& f : facilities)
	{
		if (f.kind != Kind::Cemetery) continue;
		gravesUsed[Key(f.cellX, f.cellZ)] = f.used;
		totals.gravesUsed += f.used;
	}
	totals.collected = totals.cremated + totals.buried;
	for (const auto& [key, dead] : uncollected) totals.uncollected += dead;

	if (settings.effects) LowerHealth(city, tracts);

	monthsSinceNotice++;
	if (settings.notifyUncollected > 0 && totals.uncollected >= settings.notifyUncollected && monthsSinceNotice >= 12)
	{
		monthsSinceNotice = 0;
		const bool full = totals.gravesTotal > 0 && totals.gravesUsed >= totals.gravesTotal && totals.cremationCapacity <= 0;
		SC4NotificationDialog::ShowDialog(StringResourceKey(kLTEXTGroup, full ? kTextFullMessage : kTextUncollectedMessage),
			StringResourceKey(kLTEXTGroup, full ? kTextFullCaption : kTextUncollectedCaption));
	}
	SimLog::Write(SimLog::Level::Debug, "Deathcare: %.0f collected (%.0f cremated, %.0f buried), %.0f uncollected, graves %.0f/%.0f.",
		totals.collected, totals.cremated, totals.buried, totals.uncollected, totals.gravesUsed, totals.gravesTotal);
}

const Deathcare::Totals& Deathcare::GetTotals() { return totals; }

void Deathcare::LoadKeys(const std::map<std::wstring, std::wstring>& keys)
{
	gravesUsed.clear();
	uncollected.clear();
	for (const auto& [key, text] : keys)
	{
		int x = 0, z = 0;
		wchar_t* end = nullptr;
		const double value = std::wcstod(text.c_str(), &end);
		if (end == text.c_str() || !std::isfinite(value) || value <= 0) continue;
		if (swscanf(key.c_str(), L"GRAVES.%d.%d", &x, &z) == 2) gravesUsed[Key(x, z)] = std::floor(value);
		else if (swscanf(key.c_str(), L"UNCOLLECTED.%d.%d", &x, &z) == 2) uncollected[Key(x, z)] = std::floor(value);
	}
}

void Deathcare::SaveKeys(const std::function<void(const std::wstring&, const std::wstring&)>& write)
{
	wchar_t key[64]{}, value[64]{};
	for (const auto& [k, used] : gravesUsed)
	{
		if (used <= 0) continue;
		swprintf(key, 64, L"GRAVES.%d.%d", static_cast<int>(k >> 16), static_cast<int>(k & 0xFFFF));
		swprintf(value, 64, L"%.0f", used);
		write(key, value);
	}
	for (const auto& [k, dead] : uncollected)
	{
		if (dead <= 0) continue;
		swprintf(key, 64, L"UNCOLLECTED.%d.%d", static_cast<int>(k >> 16), static_cast<int>(k & 0xFFFF));
		swprintf(value, 64, L"%.0f", dead);
		write(key, value);
	}
}
