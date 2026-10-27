# Mixed residential + commercial occupancy (SC4SIM)

SC4SIM 1.5.0 lets one building hold residents **and** commercial jobs with independent capacities. Industry is out
of scope. The analysis was done on the SimCity 4 1.1.641 executable (`SimCity 4.exe`, MD5
`bfd49573d6588866344832b95069ce8f`). It starts from the "Allow Joint Occupancy" notes (26 October 2026), which this
document checks and extends. Every address below was checked in the disassembly of that executable.

> **Status:** compiled and statically checked, **not yet tested in game**. Test it on a copy of a city.

## Which buildings are mixed

All of these must hold:

- `Allow Joint Occupancy` (0x27812836) = true;
- `Purpose` (0x27812833) = Residence (1), Services (2) or Office (3);
- `Capacity Satisfied` (0x27812834) has at least one residential type (0x1010, 0x1020, 0x1030) and at least one
  commercial type (0x3110, 0x3120, 0x3130, 0x3320, 0x3330), each with a capacity above 0;
- no industrial type (0x4xxx).

The Purpose decides the **primary** class: the zone the building grows in, the lot wealth, the jobs used by traffic
and the abandonment rules. The other class is **secondary**. Every building that is not mixed keeps the game's code
path: each hook first checks the profile and otherwise calls the original function.

Example exemplar: an apartment block with shops on the ground floor.

| Property | Value |
| --- | --- |
| Purpose | 1 (Residence) |
| Wealth | 2 |
| Allow Joint Occupancy | true |
| Capacity Satisfied | `0x1020, 120, 0x3120, 30` (120 R$$ residents, 30 CS$$ jobs) |

## The game code

### Building profile

`cSC4BuildingDevelopmentSimulator::GetBuildingProfile` (vtable slot 39, `0x0069CA60`) returns:

| Offset | Field |
| --- | --- |
| +0x00 | purpose |
| +0x04 | wealth |
| +0x08 | current occupant capacity (uint16) |
| +0x0C..+0x14 | STLport `vector<map<DeveloperType, uint16_t>>`: the capacity groups (begin, end, end of storage) |
| +0x18 | Allow Joint Occupancy (bool) |

gzcom-dll declares `intptr_t capacitiesSatisfied[4]`: the fourth word is really the joint-occupancy flag. Each map
is 12 bytes (header node pointer, count, comparator). In a node, the key is at +0x10 and the capacity at +0x14. The
loader puts every Capacity Satisfied pair in **one** group. The lot keeps the same structure for its populations at
`cSC4Lot+0x54`.

### cSC4Lot (vtable `0x00AB19B0`)

| Slot | Function | Notes |
| --- | --- | --- |
| 21 | `GetLocation` `0x006BF2E0` | cells, bytes +0x7A / +0x7B |
| 33 | `GetState` `0x006C2100` | byte +0x7F; 3 = occupied, 4 = vacant |
| 38 | `HasCapacity` `0x006C2160` | state 2 or 3 |
| 41 | `GetOccupantWealth` `0x006C21F0` | byte +0x82 |
| 46 | `GetBuildingType(bool)` `0x006BFA00` | |
| 50 | `GetCapacity(DeveloperType, bool)` `0x006C05A0` | first group that contains the type |
| 51 | `GetCapacity(WealthType)` `0x006BF9B0` | `GetDeveloperType(purpose, wealth)` → slot 50 |
| 52 | `GetAvailableCapacity` `0x006C0640` | see below; only reached through the vtable |
| 53 | `GetPopulation` `0x006C04A0` | sum over the groups |
| 55 | `SetPopulation` `0x006C1940` | clamps to slot 52, writes the group map, calls `UpdatePrimaryOccupantWealth` |
| 61 | `GetOccupancy` `0x006C0CF0` | returns an `SC4Percentage` through a hidden pointer (gzcom-dll declares `float`) |
| 64 | `GetJobs` `0x006BF800` | uses only the population of the primary developer type |

The two `GetCapacity` overloads are in the reverse order of the gzcom-dll header (MSVC groups overloads). That does
not matter when the DLL is built with MSVC, because MSVC reverses them in the same way. The module calls the slots
by number anyway.

### GetAvailableCapacity (`0x006C0640`)

1. Without `force`, only an occupied lot (state 3) has room; with `force`, an occupied or vacant one (3 or 4).
2. It finds the group that holds the type. Then `available = 1 − Σ population[t] / capacity[t]` over the **other**
   types of that group.
