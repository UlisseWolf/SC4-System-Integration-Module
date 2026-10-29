# Mixed residential, commercial and industrial occupancy (SC4SIM)

SC4SIM 1.5.0 (updated in 1.5.1 and 1.6.0) lets one building hold residents, commercial jobs and industrial jobs, with
independent capacities. Any two classes work (RC, RI, CI), and so do all three (RCI). Industry was added in 1.6.0. The analysis was done on the SimCity 4 1.1.641 executable (`SimCity 4.exe`, MD5
`bfd49573d6588866344832b95069ce8f`). It starts from the "Allow Joint Occupancy" notes (26 October 2026), which this
document checks and extends. Every address below was checked in the disassembly of that executable.

> **Status:** compiled and statically checked, **not yet tested in game**. Test it on a copy of a city.

## Which buildings are mixed

All of these must hold:

- `Allow Joint Occupancy` (0x27812836) = true;
- `Purpose` (0x27812833) = Residence (1), Services (2), Office (3), Agriculture (5), Processing (6),
  Manufacturing (7) or High Tech (8). Tourism (4) and Other (9) are never mixed;
- `Capacity Satisfied` (0x27812834) lists types of at least two classes, each with a capacity above 0:
  - residential: 0x1010, 0x1020, 0x1030;
  - commercial: 0x3110, 0x3120, 0x3130, 0x3320, 0x3330;
  - industrial: 0x4100, 0x4200, 0x4300, 0x4400;
- no other type. Only the twelve developer types of `GetDeveloperType` (`0x00695790`) count.

The secondary types should have the building's own wealth. The game only fills a lot from a developer of the lot's
wealth (see UpdatePopulation below). The game's `GetWealth(DeveloperType)` (`0x00695840`) gives:

| Type | Game wealth |
| --- | --- |
| R$, CS$, I-A (0x4100) | 1 |
| R$$, CS$$, CO$$, I-D (0x4200), I-M (0x4300) | 2 |
| R$$$, CS$$$, CO$$$, I-HT (0x4400) | 3 |

For industrial purposes, `GetDeveloperType(purpose, wealth)` returns the same type whatever the wealth. The lot's
wealth is the exemplar's Wealth: an I-M building needs Wealth 2 for the game to fill it. The `MixedOccupancy` cheat
flags a secondary type at another wealth.

The Purpose decides the **primary** class: the zone the building grows in, the lot wealth, the jobs used by traffic
and the abandonment rules. The other classes are **secondary**. Every building that is not mixed keeps the game's code
path: each hook first checks the profile and otherwise calls the original function.

Examples: RC (flats and shops), IC (Purpose Manufacturing, `0x4300, 80, 0x3120, 10`) and RCI (Purpose Residence,
`0x1020, 120, 0x3120, 20, 0x4300, 15`). The table shows the RC one, an apartment block with shops on the ground
floor.

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

**The caller** is `cSC4GrowthDeveloper::DoIdle` (`0x006B5F20`):
- it gets the tract developer from the building development simulator (`call [edx+0x8C]` at `0x006B6946`);
- for every tract of the map, it writes the developer's capacity grid;
- it then calls slot 7 with the record {growth developer, tract x, tract z} (`call [edx+0x1C]` at `0x006B6A65`).

The call runs for every growth developer type over every tract, and does not look at the lot's zone or purpose. So
the CS$$ developer's pass reaches a mixed R$$ + CS$$ lot. It asks slot 52 for the CS$$ room and sets the CS$$
population. With hook 1 that room is the independent capacity, so the game fills the secondary types itself, with
its random spread and on its own schedule.

1.5.0 also filled them once a month with a formula without the spread; 1.5.1 removes that pass. The caller was
pointed out by the "SC4SIM mixed occupancy: the game already fills the secondary types" notes (27 October 2026),
which were checked against the executable. The first static search had missed the call, because the tract
developer is held in `ebp`, not `ecx`.

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
| 1 | vtable `0x00AB1A80` (slot 52) → `GetAvailableCapacity` | Same state test. Another class never reduces the room, so residents, shops and industry no longer compete. Within one class, the game's rule **without** joint occupancy applies: R$ / R$$ / R$$$ (or CS$ / CS$$ / …, or I-D / I-M / …) stay exclusive, so the first to move in takes the building, as in the unmodified building. It returns the capacity or 0, so the 16-bit wrap cannot happen. |
| 2 | vtable `0x00AB1AA4` (slot 61) → `GetOccupancy` | `Σ population / Σ capacity` of the primary class, at most 100%. |
| 3 | `CALL` at `0x006A7475` and `0x006A74AB` → `PerformCensus` wrapper | Calls the original, then adds each secondary type with the formula of its own class: residential for R, job (`census[t + 1]`) for C and I. |
| – | `MixedOccupancy` cheat (and month starts with `DebugLog=true`) | Read only: writes every mixed lot, with the population and capacity of each type, and flags a secondary type whose wealth differs from the lot's. It sets no population (1.5.0 did; see UpdatePopulation above). |

Before patching, the DLL checks that the game version is 641, that both vtable slots still point to the original
functions and that both call sites are still `CALL 0x006A5A00`. If any check fails, nothing is installed.

## Limits

- **Traffic.** `GetJobs` (slot 64) and the travel code use only the primary developer type. Secondary jobs get no
  commuters, and secondary residents do not commute. Secondary industry produces no freight trips. All of them
  still count as capacity, population and census demand.
- **Agriculture.** A farm lot holds only the farm building; the fields are separate lots. Secondary residents or
  shops on a farm sit on the building's lot.
- **Query tool.** The query dialog still shows the primary capacity only (`GetOccupantCapacity`, `0x004C4160`). The
  `MixedOccupancy` cheat writes every mixed lot to `SC4SIM.log`, with the population and capacity of each type.
- **Wealth.** A secondary type at a wealth other than the lot's is never filled by the game. Filling it would need
  a hook on `UpdatePopulation` (tract developer vtable slot 7, `0x00AB3134`) that fills the other wealth levels of
  mixed lots in the same tract.
- **Save games.** Lot populations are saved with the city. Without the DLL, the game's original shared-pool rule
  applies again, and the secondary types shrink or vanish when the game next updates them.

## Settings

```ini
[MixedOccupancy]
Enabled=true
DebugLog=false
```

`DebugLog=true` writes the cheat's report to `SC4SIM.log` at the start of every month. To check the game's fill, let
a mixed lot grow and compare the reports. The secondary population should change between month starts, with values
that vary a little from `capacity × desirability`.
