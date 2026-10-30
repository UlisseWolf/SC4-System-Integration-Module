////////////////////////////////////////////////////////////////////////////
//
// SC4 SIM: a dedicated manufacturing (I-M) zone.
//
// SimCity 4 has a fixed list of 16 zone types, and every table and the save file are sized for 16, so the new zone
// reuses one slot: Spaceport (13) by default, ZoneSlot in SC4SIM.ini. Military (10) is the other candidate. In the
// base game the I-D, I-M and I-HT growth developers all ask for zone 8, and zone 9 takes zone-8 buildings, so
// nothing keeps manufacturing in a zone of its own. This module follows the hook plan of the decompilation notes
// (docs/manufacturing-zone.md):
//
//  1. GetConstructionInfo (0x006B38E0, growth developer vtable): the I-M developer (purpose 7) asks for the new
//     zone. BuildingFitsInZone already accepts a building in its own zone, so I-M grows in the new zone and no
//     longer in zones 8 and 9; I-D and I-HT still ask for zone 8 and never fit the new zone.
//  2. GrowthStageFitsInZone (0x00695340): every stage fits the new zone (the game says "never" for slots 10-15, and
//     every lot would fail with reason 6, WrongZone).
//  3. IsSuitableForConstruction (0x00695430, building development simulator vtable): the land-value wealth test is
//     skipped in the new zone, as the game does in zones 7, 8 and 9 (IgnoreLandValue).
//  4. IsRCIZone (0x004DC300): true for the new zone (RCIZoneHook).
//  5. PlaceZone (zone manager vtable): with the IMZone brush on, the Dense Industrial zone tool places the new zone.
//     IMZoneConvert turns every empty dense industrial cell of the city into the new zone.
//
// How the code stays safe without the game's bytes:
//  * virtual functions are found by scanning the object's vtable for the address of the notes; when it is not
//    there (another version, another DLL), that hook is not installed;
//  * GrowthStageFitsInZone and IsRCIZone are pure functions. Before patching, the module calls the original for
//    every input it can receive and compares the answers with the rules of the notes. The probe also measures how
//    many bytes the callee pops, so the calling convention and the argument order are measured, not assumed. The
//    replacement answers from the recorded table and adds only the new zone, so every other zone behaves exactly
//    as the original did. If the probe disagrees with the notes, nothing is patched;
//  * the field of the construction profile that holds the zone is found by calling the original
//    GetConstructionInfo of five developers whose zones the notes give (R$ 1, CS$ 4, I-A 7, I-D 8, I-HT 8).
//
// The I-M redirect (1) is installed only together with the stage hook (2): without it every I-M lot in the new
// zone would fail, and I-M would grow nowhere. Without the redirect the game is unchanged.
//
// SimCity 4 1.1.641 only. Experimental: statically checked, not tested in game.
//
////////////////////////////////////////////////////////////////////////////

#include "ManufacturingZone.h"
#include "SimLog.h"
#include "VersionDetection.h"
#include "SC4NotificationDialog.h"

#include "cIGZCheatCodeManager.h"
#include "cIGZMessage2Standard.h"
#include "cISC4App.h"
#include "cISC4BuildingDevelopmentSimulator.h"
#include "cISC4City.h"
#include "cISC4GrowthDeveloper.h"
#include "cISC4Lot.h"
#include "cISC4LotManager.h"
#include "cISC4ZoneManager.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "SC4CellRegion.h"

