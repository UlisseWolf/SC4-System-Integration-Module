////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: monthly power production of each energy class, for the Power graph.
//
// The graphs of the Graphs window read their lines from the city's history warehouse (cISC4HistoryWarehouse, saved
// with the city): every plot of a graph exemplar (group 0xCA4AD545) names a history type in "Graph Plot data source 1"
// (0x6A4AEE40). The power simulator records its totals there in the last phase of its update (0x006EF23A: produced
// 0xAA11F7CE, consumed 0xAA11F7D2, ...); the neighbour-deal simulator records bought 0xAA11F837 and sold 0xAA11F83A
// (0x006D2E48). The game also records consumption by Power Plant Type (0xAA120D5E-0xAA120D6D), but it keeps only two
// points of each (SetHistoryDataQueueLength 2 at 0x006F050A) and the vanilla types cannot tell geothermal and
// hydroelectric plants from gas plants or tidal plants from wind farms (the New_Properties generators give them types
// 3 and 9).
//
// RecordHistoryData (vtable +0x30, 0x00475AA0) posts message 0x89EFA536; the warehouse's handler (0x00476EE0) finds or
// creates the entry of any type (0x00476E40), so new types need no registration. A point is appended when the date is
// later than the last point and overwrites it otherwise; queue length 0 (the default) keeps every point. This module
// records one point per class on SimNewMonth: the sum of the current output (Power Generated x efficiency,
// cISC4PowerSimulator::GetOccupantPowerProduced, vtable slot 33 = 0x006EB420) of the power plants of that class.
//
// Class of a plant: ExtendedPowerType (0x713C8912, New_Properties.xml / AMPS), else Power Plant Type (0x27812853).
//
// Power Grid graph: the same month also records production by load class, grouped as NEON does (run_grid_balance_
// variables, NEON.lua): base = coal, nuclear fission and fusion, geothermal, hydroelectric, biomass, waste to energy;
// intermediate = solar, wind, tidal; peak = oil, natural gas, microwave, generators. When NEON is installed (the
// advisor Lua has run_grid_balance_variables and grid_logic_var), the module calls run_grid_balance_variables() and
// takes the three productions from NEON (GEN_B/I/P_POWER), then records NEON's load balance (USED_B/I/P_POWER,
// SHORT_P_POWER) and free grid capacities in kVA (TRANSMIT/DISTRIB/TRANSFORM_AVAILABLE, never below 0). Without NEON
// those seven lines are not recorded.
//
////////////////////////////////////////////////////////////////////////////

#include "PowerGraphHistory.h"
#include "SimLog.h"

#include <limits> // before cISC4PowerSimulator.h: its ilist allocator uses std::numeric_limits

#include "cIGZMessage2Standard.h"
#include "cISC4AdvisorSystem.h"
#include "cISCLua.h"
#include "cIGZVariant.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4HistoryWarehouse.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantFilter.h"
#include "cISC4OccupantManager.h"
#include "cISC4PowerSimulator.h"
#include "cISCProperty.h"
#include "cISCPropertyHolder.h"
#include "GZServPtrs.h"

