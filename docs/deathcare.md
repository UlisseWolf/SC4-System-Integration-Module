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
| Cemetery | `0x5C4DD001` = 1, or occupant group `0x5C4DD101` ("Cemetery (SC4SIM Deathcare)") or `0x1700` (Maxis "Building: Cemetery") | `0x5C4DD002` graves; without it `DEATHCARE_GRAVES_PER_CELL` (40) per lot cell | `0x5C4DD003` meters; without it `DEATHCARE_RADIUS` (1024) |
| Crematorium | `0x5C4DD001` = 2, or occupant group `0x5C4DD100` | `0x5C4DD002` bodies per month; without it `DEATHCARE_CREMATION_PER_MONTH` (50) | as above |

The properties are defined in `New_Properties.xml` of
[New_Properties.xml-patches](https://github.com/UlisseWolf/New_Properties.xml-patches). Its two generators under
**Hospital**, *Cemetery (SC4SIM Deathcare)* and *Crematorium (SC4SIM Deathcare)*, fill them from the lot size, add a
Health budget line and put the building in the **Deathcare** submenu of the Health menu (`0x5C4DD200`, Submenus DLL;
the button is in `CAM_Menus.dat`). The buildings get no Health occupant group (`0x1507`): the residential simulator
counts every `0x1507` building as a hospital (`0x006FB695`). They have no Reward group (`0x150B`) either, so they
need the Submenus DLL: without it they are in no menu. The Reward section's Cemetery generator is unchanged; Maxis and Reward cemeteries (`0x1700`) also count. Every month, for each tract, the dead of the month plus the uncollected dead of
earlier months go:

1. to the nearest **crematorium** in range with room left this month (a crematorium never fills up);
2. then to the nearest **cemetery** in range with free graves (each burial uses a grave for good; a full cemetery takes
   no one);
3. the rest stays **uncollected** in the tract, at most `DEATHCARE_MAX_UNCOLLECTED_SHARE` (10%) of its residents.

## Emptying and relocation

- A cemetery that reaches `DEATHCARE_EMPTY_THRESHOLD` (80%) starts being **emptied**: every month the crematoria whose
  radius reaches it move into their ovens the room they have left after the month's dead, fullest cemetery first. A
  cemetery being emptied goes on until it is empty (the state is saved).
- When a cemetery is **demolished**, its dead are not lost: they wait for **relocation** and, the next month, go to any
  crematorium in the city first, then to any cemetery with free graves. Rebuilding a cemetery is no longer a way to
  empty it.

## Effects

With `DEATHCARE_EFFECTS=1` (default), a tract with uncollected dead:

- loses health quotient every month: `DEATHCARE_HQ_PENALTY_PER_PERCENT` (0.5) per 1% of its residents uncollected, at
  most `DEATHCARE_MAX_HQ_PENALTY` (3), on the HQ grids of all three wealths. Lower health means a shorter life
  expectancy, so more of its residents die. The change uses the same grid call as the game's own monthly HQ decay
  (`SetTractValue`, vtable `+0x68`, seen at `0x006FA5D8`); `GetHQGrids` is residential simulator slot 24 (`0x006F7300`);
- with `DEATHCARE_AURA=1`, gets a **negative transient aura**: `DEATHCARE_AURA_PER_PERCENT` (4) per 1% uncollected, at
  most `DEATHCARE_AURA_MAX` (60) on the game's -127..127 scale, written to the aura simulator's transient aura grid
  (the grid SC4DataViewExtensions reads). Transient aura lowers desirability and fades by itself once the dead are
  collected.

`DEATHCARE_EFFECTS=0` keeps deathcare cosmetic.

## Map of the uncollected dead

The **Transient Aura** data view of [SC4DataViewExtensions](https://github.com/0xC0000054/sc4-data-view-extensions)
(its optional `z_DataView - TransientAura.dat`) shows the transient aura grid, so the neighborhoods with uncollected
dead appear on it as negative areas. The view also shows other transient effects of the game, if any.

## Notices

Each at most once a year (LTEXT `6A231EA4`; `#count#`, `#x#`, `#z#` are filled in by the DLL):

| Notice | When | LTEXT |
| --- | --- | --- |
| Uncollected Dead | uncollected dead reach `DEATHCARE_NOTIFY_UNCOLLECTED` (50); gives their number and the cell of the worst neighborhood | `BB3C4000`, `BB3C4001` |
| Cemeteries Full | the same, when every cemetery is full and there is no crematorium | `BB3C4002`, `BB3C4003` |
| Cemeteries Nearly Full | a cemetery reaches `DEATHCARE_NOTIFY_NEARLY_FULL` (80%) | `BB3C4004`, `BB3C4005` |

## News

Two news events join the Afterlife's ticker (`afterlife_news.lua`): `UNCOLLECTED_DEAD_REPORT` (670 News City Desk, the
Gravediggers' Union) when the dead are left uncollected, and `CEMETERY_FULL_REPORT` (a cemetery caretaker, the
crematorium manager) when a cemetery is nearly full. The characters are original to the mod.

## Budget

The New_Properties.xml-patches generators put the buildings' monthly cost in a **Deathcare** budget department
(`0x5C4DD400`, in the Health and Education budget window; lines `0x5C4DD401` cemeteries, `0x5C4DD402` crematoria),
which needs the [Custom Budget Departments DLL](https://github.com/0xC0000054/sc4-custom-budget-departments) (part of
the CAM dependencies). The department name is the LTEXT `26350A44-5C4DD400` in `CAM_Locale_en.dat`.

## Hearses

`lua/deathcare_hearses.lua` (`CA63E2A3-4A5E8F3F-8C4DD500` in `CAM_LUA_Controller.dat`, with the automata scripts)
makes hearses, the game's own `hearse` automata group, drive out of SC4SIM cemeteries (`0x5C4DD101`) and crematoria
(`0x5C4DD100`) on the roads around them, up to two at a time. They are cosmetic: the dead are collected by the DLL.

## Limits

- No individual Sims and no hearses: the dead are counted per tract and collected by coverage.
- A crematorium needs a building: the `Crematorium` generator of New_Properties.xml-patches makes one from any model.
- Not yet tested in game.
