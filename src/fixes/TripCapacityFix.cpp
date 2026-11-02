////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: fix of the game's "high-tech industry has no $$$ jobs" bug.
//
// cSC4Lot::UpdateJobs (0x006BF6A0, SimCity 4 1.1.641) copies the lot's $, $$ and $$$ job counts into the traffic
// simulator's maximum trip capacity (lot property 0x8A0B4100, indices 0-2), the value the path finder checks when a
// commuter looks for a workplace. For each wealth it rounds the job count, reads the current capacity with
// GetMaxTripCapacity and writes the count with SetMaxTripCapacity only if the two differ by more than 10 %. The three
// reads all push wealth 0 (0x006BF6C9, 0x006BF72F, 0x006BF795), while the writes push 0, 1 and 2. So the $$ and $$$
// counts are compared with the $ capacity: one within 10 % of the $ count is never written, and the lot keeps a $$ or
// $$$ capacity of 0 (a new lot) or a stale one. Stock high-tech industry ($ 10 %, $$ 80 %, $$$ 10 %) loses its $$$
// jobs this way; data fixes only move the numbers, and rounding brings the bug back on small lots. The Medical
// Clinic, the solar and nuclear plants and SC4SIM's mixed lots are hit the same way.
//
// The fix: the operand byte of the $$ read's "push 0" (0x006BF730) becomes 1 and that of the $$$ read (0x006BF796)
// becomes 2, so each check reads its own capacity. Before patching, the module checks the whole sequence around each
// read ("push 0; push edi" before the read call, and the matching "push 1" / "push 2" of the write that follows), so
// it never patches other code. Existing lots catch up the next time the game refreshes their travel population
// (UpdateJobs' only caller is cSC4Lot::AddTravelPopulation, 0x006C0CE1). The bytes are restored when the game closes.
//
////////////////////////////////////////////////////////////////////////////

#include "TripCapacityFix.h"
#include "SimLog.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	struct Site
	{
		const char* name;
		uintptr_t readPush;    // "push 0" (6A 00) of the GetMaxTripCapacity read
		uintptr_t writePush;   // "push wealth" (6A nn) of the SetMaxTripCapacity write for the same wealth
		uint8_t wealth;
		bool patched;
	};

	Site sites[] = {
		{ "$$", 0x006BF72F, 0x006BF782, 1, false },
		{ "$$$", 0x006BF795, 0x006BF7E9, 2, false },
	};

	bool enabled = true;

	bool WriteByte(uintptr_t address, uint8_t value)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(address), 1, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
		*reinterpret_cast<uint8_t*>(address) = value;
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), 1);
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<void*>(address), 1, oldProtect, &ignored);
		return true;
	}

	// push 0; push edi at the read, push wealth; push edi at the write.
	bool SiteMatches(const Site& site)
	{
		const auto* read = reinterpret_cast<const uint8_t*>(site.readPush);
		const auto* write = reinterpret_cast<const uint8_t*>(site.writePush);
		return read[0] == 0x6A && read[1] == 0x00 && read[2] == 0x57
			&& write[0] == 0x6A && write[1] == site.wealth && write[2] == 0x57;
	}
}

void TripCapacityFix::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0)
	{
		return;
	}
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");
	wchar_t value[32]{};
	GetPrivateProfileStringW(L"GameFixes", L"TripCapacityWealthFix", L"true", value, 32, ini.c_str());
	std::wstring text = value;
	for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
	enabled = !(text == L"false" || text == L"0" || text == L"no" || text == L"off");
}

void TripCapacityFix::PostAppInit()
{
	if (!enabled)
	{
		SimLog::Write(SimLog::Level::Info, "TripCapacityFix: disabled in SC4SIM.ini ([GameFixes] TripCapacityWealthFix=false).");
		return;
	}
	// All or nothing: patch only when both sites are the expected 1.1.641 code.
	for (const Site& site : sites)
	{
		if (!SiteMatches(site))
		{
			SimLog::Write(SimLog::Level::Warning,
				"TripCapacityFix: the %s capacity read at 0x%08X is not the expected code (another game version or DLL): not patched.",
				site.name, static_cast<unsigned>(site.readPush));
			return;
		}
	}
	unsigned count = 0;
	for (Site& site : sites)
	{
		site.patched = WriteByte(site.readPush + 1, site.wealth);
		count += site.patched ? 1 : 0;
	}
	SimLog::Write(SimLog::Level::Info,
		"TripCapacityFix: cSC4Lot::UpdateJobs reads the $$ and $$$ trip capacities for their own wealth (%u of 2 sites).", count);
}

void TripCapacityFix::PostAppShutdown()
{
	for (Site& site : sites)
	{
		if (site.patched)
		{
			WriteByte(site.readPush + 1, 0x00);
			site.patched = false;
		}
	}
}
