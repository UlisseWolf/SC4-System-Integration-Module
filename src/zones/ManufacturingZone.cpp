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
//  3. No land-value wealth test in the new zone, as the game does in zones 7, 8 and 9 (IgnoreLandValue).
//     IsSuitableForConstruction (0x00695430) runs the wealth test only when the construction profile has no land
//     value range (bytes +0x10 and +0x11 both 0); the redirect gives the I-M profile the full range 0-255, so the
//     test becomes "land value 0-255", always true, and the checks after it still run.
//  4. IsRCIZone (0x004DC300): true for the new zone (RCIZoneHook).
//  5. PlaceZone (zone manager vtable): with the IMZone brush on, the Dense Industrial zone tool places the new zone.
//     IMZoneConvert turns every empty dense industrial cell of the city into the new zone.
//  6. Zone tuning (ReadSimulatorTunableValues, call at 0x004DE192): after the game reads the Zone Manager exemplar,
//     the slot gets the sizes and costs of medium industry and its own texture and drag colour, so a zoning mod that
//     overrides the exemplar does not undo them (IndustrialTuning, ZoneTexture, DragColour).
//  7. Zone tool cursor (table 0x00A92C40): the slot's cursor becomes the I-M cursor of SC4SIM_ManufacturingZone.dat
//     instead of the high density residential one (Cursor).
//  8. Parcels (0x0072DBA8): the slot skips the residential/commercial parcel pass, like industrial zones 7-9.
//  9. Lot zone list (IsCompatibleWithZoneType, lot configuration vtable 0x00A8CEF4): the growth path checks each
//     lot's Zone Types (0x0070654F); an I-M lot that fits zone 8 or 9 also fits the slot, so the game's and IRM's
//     I-M lots grow there unchanged.
// 10. Industrial checks (0x004E34BF, 0x004E38A5, 0x004E3989, 0x004E3A75, 0x00467827, 0x006CCA12, 0x006CD409): the slot
//     takes every "zone 7-9" path of the lot and zone code, so it is not split into parcels with arrows.
// 11. Zone hover tooltip: the slot's name comes from the LTEXT settings.zoneName (0x004D6C70 case table), and its extra
//     lines are those of dense industry, freight trips and crime (0x004D5586).
// 6-11 are patched at application start (9 acts once the I-M redirect is installed); 1-5 when the first city opens.
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
// Checked against the SimCity 4 1.1.641 executable (SHA-256 1189720d...0f9c), see docs/manufacturing-zone.md:
//  * GetConstructionInfo is slot 9 of the growth developer vtable (0x00AB1488) and writes the zone at +0x00;
//    the tract developer calls it through that slot before every BuildingFitsInZone, so the redirect reaches the
//    whole growth path;
//  * GrowthStageFitsInZone is __cdecl (stage, zone), called directly at 0x007084F4, 0x00708FEA and 0x007098BC;
//  * IsRCIZone is __thiscall (zone), slot 13 of the zone manager vtable (0x00A92E00), with no direct callers;
//  * PlaceZone is slot 11 of the same vtable (0x004DD780, 10 arguments), with no direct callers.
// The run-time measurements below stay as a guard against other versions and other DLLs.
// Experimental: not yet tested in game.
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
#include <cwchar>
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
		bool industrialTuning = true;      // the slot gets the medium industrial zone's sizes and costs
		uint32_t zoneTexture = 0x5C4D0D10; // SC4SIM_ManufacturingZone.dat, 0 = medium industrial texture
		uint32_t dragColour = 0x9646C8FF;  // RRGGBBAA, violet
		uint32_t cursor = 0x5C4D0D20;      // SC4SIM_ManufacturingZone.dat, 0 = game cursor
		uint32_t zoneName = 0x5C4D0D04;    // LTEXT 0x2026960B-0xEA5524EB-<id> in SC4SIM_ManufacturingZone.dat, 0 = game name
	} settings;

	bool installAttempted = false;
	cISC4City* city = nullptr;
	cRZAutoRefCount<cIGZCheatCodeManager> cheatManager;
	bool brushOn = false;

	// ---------------------------------------------------------------- SimCity 4 1.1.641 (decompilation notes)

	constexpr uintptr_t kGetConstructionInfo = 0x006B38E0;
	constexpr uintptr_t kGrowthStageFitsInZone = 0x00695340;
	constexpr uintptr_t kIsRCIZone = 0x004DC300;
	constexpr uintptr_t kPlaceZone = 0x004DD780;

	// Zone tuning: cSC4ZoneManager::Init (0x004DE170) calls ReadSimulatorTunableValues (0x004DC460, __thiscall, no
	// arguments) at 0x004DE192. It fills these zone manager fields, 16 entries each, from the Zone Manager exemplar.
	constexpr uintptr_t kReadZoneTuning = 0x004DC460;
	constexpr uintptr_t kReadZoneTuningCallSite = 0x004DE192;
	constexpr size_t kZmMinZoneSize = 0x11C;   // int32  (0xE94825B2)
	constexpr size_t kZmMaxZoneSize = 0x15C;   // int32  (0xE94825B3)
	constexpr size_t kZmTuningFloats = 0x1A0;  // float  (0xE94825B9)
	constexpr size_t kZmZoneCost = 0x1E0;      // sint64 (0xE94825B4)
	constexpr size_t kZmZoneCost2 = 0x260;     // sint64 (0xE94825B5)
	constexpr size_t kZmTexture = 0x2E0;       // uint32 FSH instance, group 0x1ABE787D (0xE94825B6)
	constexpr size_t kZmDragColour = 0x320;    // uint32 RRGGBBAA (0xE94825B8)
	constexpr uint32_t kReferenceZone = 8;     // medium (light) industrial

	// Zone tool cursor: the tool's constructor (0x004DAE80) takes its cursor from this 16-entry table, indexed by
	// zone type. Slots 10 and 13 use the high density residential cursor 0x02E43524.
	constexpr uintptr_t kZoneCursorTable = 0x00A92C40;
	constexpr uint32_t kGameSlotCursor = 0x02E43524;

	// Parcellization (0x0072DB60, called at the end of DoParcellization): zones 7-9 skip the residential and
	// commercial parcel pass ("cmp eax, 9; jg" at 0x0072DBA8). Raising the 9 to the slot makes the slot parcel like
	// industry. Only zones that pass IsRCIZone reach this code, so slots 10-12 are not affected.
	// Lot zone list: the tract developer checks the lot configuration's Zone Types against the cell's zone on the
	// growth path (0x0070654F: GetZoneType, then IsCompatibleWithZoneType; WrongZone, reason 6, if false). The game's
	// and IRM's I-M lots list zones 8 and 9 only. cSC4LotConfiguration vtable 0x00A8CEB8: slot 15
	// IsCompatibleWithZoneType (0x00477D00), slot 17 IsCompatibleWithPurposeType (0x00477DA0), both __thiscall(int).
	constexpr uintptr_t kLotConfigZoneEntry = 0x00A8CEF4;
	constexpr uintptr_t kLotConfigIsCompatibleWithZone = 0x00477D00;
	constexpr uintptr_t kLotConfigPurposeEntry = 0x00A8CEFC;
	constexpr uintptr_t kLotConfigIsCompatibleWithPurpose = 0x00477DA0;

	constexpr uintptr_t kParcelIndustrialMax = 0x0072DBA8;
	constexpr uint8_t kParcelIndustrialMaxBytes[] = { 0x83, 0xF8, 0x09, 0x7F, 0x05 };

	constexpr size_t kVtableScanSlots = 96;
	// Construction profile fields read by IsSuitableForConstruction: land value range (min, max).
	constexpr size_t kProfileLandValueMin = 0x10;
	constexpr size_t kProfileLandValueMax = 0x11;
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

	// The game compares the stage as a signed int and the zone as an unsigned one.
	bool NotesStageFits(uint32_t zone, int32_t stage)
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
		if (zone == settings.slot)
		{
			++stageSlotHits;
			return true;
		}
		// The callers pass a byte stage and a zone from GetZoneType, so the table covers every real call.
		if (zone < stageTable.size() && stage < 256) return stageTable[zone][stage];
		return NotesStageFits(zone, static_cast<int32_t>(stage));
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
				if (b >= 1 && result != NotesStageFits(a, static_cast<int32_t>(b))) ++mismatchZoneStage;   // (zone, stage)
				if (a >= 1 && result != NotesStageFits(b, static_cast<int32_t>(a))) ++mismatchStageZone;   // (stage, zone)
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
			// No range (0, 0) means "land value type must equal the wealth"; 0-255 accepts every land value.
			if (settings.ignoreLandValue && profile[kProfileLandValueMin] == 0 && profile[kProfileLandValueMax] == 0)
			{
				profile[kProfileLandValueMax] = 0xFF;
			}
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
		if (FindVtableSlot(zoneManager, kPlaceZone) != kZoneManagerPlaceZoneSlot)
		{
			SimLog::Write(SimLog::Level::Error, "ManufacturingZone: PlaceZone is not at 0x%08X in the zone manager (another DLL?): IMZone brush not installed.", kPlaceZone);
			return false;
		}
		const uintptr_t entry = VtableEntryAddress(zoneManager, kZoneManagerPlaceZoneSlot);
		return placeZoneEntry.Install(entry, reinterpret_cast<const void*>(&Hook_PlaceZone));
	}

	// ---------------------------------------------------------------- static patches (installed at application start)

	template <typename T>
	T& ZoneField(void* zoneManager, size_t offset, uint32_t zone)
	{
		return reinterpret_cast<T*>(static_cast<uint8_t*>(zoneManager) + offset)[zone];
	}

	using ReadZoneTuningFn = void(__thiscall*)(void* zoneManager);

	// After the game has read the Zone Manager exemplar (Maxis or a zoning mod), the slot gets the reference industrial
	// zone's sizes and costs and its own texture and drag colour, whatever the exemplar says about the slot.
	void __fastcall Hook_ReadZoneTuning(void* zoneManager, void* /*edx*/)
	{
		reinterpret_cast<ReadZoneTuningFn>(kReadZoneTuning)(zoneManager);
		const uint32_t slot = settings.slot;
		if (settings.industrialTuning)
		{
			ZoneField<int32_t>(zoneManager, kZmMinZoneSize, slot) = ZoneField<int32_t>(zoneManager, kZmMinZoneSize, kReferenceZone);
			ZoneField<int32_t>(zoneManager, kZmMaxZoneSize, slot) = ZoneField<int32_t>(zoneManager, kZmMaxZoneSize, kReferenceZone);
			ZoneField<float>(zoneManager, kZmTuningFloats, slot) = ZoneField<float>(zoneManager, kZmTuningFloats, kReferenceZone);
			ZoneField<int64_t>(zoneManager, kZmZoneCost, slot) = ZoneField<int64_t>(zoneManager, kZmZoneCost, kReferenceZone);
			ZoneField<int64_t>(zoneManager, kZmZoneCost2, slot) = ZoneField<int64_t>(zoneManager, kZmZoneCost2, kReferenceZone);
		}
		ZoneField<uint32_t>(zoneManager, kZmTexture, slot) = settings.zoneTexture
			? settings.zoneTexture : ZoneField<uint32_t>(zoneManager, kZmTexture, kReferenceZone);
		ZoneField<uint32_t>(zoneManager, kZmDragColour, slot) = settings.dragColour;
	}

	struct CallPatch
	{
		uintptr_t site = 0;
		uint32_t originalTarget = 0;
		bool installed = false;

		bool Install(uintptr_t at, uintptr_t expectedTarget, const void* hook)
		{
			const auto* code = reinterpret_cast<const uint8_t*>(at);
			const uint32_t target = static_cast<uint32_t>(at + 5 + *reinterpret_cast<const int32_t*>(at + 1));
			if (code[0] != 0xE8 || target != expectedTarget) return false;
			site = at;
			originalTarget = target;
			const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(hook) - (at + 5));
			installed = WriteBytes(at + 1, &rel, sizeof(rel));
			return installed;
		}

		void Uninstall()
		{
			if (!installed) return;
			const int32_t rel = static_cast<int32_t>(originalTarget - (site + 5));
			WriteBytes(site + 1, &rel, sizeof(rel));
			installed = false;
		}
	};

	// A manufacturing lot (Purpose Types includes I-M) that fits medium or dense industry also fits the slot, so the
	// game's and other mods' I-M lots grow in the I-M zone without editing their Zone Types. Other lots are unchanged.
	using LotConfigTestFn = bool(__thiscall*)(void* lotConfiguration, int32_t value);
	bool lotZoneHookActive = false;

	bool __fastcall Hook_IsCompatibleWithZoneType(void* lotConfiguration, void* /*edx*/, int32_t zone)
	{
		const auto test = reinterpret_cast<LotConfigTestFn>(kLotConfigIsCompatibleWithZone);
		if (test(lotConfiguration, zone)) return true;
		if (!lotZoneHookActive || zone != static_cast<int32_t>(settings.slot)) return false;
		const auto purpose = reinterpret_cast<LotConfigTestFn>(kLotConfigIsCompatibleWithPurpose);
		return purpose(lotConfiguration, static_cast<int32_t>(PurposeType::Manufacturing))
			&& (test(lotConfiguration, static_cast<int32_t>(ZoneType::IndustrialMediumDensity))
				|| test(lotConfiguration, static_cast<int32_t>(ZoneType::IndustrialHighDensity)));
	}

	// ---- industrial zone checks in the lot and zone code
	//
	// The game tests "is this an industrial zone" with inline compare chains on zones 7-9. Each site below is replaced
	// by a call to a small generated thunk that sets ZF when its register holds 7, 8, 9 or the slot, followed by the
	// site's own branch, so the slot takes every industrial path: lot orientation and road access (0x006CCA12,
	// 0x006CD409), the 1x1 zone lots left after demolition (0x00467827) and the lot and cell queries at
	// 0x004E34BF, 0x004E38A5, 0x004E3989 and 0x004E3A75. Every site is checked byte for byte before patching.

	enum Reg : uint8_t { kEax = 0, kEcx = 1, kEdi = 7 };

	struct IndustrialSite
	{
		uintptr_t address;
		std::array<uint8_t, 18> original;
		uint8_t length;
		Reg reg;
		uint8_t prefix;       // bytes kept before the call (0x006CCA12 keeps its "mov byte [esp+0x11], 1")
		uint8_t branch;       // 0x74 je / 0x75 jne (rel8), or 0x85 for jne rel32
		uintptr_t target;     // where the branch goes
	};

	const IndustrialSite kIndustrialSites[] = {
		{ 0x004E38A5, { 0x83,0xF8,0x07,0x74,0x0A,0x83,0xF8,0x08,0x74,0x05,0x83,0xF8,0x09,0x75,0x25 }, 15, kEax, 0, 0x75, 0x004E38D9 },
		{ 0x004E3989, { 0x83,0xF8,0x07,0x74,0x0A,0x83,0xF8,0x08,0x74,0x05,0x83,0xF8,0x09,0x75,0x2A }, 15, kEax, 0, 0x75, 0x004E39C2 },
		{ 0x004E3A75, { 0x83,0xF8,0x07,0x74,0x0A,0x83,0xF8,0x08,0x74,0x05,0x83,0xF8,0x09,0x75,0x25 }, 15, kEax, 0, 0x75, 0x004E3AA9 },
		{ 0x004E34BF, { 0x83,0xF8,0x07,0x74,0x1C,0x83,0xF8,0x08,0x74,0x17,0x83,0xF8,0x09,0x74,0x12 }, 15, kEax, 0, 0x74, 0x004E34E0 },
		{ 0x006CD409, { 0x83,0xF8,0x07,0x7C,0x0D,0x83,0xF8,0x09,0x7F,0x08 }, 10, kEax, 0, 0x75, 0x006CD41B },
		{ 0x00467827, { 0x83,0xF9,0x07,0x0F,0x8C,0x96,0x00,0x00,0x00,0x83,0xF9,0x09,0x0F,0x8F,0x8D,0x00,0x00,0x00 }, 18, kEcx, 0, 0x85, 0x004678C6 },
		{ 0x006CCA12, { 0x83,0xFF,0x07,0xC6,0x44,0x24,0x11,0x01,0x7C,0x29,0x83,0xFF,0x09,0x7F,0x24 }, 15, kEdi, 5, 0x75, 0x006CCA45 },
	};
	constexpr size_t kIndustrialSiteCount = std::size(kIndustrialSites);
	bool industrialSitePatched[kIndustrialSiteCount] = {};
	uint8_t* industrialThunks = nullptr;   // one 32-byte thunk per register (eax, ecx, edi), then the tooltip thunk

	// Zone hover tooltip (0x004D4FD0): the zone type read at 0x004D5063 ([esp+0x10]) chooses the extra lines with
	// "dec eax; cmp eax,8; ja; movzx edx,[eax+0x4D5C78]" (zones 1-3 commute, 4-6 none, 7-9 freight and crime).
	// "mov eax,[esp+0x10]; dec eax" at 0x004D5586 becomes a call to a thunk that reads the zone and turns the slot
	// into 9 (dense industry), so the slot gets the same lines as industry.
	constexpr uintptr_t kTooltipZoneSite = 0x004D5586;
	constexpr uint8_t kTooltipZoneSiteBytes[] = { 0x8B, 0x44, 0x24, 0x10, 0x48 };
	constexpr size_t kTooltipThunkOffset = 96;
	bool tooltipLinesPatched = false;

	// Zone hover tooltip name (0x004D6C70): "jmp [edi*4+0x4D7120]" picks one case per zone, each "push 1; push <LTEXT
	// instance>; push 0xEA5524EB". The slot's case loads the slot's game name (Spaceport); its LTEXT instance becomes
	// settings.zoneName.
	constexpr uintptr_t kTooltipNameTable = 0x004D7120;
	uintptr_t tooltipNameImmediate = 0;
	uint32_t tooltipNameOriginal = 0;

	// cmp r,7 / je ok / cmp r,8 / je ok / cmp r,9 / je ok / cmp r,slot / ok: ret  (ZF = industrial)
	void WriteThunk(uint8_t* at, Reg reg, uint8_t slot)
	{
		const uint8_t modrm = static_cast<uint8_t>(0xF8 + reg);
		const uint8_t code[] = { 0x83, modrm, 0x07, 0x74, 0x0D, 0x83, modrm, 0x08, 0x74, 0x08, 0x83, modrm, 0x09, 0x74, 0x03,
			0x83, modrm, slot, 0xC3 };
		std::memcpy(at, code, sizeof(code));
	}

	uint8_t* ThunkFor(Reg reg)
	{
		return industrialThunks + 32 * (reg == kEax ? 0 : reg == kEcx ? 1 : 2);
	}

	uint32_t InstallIndustrialSites()
	{
		industrialThunks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (!industrialThunks) return 0;
		for (Reg reg : { kEax, kEcx, kEdi }) WriteThunk(ThunkFor(reg), reg, static_cast<uint8_t>(settings.slot));
		{
			// mov eax,[esp+0x14] (0x10 + the return address); cmp eax,slot; jne +5; mov eax,9; dec eax; ret
			const uint8_t tooltip[] = { 0x8B, 0x44, 0x24, 0x14, 0x83, 0xF8, static_cast<uint8_t>(settings.slot), 0x75, 0x05,
				0xB8, 0x09, 0x00, 0x00, 0x00, 0x48, 0xC3 };
			std::memcpy(industrialThunks + kTooltipThunkOffset, tooltip, sizeof(tooltip));
		}
		FlushInstructionCache(GetCurrentProcess(), industrialThunks, 128);

		if (std::memcmp(reinterpret_cast<const void*>(kTooltipZoneSite), kTooltipZoneSiteBytes, sizeof(kTooltipZoneSiteBytes)) == 0)
		{
			uint8_t call[5] = { 0xE8 };
			const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(industrialThunks + kTooltipThunkOffset)
				- (kTooltipZoneSite + 5));
			std::memcpy(call + 1, &rel, 4);
			tooltipLinesPatched = WriteBytes(kTooltipZoneSite, call, sizeof(call));
		}

		if (settings.zoneName)
		{
			const uintptr_t caseAddress = *reinterpret_cast<const uint32_t*>(kTooltipNameTable + 4 * settings.slot);
			const auto* code = reinterpret_cast<const uint8_t*>(caseAddress);
			if (caseAddress > kTooltipNameTable - 0x1000 && caseAddress < kTooltipNameTable
				&& code[0] == 0x6A && code[1] == 0x01 && code[2] == 0x68)
			{
				tooltipNameImmediate = caseAddress + 3;
				tooltipNameOriginal = *reinterpret_cast<const uint32_t*>(tooltipNameImmediate);
				if (!WriteBytes(tooltipNameImmediate, &settings.zoneName, sizeof(uint32_t))) tooltipNameImmediate = 0;
			}
		}

		uint32_t patched = 0;
		for (size_t i = 0; i < kIndustrialSiteCount; ++i)
		{
			const IndustrialSite& site = kIndustrialSites[i];
			if (std::memcmp(reinterpret_cast<const void*>(site.address), site.original.data(), site.length) != 0) continue;

			std::array<uint8_t, 18> code{};
			std::fill(code.begin(), code.end(), static_cast<uint8_t>(0x90));
			size_t n = 0;
			for (; n < site.prefix; ++n) code[n] = site.original[3 + n];   // the kept instruction follows the first cmp
			const uintptr_t callAt = site.address + n;
			const int32_t callRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(ThunkFor(site.reg)) - (callAt + 5));
			code[n++] = 0xE8;
			std::memcpy(&code[n], &callRel, 4);
			n += 4;
			if (site.branch == 0x85)
			{
				const int32_t rel = static_cast<int32_t>(site.target - (site.address + n + 6));
				code[n++] = 0x0F;
				code[n++] = 0x85;
				std::memcpy(&code[n], &rel, 4);
				n += 4;
			}
			else
			{
				const int32_t rel = static_cast<int32_t>(site.target - (site.address + n + 2));
				if (rel < -128 || rel > 127) continue;
				code[n++] = site.branch;
				code[n++] = static_cast<uint8_t>(static_cast<int8_t>(rel));
			}
			if (n > site.length) continue;
			if (WriteBytes(site.address, code.data(), site.length))
			{
				industrialSitePatched[i] = true;
				++patched;
			}
		}
		return patched;
	}

	void UninstallIndustrialSites()
	{
		for (size_t i = 0; i < kIndustrialSiteCount; ++i)
		{
			if (!industrialSitePatched[i]) continue;
			WriteBytes(kIndustrialSites[i].address, kIndustrialSites[i].original.data(), kIndustrialSites[i].length);
			industrialSitePatched[i] = false;
		}
		if (tooltipLinesPatched)
		{
			WriteBytes(kTooltipZoneSite, kTooltipZoneSiteBytes, sizeof(kTooltipZoneSiteBytes));
			tooltipLinesPatched = false;
		}
		if (tooltipNameImmediate)
		{
			WriteBytes(tooltipNameImmediate, &tooltipNameOriginal, sizeof(uint32_t));
			tooltipNameImmediate = 0;
		}
		// The thunks stay allocated: another thread could still be returning through one of them.
	}

	VtableEntry lotZoneEntry;
	CallPatch zoneTuningCall;
	uint32_t originalSlotCursor = 0;
	bool cursorPatched = false;
	bool parcelPatched = false;

	void InstallStaticPatches()
	{
		const bool tuning = zoneTuningCall.Install(kReadZoneTuningCallSite, kReadZoneTuning,
			reinterpret_cast<const void*>(&Hook_ReadZoneTuning));

		const uintptr_t cursorEntry = kZoneCursorTable + 4 * settings.slot;
		if (settings.cursor && *reinterpret_cast<const uint32_t*>(cursorEntry) == kGameSlotCursor)
		{
			originalSlotCursor = kGameSlotCursor;
			cursorPatched = WriteBytes(cursorEntry, &settings.cursor, sizeof(uint32_t));
		}

		const bool lotZone = *reinterpret_cast<const uint32_t*>(kLotConfigZoneEntry) == kLotConfigIsCompatibleWithZone
			&& *reinterpret_cast<const uint32_t*>(kLotConfigPurposeEntry) == kLotConfigIsCompatibleWithPurpose
			&& lotZoneEntry.Install(kLotConfigZoneEntry, reinterpret_cast<const void*>(&Hook_IsCompatibleWithZoneType));

		const uint32_t industrialSites = InstallIndustrialSites();

		if (std::memcmp(reinterpret_cast<const void*>(kParcelIndustrialMax), kParcelIndustrialMaxBytes, sizeof(kParcelIndustrialMaxBytes)) == 0)
		{
			const uint8_t maxZone = static_cast<uint8_t>(settings.slot);
			parcelPatched = WriteBytes(kParcelIndustrialMax + 2, &maxZone, 1);
		}

		SimLog::Write(tuning && parcelPatched ? SimLog::Level::Info : SimLog::Level::Warning,
			"ManufacturingZone: zone tuning %s (texture 0x%08X, drag colour 0x%08X%s), cursor %s, industrial parcels %s, "
			"I-M lots of zones 8/9 in the slot %s, industrial checks %u of %u, tooltip lines %s, tooltip name %s.",
			tuning ? "on" : "NOT installed (the call site differs)", settings.zoneTexture, settings.dragColour,
			settings.industrialTuning ? ", sizes and costs of medium industry" : "",
			cursorPatched ? "on" : (settings.cursor ? "NOT installed (the table differs)" : "off"),
			parcelPatched ? "on" : "NOT installed (the code differs)",
			lotZone ? "on" : "NOT installed (the lot configuration vtable differs)",
			industrialSites, static_cast<uint32_t>(kIndustrialSiteCount),
			tooltipLinesPatched ? "on" : "NOT installed", tooltipNameImmediate ? "on" : (settings.zoneName ? "NOT installed" : "off"));
	}

	void UninstallStaticPatches()
	{
		lotZoneHookActive = false;
		lotZoneEntry.Uninstall();
		UninstallIndustrialSites();
		zoneTuningCall.Uninstall();
		if (cursorPatched)
		{
			WriteBytes(kZoneCursorTable + 4 * settings.slot, &originalSlotCursor, sizeof(uint32_t));
			cursorPatched = false;
		}
		if (parcelPatched)
		{
			const uint8_t original = kParcelIndustrialMaxBytes[2];
			WriteBytes(kParcelIndustrialMax + 2, &original, 1);
			parcelPatched = false;
		}
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
			lotZoneHookActive = true;   // only with the redirect does the slot hold I-M
			rci = settings.rciZoneHook && InstallRCIZoneHook(zoneManager);
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
			"ManufacturingZone: zone slot %u. I-M redirect %s, stage hook %s, IsRCIZone %s, land value range %s, IMZone brush %s.",
			settings.slot, redirectInstalled ? "on" : "OFF", stageJump.installed ? "on" : "off", rci ? "on" : "off",
			redirectInstalled && settings.ignoreLandValue ? "0-255" : "off", placeZoneEntry.installed ? "on" : "off");
	}

	void Uninstall()
	{
		placeZoneEntry.Uninstall();
		constructionInfoEntry.Uninstall();
		stageJump.Uninstall();
		rciJump.Uninstall();
		redirectInstalled = false;
		lotZoneHookActive = false;
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
			"ManufacturingZone: I-M construction profiles redirected %u, stage checks passed in the slot %u, brush placements %u.",
			redirects, stageSlotHits, brushPlacements);
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

	uint32_t ReadHex(const std::filesystem::path& ini, const wchar_t* key, uint32_t fallback)
	{
		const std::wstring value = ReadIni(ini, key, L"");
		if (value.empty()) return fallback;
		wchar_t* end = nullptr;
		const unsigned long result = std::wcstoul(value.c_str(), &end, 0);
		return end && *end == L'\0' ? static_cast<uint32_t>(result) : fallback;
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
	settings.industrialTuning = ParseBool(ReadIni(ini, L"IndustrialTuning", L"true"), true);
	settings.zoneTexture = ReadHex(ini, L"ZoneTexture", settings.zoneTexture);
	settings.dragColour = ReadHex(ini, L"DragColour", settings.dragColour);
	settings.cursor = ReadHex(ini, L"Cursor", settings.cursor);
	settings.zoneName = ReadHex(ini, L"ZoneName", settings.zoneName);
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

	if (VersionDetection::GetInstance().GetGameVersion() == 641)
	{
		// Zone tuning, cursor and parcels are code and data of the executable: patched once, before any city loads.
		InstallStaticPatches();
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
	UninstallStaticPatches();
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
