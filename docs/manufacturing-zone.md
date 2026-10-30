# Manufacturing (I-M) zone

> **Experimental, off by default.** Compiled and statically checked, not yet tested in game. Test it on a copy of a
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
| 3 | `cSC4BuildingDevelopmentSimulator::IsSuitableForConstruction` 0x00695430 | When only the land-value wealth test failed (reason 22, the last test) in the slot, the building is suitable, as the game does in zones 7, 8 and 9. | Vtable entry of the building development simulator. |
| 4 | `cSC4ZoneManager::IsRCIZone` 0x004DC300 | True for the slot. | Entry jump to a replacement. |
| 5 | `cISC4ZoneManager::PlaceZone` | With the `IMZone` brush on, the Dense Industrial zone tool places the slot. | Vtable entry of the zone manager. |

`BuildingFitsInZone` (0x00695390) needs no change: a building always fits its own zone, so I-M buildings grow in the
slot, I-D and I-HT (still asking for zone 8) never fit it, and I-M no longer grows in zones 8 and 9. **I-M becomes
exclusive to its zone.** Letting I-M also grow in zones 8 and 9 would need a `BuildingFitsInZone` hook as well; it
is not part of this version.

## Safety without the game's bytes

The decompilation notes give addresses and rules, not instruction bytes or signatures, so the module measures what it
needs at run time and installs nothing it cannot confirm:

- **Virtual functions** (1, 3) are found by scanning the object's vtable for the address of the notes. If the address
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
  every I-M lot in the slot would fail and I-M would grow nowhere. Hooks 3, 4 and 5 are installed only with the
  redirect. Without the redirect, the game is unchanged.
- Every hook is removed when the game closes.

The hooks are installed when the **first city opens** (they need the city's zone manager and building development
simulator); one install serves every later city.

## Using it

1. In `SC4SIM.ini`, set `[ManufacturingZone] Enabled=true` (and `ZoneSlot` if you do not want 13).
2. Open a city and check `SC4SIM.log`. It should report the measured convention of `GrowthStageFitsInZone`, the
   construction-profile offset, and a line like
   `ManufacturingZone: zone slot 13. I-M redirect on, stage hook on, IsRCIZone on, land value skip on, IMZone brush on.`
3. Place the zone:
   - **`IMZone`** cheat (Ctrl+X): toggles the brush. While it is on, the **Dense Industrial** zone tool places the I-M
     zone. Type it again to turn it off. It turns off when the city closes.
   - **`IMZoneConvert`** cheat: turns every *empty* dense industrial cell of the city (no lot) into the I-M zone. Use it
     if the brush does not work: draw dense industry, then convert it.
4. **`IMZoneInfo`** cheat: writes the status and counters to the log: I-M construction profiles redirected, stage
   checks passed in the slot, land-value skips, brush placements, and the slot's cell count, zone size and drag colour.

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

- **First city after starting the game.** If a growth developer caches its construction profile when the city loads,
  the first city opened after starting the game may not see the redirect (the hooks are installed after the city has
  loaded). `IMZoneInfo` shows it: if "I-M construction profiles redirected" stays 0 while I-M demand is strong, save,
  reopen the city and look again.
- **Other code that switches on the zone type** (step 8 of the notes): crime lot categories
  (`cSC4CrimeSimulator::GetLotBits` 0x0069F850), lot base textures and lot updates (0x006CA8A0, 0x006CD3A0), traffic
  volume (0x006C27E0) and the daily pollution sum (0x006E7FD0). The slot may behave there like Spaceport, not like
  industry: check pollution, freight trips and crime around I-M lots.
- **`IsRCIZone` callers** have not all been traced: watch the RCI demand, data views and abandonment.
- **The zone tool's interface** is not mapped, so there is no I-M button; the brush borrows the Dense Industrial tool.
- The land-value skip assumes the reason code is 22 (`WrongLandValue`) and that land value is the last test, as the
  notes say. If I-M only grows on medium land value, that assumption is wrong: set `IgnoreLandValue=false` and report.
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
