// SC4 SIM: deathcare, the part of the Afterlife that collects the dead (cemeteries and crematoria), in the spirit of
// Cities: Skylines. Driven by Afterlife.cpp once a month. Notes: docs/deathcare.md.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class cISC4City;

namespace Deathcare
{
	// Exemplar properties (defined in New_Properties.xml of New_Properties.xml-patches).
	constexpr uint32_t kPropertyType = 0x5C4DD001;       // Uint32: 1 cemetery, 2 crematorium
	constexpr uint32_t kPropertyCapacity = 0x5C4DD002;   // Uint32: graves (cemetery) or bodies per month (crematorium)
	constexpr uint32_t kPropertyRadius = 0x5C4DD003;     // Float32: service radius in meters
	constexpr uint32_t kOccupantGroupCemetery = 0x1700;  // Maxis "Building: Cemetery"
	constexpr uint32_t kOccupantGroupCrematorium = 0x5C4DD100; // SC4SIM "Building: Crematorium"
	constexpr uint32_t kOccupantGroupDeathcareCemetery = 0x5C4DD101; // SC4SIM "Building: Cemetery (SC4SIM Deathcare)"

	// The residential simulator's tracts, as read by Afterlife.cpp for the generation resets.
	struct TractGrid
	{
		int32_t countX = 0, countZ = 0, shift = 0; // tracts, and log2 of the tract size in cells
		std::vector<double> population;            // residents per tract (all wealths), index z * countX + x
	};

	// City totals of the month, copied to afterlife_grid_var.
	struct Totals
	{
		double collected = 0, cremated = 0, buried = 0, uncollected = 0, relocation = 0, emptied = 0;
		double gravesUsed = 0, gravesTotal = 0, cremationCapacity = 0;
		double cemeteries = 0, crematoria = 0, cemeteriesNearlyFull = 0;
	};

	// [Afterlife] keys of SC4SIM.ini (DEATHCARE, DEATHCARE_EFFECTS, ...).
	void LoadSettings(const wchar_t* ini);
	bool Enabled();
	// The open city (nullptr when it closes): registers the query hook and forgets every city-scoped value.
	void SetCity(cISC4City* city);
	// One month: places deathsPerTract (and the uncollected dead of earlier months) in cemeteries and crematoria in
	// range, empties full cemeteries into crematoria, keeps the rest as uncollected and applies the effects.
	void RunMonth(const TractGrid& tracts, const std::vector<double>& deathsPerTract);
	const Totals& GetTotals();

	// Saved with the city (the Afterlife's section of SC4SIM_Afterlife.ini).
	void LoadKeys(const std::map<std::wstring, std::wstring>& keys);
	void SaveKeys(const std::function<void(const std::wstring&, const std::wstring&)>& write);
}