#include <Windows.h>
#include <array>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <string>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace
{
	// ---------------------------------------------------------------- messages, cheats, settings

	constexpr uint32_t kMessageCheatIssued = 0x230E27AC;
	constexpr uint32_t kMessagePostCityInit = 0x26D31EC1;
	constexpr uint32_t kMessagePreCityShutdown = 0x26D31EC2;

	constexpr uint32_t kCheatBrush = 0x8E9F3630;    // IMZone
	constexpr uint32_t kCheatConvert = 0x8E9F3631;  // IMZoneConvert
	constexpr uint32_t kCheatInfo = 0x8E9F3632;     // IMZoneInfo

	using ZoneType = cISC4ZoneManager::ZoneType;
	using DeveloperType = cISC4BuildingDevelopmentSimulator::DeveloperType;
	using PurposeType = cISC4BuildingOccupant::PurposeType;

	struct Settings
	{
		bool enabled = false;
		uint32_t slot = 13;
		bool stageHook = true;
		bool rciZoneHook = true;
		bool ignoreLandValue = true;
	} settings;

	bool installAttempted = false;
	cISC4City* city = nullptr;
	cRZAutoRefCount<cIGZCheatCodeManager> cheatManager;
	bool brushOn = false;

	// ---------------------------------------------------------------- SimCity 4 1.1.641 (decompilation notes)

	constexpr uintptr_t kGetConstructionInfo = 0x006B38E0;
	constexpr uintptr_t kGrowthStageFitsInZone = 0x00695340;
	constexpr uintptr_t kIsSuitableForConstruction = 0x00695430;
	constexpr uintptr_t kIsRCIZone = 0x004DC300;

	constexpr size_t kVtableScanSlots = 96;
	constexpr int32_t kReasonWrongLandValue = 22;   // cISC4TractDeveloper::Outcome
	constexpr int32_t kReasonSuccess = 27;
	constexpr size_t kProfileProbeSize = 0x100;     // larger than any construction profile

	// The gzcom-dll interface order gives these slots; the scan confirms them against the addresses above.
	constexpr size_t kZoneManagerPlaceZoneSlot = 11;
	constexpr size_t kZoneManagerIsRCIZoneSlot = 13;

	// ---------------------------------------------------------------- memory helpers

	bool WriteBytes(uintptr_t address, const void* bytes, size_t size)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
		std::memcpy(reinterpret_cast<void*>(address), bytes, size);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &ignored);
		return true;
	}

	bool WriteDword(uintptr_t address, uint32_t value)
	{
		return WriteBytes(address, &value, sizeof(value));
	}

	// A 5-byte JMP at the entry of a function the module replaces completely; the original bytes are kept for
	// the uninstall.
	struct EntryJump
	{
		uintptr_t address = 0;
		std::array<uint8_t, 5> original{};
		bool installed = false;

		bool Install(uintptr_t at, const void* target)
		{
			address = at;
			std::memcpy(original.data(), reinterpret_cast<const void*>(at), original.size());
			std::array<uint8_t, 5> jump{ 0xE9 };
			const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (at + 5));
			std::memcpy(jump.data() + 1, &rel, sizeof(rel));
			installed = WriteBytes(at, jump.data(), jump.size());
			return installed;
		}

		void Uninstall()
		{
			if (installed) WriteBytes(address, original.data(), original.size());
			installed = false;
		}
	};

	// A replaced vtable entry.
	struct VtableEntry
	{
		uintptr_t entryAddress = 0;
		uint32_t original = 0;
		bool installed = false;

		bool Install(uintptr_t entry, const void* hook)
		{
			entryAddress = entry;
			original = *reinterpret_cast<const uint32_t*>(entry);
			installed = WriteDword(entry, reinterpret_cast<uint32_t>(hook));
			return installed;
		}

		void Uninstall()
		{
			if (installed) WriteDword(entryAddress, original);
			installed = false;
		}
	};

	// The vtable slot of `object` that points to `function`, or SIZE_MAX.
	size_t FindVtableSlot(const void* object, uintptr_t function)
	{
		const uint32_t* const vtable = *static_cast<const uint32_t* const*>(object);
		for (size_t slot = 0; slot < kVtableScanSlots; ++slot)
		{
			if (vtable[slot] == function) return slot;
		}
		return SIZE_MAX;
	}

	uintptr_t VtableEntryAddress(const void* object, size_t slot)
	{
		return reinterpret_cast<uintptr_t>(*static_cast<const uint32_t* const*>(object) + slot);
	}

	bool IsInGameImage(uintptr_t address)
	{
		const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
		return address >= base && address < base + nt->OptionalHeader.SizeOfImage;
	}

	// An entry the module may patch: not already a jump, call or breakpoint left by another DLL or a debugger.
	bool IsUnpatchedEntry(uintptr_t address)
	{
		const uint8_t first = *reinterpret_cast<const uint8_t*>(address);
		return first != 0xE9 && first != 0xE8 && first != 0xEB && first != 0xCC;
	}

	// ---------------------------------------------------------------- probing pure functions

	// Calls fn with ecx = self and the stack arguments `args` (pushed right to left), and measures how many argument
	// bytes the callee popped: all of them for __thiscall / __stdcall, none for __cdecl.
	uint8_t ProbeCall1(uintptr_t fn, void* self, uint32_t a, uint32_t& popped)
	{
		uint32_t before = 0, after = 0;
		uint8_t result = 0;
		__asm
		{
			mov before, esp
			push a
			mov ecx, self
			call fn
			mov result, al
			mov after, esp
			mov esp, before
		}
		popped = after + 4 - before;
		return result;
	}

	uint8_t ProbeCall2(uintptr_t fn, void* self, uint32_t a, uint32_t b, uint32_t& popped)
	{
		uint32_t before = 0, after = 0;
		uint8_t result = 0;
		__asm
		{
			mov before, esp
			push b
			push a
			mov ecx, self
			call fn
			mov result, al
			mov after, esp
			mov esp, before
		}
		popped = after + 8 - before;
		return result;
	}

	// The rules of the decompilation notes.
	bool NotesIsRCIZone(uint32_t zone)
	{
		return zone >= 1 && zone <= 9;
	}

	bool NotesStageFits(uint32_t zone, uint32_t stage)
	{
		switch (zone)
		{
		case 1: case 4: return stage <= 3;
		case 2: case 5: return stage <= 6;
		case 3: case 6: case 7: case 8: case 9: return true;
		default: return false;
		}
	}

	// ---------------------------------------------------------------- hook: IsRCIZone

	std::array<bool, 256> rciTable{};
	EntryJump rciJump;

	bool __fastcall Replacement_IsRCIZone(void* /*zoneManager*/, void* /*edx*/, uint32_t zone)
	{
		if (zone == settings.slot) return true;
		return zone < rciTable.size() ? rciTable[zone] : false;
	}

	bool InstallRCIZoneHook(cISC4ZoneManager* zoneManager)
	{
		if (FindVtableSlot(zoneManager, kIsRCIZone) != kZoneManagerIsRCIZoneSlot)
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: IsRCIZone is not at 0x%08X in the zone manager: hook not installed.", kIsRCIZone);
			return false;
		}
		if (!IsUnpatchedEntry(kIsRCIZone))
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: IsRCIZone is already patched by another DLL: hook not installed.");
			return false;
		}
		for (uint32_t zone = 0; zone < rciTable.size(); ++zone)
		{
			uint32_t popped = 0;
			rciTable[zone] = ProbeCall1(kIsRCIZone, zoneManager, zone, popped) != 0;
			if (popped != 4 || (zone < 16 && rciTable[zone] != NotesIsRCIZone(zone)))
			{
				SimLog::Write(SimLog::Level::Error,
					"ManufacturingZone: IsRCIZone(%u) = %d, %u bytes popped: differs from the notes, hook not installed.",
					zone, rciTable[zone] ? 1 : 0, popped);
				return false;
			}
		}
		return rciJump.Install(kIsRCIZone, reinterpret_cast<const void*>(&Replacement_IsRCIZone));
	}

	// ---------------------------------------------------------------- hook: GrowthStageFitsInZone

	// stageTable[zone][stage] recorded from the original, for zones 0-15 and stages 0-255.
	std::array<std::array<bool, 256>, 16> stageTable{};
	EntryJump stageJump;
	uint32_t stageSlotHits = 0;

	bool StageFits(uint32_t zone, uint32_t stage)
	{
		zone &= 0xFF;
		stage &= 0xFF;
		if (zone == settings.slot)
		{
			++stageSlotHits;
			return true;
		}
		return zone < stageTable.size() ? stageTable[zone][stage] : false;
	}

	// One replacement per measured calling convention and argument order.
	bool __fastcall Stage_CalleeClean_ZoneStage(void*, void*, uint32_t a, uint32_t b) { return StageFits(a, b); }
	bool __fastcall Stage_CalleeClean_StageZone(void*, void*, uint32_t a, uint32_t b) { return StageFits(b, a); }
	bool __cdecl Stage_CallerClean_ZoneStage(uint32_t a, uint32_t b) { return StageFits(a, b); }
	bool __cdecl Stage_CallerClean_StageZone(uint32_t a, uint32_t b) { return StageFits(b, a); }

	bool InstallStageHook(void* bds)
	{
		if (!IsUnpatchedEntry(kGrowthStageFitsInZone))
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: GrowthStageFitsInZone is already patched: hook not installed.");
			return false;
		}

		// Both argument orders against the notes, zones 0-15 and stages 1-15.
		uint32_t popped = 0, firstPopped = UINT32_MAX;
		int mismatchZoneStage = 0, mismatchStageZone = 0;
		for (uint32_t a = 0; a < 16; ++a)
		{
			for (uint32_t b = 0; b < 16; ++b)
			{
				const bool result = ProbeCall2(kGrowthStageFitsInZone, bds, a, b, popped) != 0;
				if (firstPopped == UINT32_MAX) firstPopped = popped;
				if (popped != firstPopped || (popped != 0 && popped != 8))
				{
					SimLog::Write(SimLog::Level::Error, "ManufacturingZone: GrowthStageFitsInZone pops %u bytes: unknown calling convention, hook not installed.", popped);
					return false;
				}
				if (b >= 1 && result != NotesStageFits(a, b)) ++mismatchZoneStage;   // (zone, stage)
				if (a >= 1 && result != NotesStageFits(b, a)) ++mismatchStageZone;   // (stage, zone)
			}
		}
		const bool zoneFirst = mismatchZoneStage == 0;
		if (!zoneFirst && mismatchStageZone != 0)
		{
			SimLog::Write(SimLog::Level::Error,
				"ManufacturingZone: GrowthStageFitsInZone differs from the notes in either argument order (%d / %d mismatches): hook not installed.",
				mismatchZoneStage, mismatchStageZone);
			return false;
		}
		const bool calleeClean = firstPopped == 8;

		for (uint32_t zone = 0; zone < stageTable.size(); ++zone)
		{
			for (uint32_t stage = 0; stage < 256; ++stage)
			{
				stageTable[zone][stage] = (zoneFirst
					? ProbeCall2(kGrowthStageFitsInZone, bds, zone, stage, popped)
					: ProbeCall2(kGrowthStageFitsInZone, bds, stage, zone, popped)) != 0;
			}
		}

		const void* replacement = calleeClean
			? (zoneFirst ? reinterpret_cast<const void*>(&Stage_CalleeClean_ZoneStage) : reinterpret_cast<const void*>(&Stage_CalleeClean_StageZone))
			: (zoneFirst ? reinterpret_cast<const void*>(&Stage_CallerClean_ZoneStage) : reinterpret_cast<const void*>(&Stage_CallerClean_StageZone));
		if (!stageJump.Install(kGrowthStageFitsInZone, replacement)) return false;
		SimLog::Write(SimLog::Level::Info, "ManufacturingZone: GrowthStageFitsInZone measured as %s, arguments (%s).",
			calleeClean ? "__thiscall/__stdcall" : "__cdecl", zoneFirst ? "zone, stage" : "stage, zone");
		return true;
	}

	// ---------------------------------------------------------------- hook: GetConstructionInfo

	using GetConstructionInfoFn = void(__thiscall*)(void* developer, void* profile);
	VtableEntry constructionInfoEntry;
	size_t zoneFieldOffset = SIZE_MAX;
	size_t zoneFieldWidth = 0;   // 4 (dword) or 1 (byte)
	uint32_t redirects = 0;

	uint32_t ReadField(const uint8_t* profile, size_t offset, size_t width)
	{
		if (width == 1) return profile[offset];
		uint32_t value = 0;
		std::memcpy(&value, profile + offset, sizeof(value));
		return value;
	}

	void __fastcall Hook_GetConstructionInfo(void* developer, void* /*edx*/, uint8_t* profile)
	{
		reinterpret_cast<GetConstructionInfoFn>(kGetConstructionInfo)(developer, profile);
		if (profile && static_cast<cISC4GrowthDeveloper*>(developer)->GetPurpose() == PurposeType::Manufacturing)
		{
			if (zoneFieldWidth == 1) profile[zoneFieldOffset] = static_cast<uint8_t>(settings.slot);
			else std::memcpy(profile + zoneFieldOffset, &settings.slot, sizeof(uint32_t));
			++redirects;
		}
	}

	// The offset of the zone in the construction profile: the one field where R$, CS$, I-A, I-D and I-HT hold the
	// zones of the notes (1, 4, 7, 8, 8).
	bool FindZoneField(cISC4BuildingDevelopmentSimulator* bds)
	{
		struct Probe { DeveloperType type; uint32_t zone; };
		constexpr Probe probes[] = {
			{ DeveloperType::ResidentialLowWealth, 1 }, { DeveloperType::CommercialServicesLowWealth, 4 },
			{ DeveloperType::IndustrialAgriculture, 7 }, { DeveloperType::IndustrialProcessing, 8 },
			{ DeveloperType::IndustrialHighTech, 8 }, { DeveloperType::IndustrialManufacturing, 8 } };

		alignas(4) uint8_t profiles[std::size(probes)][kProfileProbeSize]{};
		for (size_t i = 0; i < std::size(probes); ++i)
		{
			cISC4GrowthDeveloper* const developer = bds->GetDeveloper(probes[i].type);
			if (!developer) return false;
			reinterpret_cast<GetConstructionInfoFn>(kGetConstructionInfo)(developer, profiles[i]);
		}

		for (size_t width : { size_t(4), size_t(1) })
		{
			size_t found = SIZE_MAX, count = 0;
			for (size_t offset = 0; offset + width <= kProfileProbeSize; offset += width)
			{
				bool all = true;
				for (size_t i = 0; i < std::size(probes) && all; ++i)
				{
					all = ReadField(profiles[i], offset, width) == probes[i].zone;
				}
				if (all) { found = offset; ++count; }
			}
			if (count == 1)
			{
				zoneFieldOffset = found;
				zoneFieldWidth = width;
				return true;
			}
			if (count > 1) return false;   // ambiguous: do not guess
		}
		return false;
	}

	bool InstallConstructionInfoHook(cISC4BuildingDevelopmentSimulator* bds)
	{
		cISC4GrowthDeveloper* const developer = bds->GetDeveloper(DeveloperType::IndustrialManufacturing);
		const size_t slot = developer ? FindVtableSlot(developer, kGetConstructionInfo) : SIZE_MAX;
		if (slot == SIZE_MAX)
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: GetConstructionInfo is not at 0x%08X in the growth developer: I-M redirect not installed.", kGetConstructionInfo);
			return false;
		}
		if (!FindZoneField(bds))
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: zone field of the construction profile not found: I-M redirect not installed.");
			return false;
		}
		SimLog::Write(SimLog::Level::Info, "ManufacturingZone: construction profile zone at +0x%02X (%u byte%s).",
			static_cast<uint32_t>(zoneFieldOffset), static_cast<uint32_t>(zoneFieldWidth), zoneFieldWidth == 1 ? "" : "s");
		return constructionInfoEntry.Install(VtableEntryAddress(developer, slot), reinterpret_cast<const void*>(&Hook_GetConstructionInfo));
	}

	// ---------------------------------------------------------------- hook: IsSuitableForConstruction

	// gzcom-dll declares the profile as `intptr_t const&`; the module reads it as a pointer to the construction
	// profile. If it were a pointer to a pointer, the read lands on the caller's stack and simply does not match.
	using IsSuitableFn = bool(__thiscall*)(void* bds, const uint8_t* profile, int32_t* reason);
	VtableEntry suitableEntry;
	uint32_t landValueSkips = 0;

	// Land value is the last test of the original (zone, lot, power, land value): when only it failed in the new
	// zone, the building is suitable, as in zones 7, 8 and 9.
	bool __fastcall Hook_IsSuitableForConstruction(void* bds, void* /*edx*/, const uint8_t* profile, int32_t* reason)
	{
		const bool ok = reinterpret_cast<IsSuitableFn>(kIsSuitableForConstruction)(bds, profile, reason);
		if (!ok && reason && *reason == kReasonWrongLandValue && profile
			&& ReadField(profile, zoneFieldOffset, zoneFieldWidth) == settings.slot)
		{
			*reason = kReasonSuccess;
			++landValueSkips;
			return true;
		}
		return ok;
	}

	bool InstallLandValueHook(cISC4BuildingDevelopmentSimulator* bds)
	{
		const size_t slot = FindVtableSlot(bds, kIsSuitableForConstruction);
		if (slot == SIZE_MAX)
		{
			SimLog::Write(SimLog::Level::Warning, "ManufacturingZone: IsSuitableForConstruction not found: the new zone keeps the land-value wealth test.");
			return false;
		}
		return suitableEntry.Install(VtableEntryAddress(bds, slot), reinterpret_cast<const void*>(&Hook_IsSuitableForConstruction));
	}

	// ---------------------------------------------------------------- hook: PlaceZone (IMZone brush)

	using PlaceZoneFn = bool(__thiscall*)(void*, SC4CellRegion<int32_t>*, int32_t, bool, bool, bool, bool, bool, int64_t*, int32_t*, intptr_t);
	VtableEntry placeZoneEntry;
	uint32_t brushPlacements = 0;

	bool __fastcall Hook_PlaceZone(void* zoneManager, void* /*edx*/, SC4CellRegion<int32_t>* region, int32_t zone,
		bool placeZone, bool skipFundsCheck, bool checkAreaSlope, bool skipZoneSizeCheck, bool checkLotSize,
		int64_t* zonedCells, int32_t* errorCode, intptr_t demolishedLots)
	{
		if (brushOn && zone == static_cast<int32_t>(ZoneType::IndustrialHighDensity))
		{
			zone = static_cast<int32_t>(settings.slot);
			if (placeZone) ++brushPlacements;
		}
		return reinterpret_cast<PlaceZoneFn>(placeZoneEntry.original)(zoneManager, region, zone, placeZone, skipFundsCheck,
			checkAreaSlope, skipZoneSizeCheck, checkLotSize, zonedCells, errorCode, demolishedLots);
	}

	bool InstallBrushHook(cISC4ZoneManager* zoneManager)
	{
		const uintptr_t entry = VtableEntryAddress(zoneManager, kZoneManagerPlaceZoneSlot);
		if (!IsInGameImage(*reinterpret_cast<const uint32_t*>(entry)))
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: PlaceZone is already hooked by another DLL: IMZone brush not installed.");
			return false;
		}
		return placeZoneEntry.Install(entry, reinterpret_cast<const void*>(&Hook_PlaceZone));
	}

	// ---------------------------------------------------------------- install

	bool redirectInstalled = false;

	void Install(cISC4City* openedCity)
	{
		installAttempted = true;
		const uint16_t version = VersionDetection::GetInstance().GetGameVersion();
		if (version != 641)
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: requires game version 641, found %u: not installed.", version);
			return;
		}
		cISC4ZoneManager* const zoneManager = openedCity->GetZoneManager();
		cISC4BuildingDevelopmentSimulator* const bds = openedCity->GetBuildingDevelopmentSimulator();
		if (!zoneManager || !bds)
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: zone manager or building development simulator unavailable: not installed.");
			return;
		}

		// The other hooks only make sense with the redirect: without it the slot keeps its game meaning.
		bool rci = false;
		const bool stage = settings.stageHook && InstallStageHook(bds);
		if (stage && InstallConstructionInfoHook(bds))
		{
			redirectInstalled = true;
			rci = settings.rciZoneHook && InstallRCIZoneHook(zoneManager);
			if (settings.ignoreLandValue) InstallLandValueHook(bds);
			InstallBrushHook(zoneManager);
		}
		else
		{
			// Without the stage hook every I-M lot would fail in the new zone: leave I-M as in the base game.
			stageJump.Uninstall();
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: %s: I-M keeps growing in zones 8 and 9 as in the base game.",
				settings.stageHook ? "the stage hook or the I-M redirect could not be installed" : "StageHook=false");
		}

		SimLog::Write(SimLog::Level::Info,
			"ManufacturingZone: zone slot %u. I-M redirect %s, stage hook %s, IsRCIZone %s, land value skip %s, IMZone brush %s.",
			settings.slot, redirectInstalled ? "on" : "OFF", stageJump.installed ? "on" : "off", rci ? "on" : "off",
			suitableEntry.installed ? "on" : "off", placeZoneEntry.installed ? "on" : "off");
	}

	void Uninstall()
	{
		placeZoneEntry.Uninstall();
		suitableEntry.Uninstall();
		constructionInfoEntry.Uninstall();
		stageJump.Uninstall();
		rciJump.Uninstall();
		redirectInstalled = false;
	}

	// ---------------------------------------------------------------- cheats

	void Notify(const char* text)
	{
		SimLog::Write(SimLog::Level::Info, "ManufacturingZone: %s", text);
		SC4NotificationDialog::ShowDialog(cRZBaseString(text), cRZBaseString("Manufacturing zone"));
	}

	void ToggleBrush()
	{
		if (!placeZoneEntry.installed)
		{
			Notify("The I-M zone is not installed: see SC4SIM.log.");
			return;
		}
		brushOn = !brushOn;
		Notify(brushOn
			? "I-M brush ON: the Dense Industrial zone tool now places the manufacturing zone. Type IMZone again to turn it off."
			: "I-M brush OFF: the Dense Industrial zone tool places dense industry again.");
	}

	void ConvertDenseIndustry()
	{
		cISC4ZoneManager* const zoneManager = city ? city->GetZoneManager() : nullptr;
		cISC4LotManager* const lotManager = city ? city->GetLotManager() : nullptr;
		if (!redirectInstalled || !zoneManager || !lotManager)
		{
			Notify("The I-M zone is not installed or no city is open: see SC4SIM.log.");
			return;
		}
		uint32_t converted = 0, refused = 0;
		const int32_t sizeX = static_cast<int32_t>(city->CellCountX());
		const int32_t sizeZ = static_cast<int32_t>(city->CellCountZ());
		for (int32_t x = 0; x < sizeX; ++x)
		{
			for (int32_t z = 0; z < sizeZ; ++z)
			{
				// Empty dense industrial cells only: lots and buildings are never touched.
				if (zoneManager->GetZoneType(x, z) != ZoneType::IndustrialHighDensity || lotManager->GetLot(x, z, false)) continue;
				SC4CellRegion<int32_t> region(x, z, x, z, true);
				const bool ok = zoneManager->PlaceZone(region, static_cast<ZoneType>(settings.slot), true, true, false, true, true, nullptr, nullptr, 0);
				++(ok ? converted : refused);
			}
		}
		const std::string text = "IMZoneConvert: " + std::to_string(converted) + " empty dense industrial cell(s) converted to the I-M zone"
			+ (refused ? ", " + std::to_string(refused) + " refused by the game." : ".");
		Notify(text.c_str());
	}

	void Report()
	{
		SimLog::Write(SimLog::Level::Info, "ManufacturingZone: slot %u, I-M redirect %s, brush %s.", settings.slot,
			redirectInstalled ? "on" : "OFF", brushOn ? "on" : "off");
		SimLog::Write(SimLog::Level::Info,
			"ManufacturingZone: I-M construction profiles redirected %u, stage checks passed in the slot %u, land value skips %u, brush placements %u.",
			redirects, stageSlotHits, landValueSkips, brushPlacements);
		if (cISC4ZoneManager* const zoneManager = city ? city->GetZoneManager() : nullptr)
		{
			const auto slot = static_cast<ZoneType>(settings.slot);
			SimLog::Write(SimLog::Level::Info,
				"ManufacturingZone: slot %u in this city: %d cell(s), %d undeveloped; zone size %d-%d, drag colour 0x%08X.",
				settings.slot, zoneManager->GetZoneCount(slot), zoneManager->GetUndevelopedTileCount(slot),
				zoneManager->GetMinZoneSize(slot), zoneManager->GetMaxZoneSize(slot), zoneManager->GetZoneDragColor(slot));
		}
		Notify("IMZoneInfo: report written to SC4SIM.log.");
	}

	// ---------------------------------------------------------------- settings

	std::wstring ReadIni(const std::filesystem::path& ini, const wchar_t* key, const wchar_t* fallback)
	{
		wchar_t value[32]{};
		GetPrivateProfileStringW(L"ManufacturingZone", key, fallback, value, 32, ini.c_str());
		std::wstring result(value);
		for (auto& c : result) c = static_cast<wchar_t>(std::towlower(c));
		return result;
	}

	bool ParseBool(const std::wstring& value, bool fallback)
	{
		if (value == L"true" || value == L"1" || value == L"yes" || value == L"on") return true;
		if (value == L"false" || value == L"0" || value == L"no" || value == L"off") return false;
		return fallback;
	}
}

