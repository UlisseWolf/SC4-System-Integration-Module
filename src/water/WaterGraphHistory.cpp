////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: water production by water source and the city's wastewater, for the Water graph.
//
// Same mechanism as the power graph (src/power/PowerGraphHistory.cpp): one point per series is recorded in the
// city's history warehouse on every SimNewMonth, and CAM's Water graph exemplar (0x6534284A / 0xCA4AD545 /
// 0x00000007) plots them. The game records produced 0x0A14EBB4, consumed 0xCA14EBD0, bought 0x0A14EBB5 and sold
// 0xCA14EBC6; it has no wastewater, so the module models it:
//
//  - water by source: current output (cISC4PlumbingSimulator::GetWaterProduction) of the water producers (group
//    0x1401 with Water Produced 0x88ED3303 > 0) by Water Source (0x48F23A7E, Advanced Water Management: 0 fresh,
//    1 salt, 2 aquifer; without it the vanilla aquifer). Sewage plants (AWM group 0x280C60E3, "Sewage Sanitizer"
//    generator of New_Properties.xml) are not a source: their output is recycled wastewater.
//  - wastewater produced: the water the city consumes (GetWaterConsumed).
//  - wastewater treated: by treatment plants (groups 0x1401 and 0x1402, not sewage plants), up to their capacity:
//    Water Treatment monthly capacity (0xC8EBD131), or, when it is 0, the capacity the New_Properties.xml Treatment
//    Plant generator gives the building (WaterOutput, estimated from Water Consumed 0xC8ED2D84 = 10*int(1+WaterOutput/
//    2000): 2000*Water Consumed/10 - 1000). Never more than the wastewater produced.
//  - wastewater recycled: output of the sewage plants, never more than the wastewater produced.
//
////////////////////////////////////////////////////////////////////////////

#include "WaterGraphHistory.h"
#include "SimLog.h"
#include "WaterBuildingOccupantFilter.h"

#include <limits> // before cISC4PlumbingSimulator.h: its ilist allocator uses std::numeric_limits
#include "cIGZMessage2Standard.h"
#include "cIGZVariant.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4HistoryWarehouse.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantManager.h"
#include "cISC4PlumbingSimulator.h"
#include "cISCProperty.h"
#include "cISCPropertyHolder.h"
#include "GZServPtrs.h"

