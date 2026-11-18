// SC4 SIM: custom power poles. Ported into SC4SIM.dll from the "re/custom-powerlines" branch of
// sc4-render-services (https://github.com/caspervg/sc4-render-services/tree/re/custom-powerlines,
// src/sample/power-pole-customization), LGPL 2.1 -- see LICENSE-sc4-render-services.txt.
// Changes for SC4SIM: no ImGui status overlay (that part needs SC4RenderServices), settings in SC4SIM.ini,
// log in SC4SIM.log, and the director is now a module driven by the SC4SIM director (PowerPoleCustomization.h).
// Everything else (hooks, addresses, exemplar properties, styles, FAR drag, Tab switch, keepwires) is unchanged.
// Added by SC4SIM: CellIsOKForPlacementHook, power lines across empty zones and farm fields (CrossZones setting).
//
// Custom power-pole cable attach points: lets a pole prop exemplar declare, per connection
// direction, how many cables it carries and exactly where each one attaches on the model,
// instead of being limited to the engine's two baked 4-point tables (_cardinalConnectionPoints /
// _diagonalConnectionPoints). See docs/sc4-powerline-tool-re.md for the full reverse-engineering
// trail behind every address and struct offset below.
//
// Data-driven surface: a pole prop exemplar opts in through attachment geometry, family-wide wire
// appearance, or optional per-wire appearance arrays. Exemplars defining none of these properties
// are untouched and retain the original cardinal/diagonal table and wire rendering.
//
// ATTACH-POINT FORMAT (Float32Array), one property per connection direction:
//   [x0,y0,z0, x1,y1,z1, ..., xN,yN,zN]
// The point count is inferred from the array length and clamped to the ini-configurable maximum.
// A legacy [pointCount, xyz...] prefix is accepted for compatibility with the original PoC draft.
//
// WIRE APPEARANCE PROPERTIES:
//   Wire Width Scale / Wire Sag Scale / Wire Maximum Sag are simple family-wide Float32 defaults.
//   Optional plural Float32Array properties override those values by wire index. Short arrays are
//   allowed; missing entries inherit the family-wide value. Attachment and override arrays use the
//   same wire ordering, matching vanilla's index-based strand pairing.
//
// Status: hook sites and data structures below are real, address-confirmed (see "Hook sites"
// section and docs/sc4-powerline-tool-re.md SS7-14). The cable-count/strand-build algorithm is
// written out in full, ACTIVE, and validated in-game (2026-07-19) together with per-wire width,
// FAR drag/routing, and the Tab/Shift-Tab style switch.
//
// First attempt (2026-07-01) registered extra strands as cSC4PowerLineOccupant city objects --
// confirmed in-game (docs SS13) to run with no crash but NOT make DrawPowerlines render anything,
// since rendering reads polyline data cached directly in each pole's own tConnection entry,
// independent of any cSC4PowerLineOccupant's existence. The current approach clears and rebuilds
// the complete regular-polyline list in each active tConnection, using the same erase, control-
// point, tessellation, and vector-insert helpers as vanilla. A separate validated inline lookup
// supplies per-wire width while leaving the rest of DrawPowerlines intact.

#include "cGZPersistResourceKey.h"
#include "cIGZCOM.h"
#include "cIGZCheatCodeManager.h"
#include "cIGZFrameWork.h"
#include "cIGZMessage2.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZPersistResourceKeyFilter.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWin.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4Lot.h"
#include "cISC4LotManager.h"
#include "cISC4ZoneManager.h"
#include "SC4CellRegion.h"
#include "cISCProperty.h"
#include "cISCPropertyHolder.h"
#include "cISCExemplarPropertyHolder.h"
#include "cISCResExemplar.h"
#include "cIGZVariant.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"


#include "mini/ini.h"