void ManufacturingZone::OnStart()
{
	wchar_t modulePath[MAX_PATH]{};
	GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
	const std::filesystem::path ini = std::filesystem::path(modulePath).replace_filename(L"SC4SIM.ini");

	settings.enabled = ParseBool(ReadIni(ini, L"Enabled", L"false"), false);
	settings.stageHook = ParseBool(ReadIni(ini, L"StageHook", L"true"), true);
	settings.rciZoneHook = ParseBool(ReadIni(ini, L"RCIZoneHook", L"true"), true);
	settings.ignoreLandValue = ParseBool(ReadIni(ini, L"IgnoreLandValue", L"true"), true);
	const uint32_t slot = static_cast<uint32_t>(GetPrivateProfileIntW(L"ManufacturingZone", L"ZoneSlot", 13, ini.c_str()));
	if (slot >= 10 && slot <= 13)
	{
		settings.slot = slot;
	}
	else
	{
		SimLog::Write(SimLog::Level::Warning, "ManufacturingZone: ZoneSlot=%u is not 10-13: using 13 (Spaceport).", slot);
	}
}

void ManufacturingZone::PostAppInit()
{
	if (!settings.enabled)
	{
		SimLog::Write(SimLog::Level::Info, "ManufacturingZone: disabled in SC4SIM.ini (experimental, Enabled=false).");
		return;
	}
	if (settings.slot == 11 || settings.slot == 12)
	{
		SimLog::Write(SimLog::Level::Warning, "ManufacturingZone: ZoneSlot=%u is the %s zone, which the game uses: expect conflicts.",
			settings.slot, settings.slot == 11 ? "Airport" : "Seaport");
	}

	cISC4AppPtr app;
	if (cIGZCheatCodeManager* cheats = app ? app->GetCheatCodeManager() : nullptr)
	{
		const bool ok = cheats->RegisterCheatCode(kCheatBrush, cRZBaseString("IMZone"))
			&& cheats->RegisterCheatCode(kCheatConvert, cRZBaseString("IMZoneConvert"))
			&& cheats->RegisterCheatCode(kCheatInfo, cRZBaseString("IMZoneInfo"));
		if (ok) cheatManager = cheats;
		else SimLog::Write(SimLog::Level::Warning, "ManufacturingZone: could not register the IMZone cheats.");
	}
	SimLog::Write(SimLog::Level::Info, "ManufacturingZone: enabled, zone slot %u; the hooks are installed when a city opens.", settings.slot);
}