#include <Windows.h>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessageSimNewMonth = 0x66956816;

	constexpr uint32_t kOccupantTypeBuilding = 0x278128A0;
	constexpr uint32_t kOccupantGroupPower = 0x1400;      // Building_Power: plants, transformers, power eye candy
	constexpr uint32_t kPropPowerGenerated = 0x27812852;
	constexpr uint32_t kPropPowerPlantType = 0x27812853;
	constexpr uint32_t kPropExtendedPowerType = 0x713C8912;

	enum EnergyClass : size_t
	{
		Coal, Oil, NaturalGas, NuclearFission, NuclearFusion, Solar, Wind, Geothermal, Hydroelectric, Tidal, Microwave,
		Biomass, ElectricGenerator, WasteToEnergy,
		ClassCount, None = ClassCount
	};

	struct ClassInfo
	{
		const char* name;
		uint32_t historyType;
		uint32_t extendedPowerType;  // ExtendedPowerType option of New_Properties.xml
	};

	constexpr std::array<ClassInfo, ClassCount> kClasses = { {
		{ "coal", PowerGraphHistory::kHistoryCoal, 0xB0C1D6F9 },
		{ "oil", PowerGraphHistory::kHistoryOil, 0x45DEA4F8 },
		{ "natural gas", PowerGraphHistory::kHistoryNaturalGas, 0x5D4CE371 },
		{ "nuclear fission", PowerGraphHistory::kHistoryNuclearFission, 0xD8B65889 },
		{ "nuclear fusion", PowerGraphHistory::kHistoryNuclearFusion, 0xF5DAF839 },
		{ "solar", PowerGraphHistory::kHistorySolar, 0xA934E93E },
		{ "wind", PowerGraphHistory::kHistoryWind, 0x9E43F5C9 },
		{ "geothermal", PowerGraphHistory::kHistoryGeothermal, 0xA4FFC9DA },
		{ "hydroelectric", PowerGraphHistory::kHistoryHydroelectric, 0x0A8B2E1C },
		{ "tidal", PowerGraphHistory::kHistoryTidal, 0x951653B7 },
		{ "microwave", PowerGraphHistory::kHistoryMicrowave, 0x03D5C8FD },
		{ "biomass", PowerGraphHistory::kHistoryBiomass, 0x9BE9788B },
		{ "electric generator", PowerGraphHistory::kHistoryElectricGenerator, 0xEA9F7384 },
		{ "waste to energy", PowerGraphHistory::kHistoryWasteToEnergy, 0xC291E217 },
	} };

	bool enabled = true;

	enum LoadClass : size_t { BaseLoad, IntermediateLoad, PeakLoad };

	constexpr std::array<LoadClass, ClassCount> kLoadOfClass = {
		BaseLoad,          // coal
		PeakLoad,          // oil
		PeakLoad,          // natural gas
		BaseLoad,          // nuclear fission
		BaseLoad,          // nuclear fusion
		IntermediateLoad,  // solar
		IntermediateLoad,  // wind
		BaseLoad,          // geothermal
		BaseLoad,          // hydroelectric
		IntermediateLoad,  // tidal
		PeakLoad,          // microwave
		BaseLoad,          // biomass
		PeakLoad,          // generators
		BaseLoad,          // waste to energy
	};

	int32_t ClampHistory(double value)
	{
		if (!(value > 0.0)) return 0;
		return value >= 2147483647.0 ? 0x7FFFFFFF : static_cast<int32_t>(value + 0.5);
	}

	struct NeonGrid
	{
		double baseProduced = 0, intermediateProduced = 0, peakProduced = 0;
		double baseUsed = 0, intermediateUsed = 0, peakUsed = 0, peakShortage = 0;
		double transmissionFree = 0, distributionFree = 0, transformerFree = 0;
	};

	double ReadField(cISCLua* lua, const char* name)
	{
		lua->PushString(name);
		lua->GetTable(-2);
		const double value = lua->IsNumber(-1) ? lua->ToNumber(-1) : 0.0;
		lua->Pop(1);
		return value;
	}

	// NEON (SC4 Next-gen Energy Operating System) in the advisor Lua: run its grid balance and read grid_logic_var.
	bool ReadNeon(cISC4City* city, NeonGrid& out)
	{
		cISC4AdvisorSystem* const advisors = city->GetAdvisorSystem();
		cISCLua* const lua = advisors ? advisors->GetScriptingContext() : nullptr;
		if (!lua) return false;
		const int32_t top = lua->GetTop();

		lua->GetGlobal("run_grid_balance_variables");
		if (!lua->IsFunction(-1))
		{
			lua->SetTop(top);
			return false;
		}
		if (lua->CallProtected(0, 0) != 0)
		{
			const char* const error = lua->IsString(-1) ? lua->ToString(-1) : "?";
			SimLog::Write(SimLog::Level::Warning, "PowerGraph: NEON run_grid_balance_variables failed: %s", error);
			lua->SetTop(top);
			return false;
		}

		lua->GetGlobal("grid_logic_var");
		if (!lua->IsTable(-1))
		{
			lua->SetTop(top);
			return false;
		}
		out.baseProduced = ReadField(lua, "GEN_B_POWER");
		out.intermediateProduced = ReadField(lua, "GEN_I_POWER");
		out.peakProduced = ReadField(lua, "GEN_P_POWER");
		out.baseUsed = ReadField(lua, "USED_B_POWER");
		out.intermediateUsed = ReadField(lua, "USED_I_POWER");
		out.peakUsed = ReadField(lua, "USED_P_POWER");
		out.peakShortage = ReadField(lua, "SHORT_P_POWER");
		out.transmissionFree = ReadField(lua, "TRANSMIT_AVAILABLE");
		out.distributionFree = ReadField(lua, "DISTRIB_AVAILABLE");
		out.transformerFree = ReadField(lua, "TRANSFORM_AVAILABLE");
		lua->SetTop(top);
		return true;
	}

	// First Uint32 of a property, single value or array.
	bool ReadFirstUint32(cISCPropertyHolder* holder, uint32_t id, uint32_t& value)
	{
		const cISCProperty* const property = holder ? holder->GetProperty(id) : nullptr;
		const cIGZVariant* const variant = property ? property->GetPropertyValue() : nullptr;
		if (!variant || (variant->GetType() & ~static_cast<uint16_t>(cIGZVariant::TypeArray)) != cIGZVariant::Uint32)
		{
			return false;
		}
		if (variant->GetType() == cIGZVariant::Uint32)
		{
			return variant->GetValUint32(value);
		}
		const uint32_t* const values = variant->RefUint32();
		if (!values || variant->GetCount() == 0) return false;
		value = values[0];
		return true;
	}

	EnergyClass ClassOf(cISCPropertyHolder* holder)
	{
		uint32_t extended = 0;
		if (ReadFirstUint32(holder, kPropExtendedPowerType, extended) && extended != 0)
		{
			for (size_t i = 0; i < kClasses.size(); ++i)
			{
				if (kClasses[i].extendedPowerType == extended) return static_cast<EnergyClass>(i);
			}
			return None; // power storage, transformers: not plotted
		}

		uint32_t type = 0;
		if (!ReadFirstUint32(holder, kPropPowerPlantType, type)) return None;
		switch (type)
		{
		case 1: return Coal;
		case 2: return NuclearFusion;   // Hydrogen: the Maxis fusion plant
		case 3: return NaturalGas;
		case 4: return Microwave;
		case 5: return NuclearFission;
		case 6: return Oil;
		case 7: return Solar;
		case 8: return WasteToEnergy;
		case 9: return Wind;
		default: return None;           // 10 auxiliary
		}
	}

	class PowerBuildingFilter final : public cISC4OccupantFilter
	{
	public:
		bool QueryInterface(uint32_t riid, void** ppvObj) override
		{
			if (riid == GZIID_cISC4OccupantFilter || riid == GZIID_cIGZUnknown)
			{
				*ppvObj = this;
				AddRef();
				return true;
			}
			return false;
		}
		uint32_t AddRef() override { return ++refCount; }
		uint32_t Release() override { return refCount > 0 ? --refCount : 0; }
		bool IsOccupantIncluded(cISC4Occupant* occupant) override
		{
			return occupant && occupant->GetType() == static_cast<int32_t>(kOccupantTypeBuilding)
				&& occupant->IsOccupantGroup(kOccupantGroupPower);
		}
		bool IsOccupantTypeIncluded(uint32_t type) override { return type == kOccupantTypeBuilding; }
		bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return true; }

	private:
		uint32_t refCount = 0;
	};

	struct IterationData
	{
		std::unordered_set<cISC4Occupant*> seen;
		std::vector<cISC4Occupant*> plants;
	};

	bool CollectPlant(cISC4Occupant* occupant, void* data)
	{
		IterationData& iteration = *static_cast<IterationData*>(data);
		if (iteration.seen.insert(occupant).second)
		{
			cISCPropertyHolder* const holder = occupant->AsPropertyHolder();
			if (holder && holder->HasProperty(kPropPowerGenerated))
			{
				iteration.plants.push_back(occupant);
			}
		}
		return true;
	}

	void RecordMonth()
	{
		cISC4AppPtr app;
		cISC4City* const city = app ? app->GetCity() : nullptr;
		cISC4OccupantManager* const occupants = city ? city->GetOccupantManager() : nullptr;
		cISC4PowerSimulator* const power = city ? city->GetPowerSimulator() : nullptr;
		cISC4HistoryWarehouse* const history = city ? city->GetHistoryWarehouse() : nullptr;
		if (!occupants || !power || !history) return;

		IterationData iteration;
		PowerBuildingFilter filter;
		occupants->IterateOccupants(CollectPlant, &iteration, nullptr, nullptr, &filter);

		std::array<uint64_t, ClassCount> totals{};
		for (cISC4Occupant* const plant : iteration.plants)
		{
			const EnergyClass energyClass = ClassOf(plant->AsPropertyHolder());
			if (energyClass == None) continue;
			uint32_t produced = 0;
			uint32_t used = 0;
			power->GetOccupantPowerProduced(plant, produced, used); // leaves both 0 without Power Generated
			totals[energyClass] += produced;
		}

		for (size_t i = 0; i < kClasses.size(); ++i)
		{
			const int32_t value = totals[i] > 0x7FFFFFFFull ? 0x7FFFFFFF : static_cast<int32_t>(totals[i]);
			history->RecordHistoryData(kClasses[i].historyType, value);
		}

		// Power Grid graph.
		std::array<uint64_t, 3> loads{};
		for (size_t i = 0; i < kClasses.size(); ++i) loads[kLoadOfClass[i]] += totals[i];
		NeonGrid neon;
		const bool hasNeon = ReadNeon(city, neon);
		if (hasNeon)
		{
			history->RecordHistoryData(PowerGraphHistory::kHistoryBaseLoad, ClampHistory(neon.baseProduced));
			history->RecordHistoryData(PowerGraphHistory::kHistoryIntermediateLoad, ClampHistory(neon.intermediateProduced));
			history->RecordHistoryData(PowerGraphHistory::kHistoryPeakLoad, ClampHistory(neon.peakProduced));
			history->RecordHistoryData(PowerGraphHistory::kHistoryNeonBaseUsed, ClampHistory(neon.baseUsed));
			history->RecordHistoryData(PowerGraphHistory::kHistoryNeonIntermediateUsed, ClampHistory(neon.intermediateUsed));
			history->RecordHistoryData(PowerGraphHistory::kHistoryNeonPeakUsed, ClampHistory(neon.peakUsed));
			history->RecordHistoryData(PowerGraphHistory::kHistoryNeonPeakShortage, ClampHistory(neon.peakShortage));
			history->RecordHistoryData(PowerGraphHistory::kHistoryNeonTransmissionFree, ClampHistory(neon.transmissionFree));
			history->RecordHistoryData(PowerGraphHistory::kHistoryNeonDistributionFree, ClampHistory(neon.distributionFree));
			history->RecordHistoryData(PowerGraphHistory::kHistoryNeonTransformerFree, ClampHistory(neon.transformerFree));
		}
		else
		{
			history->RecordHistoryData(PowerGraphHistory::kHistoryBaseLoad, ClampHistory(static_cast<double>(loads[BaseLoad])));
			history->RecordHistoryData(PowerGraphHistory::kHistoryIntermediateLoad,
				ClampHistory(static_cast<double>(loads[IntermediateLoad])));
			history->RecordHistoryData(PowerGraphHistory::kHistoryPeakLoad, ClampHistory(static_cast<double>(loads[PeakLoad])));
		}
		SimLog::Write(SimLog::Level::Debug, "PowerGraph: grid %s; base %.0f, intermediate %.0f, peak %.0f.",
			hasNeon ? "from NEON" : "from the plants",
			hasNeon ? neon.baseProduced : static_cast<double>(loads[BaseLoad]),
			hasNeon ? neon.intermediateProduced : static_cast<double>(loads[IntermediateLoad]),
			hasNeon ? neon.peakProduced : static_cast<double>(loads[PeakLoad]));

		SimLog::Write(SimLog::Level::Debug,
			"PowerGraph: %u plants; coal %llu, oil %llu, gas %llu, fission %llu, fusion %llu, solar %llu, wind %llu, "
			"geothermal %llu, hydro %llu, tidal %llu, microwave %llu, biomass %llu, generators %llu, waste to energy %llu.",
			static_cast<unsigned>(iteration.plants.size()), totals[Coal], totals[Oil], totals[NaturalGas],
			totals[NuclearFission], totals[NuclearFusion], totals[Solar], totals[Wind], totals[Geothermal],
			totals[Hydroelectric], totals[Tidal], totals[Microwave], totals[Biomass], totals[ElectricGenerator],
			totals[WasteToEnergy]);
	}
}

void PowerGraphHistory::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[16]{};
	GetPrivateProfileStringW(L"PowerGraph", L"Enabled", L"true", value, 16, ini.c_str());
	std::wstring text(value);
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	SimLog::Write(SimLog::Level::Info, "PowerGraph: production by energy class %s.", enabled ? "on" : "off");
}

void PowerGraphHistory::DoMessage(cIGZMessage2* message)
{
	if (!enabled || message->GetType() != kMessageSimNewMonth) return;
	RecordMonth();
}
