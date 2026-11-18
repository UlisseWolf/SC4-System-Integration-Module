////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: criminals, inmates and police / jail ratios, for the Crime and Police & Jails graphs.
//
// Same mechanism as the power graph (src/power/PowerGraphHistory.cpp): one point per series is recorded in the
// city's history warehouse on every SimNewMonth, and CAM's graph exemplars plot them. The game records crimes
// (G_NUM_CRIMES 0x0A5CC155), arrests (G_NUM_ARRESTS 0x0A5CC156), police coverage (G_POLICE_PROTECTION_COVERAGE_P
// 0x4A11D354, percent), average police protection (0x4A11D355) and average crime (GA_CRIME 0xAA662693); it has no
// history of criminals or jails, so the module adds them.
//
// Police simulator: the vtable of cISC4PoliceSimulator in gzcom-dll is one slot short after GetPoliceStationCount
// (0x60): the game's advisor data pump (0x00416340-0x00416940) reads g_jail_capacity at +0x74, g_inmate_count at
// +0x78, g_criminal_count at +0x7C, g_arrest_count at +0x88 and g_jail_count at +0x8C, where the header has them at
// +0x70, +0x74, +0x78, +0x84 and +0x88. These getters are called through the offsets the game itself uses. Crime
// simulator: GetMonthlyCrimeCount (+0x1C, g_crime_commited_count) matches the header.
//
////////////////////////////////////////////////////////////////////////////

#include "CrimeGraphHistory.h"
#include "SimLog.h"

#include "cIGZMessage2Standard.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4CrimeSimulator.h"
#include "cISC4HistoryWarehouse.h"
#include "GZServPtrs.h"

#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr uint32_t kMessageSimNewMonth = 0x66956816;

	// cSC4PoliceSimulator vtable offsets, as called by the game (see above).
	constexpr size_t kPoliceStationCount = 0x60;
	constexpr size_t kJailCapacity = 0x74;
	constexpr size_t kJailPopulation = 0x78;
	constexpr size_t kCriminalCount = 0x7C;
	constexpr size_t kArrestCount = 0x88;
	constexpr size_t kJailCount = 0x8C;

	bool enabled = true;

	uint32_t CallGetter(void* object, size_t offset)
	{
		using Getter = uint32_t(__thiscall*)(void*);
		Getter const* const vtable = *static_cast<Getter* const*>(object);
		return vtable[offset / sizeof(void*)](object);
	}

	int32_t Clamp(uint64_t value)
	{
		return value > 0x7FFFFFFFull ? 0x7FFFFFFF : static_cast<int32_t>(value);
	}

	int32_t Percent(uint64_t part, uint64_t whole)
	{
		if (whole == 0) return 0;
		return Clamp((part * 100u + whole / 2u) / whole);
	}

	void RecordMonth()
	{
		cISC4AppPtr app;
		cISC4City* const city = app ? app->GetCity() : nullptr;
		cISC4CrimeSimulator* const crime = city ? city->GetCrimeSimulator() : nullptr;
		void* const police = city ? static_cast<void*>(city->GetPoliceSimulator()) : nullptr;
		cISC4HistoryWarehouse* const history = city ? city->GetHistoryWarehouse() : nullptr;
		if (!crime || !police || !history) return;

		const uint32_t crimes = crime->GetMonthlyCrimeCount();
		const uint32_t arrests = CallGetter(police, kArrestCount);
		const uint32_t criminals = CallGetter(police, kCriminalCount);
		const uint32_t inmates = CallGetter(police, kJailPopulation);
		const uint32_t capacity = CallGetter(police, kJailCapacity);

		history->RecordHistoryData(CrimeGraphHistory::kHistoryCriminals, Clamp(criminals));
		history->RecordHistoryData(CrimeGraphHistory::kHistoryInmates, Clamp(inmates));
		history->RecordHistoryData(CrimeGraphHistory::kHistoryUnsolvedCrimes, Clamp(crimes > arrests ? crimes - arrests : 0));
		history->RecordHistoryData(CrimeGraphHistory::kHistoryArrestRate, std::min(Percent(arrests, crimes), 100));
		history->RecordHistoryData(CrimeGraphHistory::kHistoryJailOccupancy, Percent(inmates, capacity));
		SimLog::Write(SimLog::Level::Debug,
			"CrimeGraph: %u crimes, %u arrests, %u criminals, %u inmates of %u places, %u police stations, %u jails.",
			crimes, arrests, criminals, inmates, capacity, CallGetter(police, kPoliceStationCount), CallGetter(police, kJailCount));
	}
}

void CrimeGraphHistory::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) return;
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	wchar_t value[16]{};
	GetPrivateProfileStringW(L"CrimeGraph", L"Enabled", L"true", value, 16, ini.c_str());
	std::wstring text(value);
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
	SimLog::Write(SimLog::Level::Info, "CrimeGraph: criminals, inmates and police ratios %s.", enabled ? "on" : "off");
}

void CrimeGraphHistory::DoMessage(cIGZMessage2* message)
{
	if (!enabled || message->GetType() != kMessageSimNewMonth) return;
	RecordMonth();
}