#include "RelativeCallPatch.h"
#include "SimLogFormat.h"
#include "PowerPoleCustomization.h"
#include "VersionDetection.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
    constexpr uint32_t kDirectorID = 0xB07E11E5; // arbitrary, must not collide with another director in this Plugins folder
    constexpr uint16_t kSupportedGameVersion = 641;
    constexpr uint32_t kCheatCodeMessageType = 0x230E27AC; // fixed SC4 message type, same constant every sample uses
    constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
    constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2; // same constant DateJumper/RenderServices use

    // Experimental, deliberately runtime-scoped switch: while enabled, the lot developer's
    // ClearLotBlockingObjects loop leaves power-pole and hidden power-line occupants alone.
    constexpr uint32_t kCheatKeepWires = 0xB07E1002;
    bool gKeepWiresOnZoneChange = false;

    // ------------------------------------------------------------------
    // Hook sites -- SimCity 4.exe 1.1.641 absolute addresses.
    // All confirmed via search_instructions cross-referencing in the Mac/Windows Ghidra session;
    // see docs/sc4-powerline-tool-re.md SS7 for the full evidence trail per address.
    // ------------------------------------------------------------------

    // cSC4PowerPoleOccupant::InitConnectionPoints -- picks which attach-point table this pole uses.
    // Three confirmed call sites; redirect all three so every path that (re)builds a pole's table
    // goes through our hook, not just the common one.
    constexpr uintptr_t kInitConnectionPoints = 0x00649b30;
    constexpr uintptr_t kInitConnectionPoints_CallSite_Init = 0x0064ceaa; // new-pole initialization path
    constexpr uintptr_t kInitConnectionPoints_CallSite_SetDefaultExemplar = 0x0064d94e;
    constexpr uintptr_t kInitConnectionPoints_CallSite_Read = 0x0064ef09; // save-game load path

    // cSC4PowerPoleOccupant::~cSC4PowerPoleOccupant (complete-object destructor, confirmed
    // 2026-07-19: same 7-vtable reset pattern as the Mac dtor at 0x002594ec, destroys the
    // tConnection vector at +0xac/+0xb0, calls the cSC4Occupant base dtor on +0x8). Its sole
    // caller is the scalar deleting destructor at 0x0064e380, which then returns the 292-byte
    // object to the fixed pool at 0x00b0c0c8. Hooking that one call site lets this DLL drop its
    // override for a pole the moment the engine destroys it -- without this, demolished
    // poles leave stale gOverrides entries behind and the pool's address
    // reuse can briefly pair a new pole with a dead pole's customization.
    constexpr uintptr_t kDestructor = 0x0064dfc0;
    constexpr uintptr_t kDestructor_CallSite_DeletingDtor = 0x0064e383;

    // cSC4PowerPoleOccupant::LoadModel passes its mask-derived 0/1 quarter-turn to the RKT1 model
    // selector here. The first argument is the model-resource-key vector at completeObject+0x8,
    // which lets the hook recover the pole and apply an exemplar-controlled XOR correction before
    // the selector folds camera rotation and pole rotation modulo four.
    constexpr uintptr_t kGetModelInstanceID = 0x00497180;
    constexpr uintptr_t kGetModelInstanceID_CallSite_LoadModel = 0x0064ae08;
    constexpr uint32_t kOccupant_ModelResourceKeys = 0x08;

    // cSC4PowerPoleOccupant::AddConnection -- builds the strand polylines for a brand-new connection.
    // Called from cSC4PowerLineTool::PlacePoles at exactly 4 call sites (2 logical calls x 2 args-swapped).
    constexpr uintptr_t kAddConnection = 0x0064e3e0;
    constexpr std::array<uintptr_t, 4> kAddConnectionCallSites = {
        0x00651d9c, 0x00651dad, 0x00651f86, 0x00651f93,
    };

    // cSC4PowerPoleOccupant::UpdateConnection -- rebuilds strands when a pole's exemplar changes
    // (e.g. zone-driven model swap). Called from SetDefaultExemplar at 2 call sites.
    constexpr uintptr_t kUpdateConnection = 0x0064d510;
    constexpr std::array<uintptr_t, 2> kUpdateConnectionCallSites = {
        0x0064d968, 0x0064d970,
    };

    // ------------------------------------------------------------------
    // cSC4PowerPoleOccupant field offsets (Windows). See docs/sc4-powerline-tool-re.md SS9 --
    // only offsets independently confirmed by a real call site, not guessed by Mac-offset-by-analogy.
    // ------------------------------------------------------------------
    constexpr uint32_t kOccupant_ConnectionPointTablePtr = 0xe4; // InitConnectionPoints writes here
    constexpr uint32_t kOccupant_ConnectionsBegin = 0xac;        // confirmed via DrawPowerlines (clean decompile)
    constexpr uint32_t kOccupant_ConnectionsEnd = 0xb0;
    constexpr uint32_t kOccupant_DirectionFlags = 0xb8;          // confirmed via CreatePowerPole
    constexpr uintptr_t kCardinalConnectionPoints = 0x00B0C0D8;
    constexpr uintptr_t kDiagonalConnectionPoints = 0x00B0C198;
    constexpr uint32_t kCardinalDirectionFlags = 0x5; // X | Z
    constexpr uint32_t kDiagonalDirectionFlags = 0xa; // DP | DN
    // SetPosition (0x0064c540) is invoked through the occupant subobject at completeObject+0x8.
    // Its raw [this+0x98/+0x9c/+0xa0] stores therefore resolve to these complete-object offsets.
    // GetLineConnectionPoints (0x00649ba0) independently reads the same +0xa0/+0xa4/+0xa8 triple.
    constexpr uint32_t kOccupant_PosX = 0xa0;
    constexpr uint32_t kOccupant_PosZ = 0xa8;                    // posY is the intervening +0xa4

    constexpr uint32_t kVanillaPointsPerDirection = 4; // engine's hardcoded baked-table arity
    constexpr uint32_t kVanillaDirectionStrideBytes = 0x30; // 4 points x 12 bytes, per the baked tables
    constexpr float kCellSize = 16.0f; // one city cell is 16 m

    // ------------------------------------------------------------------
    // tConnection -- one entry in a pole's connections vector (kOccupant_ConnectionsBegin/End),
    // 0x2c bytes each. Confirmed via DrawPowerlines' clean decompile: polyline-list/buoy-list
    // fields and the 0x2c stride. This is the struct DrawPowerlines actually renders from -- NOT
    // cSC4PowerLineOccupant (confirmed insufficient in-game, 2026-07-01, docs SS13).
    // UpdateConnection's raw Windows instructions independently confirm that +0x00 is the other
    // pole pointer and +0x09 is the active/render-owning flag. +0xc/+0x10 are cell coordinates,
    // not pole pointers.
    // ------------------------------------------------------------------
    constexpr uint32_t kConnection_OtherPole = 0x00;
    constexpr uint32_t kConnection_IsRenderOwner = 0x09;
    constexpr uint32_t kConnection_PolylinesBegin = 0x14; // vector<vector<cS3DVector3>>
    constexpr uint32_t kConnection_PolylinesEnd = 0x18;
    // Second vector<vector<cS3DVector3>> (confirmed 2026-07-19 via AddConnection's decompile and
    // DrawPowerlines' reads at [entry+0x20]/[entry+0x24]): coarse 3-point copies of each strand,
    // built by AddConnection ONLY when the span crosses water (terrain service vtbl+0x88 test); the
    // 0xC9AF0FCD texture draws its buoys. Same element layout and the same insert/erase helpers as
    // the regular list. Vanilla resizes each copy to 3 points (push 3 at 0x0064D842) and fills it
    // with the Bezier sampler at count 3 (0x0064D86C), which writes t = 1/3, 2/3, 1 and never t = 0.
    // UpdateConnection clears the wire list (+0x14) but never this one, so every update of a water
    // span appends more copies (a vanilla leak); the rebuild below clears it.
    constexpr uint32_t kConnection_WaterPolylinesBegin = 0x20;
    constexpr uint32_t kConnection_WaterPolylinesEnd = 0x24;
    constexpr int32_t kWaterPolylinePointCount = 3; // t = 1/3, 2/3, 1: vanilla's own layout
    constexpr uint32_t kConnection_Size = 0x2c;

    // DrawPowerlines resolves the vanilla zoom-dependent width pointer at this instruction range.
    // Replace that lookup with a helper which returns either the vanilla pointer or a precomputed
    // per-wire scaled value. The overwritten bytes and boundaries are confirmed against 1.1.641.
    constexpr uintptr_t kWireWidthLookupPatchSite = 0x0064b855;
    constexpr size_t kWireWidthLookupPatchSize = 23;
    constexpr std::array<uint8_t, kWireWidthLookupPatchSize> kWireWidthLookupExpectedBytes = {
        0x8b, 0x44, 0x24, 0x14,                         // mov eax,[esp+14h] (occupant)
        0x8b, 0x88, 0xc4, 0x00, 0x00, 0x00,             // mov ecx,[eax+0c4h] (zoom)
        0x8b, 0x15, 0x98, 0x67, 0xb4, 0x00,             // mov edx,[00b46798h]
        0xd9, 0x54, 0x24, 0x78,                         // fst dword ptr [esp+78h]
        0x8d, 0x04, 0x8a,                               // lea eax,[edx+ecx*4]
    };
    constexpr uintptr_t kPowerLineWidthFactorsPtr = 0x00b46798;
    constexpr size_t kPowerLineZoomCount = 5;

    // Bezier control-point solver (2 endpoints -> 2 control points + a length-based tessellation
    // count), and the segment tessellator it feeds. Both confirmed via clean, self-contained
    // decompiles (no register-spill ambiguity, unlike AddConnection's own messy decompile).
    // cSC4PowerPoleOccupant::GetControlPoints is a member function: __thiscall, `this` in ECX,
    // callee cleans its 9 stack arguments (`RET 0x24`). Both vanilla callers (0x0064d71e in
    // UpdateConnection, 0x0064e675 in AddConnection) load the pole into ECX first. The body does
    // not read ECX today, which is why the former __stdcall declaration happened to work; the
    // pole is passed anyway so the call matches the real signature.
    // (Declaring it __cdecl double-cleaned the stack on every call, 2026-07-01.)
    constexpr uintptr_t kGetControlPoints = 0x0064a6c0;
    using GetControlPointsFn = void(__thiscall*)(void* pole, float x1, float y1, float z1, float x2, float y2,
                                                  float z2, float* outControl1, float* outControl2,
                                                  int32_t* outCount);

    // Writes `count` tessellated points starting at byte address `outFirstPoint`, given the 4 bezier
    // control points (p0, ctrl1, ctrl2, p1) each as a 3-float pointer. Confirmed standalone.
    constexpr uintptr_t kTessellateBezierSegment = 0x006493b0;
    using TessellateBezierSegmentFn = void(__cdecl*)(const float* p0, const float* ctrl1, const float* ctrl2,
                                                       const float* p1, uintptr_t outFirstPoint, int32_t count);

    // vector<vector<cS3DVector3>>::insert(pos, count, value) -- confirmed via its own
    // std_vector_cS3DVector3_copy_construct helper (deep-copies begin/end of a vector<cS3DVector3>,
    // proving the OUTER vector's element type is itself a vector, not a raw point). Deep-copies
    // `value` rather than taking ownership, so a stack-local {begin,end,cap} view is a safe `value`
    // to pass -- no need to match this build's exact allocator/heap behavior for the source.
    constexpr uintptr_t kPolylineListInsert = 0x0064d020;
    using PolylineListInsertFn = void(__thiscall*)(void* polylinesVector, void* position, const void* value,
                                                     uint32_t unused, uint32_t count, uint8_t noTailRelocate);

    // vector<vector<cS3DVector3>>::erase(begin,end), used by vanilla UpdateConnection to destroy
    // every inner vector and reset the outer vector's end pointer before rebuilding it.
    constexpr uintptr_t kPolylineListErase = 0x0064cf40;
    using PolylineListEraseFn = void*(__thiscall*)(void* polylinesVector, void* begin, void* end);

    // cSC4NetworkRoutines::Get0To3Direction -- confirmed identical logic to the Mac twin
    // (byte-for-byte equivalent branch structure, verified by decompiling both).
    constexpr uintptr_t kGet0To3Direction = 0x0061f4c0;
    using Get0To3DirectionFn = uint8_t(__cdecl*)(uint32_t x1, uint32_t z1, uint32_t x2, uint32_t z2);

    // cSC4PowerLineOccupant (the wire/strand object) has its full vtable layout, ctor, and
    // SetConnectedPoles confirmed and documented in docs/sc4-powerline-tool-re.md SS9/SS10/SS13a --
    // no longer used by this file. Registering a strand as a cSC4PowerLineOccupant is a real,
    // working, non-crashing city-object registration (queryable/demolishable/save-load-capable),
    // but confirmed in-game (2026-07-01, docs SS13) to NOT make DrawPowerlines render it --
    // rendering reads polyline data cached directly in each pole's own tConnection entry instead
    // (see kConnection_PolylinesBegin below), independent of any cSC4PowerLineOccupant's existence.

    // ------------------------------------------------------------------
    // Custom exemplar property IDs. The full 0xB22A0000-0xB22A000F block was checked against the
    // vendored new_properties.xml registry (2026-07-01); no property in that registry uses the
    // block. Unregistered third-party properties can never be ruled out globally, but these IDs
    // have no known registry conflict and are intentionally kept contiguous for this feature.
    // ------------------------------------------------------------------
    constexpr uint32_t kPropAttachPointsDir0 = 0xB22A0000;
    constexpr uint32_t kPropAttachPointsDir1 = 0xB22A0001;
    constexpr uint32_t kPropAttachPointsDir2 = 0xB22A0002;
    constexpr uint32_t kPropAttachPointsDir3 = 0xB22A0003;
    constexpr uint32_t kPropWireWidthScale = 0xB22A0004;
    constexpr uint32_t kPropWireSagScale = 0xB22A0005;
    constexpr uint32_t kPropWireMaximumSag = 0xB22A0006;
    constexpr uint32_t kPropWireWidthScales = 0xB22A0007;
    constexpr uint32_t kPropWireSagScales = 0xB22A0008;
    constexpr uint32_t kPropWireMaximumSags = 0xB22A0009;
    constexpr uint32_t kPropFoundationHalfExtent = 0xB22A000A;
    constexpr uint32_t kPropFoundationFloorTextureId = 0xB22A000B;
    constexpr uint32_t kPropFoundationWallTextureId = 0xB22A000C;
    // Optional signed Float32, in degrees: the in-plane heading (0 = x axis, 90 = z axis) the
    // custom attach points in this exemplar were authored for. Absent = attach points are in their
    // direction's nominal 0/45/90/135-degree basis (vanilla behavior). Only meaningful together with
    // custom attach-point properties; a pole using the vanilla baked table always uses the nominal
    // basis regardless of this value. See docs/far-power-lines-design.md "Two real wrinkles".
    constexpr uint32_t kPropAttachBasisDegrees = 0xB22A000D;
    // Optional Bool. XORs the mask-derived 0/1 RKT1 quarter-turn. This is useful when two
    // perpendicular FAR headings share one logical model but SC4 rotates the wrong member of the
    // pair. Absent/false preserves vanilla model selection exactly.
    constexpr uint32_t kPropInvertModelQuarterTurn = 0xB22A000E;
    // Optional Uint32 texture IDs replacing the wire strand texture (vanilla 0xAA9DA78E) and the
    // buoy/marker-ball texture (vanilla 0xC9AF0FCD) on every connection this pole render-owns.
    // Validated and engine-registered exactly like the foundation textures (FSH group 0x1ABE787D).
    constexpr uint32_t kPropWireTextureId = 0xB22A000F;
    constexpr uint32_t kPropWireBuoyTextureId = 0xB22A0010;
    // Optional Bool. Rotates the foundation pad (floor + walls) in the ground plane so it follows
    // the pole's span bearing instead of staying axis-aligned -- the rotation is the minimal
    // deviation from the nearest 90-degree multiple, so walls keep their vanilla sides. Applied
    // only when every live connection of the pole agrees on one bearing (junctions stay vanilla).
    constexpr uint32_t kPropFoundationFollowSpan = 0xB22A0011;
    // Optional non-negative Float32: scales the water-crossing buoy/ball quads (1.0 = vanilla).
    constexpr uint32_t kPropWaterBuoySizeScale = 0xB22A0012;
    // Optional Uint32: points per water-crossing strand polyline; the engine draws points-1 balls,
    // so vanilla's 3 points = 2 balls. Clamped to [2, 17] (1-16 balls).
    constexpr uint32_t kPropWaterBuoyPointCount = 0xB22A0013;
    constexpr std::array<uint32_t, 4> kAttachPointProperties = {
        kPropAttachPointsDir0, kPropAttachPointsDir1, kPropAttachPointsDir2, kPropAttachPointsDir3,
    };

    // ------------------------------------------------------------------
    // Foundation pad (flat floor + 2 retaining walls, drawn under every pole to hide terrain
    // z-fighting on slopes). Confirmed via Ghidra (2026-07-02): cSC4PowerPoleOccupant::CreateFloor
    // (0x0064bfd0), called from SetPosition (0x0064c611) and lazily from Draw (0x0064c81f), builds a
    // flat quad from a unit square scaled by two hardcoded floats read directly from memory:
    // DAT_00a94d50 (10.0) * DAT_00a84d2c (0.5) = 5.0 half-extent -> fixed 10m x 10m pad, not read
    // from any exemplar property. cSC4PowerPoleOccupant::CreateWalls (0x0064c470) has no independent
    // size of its own -- FUN_0064c2c0 (its per-wall-quad builder) reads the floor's own vertex buffer
    // at occupant+0xe8 directly (confirmed via clean decompile), so rescaling that buffer after
    // vanilla CreateFloor runs is sufficient; CreateWalls needs no patch of its own.
    // ------------------------------------------------------------------
    constexpr uintptr_t kCreateFloor = 0x0064bfd0;
    // cSC4PowerPoleOccupant::CreateWalls -- __thiscall, no args, called immediately after
    // CreateFloor at every vanilla site (confirmed at SetPosition 0x0064c616). It rebuilds the two
    // wall quads from the floor's own vertex buffer, so any floor transform applied before it runs
    // (scale AND rotation) carries into the walls with no separate wall patch.
    constexpr uintptr_t kCreateWalls = 0x0064c470;
    constexpr uintptr_t kCreateFloor_CallSite_SetPosition = 0x0064c611;
    constexpr uintptr_t kCreateFloor_CallSite_Draw = 0x0064c81f;
    // FUN_0064c6c0 -- almost certainly cSC4PowerPoleOccupant::ChangeZoomLevel (not yet renamed in
    // Ghidra): writes this+0xc0/this+0xc4 from param_1+0x18/+0x14 immediately before this call,
    // exactly matching this file's existing doc note ("ChangeZoomLevel writes *(this+0xc0) =
    // drawContext->zoom"). A THIRD, previously-missed CreateFloor caller -- confirmed via
    // get_xrefs_to, not assumed. This is why the pad reverted to vanilla size specifically on
    // zoom/pan: crossing an LOD/zoom boundary re-triggers LoadModel+CreateFloor+CreateWalls through
    // this path, bypassing the two sites patched first.
    constexpr uintptr_t kCreateFloor_CallSite_ChangeZoomLevel = 0x0064c7de;
    constexpr uint32_t kOccupant_FloorVertsBegin = 0xe8; // vector<sVF_V3F_C4UB_T2F> begin ptr
    constexpr uint32_t kOccupant_FloorVertsEnd = 0xec;
    constexpr uint32_t kFloorVertexStride = 0x18; // 6 floats/vertex: x,y,z,color,u,v -- confirmed via FUN_0064c2c0
    constexpr float kVanillaFoundationHalfExtent = 5.0f; // DAT_00a94d50(10.0) * DAT_00a84d2c(0.5), confirmed via memory read
    constexpr float kMaxFoundationHalfExtent = 50.0f; // sanity clamp, matches this file's other property-clamping convention

    // Floor/wall texture-hash lookup sites inside cSC4PowerPoleOccupant::Draw (0x0064c800). Each is a
    // bare `MOV EAX, imm32` (5 bytes: B8 + LE imm32), read twice per texture -- once to compute the
    // hash-bucket index (DIV), once inside the chain-walk comparison. Both instances of a pair MUST
    // return the identical value or the bucket lookup mismatches its own index. Confirmed via raw
    // memory read against 1.1.641 (not just disassembly text).
    constexpr uintptr_t kFloorTextureHashSite1 = 0x0064c9e7;
    constexpr uintptr_t kFloorTextureHashSite2 = 0x0064c9f5;
    constexpr uintptr_t kWallTextureHashSite1 = 0x0064ca76;
    constexpr uintptr_t kWallTextureHashSite2 = 0x0064ca84;
    constexpr uint32_t kVanillaFloorTextureId = 0x0912220E;
    constexpr uint32_t kVanillaWallTextureId = 0x08080004;

    // Engine power-pole texture registry (confirmed 2026-07-19 on the WINDOWS binary via StaticInit
    // 0x0064cc20 / ChangeZoomLevel 0x0064c6c0 / Draw 0x0064c800 -- the container layout below is
    // read from Windows disassembly, NOT assumed from the Mac binary's gnu hashtable): a
    // hash_map<textureId, cS3DTextureBinding*> rooted at 0x00b46784 with the bucket-pointer array
    // at [0x00b46788]..[0x00b4678c], node layout {+0 next, +4 id, +8 binding}, bucket index =
    // id % bucketCount. StaticInit seeds it with exactly the four vanilla wire/floor/wall IDs.
    // Draw's bucket walk has NO null check, so any ID absent from this map crashes -- a custom ID
    // must therefore (a) resolve to a real FSH resource and (b) be inserted into this map before
    // the first Draw that uses it. Insertion goes through the engine's own map insert function
    // (0x004f8d30, __thiscall, arg = pointer to the 32-bit key, returns the value slot -- the exact
    // call StaticInit itself makes four times), then is verified by re-finding the node with the
    // same read-only bucket walk Draw performs; verification failure falls back to the vanilla
    // texture instead of trusting the insert. ChangeZoomLevel reloads the binding of every
    // registered entry whenever the zoom global at 0x00b0c0a8 differs from the draw context's
    // zoom, so resetting that global to its unset value (5; zooms are 0-4) right after inserting
    // forces the engine itself to load the new texture on the next frame -- no manual
    // texture-manager call needed.
    constexpr uintptr_t kPoleTextureRegistry = 0x00b46784;
    constexpr uintptr_t kPoleTextureRegistryInsert = 0x004f8d30;
    constexpr uintptr_t kPoleTextureBucketsBegin = 0x00b46788;
    constexpr uintptr_t kPoleTextureBucketsEnd = 0x00b4678c;
    constexpr uintptr_t kPoleTextureZoomGlobal = 0x00b0c0a8;
    constexpr uint32_t kPoleTextureZoomUnset = 5; // zooms are 0-4; the global starts at 5
    constexpr uint32_t kFshTypeId = 0x7ab50e44;
    constexpr uint32_t kPoleTextureGroupId = 0x1abe787d; // group ChangeZoomLevel loads bindings from

    // Wire texture lookup sites inside DrawPowerlines (0x0064b750). Each is a single 5-byte
    // `MOV ESI, imm32` (opcode 0xBE) whose value feeds BOTH the bucket DIV and the chain-walk
    // compare of the texture-registry walk -- unlike Draw's floor/wall pair-sites, one patch per
    // texture suffices. At both sites EBP holds the current tConnection entry (confirmed: the
    // preceding instructions read the render-owner flag at [EBP+0x9] and the buoy vector at
    // [EBP+0x20]/[EBP+0x24]), which is exactly the key the DLL's connection-texture map uses.
    // 0xAA9DA78E textures the strand polylines; 0xC9AF0FCD textures the buoy/marker balls.
    constexpr uintptr_t kWireStrandTextureSite = 0x0064b7ba;
    constexpr uintptr_t kWireBuoyTextureSite = 0x0064b9e3;
    constexpr uint32_t kVanillaWireStrandTextureId = 0xAA9DA78E;
    constexpr uint32_t kVanillaWireBuoyTextureId = 0xC9AF0FCD;
    constexpr uint8_t kMovEaxOpcode = 0xB8;
    constexpr uint8_t kMovEsiOpcode = 0xBE;

    // Water-crossing buoy/ball rendering inside DrawPowerlines (confirmed 2026-07-19): one
    // billboarded quad is drawn per INTERIOR point of each water polyline (points-1 balls per
    // strand), with half-size g_afPowerLineWidthFactors[zoom] * 4.0f (the constant at 0x00a8e17c).
    // The zoom-table read at 0x0064ba6e -- `MOV EAX,[0x00b46798]; FLD [EAX+EDX*4];
    // LEA EAX,[EAX+EDX*4]`, 11 bytes -- is the single chokepoint: everything after re-reads the
    // size through EAX. At that instruction EDX holds the zoom and EDI the current inner polyline
    // vector, so the hook can key a per-polyline-buffer override exactly like the wire-width hook
    // and return a pointer to either the vanilla slot or a pre-scaled value.
    constexpr uintptr_t kWaterBallSizeSite = 0x0064ba6e;
    constexpr uint32_t kMinWaterPolylinePoints = 2;  // 1 ball per strand
    constexpr uint32_t kMaxWaterPolylinePoints = 17; // 16 balls per strand

    // ------------------------------------------------------------------
    // Multiple pole "styles" (docs/sc4-powerline-tool-re.md SS "B. Multiple pole types/families").
    // Data-model + style-switching resolver. Style switching is driven in-game by the Tab/Shift-Tab
    // OnKeyDown hook (see OnKeyDownHook / CycleStyle), gated on the power tool being active.
    //
    // cSC4PowerLineTool::CreatePowerPole (Windows 0x00650140) reads the pole instance for a
    // direction mask from the vanilla global g_dwPowerPoleForDirectionsFlag[16] (0x00B467B0,
    // confirmed docs SS7/SS8) at exactly 2 sites -- merge-branch and new-pole branch, both the
    // identical 7-byte `MOV EAX,[EDI*4+0xB467B0]` (confirmed via raw memory read, not just
    // disassembly text). Both patched to call a resolver that passes through to the vanilla array
    // when no custom style is active (index 0), or looks up the active style's table otherwise,
    // falling back to vanilla per-entry for any direction mask a sparse custom section doesn't
    // define. EDI (the direction mask) is untouched by the hook -- it's read again immediately after
    // both call sites (OR'd into the connection's stored direction flags), so must survive.
    // ------------------------------------------------------------------
    constexpr uintptr_t kPowerPoleForDirectionsFlag = 0x00B467B0; // vanilla 16xuint32 table
    constexpr uintptr_t kPoleStyleLookupSite1 = 0x00650177; // merge-branch (existing pole at cell)
    constexpr uintptr_t kPoleStyleLookupSite2 = 0x006501cd; // new-pole branch

    // ------------------------------------------------------------------
    // Per-style inter-pole distance. cSC4PowerLineTool::DeterminePolePositions (Windows 0x00650840,
    // docs/sc4-powerline-tool-re.md SS16) forces a new pole once the current drag step exceeds the
    // tool's "Max cells between power poles" field, read at `*(tool+0x3a4) <= (curStep - lastPoleStep)`.
    // Vanilla loads that field once from the Utilities exemplar (property 0x098B25C8 = 10, docs SS2);
    // it's a plain tool-instance member, and the placement loop is its only reader. The hook saves the
    // field, writes the active style's value, runs vanilla, then restores -- fully scoped, so no
    // persistent per-tool cache is needed. Called from 3 sites (preview, alt-preview, commit/PlacePoles).
    // ------------------------------------------------------------------
    constexpr uintptr_t kDeterminePolePositions = 0x00650840;
    constexpr std::array<uintptr_t, 3> kDeterminePolePositionsCallSites = {
        0x00652063, 0x006522bf, 0x006529c2,
    };
    constexpr uint32_t kTool_MaxCellsBetweenPoles = 0x3a4; // cSC4PowerLineTool instance field
    constexpr uint32_t kMaxInterPoleDistance = 50; // sanity clamp; vanilla is 10

    // ------------------------------------------------------------------
    // FAR (fractional-angle) power-line dragging -- validated in-game 2026-07-19. See
    // docs/sc4-powerline-tool-re.md SS17 for the complete evidence trail.
    //
    // cSC4NetworkTool::DrawNetworkLine (0x0063af40) is virtual (slot +0x48) and has NO direct call
    // sites -- every network tool dispatches it through its own vtable. cSC4PowerLineTool's ctor
    // (0x006503c0) installs primary vtable 0x00aa9f30, whose DrawNetworkLine slot is the dword at
    // 0x00aa9f78. Swapping that one dword hooks the power tool ONLY -- road/rail/street vtables
    // keep their own untouched slots, so no runtime tool-identity check is needed.
    //
    // The hook, when a FAR ratio is active and the drag is not axis/45-aligned, snaps the cursor
    // to the nearest whole number of FAR periods, then synthesizes the dragged-step/cell arrays:
    // one step per FAR period whose FIRST cell is the period's integer grid node, plus a final
    // single-cell step for the last node, with this+0x74 == this+0x70 (all steps "straight", so
    // GetPrimaryCell returns each step's first cell). DeterminePolePositionsHook scopes
    // this+0x3a4 = 1 for the same drag, so vanilla DeterminePolePositions forces a pole at every
    // step's primary cell -- exactly the FAR nodes -- and nothing else (docs SS17.C).
    //
    // Allocation: the game's step/cell vectors use the game allocator, so the hook first runs the
    // ORIGINAL DrawNetworkLine with a fake straight drag of exactly the needed cell count (vanilla
    // performs all allocation), then rewrites the POD contents in place and shrinks the end
    // pointers. Any capacity/expectation mismatch falls back to a plain vanilla call.
    // ------------------------------------------------------------------
    constexpr uintptr_t kDrawNetworkLine = 0x0063af40;
    constexpr uintptr_t kPowerToolDrawNetworkLineSlot = 0x00aa9f78; // dword inside vtable 0x00aa9f30
    constexpr uint32_t kTool_StepsBegin = 0x54;   // vector<tDraggedStep> begin ptr (stride 0xc)
    constexpr uint32_t kTool_StepsEnd = 0x58;
    constexpr uint32_t kTool_CellsBegin = 0x60;   // vector<SC4Point<uint>> begin ptr (stride 8)
    constexpr uint32_t kTool_CellsEnd = 0x64;
    constexpr uint32_t kTool_DiagonalFlag = 0x6c; // byte: drag has a diagonal segment
    constexpr uint32_t kTool_TotalSteps = 0x70;
    constexpr uint32_t kTool_StraightSteps = 0x74;
    constexpr uint32_t kTool_FirstDirection = 0x7c;  // cardinal direction used by neighbor connection logic
    constexpr uint32_t kTool_SecondDirection = 0x80;
    constexpr uint32_t kTool_CityCellsX = 0x240;     // city dimensions in cells
    constexpr uint32_t kTool_CityCellsZ = 0x244;

    // cSC4PowerLineTool::UpdateOnZoneChange (Windows twin confirmed by the runtime shutdown stack
    // and Mac symbol/call-shape match). It deliberately removes both sides of every connection
    // crossing the changed zone rectangle at 0x0065279f/0x006527a9, then attempts to reposition
    // and rebuild it. Bypass the complete transaction while keepwires is enabled.
    constexpr uintptr_t kUpdateOnZoneChange = 0x006522f0;
    constexpr uintptr_t kUpdateOnZoneChangeResume = 0x006522f7;

    // ------------------------------------------------------------------
    // Tab / Shift-Tab pole-style switch. Keyboard input for every network tool is dispatched by the
    // shared cSC4ViewInputControlNetworkTool (vtable 0x00aab008, confirmed by decompile). Its
    // cISC4ViewInputControl::OnKeyDown is interface slot 14 = byte 0x38 -> the dword at 0x00aab040
    // holds the OnKeyDown body 0x00661e90. Vanilla OnKeyDown only reacts to Escape (0x1b), so Tab is
    // unclaimed here. Because the vtable is shared across road/rail/street/power, the slot patch is
    // NOT auto-scoped the way the DrawNetworkLine patch was -- it must runtime-gate on the active
    // network type. The input control stores the current network type at +0x50 (fed to
    // SL::NetworkManager in Init); the power line tool is network type 5 (cSC4PowerLineTool ctor
    // calls the cSC4NetworkTool base with 5). That enum -- not a vtable address -- is the runtime
    // discriminator: stable across rebuilds, and verified in-game. (The +0x4c subtool pointer is a
    // shared cSC4NetworkToolUI, NOT the power tool object, so a vtable compare there was wrong.)
    // See docs/power-line-style-ui-design.md.
    constexpr uintptr_t kNetworkToolInputControlVtable = 0x00aab008;
    constexpr uintptr_t kOnKeyDownSlot = 0x00aab040;   // vtable + 0x38 (interface slot 14)
    constexpr uintptr_t kOnKeyDown = 0x00661e90;       // cSC4ViewInputControlNetworkTool::OnKeyDown
    constexpr uint32_t kIC_NetworkType = 0x50;         // current network type on the input control
    constexpr uint32_t kNetworkTypePower = 5;
    constexpr uint32_t kVkTab = 0x09;
    static_assert(kOnKeyDownSlot == kNetworkToolInputControlVtable + 0x38);



    struct FarRatio {
        uint32_t run;  // cells along the major axis per period
        uint32_t rise; // cells along the minor axis per period
    };
    // arctan(rise/run): FAR-1.5 = 33.69, FAR-2 = 26.57, FAR-3 = 18.43, FAR-6 = 9.46 degrees.
    constexpr std::array<FarRatio, 4> kFarRatios = {{
        {3, 2}, {2, 1}, {3, 1}, {6, 1},
    }};

    // A FAR span is undirected, so per ratio exactly 4 distinct pole headings exist: the 1:n slope
    // tilted off the x axis with its minor step in +z (XP) or -z (XN), and the mirrored n:1 pair off
    // the z axis with its minor step in +x (ZP) or -x (ZN). See docs/far-power-lines-design.md.
    // Kept in this order so a style/table index is `ratioIndex * kFarOrientCount + orient`.
    enum FarOrient : uint32_t { kFarOrientXP = 0, kFarOrientXN = 1, kFarOrientZP = 2, kFarOrientZN = 3 };
    constexpr uint32_t kFarOrientCount = 4;
    constexpr uint32_t kFarHeadingCount = static_cast<uint32_t>(kFarRatios.size()) * kFarOrientCount; // 16

    // Human-readable tags for debug logging (order matches kFarRatios / the FarOrient enum).
    constexpr std::array<std::string_view, 4> kFarRatioLabels = {{"1.5", "2", "3", "6"}};
    constexpr std::string_view FarOrientName(const uint32_t orient) {
        switch (orient) {
            case kFarOrientXP: return "XP";
            case kFarOrientXN: return "XN";
            case kFarOrientZP: return "ZP";
            case kFarOrientZN: return "ZN";
            default: return "??";
        }
    }

    bool gFarDragActive = false;      // latest DrawNetworkLine call produced a FAR path
    uint32_t gFarDragRun = 0;         // major-axis cells per period of that FAR path
    uint32_t gFarDragRatioIndex = 0;  // index into kFarRatios for that FAR path
    uint32_t gFarDragOrient = kFarOrientXP; // FarOrient of that FAR path

    // Automatic heading selection considers the two regular headings bounding the first octant
    // (axis and 45-degree diagonal) plus every supported FAR ratio. The remaining octants are
    // reflections of this one. Candidate 0 is the nearest axis, 1..N are kFarRatios, and N+1 is
    // the diagonal. A small angular hysteresis prevents integer cursor movement from flickering
    // between adjacent headings at their exact midpoint during a drag.
    constexpr uint32_t kAxisSnapCandidate = 0;
    constexpr uint32_t kDiagonalSnapCandidate = static_cast<uint32_t>(kFarRatios.size()) + 1;
    constexpr float kDiagonalAngleRadians = 0.78539816339744830962f;
    constexpr float kSnapHysteresisRadians = 0.01745329251994329577f; // 1 degree
    bool gFarSnapAnchorValid = false;
    uint32_t gFarSnapAnchorX = 0;
    uint32_t gFarSnapAnchorZ = 0;
    uint32_t gFarSnapCandidate = kAxisSnapCandidate;

    struct PoleStyle {
        std::string name;
        std::array<uint32_t, 16> instanceByDirectionMask{};
        std::array<bool, 16> hasMask{};
        // FAR headings, indexed by `ratioIndex * kFarOrientCount + orient`. Selected during a FAR
        // drag by the exact (ratio, orientation); direction masks cannot express a FAR heading.
        std::array<uint32_t, kFarHeadingCount> farInstanceByHeading{};
        std::array<bool, kFarHeadingCount> hasFarHeading{};
        uint32_t maxCellsBetweenPoles = 0; // 0 = inherit vanilla; else cells between poles
        bool hasMaxCells = false;
    };

    // Index 0 = vanilla (no override). gStyles[gActiveStyleIndex - 1] for gActiveStyleIndex >= 1.
    std::vector<PoleStyle> gStyles;
    uint32_t gActiveStyleIndex = 0;

    // The optional [PowerPoles.FAR] section: a style-independent fallback consulted for FAR headings
    // when the active style doesn't define the exact key. Only its farInstanceByHeading is used; it
    // is never a cyclable style and never enters gStyles.
    PoleStyle gFarDefaultStyle;
    bool gHasFarDefault = false;

    // ------------------------------------------------------------------
    // Data-driven settings (SC4PowerPoleCustomization.ini).
    // ------------------------------------------------------------------
    struct Settings {
        bool enabled = true;
        uint32_t maxPointsPerDirection = 8; // raise to allow denser cable bundles than vanilla's 4
        bool crossZones = true;             // SC4SIM: poles on empty zones and farm fields (see CellIsOKForPlacementHook)
        bool dezoneUnderPoles = true;       // SC4SIM: remove the zone from the cell of every new pole
        bool dezoneFieldCells = true;       // SC4SIM: also on farm fields (the game demolishes that field lot)
    };

    Settings gSettings;

    // ------------------------------------------------------------------
    // Per-pole attach-point override -- the data-driven core of this mod.
    //
    // Deliberately NOT stored inside cSC4PowerPoleOccupant itself: the engine allocates these
    // objects through a fixed-size operator new (object size confirmed 292 bytes on Windows, see
    // docs SS9), and several "unk_*" fields past our recovered struct boundary are still unmapped.
    // Growing the live object is a real risk (see also the project's existing fixed-pool-growth
    // bug notes for this engine's allocators); a side table keyed by occupant pointer carries the
    // override data without touching engine memory layout at all.
    // ------------------------------------------------------------------
    struct AttachPoint {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct PoleAttachOverride {
        std::array<std::vector<AttachPoint>, 4> perDirection; // index = connection direction 0-3
        float defaultWidthScale = 1.0f;
        float defaultSagScale = 1.0f;
        float defaultMaximumSag = std::numeric_limits<float>::infinity();
        std::vector<float> perWireWidthScales;
        std::vector<float> perWireSagScales;
        std::vector<float> perWireMaximumSags;
        bool hasWidthCustomization = false;
        bool hasSagCustomization = false;
        float foundationHalfExtent = kVanillaFoundationHalfExtent;
        bool hasFoundationHalfExtent = false;
        uint32_t foundationFloorTextureId = kVanillaFloorTextureId;
        bool hasFloorTexture = false;
        uint32_t foundationWallTextureId = kVanillaWallTextureId;
        bool hasWallTexture = false;
        uint32_t wireTextureId = kVanillaWireStrandTextureId;
        bool hasWireTexture = false;
        uint32_t wireBuoyTextureId = kVanillaWireBuoyTextureId;
        bool hasBuoyTexture = false;
        bool foundationFollowSpan = false;
        float waterBuoySizeScale = 1.0f;
        bool hasWaterBuoySize = false;
        uint32_t waterBuoyPointCount = 0; // 0 = vanilla count
        bool hasWaterBuoyCount = false;
        // In-plane heading (radians, normalized to [0, pi)) the custom attach points were baked for.
        // Only consulted for directions that actually carry custom attach points.
        float attachBasisRadians = 0.0f;
        bool hasAttachBasis = false;

        [[nodiscard]] uint32_t PointCount(const uint32_t direction) const {
            return direction < 4 ? static_cast<uint32_t>(perDirection[direction].size()) : 0;
        }

        [[nodiscard]] const AttachPoint* Point(const uint32_t direction, const uint32_t index) const {
            if (direction >= 4 || index >= perDirection[direction].size()) {
                return nullptr;
            }
            return &perDirection[direction][index];
        }

        [[nodiscard]] bool HasAttachPointCustomization() const {
            return std::ranges::any_of(perDirection, [](const auto& points) { return !points.empty(); });
        }

        [[nodiscard]] float WidthScale(const uint32_t index) const {
            return index < perWireWidthScales.size() ? perWireWidthScales[index] : defaultWidthScale;
        }

        [[nodiscard]] float SagScale(const uint32_t index) const {
            return index < perWireSagScales.size() ? perWireSagScales[index] : defaultSagScale;
        }

        [[nodiscard]] float MaximumSag(const uint32_t index) const {
            return index < perWireMaximumSags.size() ? perWireMaximumSags[index] : defaultMaximumSag;
        }
    };

    // this(cSC4PowerPoleOccupant*) -> override. Entries exist only when at least one valid attach-
    // point or wire-appearance property is present. Everything else stays entirely vanilla.
    //
    // This is the only per-pole side table. Per-wire width, wire/buoy textures and buoy size are not
    // stored per connection or per polyline buffer: the DrawPowerlines hooks receive the pole, the
    // tConnection entry and the polyline being drawn, and resolve the value from gOverrides on the
    // spot (pole, connection's other pole, wire index = position in the entry's polyline list). A
    // table keyed by raw tConnection/buffer addresses went stale when RemoveConnection (0x0064DA00)
    // shifted the remaining entries down by assignment (the inner vectors keep their buffers but
    // receive another span's points), when Read (0x0064EA00) restored wires without going through
    // AddConnection/UpdateConnection (so nothing was registered after loading a save), and when a
    // freed buffer was reused by another pole. Nothing can go stale when nothing is cached.
    std::unordered_map<void*, PoleAttachOverride> gOverrides;
    // Bumped on every change of gOverrides; invalidates the one-entry caches of the draw hooks.
    uint32_t gOverridesGeneration = 1;
    // False until the destructor hook (which drops a dying pole's entry) is installed.
    bool gOverridesAllowed = false;

    // Private connection-point tables for poles whose exemplar leaves InitConnectionPoints without
    // a table (see RepairMissingConnectionPointTable). cSC4PowerPoleOccupant::Read (0x0064EA00)
    // writes the four saved wire points of direction 0 through the occupant's table pointer, so a
    // pole pointed at the shared static tables would overwrite the table every vanilla pole uses.
    // The arrays are heap nodes, so their addresses survive rehashing; freed with the pole.
    constexpr size_t kConnectionPointTableFloats = 4 * kVanillaPointsPerDirection * 3; // 0xC0 bytes
    std::unordered_map<void*, std::unique_ptr<std::array<float, kConnectionPointTableFloats>>> gPrivateConnectionTables;

    // ------------------------------------------------------------------
    // Property reading. cISCPropertyHolder doesn't have a typed array getter (SCPropertyUtil only
    // covers scalars), so float arrays go through cISCProperty/cIGZVariant directly.
    // ------------------------------------------------------------------
    bool TryReadAttachPointProperty(const cISCPropertyHolder* holder, const uint32_t propertyId, std::vector<AttachPoint>& outPoints) {
        outPoints.clear();
        if (holder == nullptr || !holder->HasProperty(propertyId)) {
            return false;
        }

        const cISCProperty* const prop = holder->GetProperty(propertyId);
        const cIGZVariant* const variant = prop ? prop->GetPropertyValue() : nullptr;
        if (variant == nullptr || variant->GetType() != cIGZVariant::Float32Array) {
            LOG_WARN("PowerPoleCustomization: property 0x{:08X} is present but not a Float32 array; ignoring.", propertyId);
            return false;
        }

        const uint32_t valueCount = variant->GetCount();
        if (valueCount < 3) {
            LOG_WARN("PowerPoleCustomization: property 0x{:08X} has {} values, expected one or more XYZ triples; ignoring.", propertyId, valueCount);
            return false;
        }

        const float* const values = variant->RefFloat32();
        uint32_t firstCoordinate = 0;
        uint32_t availableCount = 0;
        uint32_t requestedCount = 0;
        if (valueCount % 3 == 0) {
            availableCount = valueCount / 3;
            requestedCount = availableCount;
        } else if (valueCount >= 4 && (valueCount - 1) % 3 == 0 && std::isfinite(values[0]) &&
                   values[0] >= 1.0f && std::floor(values[0]) == values[0]) {
            // Compatibility with the original PoC draft: [count, xyz...]. New properties should
            // use plain XYZ triples; new_properties.xml enforces groups of three.
            firstCoordinate = 1;
            availableCount = (valueCount - 1) / 3;
            requestedCount = static_cast<uint32_t>(values[0]);
        } else {
            LOG_WARN("PowerPoleCustomization: property 0x{:08X} has {} values, expected 3*N XYZ values; ignoring.", propertyId, valueCount);
            return false;
        }

        const uint32_t pointCount = std::clamp(std::min(requestedCount, availableCount), 1u, gSettings.maxPointsPerDirection);

        outPoints.reserve(pointCount);
        for (uint32_t i = 0; i < pointCount; ++i) {
            const float* const xyz = values + firstCoordinate + i * 3;
            if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2])) {
                LOG_WARN("PowerPoleCustomization: property 0x{:08X} contains a non-finite coordinate; ignoring.", propertyId);
                outPoints.clear();
                return false;
            }
            outPoints.push_back(AttachPoint{xyz[0], xyz[1], xyz[2]});
        }
        return true;
    }

    bool TryReadNonNegativeFloatProperty(const cISCPropertyHolder* holder, const uint32_t propertyId, float& outValue) {
        if (holder == nullptr || !holder->HasProperty(propertyId)) {
            return false;
        }
        const cISCProperty* const prop = holder->GetProperty(propertyId);
        const cIGZVariant* const variant = prop ? prop->GetPropertyValue() : nullptr;
        float value = 0.0f;
        if (variant == nullptr || variant->GetType() != cIGZVariant::Float32 ||
            !variant->GetValFloat32(value) || !std::isfinite(value) || value < 0.0f) {
            LOG_WARN("PowerPoleCustomization: property 0x{:08X} must be a non-negative Float32; ignoring.", propertyId);
            return false;
        }
        outValue = value;
        return true;
    }

    // Like the above but accepts any finite value (used for the signed attach-basis angle).
    bool TryReadFiniteFloatProperty(const cISCPropertyHolder* holder, const uint32_t propertyId, float& outValue) {
        if (holder == nullptr || !holder->HasProperty(propertyId)) {
            return false;
        }
        const cISCProperty* const prop = holder->GetProperty(propertyId);
        const cIGZVariant* const variant = prop ? prop->GetPropertyValue() : nullptr;
        float value = 0.0f;
        if (variant == nullptr || variant->GetType() != cIGZVariant::Float32 ||
            !variant->GetValFloat32(value) || !std::isfinite(value)) {
            LOG_WARN("PowerPoleCustomization: property 0x{:08X} must be a finite Float32; ignoring.", propertyId);
            return false;
        }
        outValue = value;
        return true;
    }

    bool TryReadUint32Property(const cISCPropertyHolder* holder, const uint32_t propertyId, uint32_t& outValue) {
        if (holder == nullptr || !holder->HasProperty(propertyId)) {
            return false;
        }
        const cISCProperty* const prop = holder->GetProperty(propertyId);
        const cIGZVariant* const variant = prop ? prop->GetPropertyValue() : nullptr;
        uint32_t value = 0;
        if (variant == nullptr || variant->GetType() != cIGZVariant::Uint32 || !variant->GetValUint32(value)) {
            LOG_WARN("PowerPoleCustomization: property 0x{:08X} must be a Uint32; ignoring.", propertyId);
            return false;
        }
        outValue = value;
        return true;
    }

    bool TryReadBoolProperty(const cISCPropertyHolder* holder, const uint32_t propertyId, bool& outValue) {
        if (holder == nullptr || !holder->HasProperty(propertyId)) {
            return false;
        }
        const cISCProperty* const prop = holder->GetProperty(propertyId);
        const cIGZVariant* const variant = prop ? prop->GetPropertyValue() : nullptr;
        bool value = false;
        if (variant == nullptr || variant->GetType() != cIGZVariant::Bool || !variant->GetValBool(value)) {
            LOG_WARN("PowerPoleCustomization: property 0x{:08X} must be a Bool; ignoring.", propertyId);
            return false;
        }
        outValue = value;
        return true;
    }

    // ------------------------------------------------------------------
    // Custom foundation-texture registration. Draw() resolves texture IDs through the engine's
    // power-pole texture registry (see the kPoleTextureRegistry constants block); an ID missing
    // from that registry is a guaranteed null dereference, and StaticInit only ever registers the
    // four vanilla IDs. A custom ID is therefore accepted only after (1) its FSH resource is
    // proven to exist and (2) it has been inserted into the registry and re-found by the same
    // bucket walk Draw performs. Any failure keeps the pole on the vanilla texture.
    // ------------------------------------------------------------------
    std::unordered_set<uint32_t> gRegisteredPoleTextures; // IDs this DLL inserted this session

    // Read-only re-find using the exact node layout Draw's own walk uses.
    bool PoleTextureRegistryContains(const uint32_t textureId) {
        const auto* const bucketsBegin = *reinterpret_cast<uint8_t* const*>(kPoleTextureBucketsBegin);
        const auto* const bucketsEnd = *reinterpret_cast<uint8_t* const*>(kPoleTextureBucketsEnd);
        if (bucketsBegin == nullptr || bucketsEnd <= bucketsBegin) {
            return false;
        }
        const auto bucketCount = static_cast<uint32_t>((bucketsEnd - bucketsBegin) / 4);
        const uint8_t* node =
            reinterpret_cast<const uint8_t* const*>(bucketsBegin)[textureId % bucketCount];
        while (node != nullptr && *reinterpret_cast<const uint32_t*>(node + 4) != textureId) {
            node = *reinterpret_cast<const uint8_t* const*>(node);
        }
        return node != nullptr;
    }

    bool EnsurePoleTextureRegistered(const uint32_t textureId, const char* const use) {
        if (textureId == kVanillaFloorTextureId || textureId == kVanillaWallTextureId ||
            textureId == kVanillaWireStrandTextureId || textureId == kVanillaWireBuoyTextureId ||
            gRegisteredPoleTextures.contains(textureId)) {
            return true; // the four vanilla IDs are registered by StaticInit itself
        }

        // Registered by someone else (another pole DLL, or a future engine change): usable as-is,
        // and its live binding must not be disturbed.
        if (PoleTextureRegistryContains(textureId)) {
            gRegisteredPoleTextures.insert(textureId);
            return true;
        }

        const cIGZPersistResourceManagerPtr resourceManager;
        const cGZPersistResourceKey key(kFshTypeId, kPoleTextureGroupId, textureId);
        if (!resourceManager || !resourceManager->TestForKey(key)) {
            LOG_WARN("PowerPoleCustomization: {} texture ID 0x{:08X} has no FSH resource "
                     "(type 0x{:08X}, group 0x{:08X}); keeping the vanilla texture. Custom pad "
                     "textures must ship as FSH in that exact group.",
                     use, textureId, kFshTypeId, kPoleTextureGroupId);
            return false;
        }

        using RegistryInsertFn = uint32_t*(__thiscall*)(void* map, const uint32_t* key);
        uint32_t* const slot = reinterpret_cast<RegistryInsertFn>(kPoleTextureRegistryInsert)(
            reinterpret_cast<void*>(kPoleTextureRegistry), &textureId);
        if (slot == nullptr || !PoleTextureRegistryContains(textureId)) {
            LOG_ERROR("PowerPoleCustomization: engine texture-registry insert for {} texture ID "
                      "0x{:08X} could not be verified; keeping the vanilla texture.", use, textureId);
            return false;
        }
        *slot = 0; // fresh node: no binding yet (StaticInit zeroes its own fresh slots the same way)
        // Force the engine to reload every registered binding (including this one) on the next
        // Draw. Zoom levels are 0-4 (cSC4CameraControl ctor 0x007CC990: min 0, max 4) and the
        // global holds 5 until the first pole draw, so 5 (the engine's own "unset" value) always
        // mismatches. 0 would not: a texture registered while the camera is at zoom 0 would keep
        // its null binding and DrawPowerlines/Draw would dereference it.
        *reinterpret_cast<uint32_t*>(kPoleTextureZoomGlobal) = kPoleTextureZoomUnset;
        gRegisteredPoleTextures.insert(textureId);
        LOG_INFO("PowerPoleCustomization: registered {} texture ID 0x{:08X} with the engine's "
                 "power-pole texture registry.", use, textureId);
        return true;
    }

    bool ShouldInvertModelQuarterTurn(void* occupant) {
        cRZAutoRefCount<cISCExemplarPropertyHolder> propertyHolder;
        auto* const unknown = reinterpret_cast<cIGZUnknown*>(occupant);
        if (!unknown->QueryInterface(GZIID_cISCExemplarPropertyHolder, propertyHolder.AsPPVoid())) {
            return false;
        }

        cISCResExemplar* const exemplar = propertyHolder->GetDefaultExemplar();
        const cISCPropertyHolder* const holder = exemplar ? exemplar->AsISCPropertyHolder() : nullptr;
        bool invert = false;
        return TryReadBoolProperty(holder, kPropInvertModelQuarterTurn, invert) && invert;
    }

    using GetModelInstanceIDFn = uint32_t(__cdecl*)(void* resourceKeys, uint32_t zoom,
                                                    uint32_t cameraRotation, uint32_t quarterTurn,
                                                    bool useExplicitRotation, uint32_t* outType,
                                                    uint32_t* outGroup);

    uint32_t __cdecl GetModelInstanceIDHook(void* resourceKeys, const uint32_t zoom,
                                           const uint32_t cameraRotation, uint32_t quarterTurn,
                                           const bool useExplicitRotation, uint32_t* const outType,
                                           uint32_t* const outGroup) {
        auto* const occupant = reinterpret_cast<uint8_t*>(resourceKeys) - kOccupant_ModelResourceKeys;
        if (ShouldInvertModelQuarterTurn(occupant)) {
            quarterTurn ^= 1u;
        }

        const auto original = reinterpret_cast<GetModelInstanceIDFn>(kGetModelInstanceID);
        return original(resourceKeys, zoom, cameraRotation, quarterTurn, useExplicitRotation, outType, outGroup);
    }

    bool TryReadNonNegativeFloatArray(const cISCPropertyHolder* holder, const uint32_t propertyId,
                                      std::vector<float>& outValues) {
        outValues.clear();
        if (holder == nullptr || !holder->HasProperty(propertyId)) {
            return false;
        }
        const cISCProperty* const prop = holder->GetProperty(propertyId);
        const cIGZVariant* const variant = prop ? prop->GetPropertyValue() : nullptr;
        if (variant == nullptr || variant->GetType() != cIGZVariant::Float32Array || variant->GetCount() == 0) {
            LOG_WARN("PowerPoleCustomization: property 0x{:08X} must be a non-empty Float32 array; ignoring.", propertyId);
            return false;
        }

        const uint32_t count = std::min(variant->GetCount(), gSettings.maxPointsPerDirection);
        const float* const values = variant->RefFloat32();
        outValues.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!std::isfinite(values[i]) || values[i] < 0.0f) {
                LOG_WARN("PowerPoleCustomization: property 0x{:08X} contains a negative or non-finite value; ignoring.", propertyId);
                outValues.clear();
                return false;
            }
            outValues.push_back(values[i]);
        }
        return true;
    }

    // Builds (or refreshes) this pole's customization from its current default exemplar. Property
    // lookup includes cohort inheritance, so family-wide appearance defaults can live on a shared
    // cohort while individual prop exemplars define only their local attachment geometry.
    bool TryBuildOverride(cISCResExemplar* exemplar, PoleAttachOverride& out) {
        if (exemplar == nullptr) {
            return false;
        }

        const cISCPropertyHolder* const holder = exemplar->AsISCPropertyHolder();
        bool any = false;
        for (uint32_t direction = 0; direction < 4; ++direction) {
            if (TryReadAttachPointProperty(holder, kAttachPointProperties[direction], out.perDirection[direction])) {
                any = true;
            }
        }

        if (TryReadNonNegativeFloatProperty(holder, kPropWireWidthScale, out.defaultWidthScale)) {
            out.hasWidthCustomization = true;
            any = true;
        }
        if (TryReadNonNegativeFloatArray(holder, kPropWireWidthScales, out.perWireWidthScales)) {
            out.hasWidthCustomization = true;
            any = true;
        }
        if (TryReadNonNegativeFloatProperty(holder, kPropWireSagScale, out.defaultSagScale)) {
            out.hasSagCustomization = true;
            any = true;
        }
        if (TryReadNonNegativeFloatProperty(holder, kPropWireMaximumSag, out.defaultMaximumSag)) {
            out.hasSagCustomization = true;
            any = true;
        }
        if (TryReadNonNegativeFloatArray(holder, kPropWireSagScales, out.perWireSagScales)) {
            out.hasSagCustomization = true;
            any = true;
        }
        if (TryReadNonNegativeFloatArray(holder, kPropWireMaximumSags, out.perWireMaximumSags)) {
            out.hasSagCustomization = true;
            any = true;
        }

        if (TryReadNonNegativeFloatProperty(holder, kPropFoundationHalfExtent, out.foundationHalfExtent)) {
            out.foundationHalfExtent = std::min(out.foundationHalfExtent, kMaxFoundationHalfExtent);
            out.hasFoundationHalfExtent = true;
            any = true;
        }
        // Texture IDs are accepted only once the FSH resource is proven to exist and the ID is
        // registered with the engine's texture registry -- otherwise Draw() would null-deref on
        // the unknown ID (see EnsurePoleTextureRegistered). Failure falls back to vanilla.
        if (TryReadUint32Property(holder, kPropFoundationFloorTextureId, out.foundationFloorTextureId)) {
            if (EnsurePoleTextureRegistered(out.foundationFloorTextureId, "floor")) {
                out.hasFloorTexture = true;
                any = true;
            } else {
                out.foundationFloorTextureId = kVanillaFloorTextureId;
            }
        }
        if (TryReadUint32Property(holder, kPropFoundationWallTextureId, out.foundationWallTextureId)) {
            if (EnsurePoleTextureRegistered(out.foundationWallTextureId, "wall")) {
                out.hasWallTexture = true;
                any = true;
            } else {
                out.foundationWallTextureId = kVanillaWallTextureId;
            }
        }
        if (TryReadUint32Property(holder, kPropWireTextureId, out.wireTextureId)) {
            if (EnsurePoleTextureRegistered(out.wireTextureId, "wire strand")) {
                out.hasWireTexture = true;
                any = true;
            } else {
                out.wireTextureId = kVanillaWireStrandTextureId;
            }
        }
        if (TryReadUint32Property(holder, kPropWireBuoyTextureId, out.wireBuoyTextureId)) {
            if (EnsurePoleTextureRegistered(out.wireBuoyTextureId, "wire buoy")) {
                out.hasBuoyTexture = true;
                any = true;
            } else {
                out.wireBuoyTextureId = kVanillaWireBuoyTextureId;
            }
        }
        if (TryReadBoolProperty(holder, kPropFoundationFollowSpan, out.foundationFollowSpan) &&
            out.foundationFollowSpan) {
            any = true;
        }
        if (TryReadNonNegativeFloatProperty(holder, kPropWaterBuoySizeScale, out.waterBuoySizeScale)) {
            out.hasWaterBuoySize = true;
            any = true;
        }
        if (TryReadUint32Property(holder, kPropWaterBuoyPointCount, out.waterBuoyPointCount)) {
            out.waterBuoyPointCount = std::clamp(out.waterBuoyPointCount,
                                                 kMinWaterPolylinePoints, kMaxWaterPolylinePoints);
            out.hasWaterBuoyCount = true;
            any = true;
        }

        // Attach basis is only meaningful alongside custom attach points, so it deliberately does
        // NOT set `any`: a pole that declares a basis but no attach points has nothing to rotate and
        // stays fully vanilla. Stored normalized to [0, pi) to match ComputeSpanBearing's range.
        float basisDegrees = 0.0f;
        if (TryReadFiniteFloatProperty(holder, kPropAttachBasisDegrees, basisDegrees)) {
            constexpr float pi = 3.14159265358979323846f;
            float basisRadians = std::fmod(basisDegrees * (pi / 180.0f), pi);
            if (basisRadians < 0.0f) {
                basisRadians += pi;
            }
            out.attachBasisRadians = basisRadians;
            out.hasAttachBasis = true;
        }
        return any;
    }

    // ------------------------------------------------------------------
    // Attach-point yaw alignment. Vanilla stores attach offsets baked for exactly 4 nominal span
    // orientations (Get0To3Direction) and applies them with a flat translation add -- no rotation
    // matrix (docs SS6). For any span whose true bearing deviates from its direction's nominal
    // angle (every FAR span, and any future free-angle span), the baked offset is rotated around
    // the vertical axis by that deviation so cables meet the crossarm along the real span
    // direction. Deviation is 0 for all vanilla-aligned spans, so behavior there is unchanged.
    // ------------------------------------------------------------------
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kYawEpsilonRadians = 0.01f; // ~0.57 degrees; below this, skip the rebuild

    // Undirected nominal xz-plane angle per Get0To3Direction result: 0 = x axis, 1 = the
    // sign-agreeing diagonal (+x,+z), 2 = z axis, 3 = the opposing diagonal.
    constexpr std::array<float, 4> kNominalDirectionAngle = {0.0f, kPi / 4.0f, kPi / 2.0f, 3.0f * kPi / 4.0f};

    // Undirected in-plane bearing of the span: atan2(dz, dx) normalized to [0, pi). Both endpoints
    // of the undirected span agree on it. Returns false (and leaves the bearing at 0) when the two
    // poles coincide.
    bool ComputeSpanBearing(void* poleA, void* poleB, float& outBearing) {
        const auto* const posA = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(poleA) + kOccupant_PosX);
        const auto* const posB = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(poleB) + kOccupant_PosX);
        const float dx = posB[0] - posA[0];
        const float dz = posB[2] - posA[2];
        if (dx == 0.0f && dz == 0.0f) {
            outBearing = 0.0f;
            return false;
        }
        float bearing = std::atan2(dz, dx);
        if (bearing < 0.0f) {
            bearing += kPi; // undirected line angle in [0, pi)
        }
        outBearing = bearing;
        return true;
    }

    // Minimal undirected rotation: wrap a yaw deviation into (-pi/2, pi/2].
    float WrapYaw(float delta) {
        while (delta > kPi / 2.0f) delta -= kPi;
        while (delta <= -kPi / 2.0f) delta += kPi;
        return delta;
    }

    // The heading (radians, [0, pi)) this pole's attach offsets were baked for, in this direction.
    // A pole that both supplies custom attach points for the direction AND declares an attach basis
    // uses that basis; every other case -- vanilla baked table, or no declared basis -- uses the
    // direction's nominal 0/45/90/135-degree angle, so cables stay aligned to the table they
    // actually came from. This is what lets a FAR-authored pole and a fallback pole share one span.
    float AttachBasisAngle(void* pole, const uint32_t direction) {
        const auto it = gOverrides.find(pole);
        if (it != gOverrides.end() && it->second.hasAttachBasis && it->second.PointCount(direction) > 0) {
            return it->second.attachBasisRadians;
        }
        return kNominalDirectionAngle[direction & 3];
    }

    AttachPoint RotateOffsetAroundY(const AttachPoint& offset, const float yaw) {
        if (yaw == 0.0f) {
            return offset;
        }
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        return AttachPoint{offset.x * c - offset.z * s, offset.y, offset.x * s + offset.z * c};
    }

    // ------------------------------------------------------------------
    // Vanilla attach-point read, reimplemented for the case where a pole has no override -- exact
    // same arithmetic as the original GetLineConnectionPoints (its own function at 0x00649BA0, not
    // inlined; reimplemented here rather than called so the yaw rotation can be applied): table base is
    // read from this+kOccupant_ConnectionPointTablePtr, point N of direction D is the cS3DVector3
    // at (D & 3) * 0x30 + N * 0xc, translated by this pole's world position -- with the one
    // deliberate addition that the model-local offset is yaw-rotated first (see above).
    // ------------------------------------------------------------------
    AttachPoint ReadVanillaAttachPoint(void* occupant, const uint32_t direction, const uint32_t index,
                                       const float yawDelta) {
        auto* const base = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(occupant) + kOccupant_ConnectionPointTablePtr);
        const auto* const point = reinterpret_cast<const float*>(
            base + (direction & 3) * kVanillaDirectionStrideBytes + index * 0xc);

        const AttachPoint local = RotateOffsetAroundY(AttachPoint{point[0], point[1], point[2]}, yawDelta);
        const auto* const position = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(occupant) + kOccupant_PosX);
        return AttachPoint{local.x + position[0], local.y + position[1], local.z + position[2]};
    }

    // Number of strands this pole exposes in a given direction: override count if one exists,
    // otherwise the vanilla fixed arity. This is the function that replaces the engine's hardcoded
    // "4" -- everywhere AddConnection/UpdateConnection looped a literal 4 times, they should now
    // loop std::min(StrandCount(poleA, dir), StrandCount(poleB, dir)) times instead.
    uint32_t StrandCount(void* occupant, const uint32_t direction) {
        const auto it = gOverrides.find(occupant);
        if (it == gOverrides.end()) {
            return kVanillaPointsPerDirection;
        }
        const uint32_t count = it->second.PointCount(direction);
        return count > 0 ? count : kVanillaPointsPerDirection;
    }

    AttachPoint GetAttachPoint(void* occupant, const uint32_t direction, const uint32_t index,
                               const float yawDelta) {
        const auto it = gOverrides.find(occupant);
        if (it != gOverrides.end()) {
            if (const AttachPoint* const p = it->second.Point(direction, index)) {
                const AttachPoint local = RotateOffsetAroundY(*p, yawDelta);
                const auto* const position = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(occupant) + kOccupant_PosX);
                return AttachPoint{local.x + position[0], local.y + position[1], local.z + position[2]};
            }
        }
        return ReadVanillaAttachPoint(occupant, direction, index, yawDelta);
    }

    // ------------------------------------------------------------------
    // Finds the active/render-owning tConnection for this exact pole pair. Windows UpdateConnection
    // performs the same scan: entry+0x00 must equal otherPole and entry+0x09 must be nonzero. Only
    // that side contains the polylines DrawPowerlines visits; the reciprocal inactive entry normally
    // has an empty polyline vector and must not be topped up.
    // ------------------------------------------------------------------
    void* FindConnectionEntry(void* pole, void* otherPole) {
        auto* const begin = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(pole) + kOccupant_ConnectionsBegin);
        auto* const end = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(pole) + kOccupant_ConnectionsEnd);
        for (uint8_t* entry = begin; entry != end; entry += kConnection_Size) {
            if (*reinterpret_cast<void**>(entry + kConnection_OtherPole) == otherPole &&
                *reinterpret_cast<const uint8_t*>(entry + kConnection_IsRenderOwner) != 0) {
                return entry;
            }
        }
        return nullptr;
    }

    const PoleAttachOverride* FindAppearanceOverride(void* preferredPole, void* otherPole) {
        const auto preferred = gOverrides.find(preferredPole);
        if (preferred != gOverrides.end() &&
            (preferred->second.hasWidthCustomization || preferred->second.hasSagCustomization)) {
            return &preferred->second;
        }
        const auto other = gOverrides.find(otherPole);
        if (other != gOverrides.end() &&
            (other->second.hasWidthCustomization || other->second.hasSagCustomization)) {
            return &other->second;
        }
        return nullptr;
    }

    bool HasAttachPointCustomization(void* occupant, const uint32_t direction) {
        const auto it = gOverrides.find(occupant);
        return it != gOverrides.end() && it->second.PointCount(direction) > 0;
    }

    void AdjustControlPointSag(float* controlPoint, const AttachPoint& a, const AttachPoint& b,
                               const float sagScale, const float maximumSag) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float dz = b.z - a.z;
        const float lengthSquared = dx * dx + dy * dy + dz * dz;
        if (lengthSquared <= std::numeric_limits<float>::epsilon()) {
            return;
        }

        const float along = ((controlPoint[0] - a.x) * dx + (controlPoint[1] - a.y) * dy +
                             (controlPoint[2] - a.z) * dz) / lengthSquared;
        const float straightLineY = a.y + dy * along;
        const float vanillaSag = std::max(0.0f, straightLineY - controlPoint[1]);
        const float scaledSag = std::min(vanillaSag * sagScale, maximumSag);
        controlPoint[1] = straightLineY - scaledSag;
    }

    // Builds one tessellated Bezier polyline between two attach points and appends it into one of
    // the tConnection's two polyline lists (regular at +0x14, water-crossing coarse at +0x20 --
    // both are vector<vector<cS3DVector3>> and share the engine insert helper). Vanilla still
    // supplies its horizontal control geometry and tessellation count; only the vertical sag
    // component is scaled/clamped per wire.
    void AppendPolylineToList(void* pole, void* entry, const uint32_t listBeginOffset, const uint32_t listEndOffset,
                              const AttachPoint& a, const AttachPoint& b,
                              const float sagScale, const float maximumSag) {
        float control1[3];
        float control2[3];
        int32_t pointCount = 0;
        const auto getControlPoints = reinterpret_cast<GetControlPointsFn>(kGetControlPoints);
        getControlPoints(pole, a.x, a.y, a.z, b.x, b.y, b.z, control1, control2, &pointCount);
        AdjustControlPointSag(control1, a, b, sagScale, maximumSag);
        AdjustControlPointSag(control2, a, b, sagScale, maximumSag);
        pointCount = std::clamp(pointCount, 2, 64); // vanilla treats this as total points, including p0

        const float p0[3] = {a.x, a.y, a.z};
        const float p1[3] = {b.x, b.y, b.z};
        std::vector<float> points(static_cast<size_t>(pointCount) * 3);
        points[0] = a.x;
        points[1] = a.y;
        points[2] = a.z;
        const auto tessellate = reinterpret_cast<TessellateBezierSegmentFn>(kTessellateBezierSegment);
        tessellate(p0, control1, control2, p1, reinterpret_cast<uintptr_t>(points.data() + 3), pointCount - 1);

        const void* view[3] = {points.data(), points.data() + points.size(), points.data() + points.size()};
        auto* const polylines = reinterpret_cast<uint8_t*>(entry) + listBeginOffset;
        void* const insertPos = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(entry) + listEndOffset);
        const auto insert = reinterpret_cast<PolylineListInsertFn>(kPolylineListInsert);
        insert(polylines, insertPos, view, 0, 1, 1);
    }

    void AppendPolylineToConnection(void* pole, void* entry, const AttachPoint& a, const AttachPoint& b,
                                    const float sagScale, const float maximumSag) {
        AppendPolylineToList(pole, entry, kConnection_PolylinesBegin, kConnection_PolylinesEnd,
                             a, b, sagScale, maximumSag);
    }

    // Water-crossing variant, the same layout as vanilla: for totalPoints points it tessellates
    // totalPoints + 1 uniform points along the sagged bezier and drops the t = 0 one, so the stored
    // points sit at t = k/totalPoints for k = 1..totalPoints (vanilla: count 3, t = 1/3, 2/3, 1). The
    // engine draws a buoy at every point except the last, so neither pole wears one.
    void AppendWaterPolylineToConnection(void* pole, void* entry, const AttachPoint& a, const AttachPoint& b,
                                         const float sagScale, const float maximumSag,
                                         const int32_t totalPoints) {
        const int32_t balls = std::clamp(totalPoints - 1, 1, 62);
        float control1[3];
        float control2[3];
        int32_t ignoredCount = 0;
        const auto getControlPoints = reinterpret_cast<GetControlPointsFn>(kGetControlPoints);
        getControlPoints(pole, a.x, a.y, a.z, b.x, b.y, b.z, control1, control2, &ignoredCount);
        AdjustControlPointSag(control1, a, b, sagScale, maximumSag);
        AdjustControlPointSag(control2, a, b, sagScale, maximumSag);

        const float p0[3] = {a.x, a.y, a.z};
        const float p1[3] = {b.x, b.y, b.z};
        // balls + 2 uniform points including both endpoints; index 0 (= pole A) is discarded.
        std::vector<float> uniform(static_cast<size_t>(balls + 2) * 3);
        uniform[0] = a.x;
        uniform[1] = a.y;
        uniform[2] = a.z;
        const auto tessellate = reinterpret_cast<TessellateBezierSegmentFn>(kTessellateBezierSegment);
        tessellate(p0, control1, control2, p1, reinterpret_cast<uintptr_t>(uniform.data() + 3), balls + 1);

        const std::vector<float> points(uniform.begin() + 3, uniform.end()); // balls + 1 points
        const void* view[3] = {points.data(), points.data() + points.size(), points.data() + points.size()};
        auto* const polylines = reinterpret_cast<uint8_t*>(entry) + kConnection_WaterPolylinesBegin;
        void* const insertPos = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(entry) + kConnection_WaterPolylinesEnd);
        const auto insert = reinterpret_cast<PolylineListInsertFn>(kPolylineListInsert);
        insert(polylines, insertPos, view, 0, 1, 1);
    }

    void* ConnectionOtherPole(const void* entry) {
        return *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(entry) + kConnection_OtherPole);
    }

    const PoleAttachOverride* FindOverride(void* pole) {
        if (pole == nullptr) {
            return nullptr;
        }
        const auto it = gOverrides.find(pole);
        return it != gOverrides.end() ? &it->second : nullptr;
    }

    // Wire/buoy textures of a rendered connection: the render owner's pair if its exemplar sets
    // either texture, else the other endpoint's, mirroring FindAppearanceOverride's precedence.
    const PoleAttachOverride* FindTextureSource(void* occupant, void* otherPole) {
        const PoleAttachOverride* const self = FindOverride(occupant);
        if (self != nullptr && (self->hasWireTexture || self->hasBuoyTexture)) {
            return self;
        }
        const PoleAttachOverride* const other = FindOverride(otherPole);
        if (other != nullptr && (other->hasWireTexture || other->hasBuoyTexture)) {
            return other;
        }
        return nullptr;
    }

    // Water-crossing buoy size/count of a connection, same precedence.
    const PoleAttachOverride* FindBuoySource(void* occupant, void* otherPole) {
        const PoleAttachOverride* const self = FindOverride(occupant);
        if (self != nullptr && (self->hasWaterBuoySize || self->hasWaterBuoyCount)) {
            return self;
        }
        const PoleAttachOverride* const other = FindOverride(otherPole);
        if (other != nullptr && (other->hasWaterBuoySize || other->hasWaterBuoyCount)) {
            return other;
        }
        return nullptr;
    }

    // Index of `polyline` (an element of one of the entry's two vector<vector<cS3DVector3>> lists)
    // in that list: the wire index, in the same order AddConnection/the rebuild below create them.
    uint32_t PolylineIndex(const void* entry, const uint32_t listBeginOffset, const void* polyline) {
        const auto* const begin = *reinterpret_cast<const uint8_t* const*>(
            static_cast<const uint8_t*>(entry) + listBeginOffset);
        return static_cast<uint32_t>((static_cast<const uint8_t*>(polyline) - begin) / 12);
    }

    // One-entry cache for the per-segment hooks below: DrawPowerlines calls them once per segment of
    // the same polyline in a row, so the gOverrides lookups run once per wire instead of per segment.
    struct DrawLookupCache {
        uint32_t generation = 0;
        const void* occupant = nullptr;
        const void* entry = nullptr;
        const void* otherPole = nullptr; // entries shift on RemoveConnection: same address, another span
        const void* listBegin = nullptr;
        const void* polyline = nullptr;
        float scale = 1.0f;
    };

    bool CacheHit(const DrawLookupCache& cache, const void* occupant, const void* entry, const void* otherPole,
                  const void* listBegin, const void* polyline) {
        return cache.generation == gOverridesGeneration && cache.occupant == occupant && cache.entry == entry &&
               cache.otherPole == otherPole && cache.listBegin == listBegin && cache.polyline == polyline;
    }

    // Per-wire width for the strand DrawPowerlines is drawing. Returns either the vanilla zoom slot
    // or a pointer to a pre-scaled value (the engine reads it several times before the next call).
    const float* __cdecl ResolveWireWidthPointer(void* occupant, const void* entry, const void* polyline) noexcept {
        const float* const vanillaWidths = *reinterpret_cast<float**>(kPowerLineWidthFactorsPtr);
        const uint32_t zoom = *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const uint8_t*>(occupant) + 0xc4);
        if (gOverrides.empty() || entry == nullptr || polyline == nullptr || zoom >= kPowerLineZoomCount) {
            return vanillaWidths + zoom;
        }
        static DrawLookupCache cache;
        static float scaledWidth = 0.0f;
        const void* const listBegin = *reinterpret_cast<void* const*>(
            static_cast<const uint8_t*>(entry) + kConnection_PolylinesBegin);
        void* const otherPole = ConnectionOtherPole(entry);
        if (!CacheHit(cache, occupant, entry, otherPole, listBegin, polyline)) {
            const PoleAttachOverride* const appearance = FindAppearanceOverride(occupant, otherPole);
            cache = DrawLookupCache{gOverridesGeneration, occupant, entry, otherPole, listBegin, polyline,
                                    (appearance != nullptr && appearance->hasWidthCustomization)
                                        ? appearance->WidthScale(PolylineIndex(entry, kConnection_PolylinesBegin, polyline))
                                        : 1.0f};
        }
        if (cache.scale == 1.0f) {
            return vanillaWidths + zoom;
        }
        scaledWidth = vanillaWidths[zoom] * cache.scale;
        return &scaledWidth;
    }

    // Buoy/ball half-size for the water-crossing polyline being drawn, same scheme.
    const float* __cdecl ResolveWaterBallSizePointer(void* occupant, const void* entry, const void* polyline,
                                                     const uint32_t zoom) noexcept {
        const float* const vanillaWidths = *reinterpret_cast<float**>(kPowerLineWidthFactorsPtr);
        if (gOverrides.empty() || occupant == nullptr || entry == nullptr || polyline == nullptr ||
            zoom >= kPowerLineZoomCount) {
            return vanillaWidths + zoom;
        }
        static DrawLookupCache cache;
        static float scaledSize = 0.0f;
        const void* const listBegin = *reinterpret_cast<void* const*>(
            static_cast<const uint8_t*>(entry) + kConnection_WaterPolylinesBegin);
        void* const otherPole = ConnectionOtherPole(entry);
        if (!CacheHit(cache, occupant, entry, otherPole, listBegin, polyline)) {
            const PoleAttachOverride* const source = FindBuoySource(occupant, otherPole);
            cache = DrawLookupCache{gOverridesGeneration, occupant, entry, otherPole, listBegin, polyline,
                                    (source != nullptr && source->hasWaterBuoySize) ? source->waterBuoySizeScale : 1.0f};
        }
        if (cache.scale == 1.0f) {
            return vanillaWidths + zoom;
        }
        scaledSize = vanillaWidths[zoom] * cache.scale;
        return &scaledSize;
    }

    uint32_t __cdecl GetWireStrandTextureOverride(void* occupant, const void* entry) noexcept {
        if (gOverrides.empty() || entry == nullptr) {
            return kVanillaWireStrandTextureId;
        }
        const PoleAttachOverride* const source = FindTextureSource(occupant, ConnectionOtherPole(entry));
        return (source != nullptr && source->hasWireTexture) ? source->wireTextureId : kVanillaWireStrandTextureId;
    }

    uint32_t __cdecl GetWireBuoyTextureOverride(void* occupant, const void* entry) noexcept {
        if (gOverrides.empty() || entry == nullptr) {
            return kVanillaWireBuoyTextureId;
        }
        const PoleAttachOverride* const source = FindTextureSource(occupant, ConnectionOtherPole(entry));
        return (source != nullptr && source->hasBuoyTexture) ? source->wireBuoyTextureId : kVanillaWireBuoyTextureId;
    }

    // DrawPowerlines (0x0064b750) stack frame at every patched site, confirmed on 1.1.641: after its
    // `sub esp,98h` and four pushes, [esp+10h] holds the current tConnection entry (written at
    // 0x0064b789 and 0x0064bb7f, re-read at 0x0064b9b6/0x0064bb67) and [esp+14h] the complete pole
    // occupant (written at 0x0064b765). The hook's CALL adds 4 to both.
