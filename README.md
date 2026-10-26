# SC4SIM — SC4 System Integration Module DLL for SimCity 4

[![License: LGPL v3](https://img.shields.io/badge/License-LGPL_v3-blue.svg)](LICENSE)
![Version](https://img.shields.io/badge/version-1.1.0-informational)
![Game](https://img.shields.io/badge/SimCity_4-1.1.641-success)
![Platform](https://img.shields.io/badge/platform-Windows_x86-lightgrey)

**SC4SIM.dll** is the DLL of the **SC4 System Integration Module (SIM)** of the
[Colossus Addon Mod](https://github.com/UlisseWolf/Colossus_Addon_Mod) for SimCity 4 Deluxe / Rush Hour.
SIM collects shared game systems that any mod can rely on, without forcing players to install a specific mod
such as NEON.

> **Status:** source release, version 1.1.0. Building with MSVC and in-game testing are the next steps.

---

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Advisor rewards](#advisor-rewards)
- [Custom power poles](#custom-power-poles)
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

## Requirements

- SimCity 4 Deluxe / Rush Hour **1.1.641** on Windows. The power-pole patches stay off on any other version.
- The SIM package (`cam.sc4-system-integration-module`). Its `CAM_LUA_Controller.dat` holds the advisor reward Lua.

No other DLL is required. **Do not** install `SC4PowerPoleCustomizationSample.dll` from sc4-render-services as well:
both patch the same game code. If it is loaded, SC4SIM leaves power poles to it and says so in the log.

## Installation

1. Build `SC4SIM.dll` (see [Building](#building)), or download it from the [Releases](../../releases) page when
   one is available.
2. Copy `SC4SIM.dll` and, optionally, `SC4SIM.ini` to your SimCity 4 **Plugins** folder.
3. Start the game. `SC4SIM.log`, created next to the DLL, should report:
   - `SC4 SIM DLL 1.1.0`;
   - `PowerPoleCustomization: installed N of N patches`.

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

## Repository layout

```
src/                 SC4SIM director, log and version
src/powerpoles/      custom power-pole module (ported from sc4-render-services)
lua/                 sim_advisor_rewards.lua (advisor reward creators)
docs/                power-pole documentation and README.it.md (Italian notes)
vendor/              gzcom-dll, mINI
SC4SIM.ini           settings template
```

## Credits

- **Ulisse Wolf**: SC4 System Integration Module and the Colossus Addon Mod.
- **caspervg**: [sc4-render-services](https://github.com/caspervg/sc4-render-services) and the custom power-line
  research and implementation this DLL ports.
- **nsgomez**: [gzcom-dll](https://github.com/nsgomez/gzcom-dll).
- **metayeti**: [mINI](https://github.com/metayeti/mINI).

## License

This project is licensed under the **GNU Lesser General Public License v3.0**. See [`LICENSE`](LICENSE). The
LGPL-3.0 supplements the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html).

Third-party code keeps its own license:
- `src/powerpoles`: ported from sc4-render-services, LGPL 2.1
  ([`src/powerpoles/LICENSE-sc4-render-services.txt`](src/powerpoles/LICENSE-sc4-render-services.txt));
- `vendor/gzcom-dll`: LGPL 2.1;
- `vendor/mINI`: MIT.

SimCity 4 is a trademark of Electronic Arts Inc. This project is not affiliated with or endorsed by Electronic Arts.