3. If `available == 1`, it returns `capacity[type]`. Otherwise it returns `round(capacity[type] × available)`
   with the joint flag on (the 12582912.0 rounding trick), and 0 with the flag off.

A negative share is not clamped, and the result is truncated to 16 bits. This is the likely source of the
unexplained "66" in the 2021 hotel test.

### UpdatePopulation (`cSC4TractDeveloper`, vtable `0x00AB3118` slot 7, `0x00706BC0`)

For one growth developer and one tract:
- the factor is `desirability(tract) × 0.01` (`0x00A823A4`), spread by a seeded random number;
- above 1.0 it is replaced by `1 − random(+0x64, +0x68)`;
- the developer only fills lots whose `GetOccupantWealth()` equals its own wealth;
- it calls `SetPopulation(type, round(GetAvailableCapacity(type) × factor))`.

The caller is virtual and was not found statically. So the module does not rely on it to fill the secondary
types: it fills them itself (below).

### UpdatePrimaryOccupantWealth (`0x006C1430`)

Picks the lot wealth among the developer types of the building's **own purpose**. The secondary types never become
primary. That is what we want: the lot stays an R lot (or a C lot) for the game.

### PerformCensus (`cSC4DemandSimulator`, `0x006A5A00`, called at `0x006A7475` and `0x006A74AB`)

`PerformCensus(cellX, cellZ)` gets the lot at the cell (lot manager `this+0x40`, slot 6). It then switches on
`purpose − 1` (byte table `0x006A5CA4`):

- **Residence**, with `t = GetDeveloperType(purpose, lot wealth)`:
  - `D = clamp(desirability_t(cell) × 0.01, 0, 1)`;
  - `C = GetCapacity(t) × D`;
  - `share` = slot 30 of `this+0x4C` (cell), and `band = clamp(int(byte(slot 35 (cell)) × 0.01996 + 1), 1, 4)`;
  - `census[((wealth + 0x210) << 4) + band] += C × share`;
  - `census[(wealth << 4) + 0x1001] += C`.
- **Services, Office, Agriculture…HighTech**: `census[t + 1] += GetCapacity(t) × D`.
- Tourism: nothing.

`census` is the `map<uint32_t, float>` at `this+0x1C`; its `operator[]` is `0x006A3170`. Only the primary type
of each lot is counted.

## What SC4SIM changes

| # | Hook | Behaviour for mixed lots |
| --- | --- | --- |
| 1 | vtable `0x00AB1A80` (slot 52) → `GetAvailableCapacity` | Same state test. The other class never reduces the room, so residents and jobs no longer compete. Within one class, the game's rule **without** joint occupancy applies: R$ / R$$ / R$$$ (or CS$ / CS$$ / …) stay exclusive, so the first to move in takes the building, as in the unmodified building. It returns the capacity or 0, so the 16-bit wrap cannot happen. |
| 2 | vtable `0x00AB1AA4` (slot 61) → `GetOccupancy` | `Σ population / Σ capacity` of the primary class, at most 100%. |
| 3 | `CALL` at `0x006A7475` and `0x006A74AB` → `PerformCensus` wrapper | Calls the original, then adds each secondary type with the formula of its own class: residential for R in a C building, job for C in an R building. |
| 4 | `SimNewMonth` (`0x66956816`) and the `MixedOccupancy` cheat | For every occupied mixed lot and each secondary type: `SetPopulation(type, round(capacity × clamp(desirability × 0.01, 0, 1)))`. This is the `UpdatePopulation` formula without the random spread. |

Before patching, the DLL checks that the game version is 641, that both vtable slots still point to the original
functions and that both call sites are still `CALL 0x006A5A00`. If any check fails, nothing is installed.

## Limits

- **Traffic.** `GetJobs` (slot 64) and the travel code use only the primary developer type. Secondary jobs get no
  commuters, and secondary residents do not commute. Both still count as capacity, population and census
  demand.
- **Query tool.** The query dialog still shows the primary capacity only (`GetOccupantCapacity`, `0x004C4160`). The
  `MixedOccupancy` cheat writes every mixed lot to `SC4SIM.log`, with the population and capacity of each type.
- **Timing.** Secondary populations are updated monthly, so a new building shows them from the next month.
- **Save games.** Lot populations are saved with the city. Without the DLL, the game's original shared-pool rule
  applies again, and the secondary types shrink or vanish when the game next updates them.

## Settings

```ini
[MixedOccupancy]
Enabled=true
DebugLog=false
```

`DebugLog=true` writes every population change and a monthly summary to `SC4SIM.log`.