#if defined(_MSC_VER) && defined(_M_IX86)
    // Replaces the 11 bytes at 0x0064ba6e (MOV EAX,[0x00b46798]; FLD [EAX+EDX*4];
    // LEA EAX,[EAX+EDX*4]). On entry EDX = zoom, ECX = the pole occupant (loaded at 0x0064ba5a),
    // EDI = current inner water-polyline vector; on return EAX must point at the size base value
    // with that value pushed on the FPU stack (the code that follows both uses ST0 and re-reads
    // [EAX] several times). ECX is dead here (overwritten at 0x0064ba7f) but preserved anyway
    // alongside EDX.
    __declspec(naked) void WaterBallSizeHook() {
        __asm {
            push ecx
            push edx
            push edx                      // zoom
            push edi                      // polyline
            push dword ptr [esp + 0x24]   // entry: caller [esp+10h] + return + 4 pushes
            push ecx                      // occupant
            call ResolveWaterBallSizePointer
            add esp, 16
            pop edx
            pop ecx
            fld dword ptr [eax]
            ret
        }
    }

    // Both sites replace the 5-byte `MOV ESI, imm32` with `CALL rel32`. At each site EBP holds the
    // current tConnection entry, ECX the bucket-array base, and EDI the in-progress bucket count --
    // ECX/EDI must survive (consumed by the SAR/DIV that follow); EAX/EDX are clobbered by the
    // original code immediately after (MOV EAX,ESI / XOR EDX,EDX), so only the result register ESI
    // matters beyond preservation.
    __declspec(naked) void WireStrandTextureHook() {
        __asm {
            push ecx
            push edx
            push edi
            push ebp                      // entry
            push dword ptr [esp + 0x28]   // occupant: caller [esp+14h] + return + 4 pushes
            call GetWireStrandTextureOverride
            add esp, 8
            mov esi, eax
            pop edi
            pop edx
            pop ecx
            ret
        }
    }

    __declspec(naked) void WireBuoyTextureHook() {
        __asm {
            push ecx
            push edx
            push edi
            push ebp                      // entry
            push dword ptr [esp + 0x28]   // occupant: caller [esp+14h] + return + 4 pushes
            call GetWireBuoyTextureOverride
            add esp, 8
            mov esi, eax
            pop edi
            pop edx
            pop ecx
            ret
        }
    }

    // Called from 0x0064b855. At entry, EDI points to the current outer polyline-vector element,
    // the entry is at caller ESP+0x10 and the complete pole occupant at caller ESP+0x14; CALL pushes
    // a return address, hence +0x14/+0x18 here. Reproduce the overwritten FST at caller ESP+0x78 as
    // callee ESP+0x7c.
    __declspec(naked) void WireWidthLookupHook() {
        __asm {
            mov eax, dword ptr [esp + 0x18]
            mov edx, dword ptr [esp + 0x14]
            push edi
            push edx
            push eax
            call ResolveWireWidthPointer
            add esp, 12
            fst dword ptr [esp + 0x7c]
            ret
        }
    }
