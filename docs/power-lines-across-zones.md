# Power lines across zones and farm fields (SC4SIM)

This patch is an SC4SIM addition. It is **not** part of the sc4-render-services `re/custom-powerlines` sample:
that sample only keeps existing lines when a lot grows under them (`keepwires`) and lets FAR lines skip the
cells between poles. The analysis below was done on the SimCity 4 1.1.641 executable
(`SimCity 4.exe`, MD5 `bfd49573d6588866344832b95069ce8f`).

## The vanilla check

`cSC4PowerLineTool::CellIsOKForPlacement` — `0x0064FD00`, `__thiscall`, arguments `cellX, cellZ`, `RET 8`.

| Step | Code | Result |
| --- | --- | --- |
| Zone | `cISC4ZoneManager::GetZoneType(x, z)` (city zone manager, global `0x00B43D14`, vtable `+0x30`). Not *None*, *Plopped* (15) or *Landfill* (14). | **1** |
| Occupants | Occupants in the cell (`tool+0x48`, vtable `+0xA0`): a building occupant (type `0x278128A0`). | **2** |
| | An occupant of type `0xC772BF98` that does not answer `QueryInterface(0x4A10AFC2)`. | **3** |
| Lot | `cISC4LotManager::GetLot(x, z, false)` (global `0x00B43D08`, vtable `+0x18`) returns a lot. | **2** |
| Otherwise | | **0** |

The zone test returns at once, so every zoned cell gives 1, whether it is empty, a farm field or a developed lot.
(The third `GetZoneType` call passes `(z, z)` instead of `(x, z)`, a bug in the game: it only affects the
landfill exception.)

## The callers

| Call site | Function | Use of the result |
| --- | --- | --- |
| `0x0064FF3E` | drag validation (`0x0064FED0`) | 1: warning `0xF0000011`, the drag goes on. 2: message `0xF0000012`, the drag is cut at the previous step. 3: message `0xF0000013`, cut only on the last step. |
| `0x006509AD` | `DeterminePolePositions` (`0x00650840`) | Only 0 can take a pole; any other value goes to the "no pole here" path (`0x006510C8`). |
| `0x0065210B` | pole relocation used by `UpdateOnZoneChange` (`0x006520C0`) | Looks for the first cell outside the zone rectangle that returns 0. |

So the wire can pass over a zone, but no pole can stand on it: a zone or a field wider than the pole spacing
(10 cells in vanilla) breaks the line.

## The patch

The two calls at `0x0064FF3E` and `0x006509AD` are redirected (rel32 `CALL`, target checked against
`0x0064FD00` before patching) to `CellIsOKForPlacementHook`. It calls the original function and, when the result
is 1 or 2, looks at the lot on the cell:

| Cell | Vanilla | With the patch |
| --- | --- | --- |
| no zone, no lot | 0 | 0 |
| empty zone (no lot) | 1 | **0** |
| farm field (zoned lot without a building) | 1 or 2 | **0** |
| lot with a building | 1 or 2 | unchanged |
| building occupant without a lot | 2 | unchanged |

The relocation at `0x0065210B` is left vanilla, so poles moved by a zone change still avoid zones.

`CrossZones=false` in `[SC4PowerPoleCustomization]` of `SC4SIM.ini` turns the patch off without
uninstalling it.

## Interaction with zone growth

A pole on a zoned cell is an obstacle like any other. When a lot later grows over it, the game's
`UpdateOnZoneChange` removes and reroutes the line, as it does for any zone change; the `keepwires` cheat
keeps the line instead.

## To verify in game

The analysis is static. With `DebugLog=true`, every overridden cell is written to `SC4SIM.log`:

```
[DEBUG] PowerPoleCustomization: [zones] cell (x, z) vanilla 1 -> 0 (lot yes, building no).
```

Points to check:
- that a farm field lot really has no building (`building no`);
- that poles placed on fields and empty zones behave normally when saved, reloaded and when the farm or
  zone develops.
