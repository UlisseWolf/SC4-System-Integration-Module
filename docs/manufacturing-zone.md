# Manufacturing (I-M) zone

> **Experimental, off by default.** Compiled, checked against the 1.1.641 executable and emulated, not yet tested
> in game. Test it on a copy of a
> region, and read [Saved cities](#saved-cities) first.

SC4SIM 1.8.0 can give manufacturing industry (I-M) a zone of its own, as wanted by the Industrial Revolution Mod
(IRM) concept. This page explains the game rules it changes, how each hook is made safe, and what still has to be
checked in game. The rules come from the SimCity 4 matching decompilation notes (*SimCity 4 industrial zones: the
rules in the code, and where an IM zone would hook*, 2026-10-27, SimCity 4 1.1.641 Windows).

## The problem

To the growth code, I-D, I-M and I-HT are the same: all three industrial developers ask for zone 8 (medium density),
and a zone-8 building may also grow in zone 9 (high density). There is no free zone type: the game has a fixed list of
16, and every table and the save file are sized for 16. So the new zone **reuses a slot**:

| Slot | Zone | Use as I-M zone |
| --- | --- | --- |
| **13** | Spaceport | Default. The shipped game does not appear to place it; check in your regions. |
| 10 | Military | Possible, if nothing (for example a military base deal) places Military zones in your regions. |
| 11, 12 | Airport, Seaport | Accepted with a warning: the game uses them, expect conflicts. |

## What the DLL changes

| # | Function (1.1.641) | Change | How it is hooked |
| --- | --- | --- | --- |
| 1 | `cSC4GrowthDeveloper::GetConstructionInfo` 0x006B38E0 | The I-M developer (purpose 7) asks for the slot instead of zone 8. | Vtable entry of the growth developer. |
| 2 | `cSC4BuildingDevelopmentSimulator::GrowthStageFitsInZone` 0x00695340 | Every stage fits the slot (the game says "never" for slots 10-15: every lot would fail with reason 6, WrongZone). | Entry jump to a replacement. |
| 3 | `cSC4BuildingDevelopmentSimulator::IsSuitableForConstruction` 0x00695430 | No land-value wealth test in the slot, as the game does in zones 7, 8 and 9. | Not hooked: the redirect (1) gives the I-M construction profile the land value range 0-255 (see below). |
| 4 | `cSC4ZoneManager::IsRCIZone` 0x004DC300 | True for the slot. | Entry jump to a replacement. |
| 5 | `cSC4ZoneManager::PlaceZone` 0x004DD780 | With the `IMZone` brush on, the Dense Industrial zone tool places the slot. | Vtable entry of the zone manager. |

`BuildingFitsInZone` (0x00695390) needs no change: a building always fits its own zone, so I-M buildings grow in the
slot, I-D and I-HT (still asking for zone 8) never fit it, and I-M no longer grows in zones 8 and 9. **I-M becomes
exclusive to its zone.** Letting I-M also grow in zones 8 and 9 would need a `BuildingFitsInZone` hook as well; it
is not part of this version.

## Checked against the executable

`SimCity 4.exe` 1.1.641 (FileVersion 1.1.641.0, SHA-256
`1189720d5e15b0e172d07fe7d351d6b78394da138b1bb4f81bf2a904c8da0f9c`), disassembled, and the rule functions run in an
x86 emulator:

| Function | What the executable shows |
| --- | --- |
| `GetConstructionInfo` 0x006B38E0 | `__thiscall`, one argument. Slot 9 of the growth developer vtable 0x00AB1488; no direct callers. Writes the zone at profile **+0x00** (R 1, C 4; industry by purpose: 5 → 7, 6, 7, 8 → 8), and the land value range bytes at +0x10 / +0x11 from the developer. The tract developer calls it through slot 9 (`call [edx+0x24]`) right before each of its `BuildingFitsInZone` calls, so the redirect reaches the whole growth path, every time (nothing is cached). |
| `BuildingFitsInZone` 0x00695390 | `__cdecl (buildingZone, cellZone)`, 7 callers, all comparing the profile zone with `GetZoneType`. Same zone is always true; 13 takes nothing else. Not patched. |
| `GrowthStageFitsInZone` 0x00695340 | **`__cdecl (stage, zone)`**, stage compared as a signed int. 3 direct callers (0x007084F4, 0x00708FEA, 0x007098BC) in the tract developer, with the lot configuration's stage (a byte) and the cell's zone. Emulated over zones 0-15 and stages 0-255: identical to the notes. |
| `IsSuitableForConstruction` 0x00695430 | Checks, in order: `BuildingFitsInZone(profile zone, cell zone)`, a lot (reason 3), power (reason 5), land value, then a lot state test (reason 4). Land value: with range (0, 0) the land value type must equal the wealth (reason 2) unless the profile zone is 7, 8 or 9; with a range, the land value (a byte) must lie in it (reason 1). Its reason codes are its own (0-5), not `cISC4TractDeveloper::Outcome`. |
| `IsRCIZone` 0x004DC300 | `__thiscall (zone)`, `this` unused, true for 1-9. Slot 13 of the zone manager vtable 0x00A92E00; no direct callers. |
| `PlaceZone` 0x004DD780 | `__thiscall`, 10 arguments (`ret 0x28`). Slot 11 of the zone manager vtable; no direct callers, so the vtable hook sees every zoning. It refuses zone types of 16 or more only. |

The emulation also ran the module's probe logic: it detects `__cdecl` and the order (stage, zone) for
`GrowthStageFitsInZone`, 4 bytes popped for `IsRCIZone`, and the zone at +0x00 (one dword) in the profile.

**Land value.** Because land value is not the last check, overriding a failed result would skip the lot state test.
The module instead gives the I-M profile the range 0-255 when it has none, so `IsSuitableForConstruction` takes the
range branch, which every land value passes, and still runs the lot state test.

## Safety at run time

The decompilation notes give addresses and rules, not instruction bytes or signatures, so the module measures what it
needs at run time and installs nothing it cannot confirm:

- **Virtual functions** (1, 5) are found by scanning the object's vtable for the address of the notes. If the address
  is not there (another game version, another DLL already hooking it), the hook is not installed.
- **Pure functions** (2, 4) are called before patching for every input they can receive. The probe compares the
  answers with the rules of the notes (zones 1 and 4 up to stage 3, zones 2 and 5 up to stage 6, zones 3, 6, 7, 8 and
  9 any stage, other zones never; IsRCIZone true for 1-9). It also measures how many bytes the callee pops, so the
  calling convention (`__thiscall`/`__stdcall` or `__cdecl`) and the argument order of `GrowthStageFitsInZone` are
  measured, not assumed. The replacement answers from the recorded table and only adds the slot, so every other zone
  behaves exactly as the original. Any mismatch: nothing is patched, and the log says why.
- **The zone field of the construction profile** is found by calling the original `GetConstructionInfo` of R$, CS$,
  I-A, I-D, I-HT and I-M, whose zones the notes give (1, 4, 7, 8, 8, 8). Exactly one field must match.
- **Dependencies.** The I-M redirect (1) is installed only together with the stage hook (2), because without it
  every I-M lot in the slot would fail and I-M would grow nowhere. Hooks 4 and 5 and the land value
  range are applied only with the redirect. Without the redirect, the game is unchanged.
- The brush hook checks that slot 11 of the zone manager still points to 0x004DD780.
- Every hook is removed when the game closes.

The hooks are installed when the **first city opens** (they need the city's zone manager and building development
simulator); one install serves every later city.

## Using it

1. In `SC4SIM.ini`, set `[ManufacturingZone] Enabled=true` (and `ZoneSlot` if you do not want 13).
2. Open a city and check `SC4SIM.log`. It should report the measured convention of `GrowthStageFitsInZone`, the
   construction-profile offset, and a line like
   `ManufacturingZone: zone slot 13. I-M redirect on, stage hook on, IsRCIZone on, land value range 0-255, IMZone brush on.`
3. Place the zone:
   - **`IMZone`** cheat (Ctrl+X): toggles the brush. While it is on, the **Dense Industrial** zone tool places the I-M
     zone. Type it again to turn it off. It turns off when the city closes.
   - **`IMZoneConvert`** cheat: turns every *empty* dense industrial cell of the city (no lot) into the I-M zone. Use it
     if the brush does not work: draw dense industry, then convert it.
4. **`IMZoneInfo`** cheat: writes the status and counters to the log: I-M construction profiles redirected, stage
   checks passed in the slot, brush placements, and the slot's cell count, zone size and drag colour.

## Data the DLL does not provide

- **Zone tuning exemplar** (type 0x6534284A, group 0xE7E2C2DB, instance 0xE9482490): the slot's minimum and maximum
  zone size, costs, zone texture and drag colour. Without a modded entry the slot keeps the Spaceport (or Military)
  values, so the brush may be refused (zone size) or show an unexpected colour. `IMZoneConvert` skips the zone size
  check. The exemplar holds 16-entry arrays for all zones, so an override must carry every original value.
- **Lots.** I-M lots may list the slot in their Zone Types (0x88EDC793). The notes have not found where the growth path
  reads this list; if it does, IRM's I-M lots need the slot added. IRM's files cannot be redistributed modified: this
  needs IRM's permission or a tool that edits the player's own copy.

## Saved cities

The slot is stored in the save as an ordinary zone type. A city with I-M zones, opened **without** the DLL (or with
`Enabled=false`), sees those cells as Spaceport (or Military): I-M buildings there may decline or be demolished, and
nothing grows. Keep a copy of the region before trying it.

## Open points for the in-game test

- **Other code that switches on the zone type** (step 8 of the notes): crime lot categories
  (`cSC4CrimeSimulator::GetLotBits` 0x0069F850), lot base textures and lot updates (0x006CA8A0, 0x006CD3A0), traffic
  volume (0x006C27E0) and the daily pollution sum (0x006E7FD0). The slot may behave there like Spaceport, not like
  industry: check pollution, freight trips and crime around I-M lots.
- **`IsRCIZone` callers** have not all been traced: watch the RCI demand, data views and abandonment.
- **The zone tool's interface** is not mapped, so there is no I-M button; the brush borrows the Dense Industrial tool.
- The land value range 0-255 is set only when the I-M developer has no range of its own. Whether other code reads
  the profile's range (for example to pick lots) has not been traced: if I-M picks odd lots, try
  `IgnoreLandValue=false`.
- **Mixed occupancy.** A mixed building whose primary purpose is Manufacturing also asks for the I-M zone.

## Settings

```ini
[ManufacturingZone]
Enabled=false        ; experimental: off by default
ZoneSlot=13          ; 13 Spaceport (default) or 10 Military
StageHook=true       ; required for the redirect: without it the module changes nothing
RCIZoneHook=true     ; IsRCIZone true for the slot
IgnoreLandValue=true ; no land-value wealth test in the slot, as in zones 7-9
```