void ManufacturingZone::PostAppShutdown()
{
	if (cheatManager)
	{
		cheatManager->UnregisterCheatCode(kCheatBrush);
		cheatManager->UnregisterCheatCode(kCheatConvert);
		cheatManager->UnregisterCheatCode(kCheatInfo);
		cheatManager.Reset();
	}
	Uninstall();
	city = nullptr;
}

void ManufacturingZone::DoMessage(cIGZMessage2* message)
{
	if (!settings.enabled || !message)
	{
		return;
	}

	auto* const standardMessage = static_cast<cIGZMessage2Standard*>(message);
	switch (message->GetType())
	{
	case kMessagePostCityInit:
		city = static_cast<cISC4City*>(standardMessage->GetVoid1());
		// The hooked vtables and functions belong to the executable, so one install serves every city.
		if (city && !installAttempted) Install(city);
		if (city && redirectInstalled)
		{
			if (cISC4ZoneManager* const zoneManager = city->GetZoneManager())
			{
				SimLog::Write(SimLog::Level::Info, "ManufacturingZone: city opened with %d cell(s) in slot %u.",
					zoneManager->GetZoneCount(static_cast<ZoneType>(settings.slot)), settings.slot);
			}
		}
		break;
	case kMessagePreCityShutdown:
		city = nullptr;
		brushOn = false;
		break;
	case kMessageCheatIssued:
		switch (static_cast<uint32_t>(standardMessage->GetData1()))
		{
		case kCheatBrush: ToggleBrush(); break;
		case kCheatConvert: ConvertDenseIndustry(); break;
		case kCheatInfo: Report(); break;
		}
		break;
	}
}