#else
#error Power-pole DrawPowerlines hooks require 32-bit MSVC.
#endif

    void ClearConnectionPolylines(void* entry) {
        auto* const begin = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(entry) + kConnection_PolylinesBegin);
        auto* const end = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(entry) + kConnection_PolylinesEnd);
        const auto erase = reinterpret_cast<PolylineListEraseFn>(kPolylineListErase);
        erase(reinterpret_cast<uint8_t*>(entry) + kConnection_PolylinesBegin, begin, end);
    }

    void ClearConnectionWaterPolylines(void* entry) {
        auto* const begin = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(entry) + kConnection_WaterPolylinesBegin);
        auto* const end = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(entry) + kConnection_WaterPolylinesEnd);
        const auto erase = reinterpret_cast<PolylineListEraseFn>(kPolylineListErase);
        erase(reinterpret_cast<uint8_t*>(entry) + kConnection_WaterPolylinesBegin, begin, end);
    }

    // Uninstall guard shared by the patch classes below: the bytes are restored only if the site
    // still holds exactly what Install wrote. If another DLL patched it afterwards (and may chain
    // to our hook), restoring would silently remove its hook, so the site is left as it is.
    bool SiteStillPatched(const uintptr_t site, const uint8_t* written, const size_t size, const char* name) {
        if (std::memcmp(reinterpret_cast<const void*>(site), written, size) == 0) {
            return true;
        }
        LOG_WARN("PowerPoleCustomization: {} at 0x{:08X} was changed by someone else since it was patched; "
                 "left as it is.", name, static_cast<uint32_t>(site));
        return false;
    }

    class InlineCallPatch final {
    public:
        bool Install() {
            if (installed_) {
                return true;
            }
            auto* const site = reinterpret_cast<uint8_t*>(kWireWidthLookupPatchSite);
            if (std::memcmp(site, kWireWidthLookupExpectedBytes.data(), kWireWidthLookupPatchSize) != 0) {
                LOG_ERROR("PowerPoleCustomization: wire-width patch bytes at 0x{:08X} do not match 1.1.641; not patching.",
                          static_cast<uint32_t>(kWireWidthLookupPatchSite));
                return false;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(site, kWireWidthLookupPatchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                LOG_ERROR("PowerPoleCustomization: VirtualProtect failed for wire-width patch (error {}).", GetLastError());
                return false;
            }
            std::memcpy(original_.data(), site, original_.size());
            const auto relative = static_cast<int32_t>(
                reinterpret_cast<intptr_t>(&WireWidthLookupHook) -
                (static_cast<intptr_t>(kWireWidthLookupPatchSite) + 5));
            site[0] = 0xe8;
            std::memcpy(site + 1, &relative, sizeof(relative));
            std::fill(site + 5, site + kWireWidthLookupPatchSize, static_cast<uint8_t>(0x90));
            std::memcpy(written_.data(), site, written_.size());
            FlushInstructionCache(GetCurrentProcess(), site, kWireWidthLookupPatchSize);
            DWORD ignored = 0;
            VirtualProtect(site, kWireWidthLookupPatchSize, oldProtect, &ignored);
            installed_ = true;
            return true;
        }

        void Uninstall() {
            if (!installed_) {
                return;
            }
            installed_ = false;
            if (!SiteStillPatched(kWireWidthLookupPatchSite, written_.data(), written_.size(), "wire-width patch")) {
                return;
            }
            auto* const site = reinterpret_cast<uint8_t*>(kWireWidthLookupPatchSite);
            DWORD oldProtect = 0;
            if (VirtualProtect(site, kWireWidthLookupPatchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                std::memcpy(site, original_.data(), original_.size());
                FlushInstructionCache(GetCurrentProcess(), site, kWireWidthLookupPatchSize);
                DWORD ignored = 0;
                VirtualProtect(site, kWireWidthLookupPatchSize, oldProtect, &ignored);
            }
            installed_ = false;
        }

        [[nodiscard]] bool IsInstalled() const noexcept { return installed_; }

    private:
        std::array<uint8_t, kWireWidthLookupPatchSize> original_{};
        std::array<uint8_t, kWireWidthLookupPatchSize> written_{};
        bool installed_ = false;
    };

    // Direction between two poles, via the confirmed Get0To3Direction. Windows AddConnection reads
    // cached cell coordinates at complete-object +0xc8/+0xcc, but recomputing them from the confirmed
    // position triple mirrors SetPosition's calculation and avoids depending on cache freshness.
    uint32_t CellCoord(void* occupant, const uint32_t posOffset) {
        const float pos = *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(occupant) + posOffset);
        return static_cast<uint32_t>(pos / kCellSize);
    }

    uint32_t DirectionBetween(void* poleA, void* poleB) {
        const auto get0To3Direction = reinterpret_cast<Get0To3DirectionFn>(kGet0To3Direction);
        return get0To3Direction(
            CellCoord(poleA, kOccupant_PosX), CellCoord(poleA, kOccupant_PosZ),
            CellCoord(poleB, kOccupant_PosX), CellCoord(poleB, kOccupant_PosZ));
    }

    // Defined in the foundation-pad section below; used by the connection rebuild to keep a
    // follow-span pad aligned the moment its bearing changes.
    bool PoleFoundationYaw(void* occupant, float& outYaw);
    void RebuildFoundation(void* occupant, float halfExtent, float yaw);

    // Rebuilds the complete regular-polyline list whenever either endpoint customizes attachment
    // geometry/count or sag. Width-only customization keeps vanilla geometry; the width is applied
    // at draw time. The active/render-owning side determines appearance when two pole
    // families disagree; if it has no appearance properties, the other endpoint supplies them.
    void ApplyConnectionCustomization(void* occupant, void* otherPole) {
        const uint32_t direction = DirectionBetween(occupant, otherPole);
        const uint32_t count = std::min(StrandCount(occupant, direction), StrandCount(otherPole, direction));
        void* const entry = FindConnectionEntry(occupant, otherPole);
        if (entry == nullptr) {
            return; // reciprocal non-rendering side, or no live connection for this pair
        }

        const PoleAttachOverride* const appearance = FindAppearanceOverride(occupant, otherPole);
        // Off-nominal bearing (e.g. a FAR span) forces a rebuild so each pole's baked attach offsets
        // can be yaw-rotated onto the true span direction. A single shared delta is wrong when the
        // two endpoints were authored in different bases (a FAR-authored pole meeting a fallback or
        // vanilla one), so compute one deviation per endpoint. Both are 0 for vanilla-aligned spans,
        // so this reduces exactly to the previous single-delta behavior there.
        float spanBearing = 0.0f;
        const bool haveBearing = ComputeSpanBearing(occupant, otherPole, spanBearing);
        const float yawA = haveBearing ? WrapYaw(spanBearing - AttachBasisAngle(occupant, direction)) : 0.0f;
        const float yawB = haveBearing ? WrapYaw(spanBearing - AttachBasisAngle(otherPole, direction)) : 0.0f;
        // Vanilla built the water-crossing coarse list only when the span crosses water; reuse
        // that decision (list non-empty) instead of re-querying the terrain service, then keep it
        // aligned with the rebuilt strands -- count, attach points, yaw, and sag included. The
        // buoy source follows the same render-owner-first precedence as the appearance override.
        const bool hadWaterPolylines =
            *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(entry) + kConnection_WaterPolylinesBegin) !=
            *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(entry) + kConnection_WaterPolylinesEnd);
        const PoleAttachOverride* const buoySource = hadWaterPolylines ? FindBuoySource(occupant, otherPole) : nullptr;
        const int32_t waterPointCount = (buoySource != nullptr && buoySource->hasWaterBuoyCount)
            ? static_cast<int32_t>(buoySource->waterBuoyPointCount)
            : kWaterPolylinePointCount;
        // Any water-crossing span is rebuilt unconditionally: UpdateConnection never clears the
        // water list, so without the rebuild every update of the span would add more buoy copies
        // (a vanilla leak). On an otherwise-vanilla span the rebuild is geometry-identical
        // (vanilla table, zero yaw, default sag, the same t = k/count buoy layout as vanilla).
        const bool rebuildGeometry = HasAttachPointCustomization(occupant, direction) ||
                                     HasAttachPointCustomization(otherPole, direction) ||
                                     (appearance != nullptr && appearance->hasSagCustomization) ||
                                     std::fabs(yawA) > kYawEpsilonRadians ||
                                     std::fabs(yawB) > kYawEpsilonRadians ||
                                     hadWaterPolylines;
        if (rebuildGeometry) {
            ClearConnectionPolylines(entry);
            if (hadWaterPolylines) {
                ClearConnectionWaterPolylines(entry);
            }
            for (uint32_t i = 0; i < count; ++i) {
                const AttachPoint a = GetAttachPoint(occupant, direction, i, yawA);
                const AttachPoint b = GetAttachPoint(otherPole, direction, i, yawB);
                const float sagScale = appearance ? appearance->SagScale(i) : 1.0f;
                const float maximumSag = appearance ? appearance->MaximumSag(i) :
                                                      std::numeric_limits<float>::infinity();
                AppendPolylineToConnection(occupant, entry, a, b, sagScale, maximumSag);
                if (hadWaterPolylines) {
                    AppendWaterPolylineToConnection(occupant, entry, a, b, sagScale, maximumSag, waterPointCount);
                }
            }
            LOG_DEBUG("PowerPoleCustomization: rebuilt {} strands in direction {} for connection {} -> {}{}.",
                     count, direction, occupant, otherPole,
                     hadWaterPolylines ? " (incl. water-crossing coarse list)" : "");
        }
        // Per-wire width, textures and buoy size need no registration: the DrawPowerlines hooks
        // resolve them from gOverrides while drawing (see ResolveWireWidthPointer).

        // A new/changed connection can change the follow-span bearing (first connection appearing,
        // or a second one confirming/breaking agreement) -- rebuild the pad on the spot instead of
        // waiting for the next zoom change to re-run CreateFloor.
        const auto selfOverride = gOverrides.find(occupant);
        if (selfOverride != gOverrides.end() &&
            (selfOverride->second.foundationFollowSpan || selfOverride->second.hasFoundationHalfExtent)) {
            const float halfExtent = selfOverride->second.hasFoundationHalfExtent
                                         ? selfOverride->second.foundationHalfExtent
                                         : kVanillaFoundationHalfExtent;
            float yaw = 0.0f;
            if (selfOverride->second.foundationFollowSpan) {
                PoleFoundationYaw(occupant, yaw);
            }
            RebuildFoundation(occupant, halfExtent, yaw);
        }
    }

    // ------------------------------------------------------------------
    // Foundation pad size. CreateFloor always builds the vanilla 5.0 half-extent quad; we then
    // rescale its 4 corners in place around the pole's XZ position (leaving Y, color, and UV
    // untouched). CreateWalls runs unmodified afterward and reads this same buffer (FUN_0064c2c0
    // indexes occupant+0xe8 directly), so a 0 half-extent collapses floor AND walls to a degenerate
    // point with no separate texture-ID risk -- the safe way to get "no visible foundation".
    // ------------------------------------------------------------------
    // In-plane pad yaw for a follow-span pole: the shared undirected bearing of its live
    // connections, reduced to the minimal deviation from the nearest 90-degree multiple so the
    // walls stay on their vanilla sides. Returns false (pad stays axis-aligned) when the pole has
    // no connections or its connections disagree on a bearing (junctions/turns).
    bool PoleFoundationYaw(void* occupant, float& outYaw) {
        auto* const begin = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(occupant) + kOccupant_ConnectionsBegin);
        auto* const end = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(occupant) + kOccupant_ConnectionsEnd);
        bool haveBearing = false;
        float bearing = 0.0f;
        for (uint8_t* entry = begin; entry != end; entry += kConnection_Size) {
            void* const otherPole = *reinterpret_cast<void**>(entry + kConnection_OtherPole);
            float entryBearing = 0.0f;
            if (otherPole == nullptr || !ComputeSpanBearing(occupant, otherPole, entryBearing)) {
                continue;
            }
            if (!haveBearing) {
                bearing = entryBearing;
                haveBearing = true;
            } else if (std::fabs(WrapYaw(entryBearing - bearing)) > kYawEpsilonRadians) {
                return false; // junction: connections disagree, keep the vanilla axis-aligned pad
            }
        }
        if (!haveBearing) {
            return false;
        }
        // Reduce to (-45, 45] degrees around the nearest axis so wall sides are preserved.
        float yaw = std::fmod(bearing, kPi / 2.0f);
        if (yaw > kPi / 4.0f) {
            yaw -= kPi / 2.0f;
        }
        if (std::fabs(yaw) <= kYawEpsilonRadians) {
            return false; // axis-aligned span; nothing to rotate
        }
        outYaw = yaw;
        return true;
    }

    // Scales and/or yaw-rotates the freshly-built floor quad in place around the pole's XZ
    // position. Must run before CreateWalls, which derives the wall quads from this buffer.
    void TransformFoundationFloor(void* occupant, const float halfExtent, const float yaw) {
        const float scale = halfExtent / kVanillaFoundationHalfExtent;
        if (scale == 1.0f && yaw == 0.0f) {
            return;
        }
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        const auto* const position = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(occupant) + kOccupant_PosX);
        const float posX = position[0];
        const float posZ = position[2]; // kOccupant_PosZ - kOccupant_PosX spans posY in between

        auto* const begin = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(occupant) + kOccupant_FloorVertsBegin);
        auto* const end = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(occupant) + kOccupant_FloorVertsEnd);
        for (uint8_t* vertex = begin; vertex + kFloorVertexStride <= end; vertex += kFloorVertexStride) {
            auto* const xyz = reinterpret_cast<float*>(vertex);
            const float dx = (xyz[0] - posX) * scale;
            const float dz = (xyz[2] - posZ) * scale;
            xyz[0] = posX + dx * c - dz * s;
            xyz[2] = posZ + dx * s + dz * c;
        }
    }

    // Full foundation rebuild outside the engine's own CreateFloor call sites: vanilla floor, our
    // transform, vanilla walls -- the exact sequence SetPosition runs (CreateFloor 0x0064c611 then
    // CreateWalls 0x0064c616). Used when a connection change alters the follow-span bearing after
    // the pad was already built.
    void RebuildFoundation(void* occupant, const float halfExtent, const float yaw) {
        using CreateWallsFn = void(__fastcall*)(void* occupant, void* edxUnused);
        reinterpret_cast<CreateWallsFn>(kCreateFloor)(occupant, nullptr);
        TransformFoundationFloor(occupant, halfExtent, yaw);
        reinterpret_cast<CreateWallsFn>(kCreateWalls)(occupant, nullptr);
    }

    // ------------------------------------------------------------------
    // Foundation texture IDs. Draw() looks up the floor/wall texture via a hash-bucket chain walk
    // keyed by a hardcoded 32-bit constant read twice per texture (see the 4 patch-site constants
    // above); returning a different value per call desyncs the bucket index from the comparison, so
    // both hook instances for a given texture must return the same result -- resolved once here per
    // occupant lookup, called from 2 independent patch sites each.
    // ------------------------------------------------------------------
    // cSC4PowerPoleOccupant::Draw (0x0064c800) is entered through the interface subobject at
    // completeObject+0x120: it keeps that `this` in EBX and computes the complete object as
    // `lea esi,[ebx-0x120]` (0x0064c810). gOverrides is keyed by the complete object.
    constexpr uint32_t kDrawSubobjectOffset = 0x120;

    uint32_t __cdecl GetFloorTextureHashOverride(uint8_t* drawThis) noexcept {
        void* const occupant = drawThis - kDrawSubobjectOffset;
        const auto it = gOverrides.find(occupant);
        return (it != gOverrides.end() && it->second.hasFloorTexture) ? it->second.foundationFloorTextureId
                                                                       : kVanillaFloorTextureId;
    }

    uint32_t __cdecl GetWallTextureHashOverride(uint8_t* drawThis) noexcept {
        void* const occupant = drawThis - kDrawSubobjectOffset;
        const auto it = gOverrides.find(occupant);
        return (it != gOverrides.end() && it->second.hasWallTexture) ? it->second.foundationWallTextureId
                                                                      : kVanillaWallTextureId;
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    // Both patch sites replace `MOV EAX, imm32` (5 bytes) with `CALL rel32` (5 bytes) -- a 1:1 size
    // swap, so no surrounding instructions move. EBX holds Draw's own `this` -- the occupant's
    // interface subobject at +0x120, not the complete object (mov ebx,ecx at 0x0064c805) -- at every
    // one of the 4 sites; the callee converts it. ECX/EDX must come back
    // unchanged -- EDX in particular is 0 going into the immediately-following DIV and must stay that
    // way, which a plain push/pop pair guarantees regardless of what our __cdecl callee does to it.
    __declspec(naked) void FloorTextureHashHook() {
        __asm {
            push ecx
            push edx
            push ebx
            call GetFloorTextureHashOverride
            add esp, 4
            pop edx
            pop ecx
            ret
        }
    }

    __declspec(naked) void WallTextureHashHook() {
        __asm {
            push ecx
            push edx
            push ebx
            call GetWallTextureHashOverride
            add esp, 4
            pop edx
            pop ecx
            ret
        }
    }
#else
#error Power-pole foundation-texture hook requires 32-bit MSVC.
#endif

    // ------------------------------------------------------------------
    // Pole-style lookup. Returns the pole instance for a direction mask under the active style, or
    // the vanilla instance when no style is active / the mask isn't defined in that style's ini
    // section. EDI at the call site already holds the direction mask (0-15); pushed as the sole
    // __cdecl argument.
    // ------------------------------------------------------------------
    // Regular (non-FAR) resolution: the active style's entry for this mask, else vanilla.
    uint32_t ResolveRegularInstance(const uint32_t mask, const uint32_t* const vanillaTable) noexcept {
        if (gActiveStyleIndex == 0 || gActiveStyleIndex > gStyles.size()) {
            return vanillaTable[mask];
        }
        const PoleStyle& style = gStyles[gActiveStyleIndex - 1];
        return style.hasMask[mask] ? style.instanceByDirectionMask[mask] : vanillaTable[mask];
    }

    // Debug logging for FAR model selection. Logs only when the (heading, mask, resolved instance)
    // tuple changes, so a whole drag of identical mid-line poles logs once, and endpoints/junctions
    // that resolve differently log again. noexcept: the whole resolver path is noexcept, so the
    // formatting call is wrapped -- a logging failure must never propagate into the engine's placement
    // loop. Remove this and its call sites once the orient->model mapping is confirmed in-game.
    void LogFarModelSelection(const uint32_t headingIndex, const uint32_t mask, const uint32_t resolved,
                              const char* const source) noexcept {
        static uint32_t lastHeading = 0xFFFFFFFFu;
        static uint32_t lastMask = 0xFFFFFFFFu;
        static uint32_t lastResolved = 0xFFFFFFFFu;
        if (headingIndex == lastHeading && mask == lastMask && resolved == lastResolved) {
            return;
        }
        lastHeading = headingIndex;
        lastMask = mask;
        lastResolved = resolved;
        try {
            const uint32_t ratioIndex = std::min(headingIndex / kFarOrientCount,
                                                 static_cast<uint32_t>(kFarRatios.size() - 1));
            const uint32_t orient = headingIndex % kFarOrientCount;
            const FarRatio& r = kFarRatios[ratioIndex];
            const double slopeDeg = std::atan2(static_cast<double>(r.rise), static_cast<double>(r.run)) *
                                    180.0 / 3.14159265358979323846;
            LOG_DEBUG("PowerPoleCustomization: [FAR] model select FAR-{}.{} (slope {:.1f} deg, mask 0x{:X}{}) "
                     "-> pole instance 0x{:08X} [{}].",
                     kFarRatioLabels[ratioIndex], FarOrientName(orient), slopeDeg, mask,
                     mask == 0x8 ? " => engine +90 deg quarter-turn" : " => no engine turn",
                     resolved, source);
        } catch (...) {
        }
    }

    // FAR resolution order (docs/far-power-lines-design.md): the exact FAR key in the active style,
    // then in the [PowerPoles.FAR] default section, then the active style's regular entry for the
    // mask the span already classifies to, then the vanilla instance. Sparse tables therefore
    // degrade gracefully without unexpectedly changing pole family before the final fallback.
    uint32_t ResolveFarInstance(const uint32_t headingIndex, const uint32_t mask,
                                const uint32_t* const vanillaTable) noexcept {
        if (headingIndex < kFarHeadingCount) {
            if (gActiveStyleIndex >= 1 && gActiveStyleIndex <= gStyles.size()) {
                const PoleStyle& style = gStyles[gActiveStyleIndex - 1];
                if (style.hasFarHeading[headingIndex]) {
                    const uint32_t resolved = style.farInstanceByHeading[headingIndex];
                    LogFarModelSelection(headingIndex, mask, resolved, "active style");
                    return resolved;
                }
            }
            if (gHasFarDefault && gFarDefaultStyle.hasFarHeading[headingIndex]) {
                const uint32_t resolved = gFarDefaultStyle.farInstanceByHeading[headingIndex];
                LogFarModelSelection(headingIndex, mask, resolved, "[PowerPoles.FAR] default");
                return resolved;
            }
        }
        const uint32_t resolved = ResolveRegularInstance(mask, vanillaTable);
        LogFarModelSelection(headingIndex, mask, resolved, "REG/vanilla fallback (no FAR key)");
        return resolved;
    }

    uint32_t __cdecl ResolvePoleInstanceForDirectionMask(const uint32_t directionMask) noexcept {
        const auto* const vanillaTable = reinterpret_cast<const uint32_t*>(kPowerPoleForDirectionsFlag);
        const uint32_t mask = directionMask & 0xF;
        // A clean lone-diagonal pole placed during an active FAR drag routes to the FAR model for
        // the drag's exact (ratio, orientation). Junctions/merges (multi-bit masks) and every
        // regular drag stay on the regular path -- FAR buckets apply only to clean two-connection
        // mid-line poles (docs/far-power-lines-design.md). Any FAR span classifies as diagonal
        // (Get0To3Direction returns 1 or 3), so its lone-direction mask is exactly 0x2 or 0x8.
        if (gFarDragActive && (mask == 0x2 || mask == 0x8)) {
            const uint32_t headingIndex = gFarDragRatioIndex * kFarOrientCount + gFarDragOrient;
            return ResolveFarInstance(headingIndex, mask, vanillaTable);
        }
        return ResolveRegularInstance(mask, vanillaTable);
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    // EDI must survive untouched (read again by the caller right after both call sites); EAX is the
    // only register the original 7-byte MOV wrote, matching what CALL leaves behind here too.
    __declspec(naked) void PoleStyleLookupHook() {
        __asm {
            push edi
            call ResolvePoleInstanceForDirectionMask
            add esp, 4
            ret
        }
    }
#else
#error Power-pole style-switching hook requires 32-bit MSVC.
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
    // Entered by a CALL replacing the first 7 bytes of UpdateOnZoneChange. The CALL adds one
    // temporary return address below the function's own return address/arguments. Disabled:
    // discard it, reproduce the two overwritten instructions, and resume at 0x006522f7. Enabled:
    // discard it and return directly to the caller, cleaning the original five stack arguments.
    __declspec(naked) void KeepWiresOnZoneChangeHook() {
        __asm {
            cmp byte ptr [gKeepWiresOnZoneChange], 0
            je run_original
            add esp, 4
            ret 14h
        run_original:
            add esp, 4
            sub esp, 68h
            mov eax, [esp + 7ch]
            mov edx, kUpdateOnZoneChangeResume
            jmp edx
        }
    }
#else
#error Keep-wires zone-change hook requires 32-bit MSVC.
#endif

    // Generic N-byte-prefix-match -> `CALL rel32` (or `JMP rel32`) patch (5 bytes) + NOP padding for
    // the remainder. Used for the 7-byte pole-style lookup sites and the zoned branch of
    // CellIsOKForPlacement; kept separate from Imm32CallPatch (fixed 5-byte `MOV EAX,imm32` sites)
    // since the expected byte pattern and size differ here.
    constexpr uint8_t kCallRel32 = 0xe8;
    constexpr uint8_t kJmpRel32 = 0xe9;

    class ByteSpanCallPatch final {
    public:
        bool Install(const uintptr_t site, std::initializer_list<uint8_t> expectedBytes, void* hookFn,
                     const char* name, const uint8_t opcode = kCallRel32) {
            site_ = site;
            size_ = static_cast<uint32_t>(expectedBytes.size());
            auto* const p = reinterpret_cast<uint8_t*>(site);
            if (!std::equal(expectedBytes.begin(), expectedBytes.end(), p)) {
                LOG_ERROR("PowerPoleCustomization: {} bytes at 0x{:08X} do not match 1.1.641; not patching.",
                          name, static_cast<uint32_t>(site));
                return false;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(p, size_, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                LOG_ERROR("PowerPoleCustomization: VirtualProtect failed for {} (error {}).", name, GetLastError());
                return false;
            }
            original_.assign(p, p + size_);
            const auto relative = static_cast<int32_t>(
                reinterpret_cast<intptr_t>(hookFn) - (static_cast<intptr_t>(site) + 5));
            p[0] = opcode;
            std::memcpy(p + 1, &relative, sizeof(relative));
            std::fill(p + 5, p + size_, static_cast<uint8_t>(0x90));
            written_.assign(p, p + size_);
            name_ = name;
            FlushInstructionCache(GetCurrentProcess(), p, size_);
            DWORD ignored = 0;
            VirtualProtect(p, size_, oldProtect, &ignored);
            installed_ = true;
            return true;
        }

        [[nodiscard]] bool IsInstalled() const noexcept { return installed_; } // SC4SIM

        void Uninstall() {
            if (!installed_) {
                return;
            }
            installed_ = false;
            if (!SiteStillPatched(site_, written_.data(), written_.size(), name_)) {
                return;
            }
            auto* const p = reinterpret_cast<uint8_t*>(site_);
            DWORD oldProtect = 0;
            if (VirtualProtect(p, size_, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                std::memcpy(p, original_.data(), original_.size());
                FlushInstructionCache(GetCurrentProcess(), p, size_);
                DWORD ignored = 0;
                VirtualProtect(p, size_, oldProtect, &ignored);
            }
            installed_ = false;
        }

    private:
        uintptr_t site_ = 0;
        uint32_t size_ = 0;
        std::vector<uint8_t> original_;
        std::vector<uint8_t> written_;
        const char* name_ = "";
        bool installed_ = false;
    };

    // Replaces one function pointer inside a vtable (a 4-byte data write, not a code patch).
    // Used for the power tool's DrawNetworkLine slot: the method is only ever dispatched
    // virtually, so swapping cSC4PowerLineTool's own slot hooks that tool exclusively -- other
    // network tools dispatch through their own untouched vtables.
    class VTableSlotPatch final {
    public:
        bool Install(const uintptr_t slotAddress, const uintptr_t expectedTarget, void* hookFn, const char* name) {
            slot_ = slotAddress;
            auto* const slot = reinterpret_cast<uintptr_t*>(slotAddress);
            if (*slot != expectedTarget) {
                LOG_ERROR("PowerPoleCustomization: {} vtable slot 0x{:08X} holds 0x{:08X}, expected 0x{:08X}; not patching.",
                          name, static_cast<uint32_t>(slotAddress), static_cast<uint32_t>(*slot),
                          static_cast<uint32_t>(expectedTarget));
                return false;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
                LOG_ERROR("PowerPoleCustomization: VirtualProtect failed for {} (error {}).", name, GetLastError());
                return false;
            }
            original_ = *slot;
            *slot = reinterpret_cast<uintptr_t>(hookFn);
            written_ = *slot;
            name_ = name;
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &ignored);
            installed_ = true;
            return true;
        }

        void Uninstall() {
            if (!installed_) {
                return;
            }
            installed_ = false;
            if (!SiteStillPatched(slot_, reinterpret_cast<const uint8_t*>(&written_), sizeof(written_), name_)) {
                return;
            }
            auto* const slot = reinterpret_cast<uintptr_t*>(slot_);
            DWORD oldProtect = 0;
            if (VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
                *slot = original_;
                DWORD ignored = 0;
                VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &ignored);
            }
            installed_ = false;
        }

    private:
        uintptr_t slot_ = 0;
        uintptr_t original_ = 0;
        uintptr_t written_ = 0;
        const char* name_ = "";
        bool installed_ = false;
    };

    // Generic 5-byte `MOV r32, imm32` -> `CALL rel32` patch, one instance per hash-lookup site.
    // Kept separate from InlineCallPatch (used for the 23-byte wire-width site) since the byte
    // count and expected-opcode check differ. The opcode selects the destination register the
    // vanilla instruction wrote (0xB8 = EAX for the Draw floor/wall sites, 0xBE = ESI for the
    // DrawPowerlines wire/buoy sites); the hook is responsible for producing its result in that
    // same register.
    class Imm32CallPatch final {
    public:
        bool Install(const uintptr_t site, const uint32_t expectedImm, void* hookFn, const char* name,
                     const uint8_t expectedOpcode = kMovEaxOpcode) {
            site_ = site;
            auto* const p = reinterpret_cast<uint8_t*>(site);
            const std::array<uint8_t, 5> expected = {
                expectedOpcode,
                static_cast<uint8_t>(expectedImm), static_cast<uint8_t>(expectedImm >> 8),
                static_cast<uint8_t>(expectedImm >> 16), static_cast<uint8_t>(expectedImm >> 24),
            };
            if (std::memcmp(p, expected.data(), expected.size()) != 0) {
                LOG_ERROR("PowerPoleCustomization: {} bytes at 0x{:08X} do not match 1.1.641; not patching.",
                          name, static_cast<uint32_t>(site));
                return false;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                LOG_ERROR("PowerPoleCustomization: VirtualProtect failed for {} (error {}).", name, GetLastError());
                return false;
            }
            std::memcpy(original_.data(), p, original_.size());
            const auto relative = static_cast<int32_t>(
                reinterpret_cast<intptr_t>(hookFn) - (static_cast<intptr_t>(site) + 5));
            p[0] = 0xe8;
            std::memcpy(p + 1, &relative, sizeof(relative));
            std::memcpy(written_.data(), p, written_.size());
            name_ = name;
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            DWORD ignored = 0;
            VirtualProtect(p, 5, oldProtect, &ignored);
            installed_ = true;
            return true;
        }

        void Uninstall() {
            if (!installed_) {
                return;
            }
            installed_ = false;
            if (!SiteStillPatched(site_, written_.data(), written_.size(), name_)) {
                return;
            }
            auto* const p = reinterpret_cast<uint8_t*>(site_);
            DWORD oldProtect = 0;
            if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                std::memcpy(p, original_.data(), original_.size());
                FlushInstructionCache(GetCurrentProcess(), p, 5);
                DWORD ignored = 0;
                VirtualProtect(p, 5, oldProtect, &ignored);
            }
            installed_ = false;
        }

        [[nodiscard]] bool IsInstalled() const noexcept { return installed_; }

    private:
        uintptr_t site_ = 0;
        std::array<uint8_t, 5> original_{};
        std::array<uint8_t, 5> written_{};
        const char* name_ = "";
        bool installed_ = false;
    };

    // ------------------------------------------------------------------
    // Hook bodies.
    // ------------------------------------------------------------------
    using InitConnectionPointsFn = void(__thiscall*)(void* occupant);
    using AddConnectionFn = void(__thiscall*)(void* occupant, void* otherPole, void* lineInfoVector);
    using UpdateConnectionFn = void(__thiscall*)(void* occupant, void* otherPole);
    using CreateFloorFn = void(__fastcall*)(void* occupant, void* /*unused edx*/);

    // Vanilla InitConnectionPoints (0x00649B30) selects one of its two fixed attachment tables from
    // bits 8-11 of the pole exemplar's own resource key (its instance, read through GetKey, slot
    // +0x2C): 1 or 2 -> the cardinal table 0x00B0C0D8, 3 or 4 -> the diagonal table 0x00B0C198, any
    // other value -> nothing, and the field stays null from the constructor. GetLineConnectionPoints
    // (0x00649BA0) and Write read it without a check, so a custom pole outside 1-4 (the FAR poles use
    // 0xA) would crash; seed a safe table for those poles. Custom attachment geometry still replaces
    // the resulting vanilla strands in ApplyConnectionCustomization.
    // The seeded table is a private copy of the matching static table (gPrivateConnectionTables),
    // never the static table itself: Read writes the four saved points of direction 0 through this
    // pointer, and a pole whose direction flags changed after it was saved would otherwise write
    // cardinal points into the shared diagonal table (or the reverse) of every vanilla pole.
    uint32_t gRepairedConnectionTables = 0;

    void RepairMissingConnectionPointTable(void* occupant) {
        auto*& table = *reinterpret_cast<uint8_t**>(
            reinterpret_cast<uint8_t*>(occupant) + kOccupant_ConnectionPointTablePtr);
        if (table != nullptr) {
            return;
        }

        const uint32_t directionFlags = *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const uint8_t*>(occupant) + kOccupant_DirectionFlags);
        const bool diagonalOnly = (directionFlags & kDiagonalDirectionFlags) != 0 &&
                                  (directionFlags & kCardinalDirectionFlags) == 0;
        auto& privateTable = gPrivateConnectionTables[occupant];
        if (!privateTable) {
            privateTable = std::make_unique<std::array<float, kConnectionPointTableFloats>>();
        }
        std::memcpy(privateTable->data(),
                    reinterpret_cast<const void*>(diagonalOnly ? kDiagonalConnectionPoints : kCardinalConnectionPoints),
                    sizeof(*privateTable));
        table = reinterpret_cast<uint8_t*>(privateTable->data());
        // Expected for every FAR / custom pole: log the first one, the rest only at debug level.
        if (gRepairedConnectionTables++ == 0) {
            LOG_INFO("PowerPoleCustomization: pole {} has no vanilla connection-point table (exemplar instance "
                     "bits 8-11 outside 1-4, direction flags 0x{:X}); using the {} table. Further poles like it "
                     "are logged at debug level only.",
                     occupant, directionFlags, diagonalOnly ? "diagonal" : "cardinal");
        } else {
            LOG_DEBUG("PowerPoleCustomization: pole {} given the {} connection-point table.",
                      occupant, diagonalOnly ? "diagonal" : "cardinal");
        }
    }

    void __fastcall CreateFloorHook(void* occupant, void* edxUnused) {
        const auto original = reinterpret_cast<CreateFloorFn>(kCreateFloor);
        original(occupant, edxUnused);
        if (!gSettings.enabled) {
            return;
        }
        const auto it = gOverrides.find(occupant);
        if (it == gOverrides.end()) {
            return;
        }
        const float halfExtent = it->second.hasFoundationHalfExtent ? it->second.foundationHalfExtent
                                                                     : kVanillaFoundationHalfExtent;
        float yaw = 0.0f;
        if (it->second.foundationFollowSpan) {
            PoleFoundationYaw(occupant, yaw);
        }
        TransformFoundationFloor(occupant, halfExtent, yaw);
    }

    using DestructorFn = void(__thiscall*)(void* occupant);

    // Runs when the engine destroys a pole (demolition, zone rebuild, city teardown). Dropping the
    // pole's override and private connection-point table here guarantees no stale customization
    // can attach to a future pole that recycles the same 292-byte pool slot, and keeps both maps
    // from growing across a long session.
    void __fastcall DestructorHook(void* occupant, void* /*edx*/) {
        gOverrides.erase(occupant);
        ++gOverridesGeneration;
        const auto original = reinterpret_cast<DestructorFn>(kDestructor);
        original(occupant);
        gPrivateConnectionTables.erase(occupant); // after the engine is done with the pole
    }

    void __fastcall InitConnectionPointsHook(void* occupant, void* /*edx*/) {
        const auto original = reinterpret_cast<InitConnectionPointsFn>(kInitConnectionPoints);
        original(occupant); // always run vanilla first: it still owns model load + default-table selection
        RepairMissingConnectionPointTable(occupant);
        ++gOverridesGeneration;

        // Overrides are consumed by hooks that are only safe while the destructor hook drops a
        // pole's entry when the pole dies (the pool recycles addresses); without it, none are made.
        if (!gSettings.enabled || !gOverridesAllowed) {
            gOverrides.erase(occupant);
            return;
        }

        // cSC4PowerPoleOccupant::QueryInterface delegates through cSC4Occupant to
        // cSCExemplarPropertyHolder. Windows 1.1.641 confirms IID 0x0AC2B5F7 returns the adjusted
        // public cISCExemplarPropertyHolder interface whose +0x10 slot is GetDefaultExemplar().
        cRZAutoRefCount<cISCExemplarPropertyHolder> propertyHolder;
        cISCResExemplar* exemplar = nullptr;
        auto* const unknown = reinterpret_cast<cIGZUnknown*>(occupant);
        if (unknown->QueryInterface(GZIID_cISCExemplarPropertyHolder, propertyHolder.AsPPVoid())) {
            exemplar = propertyHolder->GetDefaultExemplar(); // borrowed; holder keeps it alive here
        } else {
            LOG_WARN("PowerPoleCustomization: pole {} did not expose cISCExemplarPropertyHolder.", occupant);
        }

        PoleAttachOverride candidate;
        if (TryBuildOverride(exemplar, candidate)) {
            gOverrides[occupant] = std::move(candidate);
        } else {
            gOverrides.erase(occupant);
        }
    }

    void __fastcall AddConnectionHook(void* occupant, void* /*edx*/, void* otherPole, void* lineInfoVector) {
        const auto original = reinterpret_cast<AddConnectionFn>(kAddConnection);
        original(occupant, otherPole, lineInfoVector);

        if (!gSettings.enabled) {
            return;
        }

        ApplyConnectionCustomization(occupant, otherPole);
    }

    void __fastcall UpdateConnectionHook(void* occupant, void* /*edx*/, void* otherPole) {
        const auto original = reinterpret_cast<UpdateConnectionFn>(kUpdateConnection);
        original(occupant, otherPole);
        if (gSettings.enabled) {
            ApplyConnectionCustomization(occupant, otherPole);
        }
    }

    // cSC4PowerPoleOccupant::RemoveConnection (0x0064DA00, __thiscall, args otherPole and a flag,
    // RET 8) erases one tConnection and shifts the later ones down. Six direct call sites
    // (0x0064DB09 is the reciprocal removal of the pole teardown at 0x0064DAE0; 0x006518DF/0x00651A27
    // in PlacePoles; 0x0065279F/0x006527A9 in UpdateOnZoneChange; 0x006EE04A). Nothing is cached per
    // connection any more, so the hook only keeps a follow-span foundation pad aligned with the
    // connections the pole still has (a junction losing a branch becomes a straight span), which
    // vanilla would only redo at the next zoom change. Skipped while the city is shutting down.
    constexpr uintptr_t kRemoveConnection = 0x0064da00;
    constexpr std::array<uintptr_t, 6> kRemoveConnectionCallSites = {
        0x0064db09, 0x006518df, 0x00651a27, 0x0065279f, 0x006527a9, 0x006ee04a,
    };
    using RemoveConnectionFn = void(__thiscall*)(void* occupant, void* otherPole, uint32_t flag);
    bool gCityShuttingDown = false;

    void __fastcall RemoveConnectionHook(void* occupant, void* /*edx*/, void* otherPole, uint32_t flag) {
        const auto original = reinterpret_cast<RemoveConnectionFn>(kRemoveConnection);
        original(occupant, otherPole, flag);
        if (!gSettings.enabled || gCityShuttingDown) {
            return;
        }
        const PoleAttachOverride* const self = FindOverride(occupant);
        if (self == nullptr || !self->foundationFollowSpan) {
            return;
        }
        float yaw = 0.0f;
        PoleFoundationYaw(occupant, yaw);
        RebuildFoundation(occupant, self->hasFoundationHalfExtent ? self->foundationHalfExtent
                                                                  : kVanillaFoundationHalfExtent, yaw);
    }

    // Inter-pole distance for the active style, or the tool's own vanilla value when no style is
    // active / the active style doesn't override it. See kDeterminePolePositions block above.
    uint32_t ResolveMaxCellsBetweenPoles(const uint32_t vanillaValue) noexcept {
        if (gActiveStyleIndex == 0 || gActiveStyleIndex > gStyles.size()) {
            return vanillaValue;
        }
        const PoleStyle& style = gStyles[gActiveStyleIndex - 1];
        return style.hasMaxCells ? style.maxCellsBetweenPoles : vanillaValue;
    }

    using DeterminePolePositionsFn = void(__thiscall*)(void* tool);

    void __fastcall DeterminePolePositionsHook(void* tool, void* /*edx*/) {
        auto* const field = reinterpret_cast<uint32_t*>(
            reinterpret_cast<uint8_t*>(tool) + kTool_MaxCellsBetweenPoles);
        const uint32_t saved = *field;
        if (gSettings.enabled) {
            uint32_t resolved = ResolveMaxCellsBetweenPoles(saved);
            // During a FAR drag every synthetic step is one FAR period, and
            // the cadence field counts step indices, not literal cells (docs SS17.C). Poles can
            // only sit on FAR nodes, so convert the cell spacing into whole periods (nearest,
            // minimum 1): FAR-2 with vanilla 10 -> every 5 nodes = 10 cells along the major axis.
            if (gFarDragActive && gFarDragRun > 0) {
                resolved = std::max(1u, (resolved + gFarDragRun / 2) / gFarDragRun);
            }
            *field = resolved;
        }
        const auto original = reinterpret_cast<DeterminePolePositionsFn>(kDeterminePolePositions);
        original(tool);
        *field = saved; // restore vanilla; the placement loop is this field's only reader
    }

    // ------------------------------------------------------------------
    // SC4SIM: power lines across zones and farm fields. Not part of the sc4-render-services sample;
    // reverse engineered on the 1.1.641 executable (see docs/power-lines-across-zones.md).
    //
    // cSC4PowerLineTool::CellIsOKForPlacement (0x0064fd00, __thiscall, args cellX, cellZ, RET 8) returns
    //   0 = a pole may stand here,
    //   1 = zoned cell (cISC4ZoneManager::GetZoneType != None, Landfill, Plopped),
    //   2 = a building occupant or a lot on the cell, 3 = a network piece that is not underground.
    // The checks run in that order and the zone test returns early: on a zoned cell vanilla returns 1
    // at 0x0064fda0 without ever looking at the cell's occupants (building 2, network 3) or its lot.
    // CheckForReservedTiles (0x0064fed0) never moves the cut point forward on 1, so a 1 cuts the drag back to the
    // last free step; a 3 cuts only on the last step when the cell before gave 0.
    // DeterminePolePositions (call 0x006509ad) only places poles on 0. Over 1 the wire may span, but no
    // pole can stand, so a zone wider than the pole spacing breaks the line; the drag validation
    // (call 0x0064ff3e) stops the drag at 2. A farm field is a zoned lot without a building.
    //
    // Turning the early 1 into 0 would skip the occupant and lot checks on every zoned cell, so a
    // pole could stand on a network piece or a building occupant without a lot. Instead, while one of
    // the two hooked calls runs, the zoned branch (13 bytes at 0x0064fda0: `mov [esp+14h],1;
    // jmp 0x0064fe64`) is redirected to the occupant checks at 0x0064fdad with the result still 0,
    // and records that the cell is zoned. The hook then answers:
    //   zoned, occupant and lot checks 0 (empty zone)         -> 0;
    //   zoned, 2 with a lot without a building (farm field)   -> 0;
    //   zoned, anything else                                   -> 1 (the vanilla answer);
    //   not zoned, 2 with a lot without a building            -> 0 (as before); otherwise vanilla.
    // The third caller (0x0065210b, the pole relocation of UpdateOnZoneChange) calls with the branch
    // disarmed, so it gets the vanilla result and relocated poles still avoid zones.
    // ------------------------------------------------------------------
    constexpr uintptr_t kCellIsOKForPlacement = 0x0064fd00;
    constexpr std::array<uintptr_t, 2> kCellIsOKForPlacementCallSites = {
        0x0064ff3e, // drag validation (truncates the drag on 2, warns on 1)
        0x006509ad, // DeterminePolePositions
    };
    constexpr uintptr_t kCellZonedBranch = 0x0064fda0;
    constexpr uintptr_t kCellOccupantChecks = 0x0064fdad;
    constexpr uintptr_t kCellResultEpilogue = 0x0064fe64;
    constexpr uint32_t kCellOK = 0;
    constexpr uint32_t kCellZoned = 1;
    constexpr uint32_t kCellBuildingOrLot = 2;
    bool gCellZoneScanArmed = false;
    bool gCellWasZoned = false;