#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessageSimNewMonth = 0x66956816;

	constexpr uint32_t kOccupantGroupTreatment = 0x1402;
	constexpr uint32_t kOccupantGroupSewagePlant = 0x280C60E3;  // AWM: Sewage Sanitizer
	constexpr uint32_t kPropWaterProduced = 0x88ED3303;
	constexpr uint32_t kPropWaterSource = 0x48F23A7E;
	constexpr uint32_t kPropTreatmentCapacity = 0xC8EBD131;
	constexpr uint32_t kPropWaterConsumed = 0xC8ED2D84;

	bool enabled = true;

	// First value of a numeric property (Uint8 or Uint32, single or array).
	bool ReadNumber(cISCPropertyHolder* holder, uint32_t id, uint32_t& value)
	{
		const cISCProperty* const property = holder ? holder->GetProperty(id) : nullptr;
		const cIGZVariant* const variant = property ? property->GetPropertyValue() : nullptr;
		if (!variant) return false;
		const uint16_t type = variant->GetType();
		const bool array = (type & cIGZVariant::TypeArray) != 0;
		if (array && variant->GetCount() == 0) return false;
		switch (type & ~cIGZVariant::TypeArray)
		{
		case cIGZVariant::Uint8:
			value = array ? variant->RefUint8()[0] : variant->GetValUint8();
			return true;
		case cIGZVariant::Uint32:
			value = array ? variant->RefUint32()[0] : variant->GetValUint32();
			return true;
		case cIGZVariant::Sint32:
			value = static_cast<uint32_t>(std::max<int32_t>(0, array ? variant->RefSint32()[0] : variant->GetValSint32()));
			return true;
		default:
			return false;
		}
	}

	struct IterationData
	{
		std::unordered_set<cISC4Occupant*> seen;
		std::vector<cISC4Occupant*> buildings;
	};

	bool CollectBuilding(cISC4Occupant* occupant, void* data)
	{
		IterationData& iteration = *static_cast<IterationData*>(data);
		if (iteration.seen.insert(occupant).second) iteration.buildings.push_back(occupant);
		return true;
	}

	uint32_t TreatmentCapacity(cISCPropertyHolder* holder)
	{
		uint32_t capacity = 0;
		if (ReadNumber(holder, kPropTreatmentCapacity, capacity) && capacity > 0) return capacity;
		uint32_t consumed = 0;
		if (ReadNumber(holder, kPropWaterConsumed, consumed) && consumed >= 10)
		{
			return 2000u * (consumed / 10u) - 1000u;
		}
		return 0;
	}

	int32_t Clamp(uint64_t value)
	{
		return value > 0x7FFFFFFFull ? 0x7FFFFFFF : static_cast<int32_t>(value);
	}

	void RecordMonth()
	{
		cISC4AppPtr app;
		cISC4City* const city = app ? app->GetCity() : nullptr;
		cISC4OccupantManager* const occupants = city ? city->GetOccupantManager() : nullptr;
		cISC4PlumbingSimulator* const plumbing = city ? city->GetPlumbingSimulator() : nullptr;
		cISC4HistoryWarehouse* const history = city ? city->GetHistoryWarehouse() : nullptr;
		if (!occupants || !plumbing || !history) return;

		IterationData iteration;
		WaterBuildingOccupantFilter filter;  // buildings of group 0x1401
		filter.AddRef();
		occupants->IterateOccupants(CollectBuilding, &iteration, nullptr, nullptr, &filter);

		uint64_t aquifer = 0, fresh = 0, salt = 0, sewageOutput = 0, treatmentCapacity = 0;
		for (cISC4Occupant* const building : iteration.buildings)
		{
			cISCPropertyHolder* const holder = building->AsPropertyHolder();
			const bool sewagePlant = building->IsOccupantGroup(kOccupantGroupSewagePlant);
			uint32_t waterProduced = 0;
			if (ReadNumber(holder, kPropWaterProduced, waterProduced) && waterProduced > 0)
			{
				const uint64_t output = plumbing->GetWaterProduction(building);
				uint32_t source = 2;
				ReadNumber(holder, kPropWaterSource, source);
				if (sewagePlant) sewageOutput += output;
				else if (source == 0) fresh += output;
				else if (source == 1) salt += output;
				else aquifer += output;
			}
			else if (!sewagePlant && building->IsOccupantGroup(kOccupantGroupTreatment))
			{
				treatmentCapacity += TreatmentCapacity(holder);
			}
		}

		const int64_t consumedSigned = plumbing->GetWaterConsumed();
		const uint64_t wastewater = consumedSigned > 0 ? static_cast<uint64_t>(consumedSigned) : 0;
		const uint64_t recycled = std::min(sewageOutput, wastewater);
		const uint64_t treated = std::min(treatmentCapacity, wastewater);

		history->RecordHistoryData(WaterGraphHistory::kHistoryAquifer, Clamp(aquifer));
		history->RecordHistoryData(WaterGraphHistory::kHistoryFreshWater, Clamp(fresh));
		history->RecordHistoryData(WaterGraphHistory::kHistorySaltWater, Clamp(salt));
		history->RecordHistoryData(WaterGraphHistory::kHistoryWastewaterProduced, Clamp(wastewater));
		history->RecordHistoryData(WaterGraphHistory::kHistoryWastewaterTreated, Clamp(treated));
		history->RecordHistoryData(WaterGraphHistory::kHistoryWastewaterRecycled, Clamp(recycled));

		SimLog::Write(SimLog::Level::Debug,
			"WaterGraph: %u water buildings; aquifer %llu, fresh %llu, salt %llu; wastewater %llu, treated %llu "
			"(capacity %llu), recycled %llu.",
			static_cast<unsigned>(iteration.buildings.size()), aquifer, fresh, salt, wastewater, treated,
			treatmentCapacity, recycled);
	}
}

void WaterGraphHistory::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[16]{};
	GetPrivateProfileStringW(L"WaterGraph", L"Enabled", L"true", value, 16, ini.c_str());
	std::wstring text(value);
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	SimLog::Write(SimLog::Level::Info, "WaterGraph: water by source and wastewater %s.", enabled ? "on" : "off");
}

void WaterGraphHistory::DoMessage(cIGZMessage2* message)
{
	if (!enabled || message->GetType() != kMessageSimNewMonth) return;
	RecordMonth();
}
