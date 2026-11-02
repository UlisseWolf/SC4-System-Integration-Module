# Deathcare (1.9.4)

Part of the Afterlife module (`src/afterlife/Deathcare.cpp`). Cemeteries and crematoria collect the dead of the
neighborhoods around them, in the spirit of Cities: Skylines, but with SimCity 4's own service model: a radius and a
capacity, like schools and hospitals, instead of hearses. On by default (`DEATHCARE=1` in `[Afterlife]`).

## Where the dead are

The Afterlife counts the dead every month (see [afterlife.md](afterlife.md)). Deathcare places them in the residential
simulator's tracts:

- the deaths of a **generation reset** (a tract whose average age passed its life expectancy) stay in that tract;
- every other death (base deaths, I.M.P. victims) is spread over the populated tracts by population, largest
  remainders first.

## Who collects them

| Building | How it is recognised | Capacity | Radius |
| --- | --- | --- | --- |
| Cemetery | `0x5C4DD001` = 1, or occupant group `0x1700` (Maxis "Building: Cemetery") | `0x5C4DD002` graves; without it `DEATHCARE_GRAVES_PER_CELL` (40) per lot cell | `0x5C4DD003` meters; without it `DEATHCARE_RADIUS` (1024) |
| Crematorium | `0x5C4DD001` = 2, or occupant group `0x5C4DD100` | `0x5C4DD002` bodies per month; without it `DEATHCARE_CREMATION_PER_MONTH` (50) | as above |

The properties are defined in `New_Properties.xml` of
[New_Properties.xml-patches](https://github.com/UlisseWolf/New_Properties.xml-patches), whose Cemetery and Crematorium
generators fill them from the lot size. Every month, for each tract, the dead of the month plus the uncollected dead of
earlier months go:

1. to the nearest **crematorium** in range with room left this month (a crematorium never fills up);
2. then to the nearest **cemetery** in range with free graves (each burial uses a grave for good; a full cemetery takes
   no one);
3. the rest stays **uncollected** in the tract, at most `DEATHCARE_MAX_UNCOLLECTED_SHARE` (10%) of its residents.

## Effects

With `DEATHCARE_EFFECTS=1` (default), a tract with uncollected dead loses health quotient every month:
`DEATHCARE_HQ_PENALTY_PER_PERCENT` (0.5) per 1% of its residents uncollected, at most `DEATHCARE_MAX_HQ_PENALTY` (3), on
the HQ grids of all three wealths. Lower health means a shorter life expectancy, so more of its residents die: an
unserved neighborhood gets worse until a cemetery or a crematorium reaches it. The change uses the same grid call as the
game's own monthly HQ decay (`SetTractValue`, vtable `+0x68`, seen at `0x006FA5D8`); `GetHQGrids` is residential
simulator slot 24 (`0x006F7300`), three AddRef'd out pointers like slot 25. `DEATHCARE_EFFECTS=0` keeps deathcare
cosmetic.

When the uncollected dead reach `DEATHCARE_NOTIFY_UNCOLLECTED` (50), a notification says so, at most once a year
("Uncollected Dead", or "Cemeteries Full" when every cemetery is full and there is no crematorium; LTEXT `6A231EA4`,
`BB3C4000`-`BB3C4003`).

## What the player sees

- **Cemetery query** (also used by the crematoria): City Graves Used, City Grave Capacity, Cremated This Month,
  Uncollected Dead (LTEXT `EA5524EB`, `BB3C3012`-`BB3C3019`).
- **`afterlife_grid_var`**: `DEATHCARE_COLLECTED`, `DEATHCARE_CREMATED`, `DEATHCARE_BURIED`, `DEATHCARE_UNCOLLECTED`,
  `GRAVES_USED`, `GRAVES_TOTAL`, `CREMATION_CAPACITY`.
- **`SC4SIM.log`**: the facilities found in the first month, and with `DebugLog` the monthly totals.

## Saved with the city

The graves used by each cemetery (`GRAVES.x.z`, the cemetery's cell) and the uncollected dead of each tract
(`UNCOLLECTED.x.z`) are written to `SC4SIM_Afterlife.ini` with the Afterlife's totals when the city is saved; the
section is rewritten as a whole, so a demolished cemetery leaves no key. A cemetery rebuilt elsewhere starts empty.

## Limits

- No individual Sims and no hearses: the dead are counted per tract and collected by coverage.
- No budget line yet (the buildings' own monthly costs apply). A deathcare department could use the Custom Budget
  Departments DLL later.
- A crematorium needs a building: the `Crematorium` generator of New_Properties.xml-patches makes one from any model.
- Not yet tested in game.