#if defined(_MSC_VER) && defined(_M_IX86)
    // Reached by a JMP (not a CALL) from 0x0064fda0, at the same stack depth as the two branches to
    // 0x0064fdad. EAX holds the zone type just compared and is free (rewritten at 0x0064fdc6 and in
    // the epilogue's destructor call). [esp+14h] is the function's result, still 0 here.
    __declspec(naked) void CellZonedBranchHook() {
        __asm {
            cmp byte ptr [gCellZoneScanArmed], 0
            je vanilla_zoned
            mov byte ptr [gCellWasZoned], 1
            mov eax, kCellOccupantChecks
            jmp eax
        vanilla_zoned:
            mov dword ptr [esp + 14h], 1
            mov eax, kCellResultEpilogue
            jmp eax
        }
    }
#else
#error Power-line zone hook requires 32-bit MSVC.
#endif

    using CellIsOKForPlacementFn = uint32_t(__thiscall*)(void* tool, int32_t cellX, int32_t cellZ);

    uint32_t __fastcall CellIsOKForPlacementHook(void* tool, void* /*edx*/, int32_t cellX, int32_t cellZ) {
        const auto original = reinterpret_cast<CellIsOKForPlacementFn>(kCellIsOKForPlacement);
        if (!gSettings.enabled || !gSettings.crossZones) {
            return original(tool, cellX, cellZ);
        }
        gCellWasZoned = false;
        gCellZoneScanArmed = true;
        const uint32_t result = original(tool, cellX, cellZ);
        gCellZoneScanArmed = false;
        const bool zoned = gCellWasZoned;

        uint32_t newResult = zoned ? kCellZoned : result;
        bool hasLot = false;
        bool hasBuilding = false;
        if (result == kCellOK && zoned) {
            newResult = kCellOK; // empty zone: no building, network piece or lot on the cell
        } else if (result == kCellBuildingOrLot) {
            const cISC4AppPtr app;
            cISC4City* const city = app ? app->GetCity() : nullptr;
            cISC4LotManager* const lotManager = city ? city->GetLotManager() : nullptr;
            cISC4Lot* const lot = lotManager ? lotManager->GetLot(cellX, cellZ, false) : nullptr;
            hasLot = lot != nullptr;
            hasBuilding = lot != nullptr && lot->GetBuilding() != nullptr;
            if (hasLot && !hasBuilding) {
                newResult = kCellOK; // farm field (or an empty lot); a lot with a building keeps blocking
            }
        }
        if (zoned || newResult != result) {
            LOG_DEBUG("PowerPoleCustomization: [zones] cell ({}, {}) {}checks {} -> {} (lot {}, building {}).",
                      cellX, cellZ, zoned ? "zoned, " : "", result, newResult, hasLot ? "yes" : "no",
                      hasBuilding ? "yes" : "no");
        }
        return newResult;
    }

    // ------------------------------------------------------------------
    // SC4SIM: remove the zone under new poles (see docs/power-lines-across-zones.md).
    //
    // cSC4PowerLineTool::PlacePoles (0x00651560, __thiscall, one byte argument, RET 4) builds the poles
    // listed in the tool's tPoleInfo vector (tool+0x36c..+0x370, stride 0x30). The entries are written by the
    // pole-add helper 0x00650760: +0x00 pole index, +0x04/+0x08 the cell X/Z taken from the dragged-cell
    // vector (the same cells passed to CellIsOKForPlacement), +0x0c the cell index, +0x10 the pole occupant
    // (cleared at the end of PlacePoles), +0x24..+0x2d the snap key compared by the list scan 0x0064f170.
    // It is called from outside itself at 0x006522c8 (FindNewPolePosition 0x006520c0, which moves a pole out of a
    // zoned rectangle for UpdateOnZoneChange and returns straight after the call) and 0x006529cb (line placement,
    // power tool vtable slot 0xD4). tool+0x50 is most likely the tool's "placing for real" (not preview) flag;
    // only calls with it 0 are recorded, which during a drag are the preview calls that list the same poles as
    // the final placement. PlacePoles also runs on every mouse move of a drag,
    // so the hook only records the pole cells; when the drag ends (OnMouseUpL hook below) every recorded pole
    // cell that is still zoned is dezoned, with the call the game itself uses to dezone a rectangle (0x00467075):
    // PlaceZone(region, None, place=1, skipFunds=1, checkSlope=0, skipZoneSize=1, checkLotSize=1, 0, 0, 0).
    // Cells with a lot are skipped (a lot cannot lose a single cell: the game would demolish it), unless
    // DezoneFieldCells is on and the lot has no building (farm field). No keepwires bypass is needed: the zone
    // change handler UpdateOnZoneChange (0x006522f0) returns at once when the new zone is 0 (0x006522fc), so
    // dezoning never reroutes a line.
    // ------------------------------------------------------------------
    constexpr uintptr_t kPlacePoles = 0x00651560;
    constexpr std::array<uintptr_t, 2> kPlacePolesCallSites = {
        0x006522c8, // neighbour auto-connect
        0x006529cb, // line placement
    };
    constexpr uint32_t kTool_RemovalMode = 0x50;     // byte; most likely "placing for real" (see above)
    constexpr uint32_t kTool_PoleInfoBegin = 0x36c;
    constexpr uint32_t kTool_PoleInfoEnd = 0x370;
    constexpr uint32_t kPoleInfoStride = 0x30;
    constexpr uint32_t kPoleInfo_CellX = 0x04;
    constexpr uint32_t kPoleInfo_CellZ = 0x08;

    using PlacePolesFn = void(__thiscall*)(void* tool, uint8_t flag);

    bool IsDezonableZone(const cISC4ZoneManager::ZoneType type) {
        using ZoneType = cISC4ZoneManager::ZoneType;
        return type != ZoneType::None && type != ZoneType::Landfill && type != ZoneType::Plopped;
    }

    // Pole cells of the latest PlacePoles call. During a drag the game runs PlacePoles on every mouse move
    // (the log shows the pole count growing with the drag), so nothing may be dezoned there: the cells are only
    // recorded, and dezoned when the drag ends (OnMouseUpL of the network tool input control, see below).
    std::vector<std::pair<int32_t, int32_t>> gPendingPoleCells;

    void RecordPoleCells(void* tool) {
        gPendingPoleCells.clear();
        const auto* const base = reinterpret_cast<const uint8_t*>(tool);
        const auto begin = *reinterpret_cast<const uintptr_t*>(base + kTool_PoleInfoBegin);
        const auto end = *reinterpret_cast<const uintptr_t*>(base + kTool_PoleInfoEnd);
        if (begin == 0 || end < begin || (end - begin) % kPoleInfoStride != 0 || (end - begin) / kPoleInfoStride > 4096) {
            return;
        }
        for (uintptr_t entry = begin; entry < end; entry += kPoleInfoStride) {
            const int32_t x = *reinterpret_cast<const int32_t*>(entry + kPoleInfo_CellX);
            const int32_t z = *reinterpret_cast<const int32_t*>(entry + kPoleInfo_CellZ);
            if (x >= 0 && z >= 0 && x <= 1024 && z <= 1024) {
                gPendingPoleCells.emplace_back(x, z);
            }
        }
    }

    void DezonePendingPoleCells() {
        const cISC4AppPtr app;
        cISC4City* const city = app ? app->GetCity() : nullptr;
        cISC4ZoneManager* const zoneManager = city ? city->GetZoneManager() : nullptr;
        cISC4LotManager* const lotManager = city ? city->GetLotManager() : nullptr;
        if (zoneManager == nullptr || lotManager == nullptr || gPendingPoleCells.empty()) {
            gPendingPoleCells.clear();
            return;
        }

        LOG_DEBUG("PowerPoleCustomization: [zones] drag ended with {} pole(s).", gPendingPoleCells.size());
        uint32_t dezoned = 0;
        for (const auto& [x, z] : gPendingPoleCells) {
            const auto zone = zoneManager->GetZoneType(x, z);
            if (!IsDezonableZone(zone)) {
                LOG_DEBUG("PowerPoleCustomization: [zones] pole cell ({}, {}) zone {}: nothing to dezone.", x, z,
                          static_cast<int32_t>(zone));
                continue;
            }
            if (cISC4Lot* const lot = lotManager->GetLot(x, z, false)) {
                if (!gSettings.dezoneFieldCells || lot->GetBuilding() != nullptr) {
                    LOG_DEBUG("PowerPoleCustomization: [zones] pole cell ({}, {}) zone {} is inside a lot{}: kept.", x, z,
                              static_cast<int32_t>(zone), lot->GetBuilding() != nullptr ? " with a building" : "");
                    continue;
                }
            }

            SC4CellRegion<int32_t> region(x, z, x, z, true);
            const bool ok = zoneManager->PlaceZone(region, cISC4ZoneManager::ZoneType::None,
                                                   true, true, false, true, true, nullptr, nullptr, 0);
            if (ok) {
                ++dezoned;
            }
            LOG_DEBUG("PowerPoleCustomization: [zones] dezone pole cell ({}, {}) zone {}: {}.", x, z,
                      static_cast<int32_t>(zone), ok ? "done" : "refused");
        }
        LOG_DEBUG("PowerPoleCustomization: [zones] dezoned {} pole cell(s).", dezoned);
        gPendingPoleCells.clear();
    }

    void __fastcall PlacePolesHook(void* tool, void* /*edx*/, uint8_t flag) {
        const auto original = reinterpret_cast<PlacePolesFn>(kPlacePoles);
        original(tool, flag);
        if (gSettings.enabled && gSettings.crossZones && gSettings.dezoneUnderPoles
            && *(reinterpret_cast<const uint8_t*>(tool) + kTool_RemovalMode) == 0) {
            RecordPoleCells(tool);
        }
    }

    // ------------------------------------------------------------------
    // FAR drag hook (see the kDrawNetworkLine constants block above and
    // docs/sc4-powerline-tool-re.md SS17 for the design). Validated in-game 2026-07-19.
    // ------------------------------------------------------------------
    using DrawNetworkLineFn = uint8_t(__thiscall*)(void* tool, uint32_t* start, uint32_t* end,
                                                    uint8_t straightOnly, int networkType);

    struct CellXZ {
        uint32_t x;
        uint32_t z;
    };

    struct DraggedStep {
        uint32_t firstCellIndex;
        uint32_t lastCellIndex;
        uint32_t unknownZeroed; // vanilla ComputeDraggedCells writes 0 here
    };

    // 4-connected supercover of one FAR period in (major, minor) cell space: every cell the
    // center-to-center node segment passes through, from (0,0) inclusive to (run,rise) exclusive,
    // in traversal order. At an exact corner crossing both adjacent cells are included, matching
    // vanilla's own 2-cells-per-column diagonal registration.
    // Supercover cell set for one FAR period. No longer used by the node-only drag synthesis (which
    // stores only pole nodes), but retained for reference / a future per-tile wire-occupancy option.
    [[maybe_unused]] std::vector<std::pair<uint32_t, uint32_t>> BuildFarPeriodPattern(const uint32_t run, const uint32_t rise) {
        std::vector<std::pair<uint32_t, uint32_t>> cells;
        uint32_t u = 0;
        uint32_t v = 0;
        cells.emplace_back(0u, 0u);
        while (u != run || v != rise) {
            // Next boundary crossings of the segment (0.5,0.5) -> (run+0.5, rise+0.5), compared
            // cross-multiplied to stay in integers: next u-boundary at t_u = (u + 0.5) / run,
            // next v-boundary at t_v = (v + 0.5) / rise (both scaled by 2*run*rise).
            bool advanceU;
            bool corner = false;
            if (v >= rise) {
                advanceU = true;
            } else if (u >= run) {
                advanceU = false;
            } else {
                const uint64_t tu = static_cast<uint64_t>(2 * u + 1) * rise;
                const uint64_t tv = static_cast<uint64_t>(2 * v + 1) * run;
                advanceU = tu < tv;
                corner = tu == tv;
            }
            if (corner) {
                // Corner crossing: register both adjacent cells, then continue diagonally.
                cells.emplace_back(u + 1, v);
                cells.emplace_back(u, v + 1);
                ++u;
                ++v;
            } else if (advanceU) {
                ++u;
            } else {
                ++v;
            }
            if (u == run && v == rise) {
                break; // the closing node belongs to the next period (or the final step)
            }
            cells.emplace_back(u, v);
        }
        return cells;
    }

    float SnapCandidateAngle(const uint32_t candidate) {
        if (candidate == kAxisSnapCandidate) {
            return 0.0f;
        }
        if (candidate == kDiagonalSnapCandidate) {
            return kDiagonalAngleRadians;
        }
        const FarRatio& ratio = kFarRatios[candidate - 1];
        return std::atan2(static_cast<float>(ratio.rise), static_cast<float>(ratio.run));
    }

    uint32_t SelectSnapCandidate(const uint32_t startX, const uint32_t startZ,
                                 const uint32_t absoluteDx, const uint32_t absoluteDz) {
        const uint32_t major = std::max(absoluteDx, absoluteDz);
        const uint32_t minor = std::min(absoluteDx, absoluteDz);
        const float angle = std::atan2(static_cast<float>(minor), static_cast<float>(major));

        uint32_t bestCandidate = kAxisSnapCandidate;
        float bestDifference = std::fabs(angle - SnapCandidateAngle(bestCandidate));
        for (uint32_t candidate = 1; candidate <= kDiagonalSnapCandidate; ++candidate) {
            const float difference = std::fabs(angle - SnapCandidateAngle(candidate));
            const bool candidateIsRegular = candidate == kAxisSnapCandidate || candidate == kDiagonalSnapCandidate;
            const bool bestIsRegular = bestCandidate == kAxisSnapCandidate || bestCandidate == kDiagonalSnapCandidate;
            // Prefer a regular heading at an exact tie, otherwise take the genuinely nearer one.
            if (difference < bestDifference || (difference == bestDifference && candidateIsRegular && !bestIsRegular)) {
                bestCandidate = candidate;
                bestDifference = difference;
            }
        }

        if (gFarSnapAnchorValid && gFarSnapAnchorX == startX && gFarSnapAnchorZ == startZ) {
            const float previousDifference = std::fabs(angle - SnapCandidateAngle(gFarSnapCandidate));
            if (previousDifference <= bestDifference + kSnapHysteresisRadians) {
                bestCandidate = gFarSnapCandidate;
            }
        }

        gFarSnapAnchorValid = true;
        gFarSnapAnchorX = startX;
        gFarSnapAnchorZ = startZ;
        gFarSnapCandidate = bestCandidate;
        return bestCandidate;
    }

    uint8_t __fastcall FarDrawNetworkLineHook(void* tool, void* /*edx*/, uint32_t* start, uint32_t* end,
                                              uint8_t straightOnly, int networkType) {
        const auto original = reinterpret_cast<DrawNetworkLineFn>(kDrawNetworkLine);
        gFarDragActive = false;
        if (!gSettings.enabled || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
            gFarSnapAnchorValid = false;
            return original(tool, start, end, straightOnly, networkType);
        }

        const int32_t dx = static_cast<int32_t>(end[0]) - static_cast<int32_t>(start[0]);
        const int32_t dz = static_cast<int32_t>(end[1]) - static_cast<int32_t>(start[1]);
        const uint32_t adx = static_cast<uint32_t>(std::abs(dx));
        const uint32_t adz = static_cast<uint32_t>(std::abs(dz));
        if (adx == 0 && adz == 0) {
            gFarSnapAnchorValid = false;
            return original(tool, start, end, straightOnly, networkType);
        }

        const uint32_t snapCandidate = SelectSnapCandidate(start[0], start[1], adx, adz);
        if (snapCandidate == kAxisSnapCandidate || snapCandidate == kDiagonalSnapCandidate) {
            return original(tool, start, end, straightOnly, networkType); // vanilla handles regular headings
        }

        const FarRatio ratio = kFarRatios[snapCandidate - 1];

        const uint32_t cityCellsX = *reinterpret_cast<const uint32_t*>(reinterpret_cast<uint8_t*>(tool) + kTool_CityCellsX);
        const uint32_t cityCellsZ = *reinterpret_cast<const uint32_t*>(reinterpret_cast<uint8_t*>(tool) + kTool_CityCellsZ);
        const bool majorIsX = adx >= adz;
        const uint32_t majorLen = majorIsX ? adx : adz;
        const uint32_t minorLen = majorIsX ? adz : adx;
        const int32_t majorSign = (majorIsX ? dx : dz) > 0 ? 1 : -1;
        const int32_t minorSign = (majorIsX ? dz : dx) > 0 ? 1 : -1;

        // Neighbor connections require the terminal cell itself to be on the city boundary. For
        // ordinary drags, retain nearest-period snapping. For a boundary drag, keep as many exact
        // FAR periods as fit and preserve the cursor's boundary cell as a short transition tail.
        const CellXZ requestedEnd{end[0], end[1]};
        const bool exitsAtNegativeX = dx < 0 && requestedEnd.x == 0;
        const bool exitsAtPositiveX = dx > 0 && requestedEnd.x + 1 == cityCellsX;
        const bool exitsAtNegativeZ = dz < 0 && requestedEnd.z == 0;
        const bool exitsAtPositiveZ = dz > 0 && requestedEnd.z + 1 == cityCellsZ;
        const bool boundaryTerminal = exitsAtNegativeX || exitsAtPositiveX ||
                                      exitsAtNegativeZ || exitsAtPositiveZ;
        uint32_t periods = boundaryTerminal
            ? std::min(majorLen / ratio.run, minorLen / ratio.rise)
            : (majorLen + ratio.run / 2) / ratio.run;
        auto nodeCell = [&](const uint32_t period) -> CellXZ {
            const int32_t du = majorSign * static_cast<int32_t>(period * ratio.run);
            const int32_t dv = minorSign * static_cast<int32_t>(period * ratio.rise);
            return CellXZ{
                static_cast<uint32_t>(static_cast<int32_t>(start[0]) + (majorIsX ? du : dv)),
                static_cast<uint32_t>(static_cast<int32_t>(start[1]) + (majorIsX ? dv : du)),
            };
        };
        while (periods >= 1) {
            const CellXZ last = nodeCell(periods);
            if (last.x < cityCellsX && last.z < cityCellsZ) {
                break;
            }
            --periods;
        }
        if (periods < 1) {
            return original(tool, start, end, straightOnly, networkType);
        }

        const CellXZ terminal = boundaryTerminal ? requestedEnd : nodeCell(periods);
        const CellXZ lastFarNode = nodeCell(periods);
        const bool hasTransitionTail = terminal.x != lastFarNode.x || terminal.z != lastFarNode.z;

        // Node-only layout: one single-cell step per FAR period node (plus the boundary tail node
        // and terminal). DeterminePolePositions is scoped to max-cells-between-poles = 1, so a pole
        // is forced at every step's primary (= only) cell. Intermediate span cells are deliberately
        // omitted: the wire renders bezier pole-to-pole and power conducts through the connection
        // graph (proven by vanilla's 10-cell empty pole gaps), so they were never needed for pole
        // placement or rendering. Omitting them also keeps the hidden per-cell line occupants off the
        // tiles between poles -- zoning/lot construction then only interrupts the wire when a lot
        // lands on a pole node itself -- and caps the buffer at ~periods+2 cells so arbitrarily long
        // FAR runs fit on any city tile (256x256 included) instead of overflowing a straight drag.
        std::vector<CellXZ> cells;
        std::vector<DraggedStep> steps;
        cells.reserve(periods + 3);
        steps.reserve(periods + 3);
        const auto pushNode = [&](const CellXZ node) {
            steps.push_back(DraggedStep{static_cast<uint32_t>(cells.size()),
                                        static_cast<uint32_t>(cells.size()), 0});
            cells.push_back(node);
        };
        for (uint32_t p = 0; p < periods; ++p) {
            pushNode(nodeCell(p));
        }
        if (hasTransitionTail) {
            // A boundary drag keeps the last whole-period node as a pole, then the boundary cell
            // itself, so AttemptNeighborConnections' outward one-cell probe actually leaves the city.
            const int32_t rawTailMajor = majorIsX
                ? static_cast<int32_t>(terminal.x) - static_cast<int32_t>(lastFarNode.x)
                : static_cast<int32_t>(terminal.z) - static_cast<int32_t>(lastFarNode.z);
            const int32_t rawTailMinor = majorIsX
                ? static_cast<int32_t>(terminal.z) - static_cast<int32_t>(lastFarNode.z)
                : static_cast<int32_t>(terminal.x) - static_cast<int32_t>(lastFarNode.x);
            if (rawTailMajor * majorSign < 0 || rawTailMinor * minorSign < 0) {
                LOG_WARN("PowerPoleCustomization: [FAR PoC] boundary transition reverses direction; falling back.");
                return original(tool, start, end, straightOnly, networkType);
            }
            pushNode(lastFarNode);
        }
        pushNode(terminal);

        // Fake straight drag sized so vanilla allocates at least as many cells (and therefore steps:
        // a straight drag makes one step per cell) as we need. Every allocated cell is overwritten
        // below, so the fake anchor is irrelevant to the result -- anchor it at the map edge instead
        // of the real `start` so a run of `totalCells` fits whenever the route length is within a map
        // dimension. A center anchor has too little clearance on either side and forced a fallback.
        const auto totalCells = static_cast<uint32_t>(cells.size());
        uint32_t fakeStart[2];
        uint32_t fakeEnd[2];
        if (totalCells <= cityCellsX) {
            fakeStart[0] = 0;         fakeStart[1] = start[1];
            fakeEnd[0] = totalCells - 1; fakeEnd[1] = start[1];
        } else if (totalCells <= cityCellsZ) {
            fakeStart[0] = start[0];  fakeStart[1] = 0;
            fakeEnd[0] = start[0];    fakeEnd[1] = totalCells - 1;
        } else {
            LOG_WARN("PowerPoleCustomization: [FAR PoC] route needs {} cells, exceeds city bounds "
                     "({}x{}); falling back to vanilla drag.", totalCells, cityCellsX, cityCellsZ);
            return original(tool, start, end, straightOnly, networkType);
        }
        if (original(tool, fakeStart, fakeEnd, straightOnly, networkType) == 0) {
            LOG_WARN("PowerPoleCustomization: [FAR PoC] vanilla allocation drag failed; falling back.");
            return original(tool, start, end, straightOnly, networkType);
        }

        auto* const base = reinterpret_cast<uint8_t*>(tool);
        auto* const stepsBegin = *reinterpret_cast<uint8_t**>(base + kTool_StepsBegin);
        auto* const cellsBegin = *reinterpret_cast<uint8_t**>(base + kTool_CellsBegin);
        const auto stepCapacity = static_cast<uint32_t>(
            (*reinterpret_cast<uint8_t**>(base + kTool_StepsEnd) - stepsBegin) / sizeof(DraggedStep));
        const auto cellCapacity = static_cast<uint32_t>(
            (*reinterpret_cast<uint8_t**>(base + kTool_CellsEnd) - cellsBegin) / sizeof(CellXZ));
        if (stepsBegin == nullptr || cellsBegin == nullptr ||
            stepCapacity < steps.size() || cellCapacity < totalCells) {
            LOG_WARN("PowerPoleCustomization: [FAR PoC] vanilla drag produced {} steps/{} cells, "
                     "need {}/{}; falling back.", stepCapacity, cellCapacity, steps.size(), totalCells);
            return original(tool, start, end, straightOnly, networkType);
        }

        // Rewrite the POD contents in place and shrink the end pointers -- no game-allocator
        // interaction. All steps are declared "straight" so GetPrimaryCell returns first cells
        // and the straight/diag-junction special case never engages.
        std::memcpy(cellsBegin, cells.data(), totalCells * sizeof(CellXZ));
        *reinterpret_cast<uint8_t**>(base + kTool_CellsEnd) = cellsBegin + totalCells * sizeof(CellXZ);
        std::memcpy(stepsBegin, steps.data(), steps.size() * sizeof(DraggedStep));
        *reinterpret_cast<uint8_t**>(base + kTool_StepsEnd) = stepsBegin + steps.size() * sizeof(DraggedStep);
        *reinterpret_cast<uint32_t*>(base + kTool_TotalSteps) = static_cast<uint32_t>(steps.size());
        *reinterpret_cast<uint32_t*>(base + kTool_StraightSteps) = static_cast<uint32_t>(steps.size());
        *reinterpret_cast<uint8_t*>(base + kTool_DiagonalFlag) = 0;

        // AttemptNeighborConnections reduces these even direction codes with `direction >> 1`
        // and indexes its cardinal X/Z delta tables. Prefer the actual crossed edge at corners;
        // otherwise the FAR major-axis direction is the outward probe direction.
        uint32_t neighborDirection = 0;
        if ((majorIsX && (exitsAtNegativeX || exitsAtPositiveX)) ||
            (!majorIsX && !(exitsAtNegativeZ || exitsAtPositiveZ) &&
             (exitsAtNegativeX || exitsAtPositiveX))) {
            neighborDirection = exitsAtPositiveX ? 4u : 0u;
        } else if (exitsAtNegativeZ || exitsAtPositiveZ) {
            neighborDirection = exitsAtPositiveZ ? 6u : 2u;
        } else if (majorIsX) {
            neighborDirection = majorSign > 0 ? 4u : 0u;
        } else {
            neighborDirection = majorSign > 0 ? 6u : 2u;
        }
        *reinterpret_cast<uint32_t*>(base + kTool_FirstDirection) = neighborDirection;
        *reinterpret_cast<uint32_t*>(base + kTool_SecondDirection) = neighborDirection;

        // BreakIntoStraightAndDiagSegments writes the snapped endpoint back into `end`; callers
        // consume it, so the FAR snap must do the same.
        end[0] = terminal.x;
        end[1] = terminal.z;
        // Record the exact heading so CreatePowerPole's resolver can pick the FAR model. Orientation
        // is fixed by which axis is major and whether the major/minor steps share sign (the +x,+z /
        // -x,-z family vs the +x,-z / -x,+z family). majorSign/minorSign share sign exactly when the
        // span runs into the sign-agreeing diagonal, matching Get0To3Direction's 1-vs-3 split.
        const bool signsAgree = majorSign == minorSign;
        gFarDragOrient = majorIsX ? (signsAgree ? kFarOrientXP : kFarOrientXN)
                                  : (signsAgree ? kFarOrientZP : kFarOrientZN);
        gFarDragRatioIndex = snapCandidate - 1; // snapCandidate 1..N maps to kFarRatios 0..N-1

        // Debug trace (silent at the default info level): report when the drag snaps to a different
        // FAR heading than last time. Pairs with the per-pole model-select trace in ResolveFarInstance
        // so a bad orientation can be traced from the heading the drag chose to the resolved instance.
        static uint32_t lastLoggedDragHeading = 0xFFFFFFFFu;
        const uint32_t dragHeading = gFarDragRatioIndex * kFarOrientCount + gFarDragOrient;
        if (dragHeading != lastLoggedDragHeading) {
            lastLoggedDragHeading = dragHeading;
            const double slopeDeg = std::atan2(static_cast<double>(ratio.rise),
                                               static_cast<double>(ratio.run)) * 180.0 / 3.14159265358979323846;
            LOG_DEBUG("PowerPoleCustomization: [FAR] drag snapped to FAR-{}.{} (slope {:.1f} deg, {}-major, "
                     "run/rise={}/{}, majorSign={}, minorSign={}).",
                     kFarRatioLabels[gFarDragRatioIndex], FarOrientName(gFarDragOrient), slopeDeg,
                     majorIsX ? "x" : "z", ratio.run, ratio.rise, majorSign, minorSign);
        }

        gFarDragActive = true;
        gFarDragRun = ratio.run;
        return 1;
    }

    // ------------------------------------------------------------------
    // Tab / Shift-Tab pole-style switch (SC4SIM: without the SC4RenderServices ImGui status overlay;
    // the active style is written to SC4SIM.log). See docs/power-line-style-ui-design.md. Both the IC vtable (0x00aab008, proven by the
    // OnKeyDown slot's install-time byte check) and the power-tool vtable (0x00aa9f30, proven by the
    // FAR DrawNetworkLine patch's byte check) are validated against the live binary before either is
    // trusted at runtime, so a recompiled SC4 with moved vtables degrades to "no overlay / no Tab"
    // rather than misbehaving. The runtime checks below are pure reads + compares (no writes).
    // ------------------------------------------------------------------

    // True when `control` is the shared network-tool input control AND the subtool it is currently
    // driving is the power line tool. Used by the key hook (control = its own `this`) and by the
    // overlay visibility check (control = GetCurrentViewInputControl()).
    bool IsNetworkControlDrivingPowerTool(void* control) {
        if (control == nullptr || *reinterpret_cast<uintptr_t*>(control) != kNetworkToolInputControlVtable) {
            return false;
        }
        return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(control) + kIC_NetworkType) == kNetworkTypePower;
    }

    // Advances the active pole style. dir > 0 forward, dir < 0 backward. Index 0 is vanilla; custom
    // styles occupy 1..gStyles.size(). Wraps. Driven by the Tab/Shift-Tab key hook.
    void CycleStyle(const int dir) {
        if (gStyles.empty()) {
            gActiveStyleIndex = 0;
            return;
        }
        const uint32_t count = static_cast<uint32_t>(gStyles.size()) + 1; // + vanilla(0)
        const uint32_t step = dir >= 0 ? 1u : count - 1u;
        gActiveStyleIndex = (gActiveStyleIndex + step) % count;
        const std::string activeName = gActiveStyleIndex == 0 ? "vanilla" : gStyles[gActiveStyleIndex - 1].name;
        LOG_INFO("PowerPoleCustomization: active pole style -> \"{}\" ({} of {}) -- applies to newly-placed poles.",
                 activeName, gActiveStyleIndex, gStyles.size());
    }

    using OnKeyDownFn = uint8_t(__thiscall*)(void* control, int vkCode, uint32_t modifiers);

    // Bit 0 of the OnKeyDown modifiers argument is Shift -- confirmed against vanilla's own
    // Tab handler in cSC4ViewInputControlNetworkIntxTool::OnKeyDown (0x00660e80), which picks the
    // forward/backward intersection-rule direction from exactly `modifiers & 1`.
    constexpr uint32_t kKeyModifierShift = 0x1;

    // Vtable-slot hook (installed like the FAR DrawNetworkLine hook: __fastcall matches __thiscall
    // with a throwaway edx). Consumes Tab only while the power tool is the active network subtool;
    // everything else -- including Tab under road/rail/street -- falls through to vanilla untouched.
    uint8_t __fastcall OnKeyDownHook(void* control, void* /*edx*/, int vkCode, uint32_t modifiers) {
        if (gSettings.enabled && vkCode == static_cast<int>(kVkTab) &&
            IsNetworkControlDrivingPowerTool(control)) {
            const bool shift = (modifiers & kKeyModifierShift) != 0;
            CycleStyle(shift ? -1 : 1);
            return 1; // consume: don't let vanilla focus handling also act on Tab
        }
        const auto original = reinterpret_cast<OnKeyDownFn>(kOnKeyDown);
        return original(control, vkCode, modifiers);
    }

    // SC4SIM: end of a power-line drag. cSC4ViewInputControlNetworkTool::OnMouseDownL (vtable +0x40,
    // 0x00661ee0) starts a drag, OnMouseUpL (vtable +0x48, 0x00661f50) ends it and hands it to the tool.
    // The pole cells recorded by PlacePolesHook during the drag are dezoned after OnMouseUpL returns.
    constexpr uintptr_t kOnMouseDownLSlot = kNetworkToolInputControlVtable + 0x40;
    constexpr uintptr_t kOnMouseDownL = 0x00661ee0;
    constexpr uintptr_t kOnMouseUpLSlot = kNetworkToolInputControlVtable + 0x48;
    constexpr uintptr_t kOnMouseUpL = 0x00661f50;
    using OnMouseButtonFn = uint8_t(__thiscall*)(void* control, int32_t x, int32_t z, uint32_t modifiers);

    uint8_t __fastcall OnMouseDownLHook(void* control, void* /*edx*/, int32_t x, int32_t z, uint32_t modifiers) {
        if (IsNetworkControlDrivingPowerTool(control)) {
            gPendingPoleCells.clear(); // a new drag: forget any cells of a cancelled one
        }
        return reinterpret_cast<OnMouseButtonFn>(kOnMouseDownL)(control, x, z, modifiers);
    }

    uint8_t __fastcall OnMouseUpLHook(void* control, void* /*edx*/, int32_t x, int32_t z, uint32_t modifiers) {
        const bool powerTool = IsNetworkControlDrivingPowerTool(control);
        const uint8_t result = reinterpret_cast<OnMouseButtonFn>(kOnMouseUpL)(control, x, z, modifiers);
        if (powerTool && gSettings.enabled && gSettings.crossZones && gSettings.dezoneUnderPoles) {
            DezonePendingPoleCells();
        }
        return result;
    }

    // Does a style define any orientation of the given FAR ratio?
    bool StyleDefinesRatio(const PoleStyle& style, const uint32_t ratioIndex) {
        for (uint32_t orient = 0; orient < kFarOrientCount; ++orient) {
            if (style.hasFarHeading[ratioIndex * kFarOrientCount + orient]) {
                return true;
            }
        }
        return false;
    }

    // The active style's supported FAR ratios: the style's own headings plus the [PowerPoles.FAR]
    // default fallback (which applies under any style, vanilla included).
    bool ActiveStyleSupportsRatio(const uint32_t ratioIndex) {
        if (gActiveStyleIndex >= 1 && StyleDefinesRatio(gStyles[gActiveStyleIndex - 1], ratioIndex)) {
            return true;
        }
        return gHasFarDefault && StyleDefinesRatio(gFarDefaultStyle, ratioIndex);
    }

    // ------------------------------------------------------------------
    // Settings (SC4SIM.ini, section [SC4PowerPoleCustomization] and the [PowerPoles.*] styles).
    // ------------------------------------------------------------------
    bool ParseBool(const std::string& value, const bool defaultValue) {
        std::string text(value);
        std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true" || text == "1" || text == "yes") return true;
        if (text == "false" || text == "0" || text == "no") return false;
        return defaultValue;
    }

    uint32_t ParseUInt32(const std::string& value, const uint32_t defaultValue) {
        uint32_t result = defaultValue;
        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
        return (ec == std::errc() && ptr == value.data() + value.size()) ? result : defaultValue;
    }

    // Instance IDs remain hexadecimal values even though direction masks now use named REG.* keys.
    bool TryParseHexUInt32(const std::string& value, uint32_t& result) {
        std::string_view text = value;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            text.remove_prefix(2);
        }
        result = 0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), result, 16);
        return ec == std::errc() && ptr == text.data() + text.size();
    }

    std::filesystem::path GetDllDirectoryPath() {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&GetDllDirectoryPath), &module)) {
            return {};
        }
        wchar_t path[MAX_PATH]{};
        if (GetModuleFileNameW(module, path, MAX_PATH) == 0) {
            return {};
        }
        return std::filesystem::path(path).parent_path();
    }

    // UTF-8 text of a path for the log. std::filesystem::path::string() converts to the ANSI code
    // page and throws when a character has no ANSI equivalent (a Windows user name in another
    // script), so it is never used here.
    std::string PathForLog(const std::filesystem::path& path) {
        const std::wstring& wide = path.native();
        if (wide.empty()) {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                             nullptr, 0, nullptr, nullptr);
        std::string result(size > 0 ? static_cast<size_t>(size) : 0, '\0');
        if (size > 0) {
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size,
                                nullptr, nullptr);
        }
        return result;
    }

    // [PowerPoles.<Name>] sections use named REG.* connection combinations. The names below are in
    // engine mask order, so parsing compiles the author-facing key directly into the existing
    // 16-entry lookup table. mINI makes keys case-insensitive. A section may be sparse -- masks it
    // doesn't define fall back to the vanilla instance for that mask at resolve time, not here.
    constexpr std::array<std::string_view, 16> kRegularDirectionKeys = {{
        "reg.none", "reg.x", "reg.dp", "reg.x+dp",
        "reg.z", "reg.x+z", "reg.dp+z", "reg.x+dp+z",
        "reg.dn", "reg.x+dn", "reg.dp+dn", "reg.x+dp+dn",
        "reg.z+dn", "reg.x+z+dn", "reg.dp+z+dn", "reg.x+dp+z+dn",
    }};

    // FAR heading keys, index = ratioIndex * kFarOrientCount + orient (same layout as
    // PoleStyle::farInstanceByHeading and the gFarDragRatioIndex/gFarDragOrient pair). Ratio order
    // matches kFarRatios (1.5, 2, 3, 6); orientation order matches the FarOrient enum (XP,XN,ZP,ZN).
    constexpr std::array<std::string_view, kFarHeadingCount> kFarHeadingKeys = {{
        "far-1.5.xp", "far-1.5.xn", "far-1.5.zp", "far-1.5.zn",
        "far-2.xp",   "far-2.xn",   "far-2.zp",   "far-2.zn",
        "far-3.xp",   "far-3.xn",   "far-3.zp",   "far-3.zn",
        "far-6.xp",   "far-6.xn",   "far-6.zp",   "far-6.zn",
    }};

    // Parses every FAR-<ratio>.<orient> key present in a section into the style's FAR table. Returns
    // the number of valid keys read. Shared by named styles and the [PowerPoles.FAR] default section.
    uint32_t ParseFarHeadingKeys(const mINI::INIMap<std::string>& section, PoleStyle& style,
                                 const std::string& sectionLabel) {
        uint32_t count = 0;
        for (uint32_t i = 0; i < kFarHeadingCount; ++i) {
            const std::string key(kFarHeadingKeys[i]);
            if (!section.has(key)) {
                continue;
            }
            uint32_t instanceId = 0;
            if (!TryParseHexUInt32(section.get(key), instanceId)) {
                LOG_WARN("PowerPoleCustomization: [PowerPoles.{}] {} has invalid hexadecimal instance "
                         "ID \"{}\"; ignoring it.", sectionLabel, key, section.get(key));
                continue;
            }
            style.farInstanceByHeading[i] = instanceId;
            style.hasFarHeading[i] = true;
            ++count;
        }
        return count;
    }

    // Warns about far-* keys that aren't one of the 16 canonical headings (typo/wrong ratio/orient).
    void WarnUnknownFarKeys(const mINI::INIMap<std::string>& section, const std::string& sectionLabel) {
        for (const auto& entry : section) {
            const std::string& key = entry.first;
            if (key.starts_with("far-") &&
                std::ranges::find(kFarHeadingKeys, key) == kFarHeadingKeys.end()) {
                LOG_WARN("PowerPoleCustomization: [PowerPoles.{}] unknown FAR heading key {}; expected "
                         "far-<1.5|2|3|6>.<xp|xn|zp|zn>. Ignoring it.", sectionLabel, key);
            }
        }
    }

    void LoadPoleStyles(const mINI::INIStructure& ini) {
        gStyles.clear();
        gFarDefaultStyle = PoleStyle{};
        gHasFarDefault = false;
        constexpr std::string_view kPrefix = "powerpoles.";
        for (const auto& [sectionName, section] : ini) {
            std::string lowerName = sectionName;
            std::ranges::transform(lowerName, lowerName.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowerName.size() <= kPrefix.size() || lowerName.compare(0, kPrefix.size(), kPrefix) != 0) {
                continue;
            }

            const std::string styleName = sectionName.substr(kPrefix.size());

            // [PowerPoles.FAR] is not a cyclable style: it's the style-independent FAR fallback.
            // Parse only its FAR headings and never push it into gStyles.
            if (lowerName == "powerpoles.far") {
                const uint32_t farCount = ParseFarHeadingKeys(section, gFarDefaultStyle, styleName);
                WarnUnknownFarKeys(section, styleName);
                if (farCount > 0) {
                    gHasFarDefault = true;
                    LOG_INFO("PowerPoleCustomization: loaded [PowerPoles.FAR] default ({} of {} FAR "
                             "headings defined).", farCount, kFarHeadingCount);
                } else {
                    LOG_WARN("PowerPoleCustomization: [PowerPoles.FAR] defines no valid FAR headings; "
                             "ignoring the section.");
                }
                continue;
            }

            PoleStyle style;
            style.name = styleName;
            uint32_t definedCount = 0;
            for (uint32_t mask = 0; mask < 16; ++mask) {
                const std::string key(kRegularDirectionKeys[mask]);
                if (section.has(key)) {
                    uint32_t instanceId = 0;
                    if (!TryParseHexUInt32(section.get(key), instanceId)) {
                        LOG_WARN("PowerPoleCustomization: [PowerPoles.{}] {} has invalid hexadecimal "
                                 "instance ID \"{}\"; ignoring it.", style.name, key, section.get(key));
                        continue;
                    }
                    style.instanceByDirectionMask[mask] = instanceId;
                    style.hasMask[mask] = true;
                    ++definedCount;
                }
            }

            const uint32_t farCount = ParseFarHeadingKeys(section, style, style.name);

            for (const auto& entry : section) {
                const std::string& key = entry.first;
                if (key.starts_with("0x")) {
                    LOG_WARN("PowerPoleCustomization: [PowerPoles.{}] legacy mask key {} is unsupported; "
                             "use a named REG.* key.", style.name, key);
                } else if (key.starts_with("reg.") &&
                           std::ranges::find(kRegularDirectionKeys, key) == kRegularDirectionKeys.end()) {
                    LOG_WARN("PowerPoleCustomization: [PowerPoles.{}] unknown or non-canonical regular "
                             "direction key {}; ignoring it.", style.name, key);
                }
            }
            WarnUnknownFarKeys(section, style.name);
            if (section.has("InterPoleDistance")) {
                const uint32_t d = std::clamp(ParseUInt32(section.get("InterPoleDistance"), 0u), 1u, kMaxInterPoleDistance);
                style.maxCellsBetweenPoles = d;
                style.hasMaxCells = true;
            }

            if (definedCount == 0 && farCount == 0 && !style.hasMaxCells) {
                LOG_WARN("PowerPoleCustomization: [PowerPoles.{}] defines no valid REG.*/FAR.* keys or "
                         "InterPoleDistance; skipping.", style.name);
                continue;
            }
            LOG_INFO("PowerPoleCustomization: loaded pole style \"{}\" ({} of 16 direction masks, {} of {} "
                     "FAR headings defined; rest fall back; InterPoleDistance={}).", style.name, definedCount,
                     farCount, kFarHeadingCount,
                     style.hasMaxCells ? std::to_string(style.maxCellsBetweenPoles) : std::string("vanilla"));
            gStyles.push_back(std::move(style));
        }
    }

    // ------------------------------------------------------------------
    // Pole styles from exemplars (SC4SIM 1.12): a plugin can ship its styles in a .dat instead of
    // asking the player to copy [PowerPoles.*] sections into SC4SIM.ini. One exemplar per style,
    // type 0x6534284A, group kPoleStyleGroup, any instance:
    //   0x00000020 Exemplar Name       string     style name (shown in SC4SIM.log when Tab selects it)
    //   0x5C4DB001 Regular poles       Uint32 x16 pole instance per direction mask, REG.* order
    //                                             (None, X, DP, X+DP, Z, X+Z, ...); 0 = vanilla pole
    //   0x5C4DB002 FAR poles           Uint32 x16 FAR-1.5/2/3/6 x XP/XN/ZP/ZN; 0 = fallback
    //   0x5C4DB003 Inter-pole distance Uint32     max cells between poles, 1-50 (absent = vanilla)
    //   0x5C4DB004 Order               Uint32     position in the Tab cycle (then by instance)
    //   0x5C4DB005 FAR default         Bool       true = the [PowerPoles.FAR] fallback, not a style
    // Read once, at the first PostCityInit (every plugin is loaded by then). The resource manager
    // resolves duplicates like any other exemplar: the plugin loaded last wins for one instance.
    // SC4SIM.ini keeps priority: an ini style with the same name replaces the exemplar style, and
    // the ini [PowerPoles.FAR] keys override the exemplar FAR default heading by heading.
    // ------------------------------------------------------------------
    constexpr uint32_t kExemplarType = 0x6534284A;
    constexpr uint32_t kPoleStyleGroup = 0x5C4DB000;
    constexpr uint32_t kPropExemplarName = 0x00000020;
    constexpr uint32_t kPropStyleRegular = 0x5C4DB001;
    constexpr uint32_t kPropStyleFar = 0x5C4DB002;
    constexpr uint32_t kPropStyleInterPoleDistance = 0x5C4DB003;
    constexpr uint32_t kPropStyleOrder = 0x5C4DB004;
    constexpr uint32_t kPropStyleFarDefault = 0x5C4DB005;
    bool gExemplarStylesLoaded = false;

    // Stack-allocated filter for GetAvailableResourceList: exemplars of the pole-style group.
    class PoleStyleKeyFilter final : public cIGZPersistResourceKeyFilter {
    public:
        bool QueryInterface(const uint32_t riid, void** ppvObj) override {
            if (riid == GZIID_cIGZPersistResourceKeyFilter || riid == GZIID_cIGZUnknown) {
                *ppvObj = static_cast<cIGZPersistResourceKeyFilter*>(this);
                AddRef();
                return true;
            }
            *ppvObj = nullptr;
            return false;
        }
        uint32_t AddRef() override { return ++refCount_; }
        uint32_t Release() override { return refCount_ > 0 ? --refCount_ : 0; } // owned by the caller's stack
        bool IsKeyIncluded(cGZPersistResourceKey const& key) override {
            return key.type == kExemplarType && key.group == kPoleStyleGroup;
        }
    private:
        uint32_t refCount_ = 0;
    };

    // Up to 16 Uint32 values of an array property (Sint32 arrays are accepted too: some editors
    // write instance IDs as signed). Returns how many were read.
    uint32_t ReadUint32Array16(const cISCPropertyHolder* holder, const uint32_t propertyId,
                               std::array<uint32_t, 16>& out, const uint32_t instance) {
        if (holder == nullptr || !holder->HasProperty(propertyId)) {
            return 0;
        }
        const cISCProperty* const prop = holder->GetProperty(propertyId);
        const cIGZVariant* const variant = prop ? prop->GetPropertyValue() : nullptr;
        if (variant == nullptr || (variant->GetType() != cIGZVariant::Uint32Array &&
                                   variant->GetType() != cIGZVariant::Sint32Array)) {
            LOG_WARN("PowerPoleCustomization: pole style exemplar 0x{:08X}: property 0x{:08X} must be a Uint32 "
                     "array; ignoring it.", instance, propertyId);
            return 0;
        }
        const uint32_t count = std::min<uint32_t>(variant->GetCount(), 16);
        if (variant->GetCount() > 16) {
            LOG_WARN("PowerPoleCustomization: pole style exemplar 0x{:08X}: property 0x{:08X} has {} values; only "
                     "the first 16 are used.", instance, propertyId, variant->GetCount());
        }
        const uint32_t* const values = variant->RefUint32();
        if (values == nullptr) {
            return 0;
        }
        std::copy_n(values, count, out.begin());
        return count;
    }

    struct ExemplarStyle {
        PoleStyle style;
        uint32_t order = 0;
        uint32_t instance = 0;
        bool farDefault = false;
    };

    bool ReadStyleExemplar(cIGZPersistResourceManager* resourceManager, const cGZPersistResourceKey& key,
                           ExemplarStyle& out) {
        cRZAutoRefCount<cISCResExemplar> exemplar;
        if (!resourceManager->GetResource(key, GZIID_cISCResExemplar, exemplar.AsPPVoid(), 0, nullptr) || !exemplar) {
            return false;
        }
        const cISCPropertyHolder* const holder = exemplar->AsISCPropertyHolder();
        out = ExemplarStyle{};
        out.instance = key.instance;

        cRZBaseString name;
        if (holder->HasProperty(kPropExemplarName) && holder->GetProperty(kPropExemplarName, name) && name.Strlen() > 0) {
            out.style.name.assign(name.ToChar(), name.Strlen());
        } else {
            out.style.name = std::format("Style 0x{:08X}", key.instance);
        }
        TryReadUint32Property(holder, kPropStyleOrder, out.order);
        TryReadBoolProperty(holder, kPropStyleFarDefault, out.farDefault);

        std::array<uint32_t, 16> regular{};
        const uint32_t regularCount = ReadUint32Array16(holder, kPropStyleRegular, regular, key.instance);
        for (uint32_t mask = 0; mask < regularCount; ++mask) {
            if (regular[mask] != 0) {
                out.style.instanceByDirectionMask[mask] = regular[mask];
                out.style.hasMask[mask] = true;
            }
        }
        std::array<uint32_t, 16> farPoles{}; // not `far`: a Windows macro
        const uint32_t farCount = ReadUint32Array16(holder, kPropStyleFar, farPoles, key.instance);
        for (uint32_t i = 0; i < farCount && i < kFarHeadingCount; ++i) {
            if (farPoles[i] != 0) {
                out.style.farInstanceByHeading[i] = farPoles[i];
                out.style.hasFarHeading[i] = true;
            }
        }
        uint32_t distance = 0;
        if (TryReadUint32Property(holder, kPropStyleInterPoleDistance, distance) && distance > 0) {
            out.style.maxCellsBetweenPoles = std::clamp(distance, 1u, kMaxInterPoleDistance);
            out.style.hasMaxCells = true;
        }
        return true;
    }

    uint32_t CountTrue(const auto& flags) {
        return static_cast<uint32_t>(std::ranges::count(flags, true));
    }

    bool SameStyleName(const std::string& a, const std::string& b) {
        return std::ranges::equal(a, b, [](const unsigned char x, const unsigned char y) {
            return std::tolower(x) == std::tolower(y);
        });
    }

    // Merges the exemplar styles into gStyles / gFarDefaultStyle, which hold the SC4SIM.ini ones.
    // Final Tab order: exemplar styles (by Order, then instance), each replaced in place by an ini
    // style of the same name, then the remaining ini styles in ini order.
    void LoadExemplarPoleStylesOnce() {
        if (gExemplarStylesLoaded || !gSettings.enabled) {
            return;
        }
        gExemplarStylesLoaded = true;

        cIGZPersistResourceManagerPtr resourceManager;
        if (!resourceManager) {
            LOG_WARN("PowerPoleCustomization: no resource manager; pole styles from exemplars not read.");
            return;
        }
        PoleStyleKeyFilter filter;
        cRZAutoRefCount<cIGZPersistResourceKeyList> keys;
        resourceManager->GetAvailableResourceList(keys.AsPPObj(), &filter);
        if (!keys || keys->Size() == 0) {
            return;
        }

        std::vector<ExemplarStyle> found;
        std::vector<uint32_t> seenInstances;
        for (uint32_t i = 0; i < keys->Size(); ++i) {
            const cGZPersistResourceKey& key = keys->GetKey(i);
            if (std::ranges::find(seenInstances, key.instance) != seenInstances.end()) {
                continue; // the same key from several plugins: GetResource returns the winning one
            }
            seenInstances.push_back(key.instance);
            ExemplarStyle entry;
            if (ReadStyleExemplar(resourceManager, key, entry)) {
                found.push_back(std::move(entry));
            }
        }
        std::ranges::sort(found, [](const ExemplarStyle& a, const ExemplarStyle& b) {
            return a.order != b.order ? a.order < b.order : a.instance < b.instance;
        });

        // FAR default: exemplar defaults first (later Order wins per heading), then the ini keys.
        PoleStyle farDefault;
        bool hasFarDefault = false;
        for (const ExemplarStyle& entry : found) {
            if (!entry.farDefault) {
                continue;
            }
            for (uint32_t i = 0; i < kFarHeadingCount; ++i) {
                if (entry.style.hasFarHeading[i]) {
                    farDefault.farInstanceByHeading[i] = entry.style.farInstanceByHeading[i];
                    farDefault.hasFarHeading[i] = true;
                    hasFarDefault = true;
                }
            }
            LOG_INFO("PowerPoleCustomization: FAR default from exemplar 0x{:08X} ({} of {} FAR headings).",
                     entry.instance, CountTrue(entry.style.hasFarHeading), kFarHeadingCount);
        }
        if (gHasFarDefault) {
            for (uint32_t i = 0; i < kFarHeadingCount; ++i) {
                if (gFarDefaultStyle.hasFarHeading[i]) {
                    farDefault.farInstanceByHeading[i] = gFarDefaultStyle.farInstanceByHeading[i];
                    farDefault.hasFarHeading[i] = true;
                    hasFarDefault = true;
                }
            }
        }
        gFarDefaultStyle = farDefault;
        gHasFarDefault = hasFarDefault;

        std::vector<PoleStyle> iniStyles = std::move(gStyles);
        std::vector<bool> iniUsed(iniStyles.size(), false);
        std::vector<PoleStyle> merged;
        uint32_t fromExemplars = 0;
        for (ExemplarStyle& entry : found) {
            if (entry.farDefault) {
                continue;
            }
            const auto iniMatch = std::ranges::find_if(iniStyles, [&entry](const PoleStyle& s) {
                return SameStyleName(s.name, entry.style.name);
            });
            if (iniMatch != iniStyles.end()) {
                iniUsed[static_cast<size_t>(iniMatch - iniStyles.begin())] = true;
                LOG_INFO("PowerPoleCustomization: pole style \"{}\" (exemplar 0x{:08X}) is replaced by the "
                         "[PowerPoles.{}] section of SC4SIM.ini.", entry.style.name, entry.instance, iniMatch->name);
                merged.push_back(*iniMatch);
                continue;
            }
            const uint32_t regularCount = CountTrue(entry.style.hasMask);
            const uint32_t farCount = CountTrue(entry.style.hasFarHeading);
            if (regularCount == 0 && farCount == 0 && !entry.style.hasMaxCells) {
                LOG_WARN("PowerPoleCustomization: pole style exemplar 0x{:08X} (\"{}\") defines no pole, FAR pole "
                         "or inter-pole distance; skipping it.", entry.instance, entry.style.name);
                continue;
            }
            LOG_INFO("PowerPoleCustomization: loaded pole style \"{}\" from exemplar 0x{:08X} ({} of 16 direction "
                     "masks, {} of {} FAR headings; InterPoleDistance={}).", entry.style.name, entry.instance,
                     regularCount, farCount, kFarHeadingCount,
                     entry.style.hasMaxCells ? std::to_string(entry.style.maxCellsBetweenPoles) : std::string("vanilla"));
            merged.push_back(std::move(entry.style));
            ++fromExemplars;
        }
        for (size_t i = 0; i < iniStyles.size(); ++i) {
            if (!iniUsed[i]) {
                merged.push_back(std::move(iniStyles[i]));
            }
        }
        gStyles = std::move(merged);
        if (gActiveStyleIndex > gStyles.size()) {
            gActiveStyleIndex = 0;
        }
        LOG_INFO("PowerPoleCustomization: {} pole style(s) from exemplars, {} in all (Tab / Shift-Tab cycles them).",
                 fromExemplars, gStyles.size());
    }

    void LoadSettings() {
        gSettings = {};
        gStyles.clear();
        gFarDefaultStyle = PoleStyle{};
        gHasFarDefault = false;
        gActiveStyleIndex = 0;
        const auto settingsPath = GetDllDirectoryPath() / L"SC4SIM.ini";
        mINI::INIFile file(settingsPath); // mINI opens the std::filesystem::path itself (wide on Windows)
        mINI::INIStructure ini;
        if (!file.read(ini) || !ini.has("SC4PowerPoleCustomization")) {
            LOG_INFO("PowerPoleCustomization: using default settings; no readable {}.", PathForLog(settingsPath));
            return;
        }

        const auto section = ini.get("SC4PowerPoleCustomization");
        if (section.has("Enabled")) {
            gSettings.enabled = ParseBool(section.get("Enabled"), gSettings.enabled);
        }
        if (section.has("MaxPointsPerDirection")) {
            gSettings.maxPointsPerDirection = std::clamp(
                ParseUInt32(section.get("MaxPointsPerDirection"), gSettings.maxPointsPerDirection), 1u, 15u);
        }

        if (section.has("CrossZones")) {
            gSettings.crossZones = ParseBool(section.get("CrossZones"), gSettings.crossZones);
        }
        if (section.has("DezoneUnderPoles")) {
            gSettings.dezoneUnderPoles = ParseBool(section.get("DezoneUnderPoles"), gSettings.dezoneUnderPoles);
        }
        if (section.has("DezoneFieldCells")) {
            gSettings.dezoneFieldCells = ParseBool(section.get("DezoneFieldCells"), gSettings.dezoneFieldCells);
        }
        if (section.has("DebugLog")) {
            SimLog::SetDebug(ParseBool(section.get("DebugLog"), false));
        }

        LOG_INFO("PowerPoleCustomization: settings enabled={}, maxPointsPerDirection={}, crossZones={}, "
                 "dezoneUnderPoles={}, dezoneFieldCells={}",
                 gSettings.enabled, gSettings.maxPointsPerDirection, gSettings.crossZones,
                 gSettings.dezoneUnderPoles, gSettings.dezoneFieldCells);

        LoadPoleStyles(ini);
    }

    // City teardown: every pole is destroyed (the destructor hook drops each pole's override and
    // private table), the engine's texture registry is torn down by its own static shutdown. Belt and
    // suspenders: drop everything city-scoped here so nothing stale survives into the next city,
    // whatever destruction order the engine picked.
    void FlushCityScopedState() {
        gOverrides.clear();
        ++gOverridesGeneration;
        // gPrivateConnectionTables is NOT cleared: the poles still point at their private tables
        // until the engine destroys them, and the destructor hook frees each one then.
        gRegisteredPoleTextures.clear();
        gFarDragActive = false;
        gFarSnapAnchorValid = false;
        LOG_DEBUG("PowerPoleCustomization: city-scoped state flushed on city shutdown.");
    }
}

