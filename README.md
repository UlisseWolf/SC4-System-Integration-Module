# SC4SIM — SC4 System Integration Module DLL for SimCity 4

[![License: LGPL v3](https://img.shields.io/badge/License-LGPL_v3-blue.svg)](LICENSE)
![Version](https://img.shields.io/badge/version-1.5.1-informational)
![Game](https://img.shields.io/badge/SimCity_4-1.1.641-success)
![Platform](https://img.shields.io/badge/platform-Windows_x86-lightgrey)
[![Build and release](https://github.com/UlisseWolf/SC4-System-Integration-Module/actions/workflows/build-release.yml/badge.svg?branch=master)](https://github.com/UlisseWolf/SC4-System-Integration-Module/actions/workflows/build-release.yml)

**SC4SIM.dll** is the DLL of the **SC4 System Integration Module (SIM)** of the
[Colossus Addon Mod](https://github.com/UlisseWolf/Colossus_Addon_Mod) for SimCity 4 Deluxe / Rush Hour.
SIM collects shared game systems that any mod can rely on, without forcing players to install a specific mod
such as NEON.

> **Status:** source release, version 1.5.1. Building with MSVC and in-game testing are the next steps.

---

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Advisor rewards](#advisor-rewards)
- [Advanced Water Management](#advanced-water-management)
- [Mixed R + C occupancy](#mixed-r--c-occupancy)
- [Custom power poles](#custom-power-poles)
- [Configuration](#configuration)
- [Building](#building)
- [Repository layout](#repository-layout)
- [Credits](#credits)
- [License](#license)

## Features

| Feature | Since | Summary |
| --- | --- | --- |
| Advisor reward creators | 1.0.0 | `create_reward_utilities`, `create_reward_transportation`, `create_reward_environment` and `create_reward_health_education`, so a reward can come from the right advisor. |
| `sim` Lua table | 1.0.0 | `sim.version()` lets scripts detect the DLL. When a city opens, the DLL logs whether the SIM Lua and every reward creator are available. |
| Custom power poles | 1.1.0 | Custom cable geometry, wire appearance, named pole styles switched with Tab, fractional-angle (FAR) power lines and the `keepwires` cheat. |
| Power lines across zones | 1.3.0 | Power poles can stand on empty zones and farm fields, so lines are no longer cut by zones wider than the pole spacing. |
| Dezoning under poles | 1.4.0 | The cell under every new pole loses its zone (optionally also on farm fields). |
| Mixed R + C occupancy | 1.5.0 | Residents and commercial jobs in one building with independent capacities, census demand for both, and the `MixedOccupancy` cheat. 1.5.1: the game itself fills the secondary types. *Experimental, not yet tested in game.* |
| Advanced Water Management | 1.2.0 | The real efficiency of water buildings in the query dialog and tool tip, a monthly warning for fresh or salt water pumps that produce nothing, and the `ScanPumps` cheat. |

## Requirements

- SimCity 4 Deluxe / Rush Hour **1.1.641** on Windows. The power-pole and water hooks stay off on any other version.
- The [SIM package](https://community.simtropolis.com/files/file/36978-sc4-system-integration-module-sim/):
  - its `CAM_LUA_Controller.dat` holds the advisor reward Lua;
  - its `CAM_Locale_en.dat` holds the water warning texts.
- [sc4-query-ui-hooks](https://github.com/0xC0000054/sc4-query-ui-hooks): optional, needed only for the water
  building hover tool tip.

**Do not** install these DLLs as well, because SC4SIM includes them. If one is loaded, SC4SIM leaves that feature to
it and says so in the log:
- `SC4PowerPoleCustomizationSample.dll` from sc4-render-services;
- `AdvancedWaterManagement.dll`.

## Installation

1. Download `SC4SIM-<version>.zip` from the [Releases](../../releases) page, or build `SC4SIM.dll` yourself (see
   [Building](#building)).
2. Copy `SC4SIM.dll` and, optionally, `SC4SIM.ini` to your SimCity 4 **Plugins** folder.
3. Start the game. `SC4SIM.log`, created next to the DLL, should report:
   - `SC4 SIM DLL 1.5.1`;
   - `PowerPoleCustomization: installed N of N patches`;
   - `AdvancedWater: installed the water building query UI hook`;
   - `MixedOccupancy: enabled (...)`.

## Advisor rewards

Maxis only provides reward creators for three advisors: `create_reward_cityplanning`, `create_reward_finances` and
`create_reward_safety`. SIM adds the other four:

| Function | Advisor |
| --- | --- |
| `create_reward_utilities('<building IID>')` | Utilities |
| `create_reward_transportation('<building IID>')` | Transportation |
| `create_reward_environment('<building IID>')` | Environment |
| `create_reward_health_education('<building IID>')` | Health & Education |

They behave exactly like `create_reward_cityplanning`:
- they create a `cSC4RewardAdvice` record (class 0xAA371C32) with `once = 0` and the advisor's own advice type;
- each one is defined only if no other file has already defined it.

The functions are plain Lua, in [`lua/sim_advisor_rewards.lua`](lua/sim_advisor_rewards.lua). The SIM ships this
script inside `CAM_LUA_Controller.dat` as entry `CA63E2A3 / 4A5E8EF6 / 8151A000`, in Lua package **0x0151A000**.

The game compiles Lua packages in ascending order, so scripts that use these functions need a higher package
number. The DLL is not needed for them to work: the game runs reward scripts while it compiles them, before any DLL
can reach the city's Lua.

```lua
--#-package:0c000000# -- any package number above 0151a000
a = create_reward_utilities('E0C896CC')
function a.condition() return game.g_city_population > 50000 end
a.title   = [[text@...]]
a.message = [[text@...]]
```

## Advanced Water Management

A port of null 45's [sc4-advanced-water-management](https://github.com/0xC0000054/sc4-advanced-water-management),
which improves the game's handling of water buildings that draw from fresh or salt water instead of an aquifer.

- **Query dialog.** The efficiency meter of a water building shows its real efficiency. A fresh or salt water pump
  that produces nothing, because it was placed too far from its water source, shows 0%.
- **Hover tool tip.** With [sc4-query-ui-hooks](https://github.com/0xC0000054/sc4-query-ui-hooks) installed:
  - water buildings get a tool tip with the same efficiency meter;
  - the tool tip shows a message when the building is shut down by pollution.
- **Monthly check.** Once a month, the DLL warns with a notification if a fresh or salt water pump produces nothing.
  It shows one message per month.
- **`ScanPumps` cheat.** Runs the same check on demand.

**Differences from the original DLL:**
- log in `SC4SIM.log`;
- an `[AdvancedWaterManagement]` `Enabled` switch in `SC4SIM.ini`;
- the query hook is installed at application start (PostAppInit);
- no dependency on the Windows Implementation Library.

The game addresses and behaviour are unchanged.

## Mixed R + C occupancy

> **Experimental:** compiled and statically checked, not yet tested in game. Test it on a copy of a city.

In the base game, Allow Joint Occupancy only lets several occupant types split **one** building's capacity. SC4SIM
lets a building hold residents and commercial jobs with independent capacities. Industry is excluded.

**Which buildings.** A building is mixed when its exemplar has all of the following; every other building is
unchanged:
- `Allow Joint Occupancy` (0x27812836) = true;
- `Purpose` = Residence, Services or Office: this is the primary class;
- a `Capacity Satisfied` (0x27812834) that lists at least one residential type (R$, R$$, R$$$) and at least one
  commercial type (CS$, CS$$, CS$$$, CO$$, CO$$$), and no industrial type.

For example, Purpose Residence, Wealth 2, Capacity Satisfied `0x1020, 120, 0x3120, 30`: 120 R$$ residents and
30 CS$$ jobs above and below each other.

**What changes, for mixed lots only:**
- **Available capacity.** Residential and commercial capacities are independent. Within one class, the wealth
  levels (for example R$ and R$$) stay exclusive, as in a building without joint occupancy.
- **Occupancy.** It counts only the primary class, so the secondary class cannot make the building decline.
- **Secondary population.** The game's own growth code fills it, with its usual random spread and schedule. The
  growth pass of each developer type reaches every lot whose wealth equals its own, and the capacity hook gives it
  the room. So the secondary types must have the building's own wealth: R$$ with CS$$, for example.
- **Demand.** The demand census also counts the secondary types, with the game's residential or job formula.
- **`MixedOccupancy` cheat.** Writes every mixed lot, with the population and capacity of each type, to
  `SC4SIM.log`. It also flags a secondary type whose wealth differs from the lot's, because the game never fills
  it. It changes nothing.

**Limits:**
- secondary jobs get no commuters, and secondary residents do not commute, because the game's traffic code reads
  only the primary type;
- the query dialog shows the primary capacity only;
- without the DLL, a saved city falls back to the game's shared-pool rule.

The reverse-engineering notes are in [`docs/mixed-occupancy.md`](docs/mixed-occupancy.md).

## Custom power poles

A port of caspervg's **custom-powerlines** work
([sc4-render-services, branch `re/custom-powerlines`](https://github.com/caspervg/sc4-render-services/tree/re/custom-powerlines)).
SC4SIM includes only the power-pole module: it does not contain SC4RenderServices or ImGui, and does not need them.

- **Cable geometry.** A pole prop exemplar can declare, for each connection direction:
  - how many cables it carries (1–15) and exactly where each one attaches;
  - wire width and sag;
  - wire and water-crossing (buoy) textures;
  - foundation textures.

  The properties are 0xB22A0000–0xB22A0013, documented in [`docs/power-pole-customization.md`](docs/power-pole-customization.md).
  Props without them render exactly as in the base game.
- **Pole styles.** Named `[PowerPoles.<name>]` sections in `SC4SIM.ini` choose a pole model for every combination of
  connected headings (`REG.*`). `InterPoleDistance` sets the pole spacing, 1–50 cells (vanilla 10). With the
  power-line tool active, **Tab** / **Shift+Tab** cycle through the styles; the active style is written to the log.
- **FAR power lines.** Drags snap automatically to 33.7°, 26.6°, 18.4° and 9.5° and use the style's `FAR-*` models
  or the `[PowerPoles.FAR]` defaults. Hold **Shift** for a regular line. See
  [`docs/far-power-lines-design.md`](docs/far-power-lines-design.md).
- **`keepwires` cheat.** Toggles whether power lines are rerouted when the zoning underneath changes.
- **Across zones and farm fields** *(SC4SIM addition, not in the original sample)*. In the base game a pole can
  never stand on a zoned cell, so a zone or a farm field wider than the pole spacing cuts the line. SC4SIM lets
  poles stand on empty zones and on farm fields; lots with a building still block. `CrossZones=false` turns it
  off.
- **Dezoning under poles** *(SC4SIM addition)*. When a drag ends, the cell under each new pole loses its zone.
  - Lots with a building are never touched.
  - On a farm field the game demolishes that field lot (it cannot take one cell from a lot) and the farm grows new
    fields around the pole; `DezoneFieldCells=false` keeps fields untouched.
  - The reverse-engineering notes are in [`docs/power-lines-across-zones.md`](docs/power-lines-across-zones.md).

**Differences from the original sample:**
- no ImGui status overlay;
- settings in `SC4SIM.ini` (`[SC4PowerPoleCustomization]`, plus a `DebugLog` switch);
- log in `SC4SIM.log`;
- the original director is now a module driven by the single SC4SIM director.

The hook sites, addresses and exemplar properties are unchanged.

```ini
[SC4PowerPoleCustomization]
Enabled=true
DebugLog=false
MaxPointsPerDirection=8

;[PowerPoles.Wood]
;REG.None=0x06000205
;REG.X+Z=0x06000105
;InterPoleDistance=7
```

The reverse-engineering notes behind every address are in [`docs/sc4-powerline-tool-re.md`](docs/sc4-powerline-tool-re.md).

## Configuration

`SC4SIM.ini` sits next to the DLL; without it, the defaults are used. See the [template](SC4SIM.ini).

| Section | Key | Default | Effect |
| --- | --- | --- | --- |
| `[AdvancedWaterManagement]` | `Enabled` | `true` | Turns Advanced Water Management on or off. |
| `[MixedOccupancy]` | `Enabled` | `true` | Turns mixed R + C occupancy on or off. |
| | `DebugLog` | `false` | Writes the cheat's report of the mixed lots to `SC4SIM.log` at the start of every month. |
| `[SC4PowerPoleCustomization]` | `Enabled` | `true` | Turns the custom power poles on or off. |
| | `CrossZones` | `true` | Lets poles stand on empty zones and farm fields. |
| | `DezoneUnderPoles` | `true` | Removes the zone from the cell of every new pole when the drag ends. |
| | `DezoneFieldCells` | `true` | Also on farm fields; the game demolishes that field lot. |
| | `DebugLog` | `false` | Writes the power-pole debug traces to `SC4SIM.log`. |
| | `MaxPointsPerDirection` | `8` | Maximum number of cables per direction (1–15). |
| `[PowerPoles.<name>]` | `REG.*`, `FAR-*`, `InterPoleDistance` | — | Pole styles, see above. |

## Building

**Visual Studio 2022 or later** with the *Desktop development with C++* workload. The power-pole hooks use x86
MSVC inline assembly, so the Microsoft compiler is required: MinGW and clang are not supported.

1. Open `SC4SIM.sln`.
2. Build the only configuration, **Release | x86**.
3. The output is `bin\Release\SC4SIM.dll`.

**CMake** works as well:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Dependencies are vendored:
- `vendor/gzcom-dll`;
- `vendor/mINI`, the INI parser;
- the DLL also links Windows' `version.lib`.

The C runtime is linked statically, so players do not need the Visual C++ Redistributable.

## Continuous integration

[`.github/workflows/build-release.yml`](.github/workflows/build-release.yml) runs on every push to `master` (and on manual
runs from `master`):

1. builds `SC4SIM.sln` (Release | x86) with MSBuild on Windows;
2. packages `SC4SIM-<version>.zip` with `SC4SIM.dll`, `SC4SIM.ini` and the license files (`Licenses/`), and keeps it as
   a build artifact;
3. publishes it as the GitHub release `v<version>` if that release does not exist yet.

The version is `SIM_DLL_VERSION_STR` in `src/version.h`: bump it to publish a new release. Pushes that do not change
the version only check that the DLL still builds. A manual run with *Replace the zip of an existing release* replaces
the zip of the current version's release instead.

## Repository layout

```
src/                 SC4SIM director, log and version
src/powerpoles/      custom power-pole module (ported from sc4-render-services)
src/water/           Advanced Water Management module (ported from sc4-advanced-water-management)
src/mixed/           mixed R + C occupancy module
lua/                 sim_advisor_rewards.lua (advisor reward creators)
docs/                power-pole documentation and README.it.md (Italian notes)
vendor/              gzcom-dll, mINI
SC4SIM.ini           settings template
```

## Credits

- **Ulisse Wolf**: SC4 System Integration Module and the Colossus Addon Mod.
- **caspervg**: [sc4-render-services](https://github.com/caspervg/sc4-render-services) and the custom power-line
  research and implementation this DLL ports.
- **null 45 (Nicholas Hayes)**: [sc4-advanced-water-management](https://github.com/0xC0000054/sc4-advanced-water-management)
  and [sc4-query-ui-hooks](https://github.com/0xC0000054/sc4-query-ui-hooks).
- **nsgomez**: [gzcom-dll](https://github.com/nsgomez/gzcom-dll).
- **metayeti**: [mINI](https://github.com/metayeti/mINI).

## License

This project is licensed under the **GNU Lesser General Public License v3.0**. See [`LICENSE`](LICENSE). The
LGPL-3.0 supplements the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html).

Third-party code keeps its own license:
- `src/powerpoles`: ported from sc4-render-services, LGPL 2.1
  ([`src/powerpoles/LICENSE-sc4-render-services.txt`](src/powerpoles/LICENSE-sc4-render-services.txt));
- `src/water`: ported from sc4-advanced-water-management, including the sc4-query-ui-hooks interface headers,
  MIT ([`src/water/LICENSE-sc4-advanced-water-management.txt`](src/water/LICENSE-sc4-advanced-water-management.txt));
- `vendor/gzcom-dll`: LGPL 2.1;
- `vendor/mINI`: MIT.

SimCity 4 is a trademark of Electronic Arts Inc. This project is not affiliated with or endorsed by Electronic Arts.