// Interdependent patches are installed all-or-nothing: if one member of a group cannot be installed,
// the members that were installed are removed again and the feature stays vanilla. A half-installed
// group is worse than none -- e.g. only one of the two floor-texture sites patched makes Draw compute
// the bucket of one texture ID and compare against another, and a FAR drag without the
// DeterminePolePositions scope places poles off the FAR nodes.
class PatchGroup final {
public:
    explicit PatchGroup(const char* name) : name_(name) {}

    template <typename Patch>
    void Add(const bool installed, Patch& patch) {
        ++total_;
        if (installed) {
            undo_.push_back([&patch] { patch.Uninstall(); });
        } else {
            failed_ = true;
        }
    }

    bool Commit() {
        if (!failed_) {
            return true;
        }
        LOG_ERROR("PowerPoleCustomization: {}: {} of {} patches installed; the feature needs all of them, so none "
                  "is kept.", name_, undo_.size(), total_);
        for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) {
            (*it)();
        }
        undo_.clear();
        return false;
    }

    [[nodiscard]] size_t Total() const noexcept { return total_; }
    [[nodiscard]] size_t Installed() const noexcept { return undo_.size(); }

private:
    const char* name_;
    std::vector<std::function<void()>> undo_;
    size_t total_ = 0;
    bool failed_ = false;
};

// SC4SIM: the former PowerPoleCustomizationDirector, driven by the SC4SIM director (one director per DLL).
class PowerPoleModule final {
public:
    bool PostAppInit(cIGZMessageTarget2* target) {
        const uint16_t version = VersionDetection::GetInstance().GetGameVersion();
        if (version != kSupportedGameVersion) {
            LOG_WARN("PowerPoleCustomization: game version {} unsupported (addresses target {}); not patching.",
                     version, kSupportedGameVersion);
            return true;
        }

        if (GetModuleHandleW(L"SC4PowerPoleCustomizationSample.dll") != nullptr) {
            LOG_WARN("PowerPoleCustomization: SC4PowerPoleCustomizationSample.dll (SC4RenderServices sample) is "
                     "loaded and patches the same code; SC4SIM leaves power poles to it. Remove one of the two.");
            return true;
        }

        size_t installed = 0;
        size_t total = 0;
        const auto commit = [&installed, &total](PatchGroup& group) {
            total += group.Total();
            const bool ok = group.Commit();
            installed += group.Installed();
            return ok;
        };

        // The connection-point table repair is a crash guard, not a feature: it is installed even when the module
        // is disabled, because pole exemplars whose instance bits 8-11 are not 1-4 (the FAR poles, any custom pole)
        // leave the table null and the game reads it without a check. With the module disabled the hook only
        // repairs the table (InitConnectionPointsHook drops every override when gSettings.enabled is false).
        // Each site guards its own path, so they are not a group: every site that installs helps.
        constexpr std::array<const char*, 3> kInitNames = {
            "InitConnectionPoints@Init", "InitConnectionPoints@SetDefaultExemplar", "InitConnectionPoints@Read"};
        constexpr std::array<uintptr_t, 3> kInitSites = {
            kInitConnectionPoints_CallSite_Init, kInitConnectionPoints_CallSite_SetDefaultExemplar,
            kInitConnectionPoints_CallSite_Read};
        for (size_t i = 0; i < kInitSites.size(); ++i) {
            ++total;
            installed += InstallCallSitePatch(initConnectionPointsPatches_[i], kInitNames[i], kInitSites[i],
                                              kInitConnectionPoints, &InitConnectionPointsHook) ? 1 : 0;
        }

        if (!gSettings.enabled) {
            LOG_INFO("PowerPoleCustomization: disabled via ini; only the connection-point table guard is installed "
                     "({} of 3 sites).", installed);
            return true;
        }

        PatchGroup modelGroup("model quarter-turn");
        modelGroup.Add(InstallCallSitePatch(modelInstanceIdPatch_, "GetModelInstanceID@LoadModel",
                                            kGetModelInstanceID_CallSite_LoadModel, kGetModelInstanceID,
                                            &GetModelInstanceIDHook), modelInstanceIdPatch_);
        commit(modelGroup);

        // Everything that reads gOverrides needs the destructor hook: the engine recycles pole
        // addresses from a fixed pool, so without it a new pole could inherit a dead pole's override.
        PatchGroup lifetimeGroup("pole lifetime (destructor)");
        lifetimeGroup.Add(InstallCallSitePatch(destructorPatch_, "Destructor@DeletingDtor",
                                               kDestructor_CallSite_DeletingDtor, kDestructor, &DestructorHook),
                          destructorPatch_);
        gOverridesAllowed = commit(lifetimeGroup);
        if (!gOverridesAllowed) {
            LOG_ERROR("PowerPoleCustomization: without the destructor hook no pole customization is applied "
                      "(attach points, wire appearance, foundation pads).");
        } else {
            PatchGroup connectionGroup("custom wires (AddConnection/UpdateConnection)");
            for (size_t i = 0; i < kAddConnectionCallSites.size(); ++i) {
                connectionGroup.Add(InstallCallSitePatch(addConnectionPatches_[i], "AddConnection@PlacePoles",
                                                         kAddConnectionCallSites[i], kAddConnection,
                                                         &AddConnectionHook), addConnectionPatches_[i]);
            }
            for (size_t i = 0; i < kUpdateConnectionCallSites.size(); ++i) {
                connectionGroup.Add(InstallCallSitePatch(updateConnectionPatches_[i],
                                                         "UpdateConnection@SetDefaultExemplar",
                                                         kUpdateConnectionCallSites[i], kUpdateConnection,
                                                         &UpdateConnectionHook), updateConnectionPatches_[i]);
            }
            commit(connectionGroup);

            // Each draw-time lookup patches one site and resolves its value by itself.
            PatchGroup widthGroup("per-wire width");
            widthGroup.Add(wireWidthLookupPatch_.Install(), wireWidthLookupPatch_);
            commit(widthGroup);
            PatchGroup strandGroup("wire strand texture");
            strandGroup.Add(wireStrandTexturePatch_.Install(kWireStrandTextureSite, kVanillaWireStrandTextureId,
                                                            &WireStrandTextureHook, "WireStrandTexture",
                                                            kMovEsiOpcode), wireStrandTexturePatch_);
            commit(strandGroup);
            PatchGroup buoyTextureGroup("wire buoy texture");
            buoyTextureGroup.Add(wireBuoyTexturePatch_.Install(kWireBuoyTextureSite, kVanillaWireBuoyTextureId,
                                                               &WireBuoyTextureHook, "WireBuoyTexture",
                                                               kMovEsiOpcode), wireBuoyTexturePatch_);
            commit(buoyTextureGroup);
            PatchGroup buoySizeGroup("water buoy size");
            buoySizeGroup.Add(waterBallSizePatch_.Install(kWaterBallSizeSite,
                                  {0xa1, 0x98, 0x67, 0xb4, 0x00, 0xd9, 0x04, 0x90, 0x8d, 0x04, 0x90},
                                  &WaterBallSizeHook, "WaterBallSize"), waterBallSizePatch_);
            commit(buoySizeGroup);

            // The three CreateFloor callers rebuild the same pad: all of them, or the pad reverts on some paths.
            PatchGroup floorGroup("foundation pad (CreateFloor)");
            floorGroup.Add(InstallCallSitePatch(createFloorPatches_[0], "CreateFloor@SetPosition",
                                                kCreateFloor_CallSite_SetPosition, kCreateFloor, &CreateFloorHook),
                           createFloorPatches_[0]);
            floorGroup.Add(InstallCallSitePatch(createFloorPatches_[1], "CreateFloor@Draw",
                                                kCreateFloor_CallSite_Draw, kCreateFloor, &CreateFloorHook),
                           createFloorPatches_[1]);
            floorGroup.Add(InstallCallSitePatch(createFloorPatches_[2], "CreateFloor@ChangeZoomLevel",
                                                kCreateFloor_CallSite_ChangeZoomLevel, kCreateFloor,
                                                &CreateFloorHook), createFloorPatches_[2]);
            const bool floorInstalled = commit(floorGroup);

            // Both sites of one texture feed the same bucket walk (index and compare): never one of them alone.
            PatchGroup floorTextureGroup("foundation floor texture");
            floorTextureGroup.Add(floorTextureHashPatch1_.Install(kFloorTextureHashSite1, kVanillaFloorTextureId,
                                                                  &FloorTextureHashHook, "FloorTextureHash@1"),
                                  floorTextureHashPatch1_);
            floorTextureGroup.Add(floorTextureHashPatch2_.Install(kFloorTextureHashSite2, kVanillaFloorTextureId,
                                                                  &FloorTextureHashHook, "FloorTextureHash@2"),
                                  floorTextureHashPatch2_);
            commit(floorTextureGroup);
            PatchGroup wallTextureGroup("foundation wall texture");
            wallTextureGroup.Add(wallTextureHashPatch1_.Install(kWallTextureHashSite1, kVanillaWallTextureId,
                                                                &WallTextureHashHook, "WallTextureHash@1"),
                                 wallTextureHashPatch1_);
            wallTextureGroup.Add(wallTextureHashPatch2_.Install(kWallTextureHashSite2, kVanillaWallTextureId,
                                                                &WallTextureHashHook, "WallTextureHash@2"),
                                 wallTextureHashPatch2_);
            commit(wallTextureGroup);

            // Re-aligns follow-span pads when a connection goes away; only meaningful with the pad hooks.
            if (floorInstalled) {
                PatchGroup removeGroup("follow-span pad on RemoveConnection");
                for (size_t i = 0; i < kRemoveConnectionCallSites.size(); ++i) {
                    removeGroup.Add(InstallCallSitePatch(removeConnectionPatches_[i], "RemoveConnection",
                                                         kRemoveConnectionCallSites[i], kRemoveConnection,
                                                         &RemoveConnectionHook), removeConnectionPatches_[i]);
                }
                commit(removeGroup);
            }
        }

        PatchGroup styleGroup("pole styles (CreatePowerPole lookups)");
        styleGroup.Add(poleStyleLookupPatch1_.Install(kPoleStyleLookupSite1,
                           {0x8b, 0x04, 0xbd, 0xb0, 0x67, 0xb4, 0x00}, &PoleStyleLookupHook, "PoleStyleLookup@1"),
                       poleStyleLookupPatch1_);
        styleGroup.Add(poleStyleLookupPatch2_.Install(kPoleStyleLookupSite2,
                           {0x8b, 0x04, 0xbd, 0xb0, 0x67, 0xb4, 0x00}, &PoleStyleLookupHook, "PoleStyleLookup@2"),
                       poleStyleLookupPatch2_);
        const bool stylesInstalled = commit(styleGroup);

        PatchGroup spacingGroup("pole spacing (DeterminePolePositions)");
        for (size_t i = 0; i < kDeterminePolePositionsCallSites.size(); ++i) {
            spacingGroup.Add(InstallCallSitePatch(determinePolePositionsPatches_[i], "DeterminePolePositions",
                                                  kDeterminePolePositionsCallSites[i], kDeterminePolePositions,
                                                  &DeterminePolePositionsHook), determinePolePositionsPatches_[i]);
        }
        const bool spacingInstalled = commit(spacingGroup);

        // FAR drag (docs SS17): a vtable-slot swap, valid only together with the spacing scope above.
        bool farInstalled = false;
        if (spacingInstalled) {
            PatchGroup farGroup("FAR drag");
            farGroup.Add(farDrawNetworkLinePatch_.Install(kPowerToolDrawNetworkLineSlot, kDrawNetworkLine,
                                                          &FarDrawNetworkLineHook, "FarDrawNetworkLine"),
                         farDrawNetworkLinePatch_);
            farInstalled = commit(farGroup);
        }

        PatchGroup keepWiresGroup("keepwires");
        keepWiresGroup.Add(keepWiresPatch_.Install(kUpdateOnZoneChange, {0x83, 0xec, 0x68, 0x8b, 0x44, 0x24, 0x7c},
                                                   &KeepWiresOnZoneChangeHook, "KeepWiresOnZoneChange"),
                           keepWiresPatch_);
        commit(keepWiresGroup);

        // SC4SIM: power lines across zones -- the two hooked callers and the zoned branch they arm.
        PatchGroup zonesGroup("power lines across zones");
        zonesGroup.Add(cellZonedBranchPatch_.Install(kCellZonedBranch,
                           {0xc7, 0x44, 0x24, 0x14, 0x01, 0x00, 0x00, 0x00, 0xe9, 0xb7, 0x00, 0x00, 0x00},
                           &CellZonedBranchHook, "CellIsOKForPlacement zoned branch", kJmpRel32),
                       cellZonedBranchPatch_);
        for (size_t i = 0; i < kCellIsOKForPlacementCallSites.size(); ++i) {
            zonesGroup.Add(InstallCallSitePatch(cellIsOKForPlacementPatches_[i], "CellIsOKForPlacement",
                                                kCellIsOKForPlacementCallSites[i], kCellIsOKForPlacement,
                                                &CellIsOKForPlacementHook), cellIsOKForPlacementPatches_[i]);
        }
        commit(zonesGroup);

        // SC4SIM: dezoning under new poles -- recorded by PlacePoles, applied when the drag ends.
        PatchGroup dezoneGroup("dezone under new poles");
        for (size_t i = 0; i < kPlacePolesCallSites.size(); ++i) {
            dezoneGroup.Add(InstallCallSitePatch(placePolesPatches_[i], "PlacePoles", kPlacePolesCallSites[i],
                                                 kPlacePoles, &PlacePolesHook), placePolesPatches_[i]);
        }
        dezoneGroup.Add(onMouseDownLPatch_.Install(kOnMouseDownLSlot, kOnMouseDownL, &OnMouseDownLHook,
                                                   "OnMouseDownL@NetworkToolInputControl"), onMouseDownLPatch_);
        dezoneGroup.Add(onMouseUpLPatch_.Install(kOnMouseUpLSlot, kOnMouseUpL, &OnMouseUpLHook,
                                                 "OnMouseUpL@NetworkToolInputControl"), onMouseUpLPatch_);
        commit(dezoneGroup);

        // Tab / Shift-Tab pole-style switch: useless without the style lookups.
        bool keyHookInstalled = false;
        if (stylesInstalled) {
            PatchGroup keyGroup("Tab style switch");
            keyGroup.Add(onKeyDownPatch_.Install(kOnKeyDownSlot, kOnKeyDown, &OnKeyDownHook,
                                                 "OnKeyDown@NetworkToolInputControl"), onKeyDownPatch_);
            keyHookInstalled = commit(keyGroup);
        }
        styleUiValidated_ = keyHookInstalled && farInstalled;

        LOG_INFO("PowerPoleCustomization: installed {} of {} patches.", installed, total);

        // SC4SIM: the SC4SIM director subscribes once to PostCityInit, PreCityShutdown and to the
        // cheat-code notifications for all its modules and forwards them to DoMessage.
        (void)target;

        const cISC4AppPtr app;
        if (app) {
            if (auto* const cheats = app->GetCheatCodeManager()) {
                if (cheats->RegisterCheatCode(kCheatKeepWires, cRZBaseString("keepwires"))) {
                    cheatManager_ = cheats;
                    LOG_INFO("PowerPoleCustomization: cheat code registered (keepwires) -- toggles "
                             "suppression of power-line rerouting on zone changes.");
                } else {
                    LOG_WARN("PowerPoleCustomization: failed to register cheat code keepwires.");
                }
            }
        }

        if (!styleUiValidated_) {
            LOG_INFO("PowerPoleCustomization: power-tool vtables not confirmed on this binary; Tab style switching inactive.");
        }
        return true;
    }

    bool PostAppShutdown(cIGZMessageTarget2* target) {
        (void)target;
        if (cheatManager_) {
            cheatManager_->UnregisterCheatCode(kCheatKeepWires);
            cheatManager_.Reset();
        }
        onKeyDownPatch_.Uninstall();
        onMouseDownLPatch_.Uninstall();
        onMouseUpLPatch_.Uninstall();
        gPendingPoleCells.clear();
        farDrawNetworkLinePatch_.Uninstall();
        keepWiresPatch_.Uninstall();
        modelInstanceIdPatch_.Uninstall();
        destructorPatch_.Uninstall();
        for (auto& patch : initConnectionPointsPatches_) patch.Uninstall();
        for (auto& patch : addConnectionPatches_) patch.Uninstall();
        for (auto& patch : updateConnectionPatches_) patch.Uninstall();
        for (auto& patch : removeConnectionPatches_) patch.Uninstall();
        for (auto& patch : determinePolePositionsPatches_) patch.Uninstall();
        for (auto& patch : cellIsOKForPlacementPatches_) patch.Uninstall();
        cellZonedBranchPatch_.Uninstall();
        for (auto& patch : placePolesPatches_) patch.Uninstall();
        wireWidthLookupPatch_.Uninstall();
        for (auto& patch : createFloorPatches_) patch.Uninstall();
        floorTextureHashPatch1_.Uninstall();
        floorTextureHashPatch2_.Uninstall();
        wallTextureHashPatch1_.Uninstall();
        wallTextureHashPatch2_.Uninstall();
        wireStrandTexturePatch_.Uninstall();
        wireBuoyTexturePatch_.Uninstall();
        waterBallSizePatch_.Uninstall();
        poleStyleLookupPatch1_.Uninstall();
        poleStyleLookupPatch2_.Uninstall();
        gOverrides.clear();
        ++gOverridesGeneration;
        gOverridesAllowed = false;
        return true;
    }

    bool DoMessage(cIGZMessage2* pMsg) {
        if (pMsg) {
            auto* const stdMsg = static_cast<cIGZMessage2Standard*>(pMsg);
            if (stdMsg->GetType() == kSC4MessagePostCityInit) {
                gCityShuttingDown = false;
                LoadExemplarPoleStylesOnce();
            } else if (stdMsg->GetType() == kSC4MessagePreCityShutdown) {
                gCityShuttingDown = true;
                FlushCityScopedState();
            } else if (stdMsg->GetType() == kCheatCodeMessageType &&
                       static_cast<uint32_t>(stdMsg->GetData1()) == kCheatKeepWires) {
                gKeepWiresOnZoneChange = !gKeepWiresOnZoneChange;
                LOG_INFO("PowerPoleCustomization: keepwires {} -- zone changes {} "
                         "the power-line remove/reposition/rebuild handler.",
                         gKeepWiresOnZoneChange ? "ENABLED" : "disabled",
                         gKeepWiresOnZoneChange ? "will bypass" : "will run");
            }
        }
        return true;
    }

private:
    // The call site must target expectedTarget; RelativeCallPatch checks it before writing anything.
    static bool InstallCallSitePatch(PowerPoles::RelativeCallPatch& patch, const char* name,
                                     const uintptr_t callSite, const uintptr_t expectedTarget, void* hookFn) {
        patch.Configure(name, callSite, hookFn, expectedTarget);
        if (!patch.Install()) {
            LOG_ERROR("PowerPoleCustomization: failed to install {} at 0x{:08X}.", name, static_cast<uint32_t>(callSite));
            return false;
        }
        return true;
    }

    std::array<PowerPoles::RelativeCallPatch, 3> initConnectionPointsPatches_{};
    PowerPoles::RelativeCallPatch modelInstanceIdPatch_{};
    PowerPoles::RelativeCallPatch destructorPatch_{}; // override/table cleanup on pole destruction
    std::array<PowerPoles::RelativeCallPatch, 4> addConnectionPatches_{};
    std::array<PowerPoles::RelativeCallPatch, 2> updateConnectionPatches_{};
    std::array<PowerPoles::RelativeCallPatch, 6> removeConnectionPatches_{};
    std::array<PowerPoles::RelativeCallPatch, 3> determinePolePositionsPatches_{};
    std::array<PowerPoles::RelativeCallPatch, 2> cellIsOKForPlacementPatches_{}; // SC4SIM: zones / farm fields
    ByteSpanCallPatch cellZonedBranchPatch_{};                                    // SC4SIM: zoned-cell checks
    std::array<PowerPoles::RelativeCallPatch, 2> placePolesPatches_{};           // SC4SIM: dezone under new poles
    std::array<PowerPoles::RelativeCallPatch, 3> createFloorPatches_{};
    Imm32CallPatch floorTextureHashPatch1_{};
    Imm32CallPatch floorTextureHashPatch2_{};
    Imm32CallPatch wallTextureHashPatch1_{};
    Imm32CallPatch wallTextureHashPatch2_{};
    Imm32CallPatch wireStrandTexturePatch_{}; // DrawPowerlines strand texture (MOV ESI site)
    Imm32CallPatch wireBuoyTexturePatch_{};   // DrawPowerlines water-crossing texture (MOV ESI site)
    ByteSpanCallPatch waterBallSizePatch_{};  // DrawPowerlines buoy-ball size read (11-byte site)
    ByteSpanCallPatch poleStyleLookupPatch1_{};
    ByteSpanCallPatch poleStyleLookupPatch2_{};
    ByteSpanCallPatch keepWiresPatch_{};
    InlineCallPatch wireWidthLookupPatch_{};
    VTableSlotPatch farDrawNetworkLinePatch_{}; // automatic FAR snapping; hold Shift for regular-only
    VTableSlotPatch onKeyDownPatch_{};         // Tab/Shift-Tab pole-style switch
    VTableSlotPatch onMouseDownLPatch_{};      // SC4SIM: drag start (forget pending pole cells)
    VTableSlotPatch onMouseUpLPatch_{};        // SC4SIM: drag end (dezone pending pole cells)
    bool styleUiValidated_ = false;            // OnKeyDown + FAR vtable slots byte-confirmed at install
    cRZAutoRefCount<cIGZCheatCodeManager> cheatManager_; // holds the keepwires cheat registration
};

PowerPoleModule sModule;

void PowerPoleCustomization::OnStart() {
    try {
        LoadSettings();
    } catch (const std::exception& e) {
        // Nothing may escape into the game's DLL loader: keep the defaults and say why.
        gSettings = {};
        LOG_ERROR("PowerPoleCustomization: could not read the settings ({}); using the defaults.", e.what());
    }
}

bool PowerPoleCustomization::PostAppInit(cIGZMessageTarget2* target) {
    return sModule.PostAppInit(target);
}

void PowerPoleCustomization::PostAppShutdown(cIGZMessageTarget2* target) {
    sModule.PostAppShutdown(target);
}

void PowerPoleCustomization::DoMessage(cIGZMessage2* message) {
    sModule.DoMessage(message);
}

// ------------------------------------------------------------------
// Current verification state:
//   1. CLOSED: all three InitConnectionPoints call sites are hooked, including 0x0064ceaa inside
//      cSC4PowerPoleOccupant::Init, the new-pole creation path.
//   2. CLOSED statically, runtime validation pending: public cISCExemplarPropertyHolder access is
//      confirmed through cSC4PowerPoleOccupant::QueryInterface and replaces the old nullptr stub.
//   3. CLOSED: Get0To3Direction confirmed at 0x0061f4c0. ApplyConnectionCustomization computes real
//      direction and strand counts.
//   4. CLOSED (superseded): cSC4PowerLineOccupant's vtable was fully mapped and used to register
//      extra strands as city objects (see docs SS9/SS10). Confirmed in-game (SS13) that this alone
//      doesn't render anything -- removed from the render path. The vtable mapping itself remains
//      documented in docs/sc4-powerline-tool-re.md if city-object/save-load registration is wanted
//      again later; the code for it was deleted from this file (kept unused code out).
//   5. CLOSED (validated in-game 2026-07-19): full 1-N geometry and sag rebuild in the active
//      tConnection regular-polyline vector, including counts below vanilla's fixed four.
//   6. CLOSED (validated in-game 2026-07-19): per-wire width lookup at 0x0064b855. The patch is
//      byte-validated before installation. Since 2026-11 width, wire/buoy textures and buoy size
//      are resolved at draw time from (pole, connection, wire index): they survive save/load and
//      RemoveConnection, which the former address-keyed tables did not.
//   7. CLOSED (2026-07-19): pole lifetime. The complete destructor (0x0064dfc0) is hooked at its
//      sole call site inside the scalar deleting destructor (0x0064e383), so overrides and private
//      connection-point tables die with their pole; a PreCityShutdown flush clears the overrides.
//      No override is made when this hook cannot be installed.
//   8. CLOSED (2026-07-19): custom foundation texture IDs are validated against the resource
//      manager and registered with the engine's texture registry before use (StaticInit only
//      registers the four vanilla IDs; an unregistered ID null-derefs in Draw).
// ------------------------------------------------------------------
